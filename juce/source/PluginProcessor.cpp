#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstring>

using namespace fable;

FableAudioProcessor::FableAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {
    // Cache raw atomic pointers, indexed by the engine's flat parameter id.
    const auto& info = paramInfo();
    for (int i = 0; i < NUM_PARAMS; ++i)
        rawParams[i] = apvts.getRawParameterValue(info[i].pid);
    // Procedural wavetables are sample-rate independent: build once.
    tables = generateTables();
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

void FableAudioProcessor::prepareToPlay(double sampleRate, int) {
    engine.prepare(sampleRate);
    rebuildEngineTables();
    fx.prepare(sampleRate);
    currentSr.store(sampleRate);
    prepared = true;
}

// ---- table addressing & user-table management (message thread) ----
const fable::GeneratedTable* FableAudioProcessor::tableAt(int idx) const {
    if (idx < 0) return nullptr;
    if (idx < (int)tables.size()) return &tables[(size_t)idx];
    int u = idx - (int)tables.size();
    if (u < (int)userTables.size()) return &userTables[(size_t)u].table;
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
    std::vector<fable::GeneratedTable> all;
    all.reserve(tables.size() + userTables.size());
    for (const auto& t : tables) all.push_back(t);
    for (const auto& u : userTables) all.push_back(u.table);
    engine.setTables(all); // thread-safe swap; safe to call while audio renders
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
    for (int f = 0; f < src.frames; ++f)
        frames.emplace_back(src.wave.begin() + (size_t)f * fable::SIZE,
                            src.wave.begin() + (size_t)(f + 1) * fable::SIZE);
    std::string nm = (src.name + " COPY").substr(0, 14);
    return addUserTable(fable::makeUserTable(nm, frames));
}

int FableAudioProcessor::duplicateFactoryTable(int factoryIndex) {
    if (factoryIndex < 0 || factoryIndex >= (int)tables.size()) return -1;
    auto frames = fable::framesFromGenerated(tables[(size_t)factoryIndex]);
    std::string nm = (tables[(size_t)factoryIndex].name + " COPY").substr(0, 14);
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
    for (int i = 0; i < NUM_PARAMS; ++i)
        if (rawParams[i]) p[i] = rawParams[i]->load();
    fx.setParams(p);

    // Push host tempo + transport for LFO sync and downbeat phase-locking
    // (fallbacks when the host provides nothing: 120 BPM, position 0, stopped).
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    engine.setBpm(bpm);
    engine.setTransport(ppq, playing);
    hostBpm.store((float)bpm, std::memory_order_relaxed);
    hostPpq.store(ppq, std::memory_order_relaxed);
    hostPlaying.store(playing, std::memory_order_relaxed);

    // MIDI -> note / bend events.
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn())        { engine.noteOn(m.getNoteNumber(), m.getFloatVelocity()); midiGlow.store(20); }
        else if (m.isNoteOff())  engine.noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.panic();
        else if (m.isPitchWheel()) { engine.pitchBend((m.getPitchWheelValue() - 8192) / 8192.0 * 2.0); midiGlow.store(20); }
    }

    if (buffer.getNumChannels() > 1) {
        float* L = buffer.getWritePointer(0);
        float* R = buffer.getWritePointer(1);
        engine.render(L, R, n);     // fills L/R with the summed (pre-FX) voice mix
        fx.process(L, R, n);
    } else {
        // Mono out: render to a stereo scratch then downmix (engine is stereo).
        scratchR.setSize(1, n, false, false, true);
        float* L = buffer.getWritePointer(0);
        float* R = scratchR.getWritePointer(0);
        engine.render(L, R, n);
        fx.process(L, R, n);
        juce::FloatVectorOperations::add(L, R, n);
        juce::FloatVectorOperations::multiply(L, 0.5f, n);
    }

    // Publish the live modulated wavetable positions for the editor.
    vizPosA.store((float)engine.vizA, std::memory_order_relaxed);
    vizPosB.store((float)engine.vizB, std::memory_order_relaxed);

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
    ParamArray pv = applyPreset(factoryPresets()[index]);
    const auto& info = paramInfo();
    for (int i = 0; i < NUM_PARAMS; ++i) {
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
    return factoryPresets()[index].name;
}

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
    if (auto xml = root.createXml()) copyXmlToBinary(*xml, destData);
}

void FableAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml) return;

    juce::ValueTree params, pool;
    if (xml->hasTagName("FABLESTATE")) {
        auto root = juce::ValueTree::fromXml(*xml);
        params = root.getChildWithName(apvts.state.getType());
        pool   = root.getChildWithName("USERTABLES");
    } else if (xml->hasTagName(apvts.state.getType())) {
        params = juce::ValueTree::fromXml(*xml); // legacy: bare parameter tree
    }

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
