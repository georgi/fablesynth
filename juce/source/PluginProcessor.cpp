#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

using namespace fable;

FableAudioProcessor::FableAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {
    // Cache raw atomic pointers, indexed by the engine's flat parameter id.
    const auto& info = paramInfo();
    for (size_t i = 0; i < (size_t)NUM_PARAMS; ++i)
        rawParams[i] = apvts.getRawParameterValue(info[i].pid);
    // Procedural wavetables are sample-rate independent: build once.
    for (auto& g : generateTables())
        tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
}

// Build the APVTS layout from the single canonical descriptor table, using the
// exact same value<->norm mapping as the web app so curves are identical.
juce::AudioProcessorValueTreeState::ParameterLayout FableAudioProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& d : paramInfo()) {
        juce::ParameterID pid(d.pid, 1);
        // Host-facing name MUST be unique: repeated blocks share short labels
        // (both LFOs are "SHAPE"/"RATE", both oscs "POS", etc.). A DAW that keys
        // automation / MIDI-learn / its generic editor by name would otherwise
        // alias them (move one "SHAPE" -> both move). Derive a unique name from
        // the id; the plugin's own UI still uses the short ParamInfo.label.
        juce::String name = juce::String(d.pid).replaceCharacter('.', ' ').toUpperCase();
        if (d.kind == Kind::Bool) {
            layout.add(std::make_unique<juce::AudioParameterBool>(pid, name, d.def != 0.0f));
        } else if (d.kind == Kind::Enum) {
            juce::StringArray choices;
            for (const auto& s : *d.options) choices.add(s);
            layout.add(std::make_unique<juce::AudioParameterChoice>(pid, name, choices, (int)d.def));
        } else {
            // Custom range whose 0..1 mapping matches normToValue / valueToNorm.
            ParamInfo info = d;
            juce::NormalisableRange<float> range(
                d.min, d.max,
                [info](float, float, float n) { return normToValue(info, n); },
                [info](float, float, float v) { return valueToNorm(info, v); });
            if (d.curve == Curve::Int) range.interval = 1.0f;
            layout.add(std::make_unique<juce::AudioParameterFloat>(pid, name, range, d.def));
        }
    }
    return layout;
}

void FableAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate);
    rebuildEngineTables();
    fx.prepare(sampleRate);
    setLatencySamples(fx.latencySamples());
    // Pre-size the render scratch generously; processBlock never allocates on
    // the audio thread — oversized host blocks are rendered in chunks instead.
    scratchR.setSize(1, std::max(samplesPerBlock, 8192));
    currentSr.store(sampleRate);
    prepared = true;
    // Re-sync sequencer content into the (possibly reset) engine.
    shareSeqState(true, true);
}

// ---- note sequencer bridge (message thread) --------------------------------

void FableAudioProcessor::pushCmd(int type) {
    int s1, sz1, s2, sz2;
    cmdFifo_.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)      cmds_[(size_t)s1] = type;
    else if (sz2 > 0) cmds_[(size_t)s2] = type;
    cmdFifo_.finishedWrite(sz1 + sz2);
}

void FableAudioProcessor::setSeqPlaying(bool on) {
    pushCmd(on ? CmdPlay : CmdStop);
    seqPlaying_.store(on); // optimistic; processBlock republishes the engine's state
}

// Copy the message-thread sequencer content into the shared mirror the audio
// thread applies on its next block (try-lock there, so this never glitches).
void FableAudioProcessor::shareSeqState(bool patterns, bool chain) {
    std::lock_guard<std::mutex> lk(shareMutex_);
    if (patterns) { patternsShared_ = patterns_; patternsDirty_ = true; }
    if (chain)    { chainShared_ = chain_;       chainDirty_ = true; }
}

fable::NoteSeqStep FableAudioProcessor::getSeqStep(int pattern, int step) const {
    if (pattern < 0 || pattern >= fable::SEQ_NPATTERNS || step < 0 || step >= fable::SEQ_STEPS)
        return {};
    return fable::getNoteSeqStep(patterns_.data(), pattern, step);
}

