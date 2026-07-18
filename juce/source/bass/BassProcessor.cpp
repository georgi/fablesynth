#include "BassProcessor.h"
#include "BassEditor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

using namespace fable;

// ---- construction --------------------------------------------------------

BassAudioProcessor::BassAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {
    // Cache raw atomic pointers, indexed by the engine's flat parameter id.
    const auto& info = bassParamInfo();
    for (int i = 0; i < BL_NUM_PARAMS; ++i)
        rawParams_[(size_t)i] = apvts.getRawParameterValue(info[(size_t)i].pid);

    // The BL-1 table list: the 6 WT-1 procedurals, web order (TABLE_NAMES).
    for (auto& g : generateTables())
        tables_.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    engine.setTables(tables_);

    // Boot on ACID LINE like the web app (params + patterns + chain).
    setCurrentProgram(0);
}

// Build the APVTS layout from the canonical descriptor table, using the exact
// same value<->norm mapping as the web app so curves are identical. Grouped
// SOUND / SEQ / FX so hosts display a sane 45-parameter tree.
juce::AudioProcessorValueTreeState::ParameterLayout BassAudioProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& info = bassParamInfo();

    auto make = [](const ParamInfo& d) -> std::unique_ptr<juce::RangedAudioParameter> {
        juce::ParameterID pid(d.pid, 1);
        // Host-facing name derived from the id so it stays unique (the short
        // labels repeat: LVL, MIX, ...) — WT-1/DR-1 scheme.
        juce::String name = juce::String(d.pid).replaceCharacter('.', ' ').toUpperCase();
        if (d.kind == Kind::Bool)
            return std::make_unique<juce::AudioParameterBool>(pid, name, d.def != 0.0f);
        if (d.kind == Kind::Enum) {
            juce::StringArray choices;
            for (const auto& s : *d.options) choices.add(s);
            return std::make_unique<juce::AudioParameterChoice>(pid, name, choices, (int)d.def);
        }
        ParamInfo di = d; // captured copy: custom range matching normToValue/valueToNorm
        juce::NormalisableRange<float> range(
            d.min, d.max,
            [di](float, float, float n) { return normToValue(di, n); },
            [di](float, float, float v) { return valueToNorm(di, v); });
        if (d.curve == Curve::Int) range.interval = 1.0f;
        return std::make_unique<juce::AudioParameterFloat>(pid, name, range, d.def);
    };

    auto sound = std::make_unique<juce::AudioProcessorParameterGroup>("sound", "SOUND", " | ");
    for (int i = 0; i < BL_FXDRIVE_ON; ++i)
        sound->addChild(make(info[(size_t)i]));
    layout.add(std::move(sound));
    auto fxg = std::make_unique<juce::AudioProcessorParameterGroup>("fx", "FX", " | ");
    for (int i = BL_FXDRIVE_ON; i < BL_SEQ_BPM; ++i)
        fxg->addChild(make(info[(size_t)i]));
    layout.add(std::move(fxg));
    auto seq = std::make_unique<juce::AudioProcessorParameterGroup>("seq", "SEQ", " | ");
    for (int i = BL_SEQ_BPM; i < BL_NUM_PARAMS; ++i)
        seq->addChild(make(info[(size_t)i]));
    layout.add(std::move(seq));
    return layout;
}

void BassAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate);
    engine.setTables(tables_);
    fx.prepare(sampleRate);
    setLatencySamples(fx.latencySamples());
    // Sized generously here so processBlock never allocates; oversized host
    // blocks are rendered in chunks of this capacity.
    scratch_.setSize(1, std::max(samplesPerBlock, 8192));
    currentSr_.store(sampleRate);
    // Re-sync sequencer content into the (possibly reset) engine.
    shareSeqState(true, true);
}

bool BassAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto s = layouts.getChannelSet(false, 0);
    return s == juce::AudioChannelSet::stereo() || s == juce::AudioChannelSet::mono();
}

// ---- tables ---------------------------------------------------------------

const fable::GeneratedTable* BassAudioProcessor::tableAt(int idx) const {
    if (idx < 0 || idx >= (int)tables_.size()) return nullptr;
    return tables_[(size_t)idx].get();
}

// ---- audition / transport (message thread) --------------------------------