void FableAudioProcessor::setSeqStep(int pattern, int step, const fable::NoteSeqStep& s) {
    if (pattern < 0 || pattern >= fable::SEQ_NPATTERNS || step < 0 || step >= fable::SEQ_STEPS)
        return;
    fable::setNoteSeqStep(patterns_.data(), pattern, step, s);
    shareSeqState(true, false);
}

void FableAudioProcessor::setChain(std::vector<int> c) {
    const int bars = juce::jlimit(1, fable::SEQ_NPATTERNS, (int)c.size());
    chain_.resize((size_t)bars);
    std::iota(chain_.begin(), chain_.end(), 0);
    shareSeqState(false, true);
}

void FableAudioProcessor::setEditPattern(int p) {
    editPattern_ = juce::jlimit(0, fable::SEQ_NPATTERNS - 1, p);
}

// ---- table addressing & user-table management (message thread) ----
const fable::GeneratedTable* FableAudioProcessor::tableAt(int idx) const {
    if (idx < 0) return nullptr;
    if (idx < (int)tables.size()) return tables[(size_t)idx].get();
    int u = idx - (int)tables.size();
    if (u < (int)userTables.size()) return userTables[(size_t)u].table.get();
    return nullptr; // empty user slot
}

juce::String FableAudioProcessor::tableName(int idx) const {
    if (idx >= 0 && idx < (int)fable::tableSlotNames().size())
        if (idx < (int)tables.size())
            return juce::String(fable::TABLE_NAMES[(size_t)idx]);
    int u = idx - (int)tables.size();
    if (u >= 0 && u < (int)userTables.size())
        return juce::String(userTables[(size_t)u].name);
    if (idx >= 0 && idx < (int)fable::tableSlotNames().size())
        return juce::String(fable::tableSlotNames()[(size_t)idx]); // empty "USER n"
    return "-";
}

void FableAudioProcessor::rebuildEngineTables() {
    ++tablesGen;
    std::vector<fable::TablePtr> all;
    all.reserve(tables.size() + userTables.size());
    for (const auto& t : tables) all.push_back(t);
    for (const auto& u : userTables) all.push_back(u.table);
    engine.setTables(std::move(all)); // thread-safe swap; shares data, copies nothing
}

int FableAudioProcessor::addUserTable(fable::UserTable table) {
    if ((int)userTables.size() >= fable::MAX_USER_TABLES) return -1;
    userTables.push_back(std::move(table));
    rebuildEngineTables();
    return (int)tables.size() + (int)userTables.size() - 1;
}

void FableAudioProcessor::deleteUserTable(int poolIndex) {
    if (poolIndex < 0 || poolIndex >= (int)userTables.size()) return;
    userTables.erase(userTables.begin() + poolIndex);
    rebuildEngineTables();
    // Repair oscillator TABLE references around the removed slot (mirrors the web).
    const int removed = (int)tables.size() + poolIndex;
    for (const char* id : {"oscA.table", "oscB.table"}) {
        if (auto* p = apvts.getParameter(id)) {
            int v = (int)std::round(p->convertFrom0to1(p->getValue()));
            int nv = v == removed ? 0 : (v > removed ? v - 1 : v);
            if (nv != v) p->setValueNotifyingHost(p->convertTo0to1((float)nv));
        }
    }
}

void FableAudioProcessor::renameUserTable(int poolIndex, std::string name) {
    if (poolIndex < 0 || poolIndex >= (int)userTables.size()) return;
    juce::String s = juce::String(name).trim().toUpperCase();
    if (s.isEmpty()) s = "USER";
    userTables[(size_t)poolIndex].name = s.substring(0, 14).toStdString();
}

void FableAudioProcessor::updateUserTable(int poolIndex, fable::UserTable u) {
    if (poolIndex < 0 || poolIndex >= (int)userTables.size()) return;
    // In-place replace keeps the pool index (and thus the combined .table index
    // every oscillator/preset references) stable — unlike delete+add.
    userTables[(size_t)poolIndex] = std::move(u);
    rebuildEngineTables();
}