void BassAudioProcessor::pushCmd(int type, int a, float v) {
    int s1, sz1, s2, sz2;
    cmdFifo_.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)      cmds_[(size_t)s1] = { type, a, v };
    else if (sz2 > 0) cmds_[(size_t)s2] = { type, a, v };
    cmdFifo_.finishedWrite(sz1 + sz2);
}

void BassAudioProcessor::noteOn(int semi, float vel) {
    pushCmd(CmdNoteOn, semi, juce::jlimit(0.0f, 1.0f, vel));
}

void BassAudioProcessor::noteOff(int semi) {
    pushCmd(CmdNoteOff, semi, 0);
}

void BassAudioProcessor::setSeqPlaying(bool on) {
    pushCmd(on ? CmdPlay : CmdStop, 0, 0);
    seqPlaying_.store(on); // optimistic; processBlock republishes the engine's state
}

// ---- patterns / chain (message thread) -------------------------------------

// Copy the message-thread sequencer content into the shared mirror the audio
// thread applies on its next block (try-lock there, so this never glitches).
void BassAudioProcessor::shareSeqState(bool patterns, bool chain) {
    std::lock_guard<std::mutex> lk(shareMutex_);
    if (patterns) { patternsShared_ = patterns_; patternsDirty_ = true; }
    if (chain)    { chainShared_ = chain_;       chainDirty_ = true; }
}

fable::BassSeqStep BassAudioProcessor::getSeqStep(int pattern, int step) const {
    if (pattern < 0 || pattern >= BL_NPATTERNS || step < 0 || step >= BL_STEPS)
        return {};
    return getBassStep(patterns_.data(), pattern, step);
}

void BassAudioProcessor::setSeqStep(int pattern, int step, const fable::BassSeqStep& s) {
    if (pattern < 0 || pattern >= BL_NPATTERNS || step < 0 || step >= BL_STEPS)
        return;
    setBassStep(patterns_.data(), pattern, step, s);
    programDirty_.markEdited();
    shareSeqState(true, false);
}

std::vector<uint8_t> BassAudioProcessor::getPatternBytes() const {
    return std::vector<uint8_t>(patterns_.begin(), patterns_.end());
}

void BassAudioProcessor::setPatternBytes(const std::vector<uint8_t>& bytes) {
    std::copy_n(bytes.begin(), std::min(bytes.size(), patterns_.size()), patterns_.begin());
    programDirty_.markEdited();
    shareSeqState(true, false);
}

void BassAudioProcessor::setChain(std::vector<int> c) {
    const int bars = juce::jlimit(1, BL_NPATTERNS, (int)c.size());
    chain_.resize((size_t)bars);
    std::iota(chain_.begin(), chain_.end(), 0);
    programDirty_.markEdited();
    shareSeqState(false, true);
}

void BassAudioProcessor::setEditPattern(int p) {
    editPattern_ = juce::jlimit(0, BL_NPATTERNS - 1, p);
}

// ---- patches as programs ----------------------------------------------------

void BassAudioProcessor::setCurrentProgram(int index) {
    const auto& bank = bassFactoryPatches();
    if (index < 0 || index >= (int)bank.size()) return;
    currentProgram_ = index;
    fui::ProgramDirtyTracker::LoadScope loading(programDirty_);
    const BassPatch& patch = bank[(size_t)index];

    // Apply patch values onto the APVTS so the host + UI reflect them.
    BassParamArray pv = applyBassPatch(patch);
    const auto& info = bassParamInfo();
    for (int i = 0; i < BL_NUM_PARAMS; ++i) {
        if (auto* param = apvts.getParameter(info[(size_t)i].pid)) {
            const auto& d = info[(size_t)i];
            float norm = (d.kind == Kind::Bool) ? (pv[(size_t)i] != 0.0f ? 1.0f : 0.0f)
                       : (d.kind == Kind::Enum) ? param->convertTo0to1(pv[(size_t)i])
                       : valueToNorm(d, pv[(size_t)i]);
            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
        }
    }

    // Non-param patch content: patterns + chain (patchToState).
    if ((int)patch.patterns.size() == BL_PATTERN_BYTES)
        std::copy(patch.patterns.begin(), patch.patterns.end(), patterns_.begin());
    const int bars = juce::jlimit(1, BL_NPATTERNS, (int)patch.chain.size());
    chain_.resize((size_t)bars);
    std::iota(chain_.begin(), chain_.end(), 0);
    for (int& c : chain_) c = juce::jlimit(0, BL_NPATTERNS - 1, c);
    editPattern_ = chain_[0];
    pushCmd(CmdPanic, 0, 0);          // web loadPatchByValue panics the voice
    shareSeqState(true, true);
}