int FableAudioProcessor::duplicateUserTable(int poolIndex) {
    if (poolIndex < 0 || poolIndex >= (int)userTables.size()) return -1;
    const auto& src = userTables[(size_t)poolIndex];
    std::vector<std::vector<float>> frames;
    for (int f = 0; f < src.frames; ++f) {
        const auto first = (std::vector<float>::difference_type)((size_t)f * fable::SIZE);
        const auto last = (std::vector<float>::difference_type)((size_t)(f + 1) * fable::SIZE);
        frames.emplace_back(src.wave.begin() + first, src.wave.begin() + last);
    }
    std::string nm = (src.name + " COPY").substr(0, 14);
    return addUserTable(fable::makeUserTable(nm, frames));
}

int FableAudioProcessor::duplicateFactoryTable(int factoryIndex) {
    if (factoryIndex < 0 || factoryIndex >= (int)tables.size()) return -1;
    auto frames = fable::framesFromGenerated(*tables[(size_t)factoryIndex]);
    std::string nm = (tables[(size_t)factoryIndex]->name + " COPY").substr(0, 14);
    return addUserTable(fable::makeUserTable(nm, frames));
}

void FableAudioProcessor::readScope(float* dst, int n) const {
    int w = scopeW.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        dst[i] = scopeBuf[(size_t)((w - n + i) & (kScopeSize - 1))];
}

bool FableAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FableAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // Pull current parameter values into the engine's flat array (audio-thread safe).
    auto& p = engine.params();
    for (size_t i = 0; i < (size_t)NUM_PARAMS; ++i)
        if (rawParams[i]) p[i] = rawParams[i]->load();
    fx.setParams(p);

    // Push host tempo + transport for LFO sync and downbeat phase-locking
    // (fallbacks when the host provides nothing: 120 BPM, position 0, stopped).
    // Sequencer host sync (BL-1 conventions): a reported tempo overrides
    // seq.bpm; a rolling transport that also reports song position slaves the
    // sequencer to the playhead (steps derive from ppq). Without ppq (or
    // stopped) the internal play control drives the clock.
    double bpm = 120.0, ppq = 0.0;
    bool playing = false, synced = false, hasPpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) { bpm = *b; synced = bpm > 0; }
            if (auto q = pos->getPpqPosition()) { ppq = *q; hasPpq = true; }
            playing = pos->getIsPlaying();
        }
    engine.setBpm(bpm);
    engine.setTransport(ppq, playing);
    engine.setBpmOverride(synced ? bpm : 0.0);
    engine.setSeqHostTransport(ppq, synced ? bpm : 120.0,
                               playing && hasPpq && synced);
    hostBpm.store((float)bpm, std::memory_order_relaxed);
    hostPpq.store(ppq, std::memory_order_relaxed);
    hostPlaying.store(playing, std::memory_order_relaxed);
    hostSynced_.store(synced, std::memory_order_relaxed);
    hostSeqBpm_.store(synced ? bpm : 0.0, std::memory_order_relaxed);

    // Drain the UI command FIFO (sequencer transport / panic).
    {
        int s1, sz1, s2, sz2;
        cmdFifo_.prepareToRead(cmdFifo_.getNumReady(), s1, sz1, s2, sz2);
        auto run = [&](int start, int count) {
            for (int i = 0; i < count; ++i) {
                switch (cmds_[(size_t)(start + i)]) {
                    case CmdPlay:  engine.seqPlay();  break;
                    case CmdStop:  engine.seqStop();  break;
                    case CmdPanic: engine.panic();    break;
                }
            }
        };
        run(s1, sz1); run(s2, sz2);
        cmdFifo_.finishedRead(sz1 + sz2);
    }

    // Sync pattern/chain edits (try-lock: skip on contention, retry next block).
    if (shareMutex_.try_lock()) {
        if (patternsDirty_) {
            engine.setSeqPatterns(patternsShared_.data(), (int)patternsShared_.size());
            patternsDirty_ = false;
        }
        if (chainDirty_) {
            engine.setSeqChain(chainShared_.data(), (int)chainShared_.size());
            chainDirty_ = false;
        }
        shareMutex_.unlock();
    }

    // Sample-accurate MIDI: render engine+FX up to each event's offset, apply
    // the event, continue. FX state is continuous, so per-segment processing is
    // equivalent to one full-block pass. Segments are capped at the scratch
    // capacity so an oversized host block never allocates on the audio thread.
    const bool stereo = buffer.getNumChannels() > 1;
    float* L = buffer.getWritePointer(0);
    float* R = stereo ? buffer.getWritePointer(1) : scratchR.getWritePointer(0);
    const int cap = scratchR.getNumSamples();
    int pos = 0;
    auto renderTo = [&](int end) {
        while (pos < end) {
            const int len = std::min(end - pos, cap);
            float* l = L + pos;
            float* r = stereo ? R + pos : R; // mono: scratch reused per chunk
            engine.render(l, r, len); // summed (pre-FX) voice mix
            fx.process(l, r, len);
            if (!stereo) { // mono out: downmix (engine is stereo)
                juce::FloatVectorOperations::add(l, r, len);
                juce::FloatVectorOperations::multiply(l, 0.5f, len);
            }
            pos += len;
        }
    };
    for (const auto meta : midi) {
        renderTo(juce::jlimit(0, n, meta.samplePosition));
        const auto m = meta.getMessage();
        if (m.isNoteOn())        { engine.noteOn(m.getNoteNumber(), m.getFloatVelocity()); midiGlow.store(20); }
        else if (m.isNoteOff())  engine.noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.panic();
        else if (m.isPitchWheel()) { engine.pitchBend((m.getPitchWheelValue() - 8192) / 8192.0 * 2.0); midiGlow.store(20); }
    }
    renderTo(n);

    // Publish the live modulated wavetable positions for the editor.
    vizPosA.store((float)engine.vizA, std::memory_order_relaxed);
    vizPosB.store((float)engine.vizB, std::memory_order_relaxed);

    // Publish sequencer transport feedback for the editor.
    curStep_.store(engine.seqCurrentStep(), std::memory_order_relaxed);
    curPattern_.store(engine.seqCurrentPattern(), std::memory_order_relaxed);
    seqPlaying_.store(engine.seqIsPlaying(), std::memory_order_relaxed);

    // HUD feeds: voice count, MIDI led decay, and the post-FX scope ring buffer.
    voiceCount.store(engine.vizActive, std::memory_order_relaxed);
    if (int g = midiGlow.load(); g > 0) midiGlow.store(g - 1);
    {
        const float* l0 = buffer.getReadPointer(0);
        const float* r0 = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : l0;
        int w = scopeW.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            scopeBuf[(size_t)((w + i) & (kScopeSize - 1))] = 0.5f * (l0[i] + r0[i]);
        scopeW.store(w + n, std::memory_order_relaxed);
    }
}

void FableAudioProcessor::setCurrentProgram(int index) {
    if (index < 0 || index >= (int)factoryPresets().size()) return;
    currentProgram = index;
    // Apply preset values onto the APVTS so the host + UI reflect them.
    ParamArray pv = applyPreset(factoryPresets()[(size_t)index]);
    const auto& info = paramInfo();
    for (size_t i = 0; i < (size_t)NUM_PARAMS; ++i) {
        if (auto* param = apvts.getParameter(info[i].pid)) {
            const auto& d = info[i];
            float norm = (d.kind == Kind::Bool) ? (pv[i] != 0.0f ? 1.0f : 0.0f)
                       : (d.kind == Kind::Enum) ? param->convertTo0to1(pv[i])
                       : valueToNorm(d, pv[i]);
            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
        }
    }
}

const juce::String FableAudioProcessor::getProgramName(int index) {
    if (index < 0 || index >= (int)factoryPresets().size()) return {};
    return factoryPresets()[(size_t)index].name;
}