const juce::String BassAudioProcessor::getProgramName(int index) {
    const auto& bank = bassFactoryPatches();
    if (index < 0 || index >= (int)bank.size()) return {};
    return juce::String(bank[(size_t)index].name);
}

// ---- audio ------------------------------------------------------------------

void BassAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // Pull current parameter values into the engine's flat array.
    auto& p = engine.params();
    for (int i = 0; i < BL_NUM_PARAMS; ++i)
        if (rawParams_[(size_t)i]) p[(size_t)i] = rawParams_[(size_t)i]->load();
    fx.setParams(p);

    // Host sync. A reported tempo overrides seq.bpm; a rolling transport that
    // also reports song position slaves the whole sequencer to the host
    // (steps derive from ppq — the standard JUCE AudioPlayHead behavior).
    // Without ppq (or stopped) the internal play button drives the clock.
    bool synced = false; double bpm = 0;
    bool hostRolling = false; double ppq = 0; bool hasPpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) { bpm = *b; synced = bpm > 0; }
            hostRolling = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { ppq = *q; hasPpq = true; }
        }
    engine.setBpmOverride(synced ? bpm : 0.0);
    engine.setHostTransport(ppq, synced ? bpm : 120.0,
                            hostRolling && hasPpq && synced);
    hostSynced_.store(synced, std::memory_order_relaxed);
    hostBpm_.store(synced ? bpm : 0.0, std::memory_order_relaxed);

    // Drain the UI command FIFO (audition/transport).
    {
        int s1, sz1, s2, sz2;
        cmdFifo_.prepareToRead(cmdFifo_.getNumReady(), s1, sz1, s2, sz2);
        auto run = [&](int start, int count) {
            for (int i = 0; i < count; ++i) {
                const Cmd& c = cmds_[(size_t)(start + i)];
                switch (c.type) {
                    case CmdNoteOn:  engine.keyOn(c.a, c.v);  break;
                    case CmdNoteOff: engine.keyOff(c.a);      break;
                    case CmdPlay:    engine.play();           break;
                    case CmdStop:    engine.stop();           break;
                    case CmdPanic:   engine.panic();          break;
                }
            }
        };
        run(s1, sz1); run(s2, sz2);
        cmdFifo_.finishedRead(sz1 + sz2);
    }

    // Sync pattern/chain edits (try-lock: skip on contention, retry next block).
    if (shareMutex_.try_lock()) {
        if (patternsDirty_) {
            engine.setPatterns(patternsShared_.data(), (int)patternsShared_.size());
            patternsDirty_ = false;
        }
        if (chainDirty_) {
            engine.setChain(chainShared_.data(), (int)chainShared_.size());
            chainDirty_ = false;
        }
        shareMutex_.unlock();
    }

    // Sample-accurate MIDI: render engine+FX up to each event's offset, apply
    // the event, continue (FX state is continuous across segments). Notes
    // audition the mono voice around root C2 (36) through the same
    // last-note/legato path as the on-screen keyboard. The engine ignores them
    // while the sequencer is playing (it owns the voice). Segments are capped
    // at the scratch capacity so oversized host blocks never allocate here.
    const bool isStereo = buffer.getNumChannels() > 1;
    float* L = buffer.getWritePointer(0);
    float* R = isStereo ? buffer.getWritePointer(1) : scratch_.getWritePointer(0);
    const int cap = scratch_.getNumSamples();
    int pos = 0;
    auto renderTo = [&](int end) {
        while (pos < end) {
            const int len = std::min(end - pos, cap);
            float* l = L + pos;
            float* r = isStereo ? R + pos : R; // mono host: scratch reused per chunk
            engine.render(l, r, len);
            fx.process(l, r, len);
            if (!isStereo) // mono host: downmix
                for (int i = 0; i < len; ++i) l[i] = 0.5f * (l[i] + r[i]);
            pos += len;
        }
    };
    for (const auto meta : midi) {
        renderTo(juce::jlimit(0, n, meta.samplePosition));
        const auto m = meta.getMessage();
        if (m.isNoteOn()) {
            engine.keyOn(m.getNoteNumber() - BL_ROOT_MIDI, m.getFloatVelocity());
            midiGlow_.store(20);
        } else if (m.isNoteOff()) {
            engine.keyOff(m.getNoteNumber() - BL_ROOT_MIDI);
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            engine.panic();
        }
    }
    renderTo(n);

    // Publish transport/viz feedback for the editor.
    curStep_.store(engine.currentStep(), std::memory_order_relaxed);
    curPattern_.store(engine.currentPattern(), std::memory_order_relaxed);
    seqPlaying_.store(engine.isPlaying(), std::memory_order_relaxed);
    curSemi_.store(engine.vizSemi, std::memory_order_relaxed);
    vizPos_.store(engine.vizPos, std::memory_order_relaxed);
    vizEnv_.store(engine.vizEnv, std::memory_order_relaxed);
    vizFenv_.store(engine.vizFenv, std::memory_order_relaxed);
    vizCut_.store(engine.vizCut, std::memory_order_relaxed);
    vizGate_.store(engine.vizGate, std::memory_order_relaxed);
    if (int g = midiGlow_.load(); g > 0) midiGlow_.store(g - 1);

    // Post-FX scope ring buffer (mono mix).
    {
        const float* l0 = buffer.getReadPointer(0);
        const float* r0 = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : l0;
        int w = scopeW_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            scopeBuf_[(size_t)((w + i) & (kScopeSize - 1))] = 0.5f * (l0[i] + r0[i]);
        scopeW_.store(w + n, std::memory_order_relaxed);
    }
}