// ---- standalone WT UI model ----------------------------------------------
fui::ParameterSource StandaloneWtUiModel::parameters() {
    const auto& info = fable::paramInfo();
    return fui::ParameterSource::fromApvts(proc.apvts, info.data(), info.size());
}
int StandaloneWtUiModel::currentProgram() const { return proc.getCurrentProgram(); }
int StandaloneWtUiModel::numPrograms() const { return proc.getNumPrograms(); }
juce::String StandaloneWtUiModel::programName(int i) const { return proc.getProgramName(i); }
void StandaloneWtUiModel::selectProgram(int i) { proc.setCurrentProgram(i); }
int StandaloneWtUiModel::numTables() const { return proc.numTables(); }
const fable::GeneratedTable* StandaloneWtUiModel::tableAt(int i) const { return proc.tableAt(i); }
juce::String StandaloneWtUiModel::tableName(int i) const { return proc.tableName(i); }
int StandaloneWtUiModel::tablesGeneration() const { return proc.getTablesGeneration(); }
float StandaloneWtUiModel::vizPosition(int i) const { return proc.getVizPos(i); }
int StandaloneWtUiModel::voiceCount() const { return proc.getVoiceCount(); }
bool StandaloneWtUiModel::midiActive() const { return proc.getMidiActive(); }
double StandaloneWtUiModel::sampleRate() const { return proc.getCurrentSr(); }
void StandaloneWtUiModel::readScope(float* d, int n) const { proc.readScope(d, n); }
bool StandaloneWtUiModel::hostSynced() const { return proc.isHostSynced(); }
double StandaloneWtUiModel::hostBpm() const { return proc.getHostBpm(); }
bool StandaloneWtUiModel::sequencerPlaying() const { return proc.isSeqPlaying(); }
void StandaloneWtUiModel::setSequencerPlaying(bool on) { proc.setSeqPlaying(on); }
int StandaloneWtUiModel::currentStep() const { return proc.getCurrentStep(); }
int StandaloneWtUiModel::currentPattern() const { return proc.getCurrentPattern(); }
int StandaloneWtUiModel::editPattern() const { return proc.getEditPattern(); }
void StandaloneWtUiModel::setEditPattern(int i) { proc.setEditPattern(i); }
fable::NoteSeqStep StandaloneWtUiModel::sequenceStep(int p, int s) const { return proc.getSeqStep(p, s); }
void StandaloneWtUiModel::setSequenceStep(int p, int s, const fable::NoteSeqStep& v) { proc.setSeqStep(p, s, v); }
const std::vector<int>& StandaloneWtUiModel::chain() const { return proc.getChain(); }
void StandaloneWtUiModel::setChain(std::vector<int> v) { proc.setChain(std::move(v)); }
const std::vector<fable::UserTable>& StandaloneWtUiModel::userTables() const { return proc.getUserTables(); }
const std::vector<std::shared_ptr<const fable::GeneratedTable>>& StandaloneWtUiModel::factoryTables() const { return proc.factoryTables(); }
int StandaloneWtUiModel::maxUserTables() const { return proc.maxUserTables(); }
int StandaloneWtUiModel::addUserTable(fable::UserTable v) { return proc.addUserTable(std::move(v)); }
void StandaloneWtUiModel::deleteUserTable(int i) { proc.deleteUserTable(i); }
void StandaloneWtUiModel::renameUserTable(int i, std::string n) { proc.renameUserTable(i, std::move(n)); }
void StandaloneWtUiModel::updateUserTable(int i, fable::UserTable v) { proc.updateUserTable(i, std::move(v)); }
int StandaloneWtUiModel::duplicateUserTable(int i) { return proc.duplicateUserTable(i); }
int StandaloneWtUiModel::duplicateFactoryTable(int i) { return proc.duplicateFactoryTable(i); }

void FableAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    if (!state.isValid()) return;
    // Wrap the parameter tree plus the user-table pool so both round-trip with
    // the host project. Each table stores its raw single-cycle frames (base64
    // little-endian float); the band-limited pyramid is rebuilt on load.
    juce::ValueTree root("FABLESTATE");
    root.appendChild(state, nullptr);
    juce::ValueTree pool("USERTABLES");
    for (const auto& u : userTables) {
        juce::ValueTree t("TABLE");
        t.setProperty("name", juce::String(u.name), nullptr);
        t.setProperty("frames", u.frames, nullptr);
        t.setProperty("wave", juce::Base64::toBase64(u.wave.data(),
                       u.wave.size() * sizeof(float)), nullptr);
        pool.appendChild(t, nullptr);
    }
    root.appendChild(pool, nullptr);

    // Note sequencer session: all 4 patterns in the web's packed 3-byte/step
    // layout (base64) + the chain + the edit pattern (BL-1 BASS-child scheme).
    juce::ValueTree seq("NOTESEQ");
    seq.setProperty("patterns",
        juce::Base64::toBase64(patterns_.data(), patterns_.size()), nullptr);
    juce::StringArray chainStr;
    for (int c : chain_) chainStr.add(juce::String(c));
    seq.setProperty("chain", chainStr.joinIntoString(","), nullptr);
    seq.setProperty("editPattern", editPattern_, nullptr);
    root.appendChild(seq, nullptr);

    if (auto xml = root.createXml()) copyXmlToBinary(*xml, destData);
}

void FableAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml) return;

    juce::ValueTree params, pool, seq;
    if (xml->hasTagName("FABLESTATE")) {
        auto root = juce::ValueTree::fromXml(*xml);
        params = root.getChildWithName(apvts.state.getType());
        pool   = root.getChildWithName("USERTABLES");
        seq    = root.getChildWithName("NOTESEQ");
    } else if (xml->hasTagName(apvts.state.getType())) {
        params = juce::ValueTree::fromXml(*xml); // legacy: bare parameter tree
    }

    // Restore the sequencer session (legacy states without NOTESEQ keep the
    // empty-pattern defaults).
    if (seq.isValid()) {
        juce::MemoryOutputStream raw;
        if (juce::Base64::convertFromBase64(raw, seq.getProperty("patterns", "").toString())
            && raw.getDataSize() == patterns_.size())
            std::memcpy(patterns_.data(), raw.getData(), patterns_.size());
        juce::StringArray chainStr;
        chainStr.addTokens(seq.getProperty("chain", "").toString(), ",", "");
        std::vector<int> c;
        for (const auto& s : chainStr)
            if (s.trim().isNotEmpty())
                c.push_back(juce::jlimit(0, fable::SEQ_NPATTERNS - 1, s.getIntValue()));
        setChain(c);
        editPattern_ = juce::jlimit(0, fable::SEQ_NPATTERNS - 1,
                                    (int)seq.getProperty("editPattern", 0));
    }
    shareSeqState(true, true);
    pushCmd(CmdPanic);

    // Restore user tables first so the engine has them before params apply.
    userTables.clear();
    if (pool.isValid()) {
        for (int i = 0; i < pool.getNumChildren() && (int)userTables.size() < fable::MAX_USER_TABLES; ++i) {
            auto t = pool.getChild(i);
            juce::String name = t.getProperty("name", "USER");
            int frames = (int)t.getProperty("frames", 1);
            juce::MemoryOutputStream raw;
            if (juce::Base64::convertFromBase64(raw, t.getProperty("wave", "").toString())) {
                // Don't trust the persisted size: cap the decoded float count to
                // what a valid table can hold so corrupt/hostile state can't
                // trigger a huge allocation. userTableFromWave clamps frames too.
                size_t maxFloats = (size_t)fable::MAX_FRAMES * fable::SIZE;
                int n = (int)std::min((size_t)(raw.getDataSize() / sizeof(float)), maxFloats);
                if (n > 0) {
                    std::vector<float> wave((size_t)n);
                    std::memcpy(wave.data(), raw.getData(), (size_t)n * sizeof(float));
                    userTables.push_back(fable::userTableFromWave(name.toStdString(), frames, wave));
                }
            }
        }
    }
    rebuildEngineTables();

    if (params.isValid())
        apvts.replaceState(params);
}

juce::AudioProcessorEditor* FableAudioProcessor::createEditor() {
    return new FableAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new FableAudioProcessor();
}