void BassAudioProcessor::readScope(float* dst, int n) const {
    int w = scopeW_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        dst[i] = scopeBuf_[(size_t)((w - n + i) & (kScopeSize - 1))];
}

// ---- state ------------------------------------------------------------------

void BassAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    if (!state.isValid()) return;
    // DR-1 scheme: parameter tree + a BASS child carrying the non-param
    // session (patterns/chain/edit pattern).
    juce::ValueTree root("BL1STATE");
    root.appendChild(state, nullptr);

    juce::ValueTree bass("BASS");
    bass.setProperty("patterns",
        juce::Base64::toBase64(patterns_.data(), patterns_.size()), nullptr);
    juce::StringArray chainStr;
    for (int c : chain_) chainStr.add(juce::String(c));
    bass.setProperty("chain", chainStr.joinIntoString(","), nullptr);
    bass.setProperty("editPattern", editPattern_, nullptr);
    root.appendChild(bass, nullptr);

    if (auto xml = root.createXml()) copyXmlToBinary(*xml, destData);
}

void BassAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml) return;
    fui::ProgramDirtyTracker::LoadScope loading(programDirty_);

    juce::ValueTree params, bass;
    if (xml->hasTagName("BL1STATE")) {
        auto root = juce::ValueTree::fromXml(*xml);
        params = root.getChildWithName(apvts.state.getType());
        bass   = root.getChildWithName("BASS");
    } else if (xml->hasTagName(apvts.state.getType())) {
        params = juce::ValueTree::fromXml(*xml); // legacy: bare parameter tree
    }

    if (bass.isValid()) {
        juce::MemoryOutputStream raw;
        if (juce::Base64::convertFromBase64(raw, bass.getProperty("patterns", "").toString())
            && raw.getDataSize() == patterns_.size())
            std::memcpy(patterns_.data(), raw.getData(), patterns_.size());
        juce::StringArray chainStr;
        chainStr.addTokens(bass.getProperty("chain", "").toString(), ",", "");
        std::vector<int> c;
        for (const auto& s : chainStr)
            if (s.trim().isNotEmpty())
                c.push_back(juce::jlimit(0, BL_NPATTERNS - 1, s.getIntValue()));
        const int bars = juce::jlimit(1, BL_NPATTERNS, (int)c.size());
        chain_.resize((size_t)bars);
        std::iota(chain_.begin(), chain_.end(), 0);
        editPattern_ = juce::jlimit(0, BL_NPATTERNS - 1,
                                    (int)bass.getProperty("editPattern", 0));
    }
    shareSeqState(true, true);
    pushCmd(CmdPanic, 0, 0);

    if (params.isValid())
        apvts.replaceState(params);
}

// ---- editor -------------------------------------------------------------------

juce::AudioProcessorEditor* BassAudioProcessor::createEditor() {
    return new BassEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new BassAudioProcessor();
}
