#include "DrumProcessor.h"
#include "DrumEditor.h"
#include "dsp/DrumPatches.h"
#include "dsp/DrumTables.h"
#include "dsp/SampledTables.gen.h"
#include <cmath>
#include <cstring>

using namespace fable;

// ---- construction --------------------------------------------------------

DrumAudioProcessor::DrumAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("MAIN",  juce::AudioChannelSet::stereo(), true)
          .withOutput("AUX 1", juce::AudioChannelSet::stereo(), false)
          .withOutput("AUX 2", juce::AudioChannelSet::stereo(), false)
          .withOutput("AUX 3", juce::AudioChannelSet::stereo(), false)
          .withOutput("AUX 4", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {
    // Cache raw atomic pointers, indexed by the engine's flat parameter id.
    const auto& info = drumParamInfo();
    for (int i = 0; i < DR_NUM_PARAMS; ++i)
        rawParams_[(size_t)i] = apvts.getRawParameterValue(info[(size_t)i].pid);

    // The DR-1 table list: 4 drum tables then the 6 WT-1 procedurals, in the
    // exact web order (DRUM_TABLE_NAMES) so kit table indices line up.
    for (auto& g : generateDrumTables())
        tables_.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    for (auto& g : generateTables())
        tables_.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    for (auto& g : generateSampledDrumTables())
        tables_.push_back(std::make_shared<const GeneratedTable>(std::move(g)));

    // Boot on TR-VOID like the web app (params + patterns + chain + names).
    setCurrentProgram(0);
}

// Build the APVTS layout from the canonical descriptor table, using the exact
// same value<->norm mapping as the web app so curves are identical. Grouped
// PAD 01..PAD 16 / SEQ / FX so hosts display a sane 788-parameter tree.
juce::AudioProcessorValueTreeState::ParameterLayout DrumAudioProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& info = drumParamInfo();

    auto make = [](const ParamInfo& d) -> std::unique_ptr<juce::RangedAudioParameter> {
        juce::ParameterID pid(d.pid, 1);
        // Host-facing name MUST be unique (all 16 pads share short labels);
        // derive it from the id like WT-1 does.
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

    for (int pad = 0; pad < DR_NPADS; ++pad) {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>(
            "pad" + juce::String(pad), juce::String::formatted("PAD %02d", pad + 1), " | ");
        for (int f = 0; f < DPAD_NFIELDS; ++f)
            g->addChild(make(info[(size_t)dpid(pad, f)]));
        layout.add(std::move(g));
    }
    auto seq = std::make_unique<juce::AudioProcessorParameterGroup>("seq", "SEQ", " | ");
    for (int i : {(int)DG_SEQ_BPM, (int)DG_MASTER_SWING, (int)DG_MASTER_VOLUME})
        seq->addChild(make(info[(size_t)i]));
    layout.add(std::move(seq));
    auto fxg = std::make_unique<juce::AudioProcessorParameterGroup>("fx", "FX", " | ");
    for (int i = DG_FXDRIVE_ON; i < DR_NUM_PARAMS; ++i)
        fxg->addChild(make(info[(size_t)i]));
    layout.add(std::move(fxg));
    return layout;
}

void DrumAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate);
    rebuildEngineTables();
    fx.prepare(sampleRate);
    // Backing storage for disabled AUX buses so the engine's 5x2 output API
    // always gets valid pointers; sized here so processBlock never allocates.
    scratch_.setSize(2 * DR_NBUSES, samplesPerBlock);
    currentSr_.store(sampleRate);
    // Re-sync sequencer content into the (possibly reset) engine.
    shareSeqState(true, true);
}

bool DrumAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getChannelSet(false, 0) != juce::AudioChannelSet::stereo())
        return false; // MAIN must be stereo
    for (int b = 1; b < DR_NBUSES; ++b) {
        auto s = layouts.getChannelSet(false, b);
        if (!s.isDisabled() && s != juce::AudioChannelSet::stereo())
            return false; // each AUX: stereo or disabled
    }
    return true;
}

// ---- tables ---------------------------------------------------------------

const fable::GeneratedTable* DrumAudioProcessor::tableAt(int idx) const {
    if (idx < 0) return nullptr;
    if (idx < (int)tables_.size()) return tables_[(size_t)idx].get();
    int u = idx - (int)tables_.size();
    if (u < (int)userTables_.size()) return userTables_[(size_t)u].table.get();
    return nullptr; // empty user slot
}

juce::String DrumAudioProcessor::tableName(int idx) const {
    if (idx >= 0 && idx < (int)tables_.size())
        return juce::String(DRUM_TABLE_NAMES[(size_t)idx]);
    int u = idx - (int)tables_.size();
    if (u >= 0 && u < (int)userTables_.size())
        return juce::String(userTables_[(size_t)u].name);
    if (idx >= 0 && idx < (int)drumTableSlotNames().size())
        return juce::String(drumTableSlotNames()[(size_t)idx]); // empty "USER n"
    return "-";
}

void DrumAudioProcessor::rebuildEngineTables() {
    ++tablesGen_;
    std::vector<TablePtr> all;
    all.reserve(tables_.size() + userTables_.size());
    for (const auto& t : tables_) all.push_back(t);
    for (const auto& u : userTables_) all.push_back(u.table);
    engine.setTables(std::move(all)); // thread-safe swap; shares data, copies nothing
}

int DrumAudioProcessor::addUserTableForPad(int pad, fable::UserTable t) {
    if (pad < 0 || pad >= DR_NPADS) return -1;
    if ((int)userTables_.size() >= MAX_USER_TABLES) return -1;
    userTables_.push_back(std::move(t));
    rebuildEngineTables();
    const int idx = (int)tables_.size() + (int)userTables_.size() - 1;
    if (auto* p = apvts.getParameter("pad" + juce::String(pad) + ".oscA.table"))
        p->setValueNotifyingHost(p->convertTo0to1((float)idx));
    return idx;
}

// ---- pads / transport / selection (message thread) ------------------------

void DrumAudioProcessor::pushCmd(int type, int a, float v) {
    int s1, sz1, s2, sz2;
    cmdFifo_.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)      cmds_[(size_t)s1] = { type, a, v };
    else if (sz2 > 0) cmds_[(size_t)s2] = { type, a, v };
    cmdFifo_.finishedWrite(sz1 + sz2);
}

void DrumAudioProcessor::triggerPad(int pad, float vel) {
    if (pad < 0 || pad >= DR_NPADS) return;
    pushCmd(CmdTrigger, pad, juce::jlimit(0.0f, 1.0f, vel));
}

void DrumAudioProcessor::setSeqPlaying(bool on) {
    pushCmd(on ? CmdPlay : CmdStop, 0, 0);
    seqPlaying_.store(on); // optimistic; processBlock republishes the engine's state
}

void DrumAudioProcessor::setSelectedPad(int i) {
    selectedPad_ = juce::jlimit(0, DR_NPADS - 1, i);
    patchContextRevision_.fetch_add(1, std::memory_order_relaxed);
    pushCmd(CmdSelect, selectedPad_, 0);
    selectionBroadcaster.sendChangeMessage();
}

// ---- patterns / chain / names (message thread) -----------------------------

// Copy the message-thread sequencer content into the shared mirror the audio
// thread applies on its next block (try-lock there, so this never glitches).
void DrumAudioProcessor::shareSeqState(bool patterns, bool chain) {
    std::lock_guard<std::mutex> lk(shareMutex_);
    if (patterns) { patternsShared_ = patterns_; patternsDirty_ = true; }
    if (chain)    { chainShared_ = chain_;       chainDirty_ = true; }
}

uint8_t DrumAudioProcessor::getStep(int pattern, int pad, int step) const {
    if (pattern < 0 || pattern >= DR_NPATTERNS || pad < 0 || pad >= DR_NPADS
        || step < 0 || step >= DR_STEPS) return 0;
    return patterns_[(size_t)(pattern * DR_NPADS * DR_STEPS + pad * DR_STEPS + step)];
}

void DrumAudioProcessor::setStep(int pattern, int pad, int step, uint8_t v) {
    if (pattern < 0 || pattern >= DR_NPATTERNS || pad < 0 || pad >= DR_NPADS
        || step < 0 || step >= DR_STEPS) return;
    patterns_[(size_t)(pattern * DR_NPADS * DR_STEPS + pad * DR_STEPS + step)] =
        (uint8_t)juce::jmin((int)v, 2);
    shareSeqState(true, false);
}

void DrumAudioProcessor::setChain(std::vector<int> c) {
    chain_.clear();
    for (int p : c) chain_.push_back(juce::jlimit(0, DR_NPATTERNS - 1, p));
    if (chain_.empty()) chain_.push_back(0);
    shareSeqState(false, true);
}

void DrumAudioProcessor::setEditPattern(int p) {
    editPattern_ = juce::jlimit(0, DR_NPATTERNS - 1, p);
}

juce::String DrumAudioProcessor::getPadName(int i) const {
    return (i >= 0 && i < DR_NPADS) ? padNames_[(size_t)i] : juce::String();
}

void DrumAudioProcessor::setPadName(int i, juce::String n) {
    if (i < 0 || i >= DR_NPADS) return;
    n = n.replaceCharacter('\n', ' ').trim();
    padNames_[(size_t)i] = n.substring(0, 14);
}

// ---- kits as programs ------------------------------------------------------

void DrumAudioProcessor::setCurrentProgram(int index) {
    const auto& kits = factoryKits();
    if (index < 0 || index >= (int)kits.size()) return;
    patchContextRevision_.fetch_add(1, std::memory_order_relaxed);
    currentProgram_ = index;
    const DrumKit& kit = kits[(size_t)index];

    // Apply kit values onto the APVTS so the host + UI reflect them (WT-1 scheme).
    DrumParamArray pv = applyKit(kit);
    const auto& info = drumParamInfo();
    for (int i = 0; i < DR_NUM_PARAMS; ++i) {
        if (auto* param = apvts.getParameter(info[(size_t)i].pid)) {
            const auto& d = info[(size_t)i];
            float norm = (d.kind == Kind::Bool) ? (pv[(size_t)i] != 0.0f ? 1.0f : 0.0f)
                       : (d.kind == Kind::Enum) ? param->convertTo0to1(pv[(size_t)i])
                       : valueToNorm(d, pv[(size_t)i]);
            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
        }
    }

    // Non-param kit content: patterns, chain, pad names.
    if ((int)kit.patterns.size() == kPatternBytes)
        std::copy(kit.patterns.begin(), kit.patterns.end(), patterns_.begin());
    chain_ = kit.chain.empty() ? std::vector<int>{0} : kit.chain;
    for (int& c : chain_) c = juce::jlimit(0, DR_NPATTERNS - 1, c);
    for (int i = 0; i < DR_NPADS; ++i)
        padNames_[(size_t)i] = juce::String(kit.padNames[(size_t)i]);
    shareSeqState(true, true);
    selectionBroadcaster.sendChangeMessage(); // pad names / bindings changed
}

// ---- pad patches -------------------------------------------------------------

void DrumAudioProcessor::applyFactoryPatch(int index) {
    const auto& bank = factoryPatches();
    if (index < 0 || index >= (int)bank.size()) return;
    const auto& info = drumParamInfo();
    for (const auto& [id, v] : applyPatchToPad(selectedPad_, bank[(size_t)index])) {
        if (auto* param = apvts.getParameter(info[(size_t)id].pid)) {
            const auto& d = info[(size_t)id];
            float norm = (d.kind == Kind::Bool) ? (v != 0.0f ? 1.0f : 0.0f)
                       : (d.kind == Kind::Enum) ? param->convertTo0to1(v)
                       : valueToNorm(d, v);
            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
        }
    }
}

const juce::String DrumAudioProcessor::getProgramName(int index) {
    const auto& kits = factoryKits();
    if (index < 0 || index >= (int)kits.size()) return {};
    return juce::String(kits[(size_t)index].name);
}

// ---- audio ------------------------------------------------------------------

void DrumAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // Pull current parameter values into the engine's flat array.
    auto& p = engine.params();
    for (int i = 0; i < DR_NUM_PARAMS; ++i)
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

    // Drain the UI command FIFO (audition/transport/selection).
    {
        int s1, sz1, s2, sz2;
        cmdFifo_.prepareToRead(cmdFifo_.getNumReady(), s1, sz1, s2, sz2);
        auto run = [&](int start, int count) {
            for (int i = 0; i < count; ++i) {
                const Cmd& c = cmds_[(size_t)(start + i)];
                switch (c.type) {
                    case CmdTrigger: engine.trigger(c.a, c.v); break;
                    case CmdPlay:    engine.play();            break;
                    case CmdStop:    engine.stop();            break;
                    case CmdSelect:  engine.selectPad(c.a);    break;
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

    // MIDI: notes 36-51 trigger pads 0-15 through the same path as the
    // sequencer (choke groups, v2l/v2m, hit flags).
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) {
            const int note = m.getNoteNumber();
            if (note >= DR_MIDI_BASE && note < DR_MIDI_BASE + DR_NPADS)
                engine.trigger(note - DR_MIDI_BASE, m.getFloatVelocity());
            midiGlow_.store(20);
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            engine.panic();
        }
    }

    // 5-bus render. Disabled buses get scratch backing so the engine always
    // sees 10 valid pointers; MAIN is stereo by layout contract.
    if (scratch_.getNumSamples() < n)
        scratch_.setSize(2 * DR_NBUSES, n, false, false, true);
    float* outs[DR_NBUSES][2];
    for (int b = 0; b < DR_NBUSES; ++b) {
        auto bb = getBusBuffer(buffer, false, b);
        if (bb.getNumChannels() >= 2) {
            outs[b][0] = bb.getWritePointer(0);
            outs[b][1] = bb.getWritePointer(1);
        } else {
            outs[b][0] = scratch_.getWritePointer(2 * b);
            outs[b][1] = scratch_.getWritePointer(2 * b + 1);
        }
    }
    engine.render(outs, n);          // zero-fills all 10, then pads accumulate
    fx.process(outs[0][0], outs[0][1], n); // master FX on MAIN only; AUX dry

    // If the host gave MAIN a single channel despite the layout contract,
    // downmix the scratch-rendered stereo MAIN into it.
    if (auto mainBus = getBusBuffer(buffer, false, 0); mainBus.getNumChannels() == 1) {
        float* d = mainBus.getWritePointer(0);
        for (int i = 0; i < n; ++i) d[i] = 0.5f * (outs[0][0][i] + outs[0][1][i]);
    }

    // Publish transport/viz feedback for the editor.
    curStep_.store(engine.currentStep(), std::memory_order_relaxed);
    curPattern_.store(engine.currentPattern(), std::memory_order_relaxed);
    seqPlaying_.store(engine.isPlaying(), std::memory_order_relaxed);
    hitFlags_.fetch_or(engine.consumeHits(), std::memory_order_relaxed);
    vizA_.store(engine.vizA, std::memory_order_relaxed);
    vizB_.store(engine.vizB, std::memory_order_relaxed);
    vizEnv_.store(engine.vizEnv, std::memory_order_relaxed);
    if (int g = midiGlow_.load(); g > 0) midiGlow_.store(g - 1);

    // Post-FX scope ring buffer (MAIN mono mix).
    {
        const float* l0 = outs[0][0];
        const float* r0 = outs[0][1];
        int w = scopeW_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            scopeBuf_[(size_t)((w + i) & (kScopeSize - 1))] = 0.5f * (l0[i] + r0[i]);
        scopeW_.store(w + n, std::memory_order_relaxed);
    }
}

void DrumAudioProcessor::readScope(float* dst, int n) const {
    int w = scopeW_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        dst[i] = scopeBuf_[(size_t)((w - n + i) & (kScopeSize - 1))];
}

// ---- state ------------------------------------------------------------------

void DrumAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    if (!state.isValid()) return;
    // WT-1 scheme extended: parameter tree + user-table pool + a DRUM child
    // carrying the non-param session (patterns/chain/names/selection).
    juce::ValueTree root("DR1STATE");
    root.appendChild(state, nullptr);

    juce::ValueTree pool("USERTABLES");
    for (const auto& u : userTables_) {
        juce::ValueTree t("TABLE");
        t.setProperty("name", juce::String(u.name), nullptr);
        t.setProperty("frames", u.frames, nullptr);
        t.setProperty("wave", juce::Base64::toBase64(u.wave.data(),
                       u.wave.size() * sizeof(float)), nullptr);
        pool.appendChild(t, nullptr);
    }
    root.appendChild(pool, nullptr);

    juce::ValueTree drum("DRUM");
    drum.setProperty("patterns",
        juce::Base64::toBase64(patterns_.data(), patterns_.size()), nullptr);
    juce::StringArray chainStr;
    for (int c : chain_) chainStr.add(juce::String(c));
    drum.setProperty("chain", chainStr.joinIntoString(","), nullptr);
    juce::StringArray names;
    for (const auto& nm : padNames_) names.add(nm);
    drum.setProperty("padNames", names.joinIntoString("\n"), nullptr);
    drum.setProperty("selectedPad", selectedPad_, nullptr);
    drum.setProperty("editPattern", editPattern_, nullptr);
    root.appendChild(drum, nullptr);

    if (auto xml = root.createXml()) copyXmlToBinary(*xml, destData);
}

void DrumAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml) return;

    patchContextRevision_.fetch_add(1, std::memory_order_relaxed);

    juce::ValueTree params, pool, drum;
    if (xml->hasTagName("DR1STATE")) {
        auto root = juce::ValueTree::fromXml(*xml);
        params = root.getChildWithName(apvts.state.getType());
        pool   = root.getChildWithName("USERTABLES");
        drum   = root.getChildWithName("DRUM");
    } else if (xml->hasTagName(apvts.state.getType())) {
        params = juce::ValueTree::fromXml(*xml); // legacy: bare parameter tree
    }

    // 1. User tables first so oscA.table indices resolve when params apply.
    userTables_.clear();
    if (pool.isValid()) {
        for (int i = 0; i < pool.getNumChildren()
                        && (int)userTables_.size() < MAX_USER_TABLES; ++i) {
            auto t = pool.getChild(i);
            juce::String name = t.getProperty("name", "USER");
            int frames = (int)t.getProperty("frames", 1);
            juce::MemoryOutputStream raw;
            if (juce::Base64::convertFromBase64(raw, t.getProperty("wave", "").toString())) {
                // Cap the decoded float count so corrupt/hostile state can't
                // trigger a huge allocation (userTableFromWave clamps frames too).
                size_t maxFloats = (size_t)MAX_FRAMES * SIZE;
                int nf = (int)std::min((size_t)(raw.getDataSize() / sizeof(float)), maxFloats);
                if (nf > 0) {
                    std::vector<float> wave((size_t)nf);
                    std::memcpy(wave.data(), raw.getData(), (size_t)nf * sizeof(float));
                    userTables_.push_back(userTableFromWave(name.toStdString(), frames, wave));
                }
            }
        }
    }
    rebuildEngineTables();

    // 2. Patterns / chain / names / selection.
    if (drum.isValid()) {
        juce::MemoryOutputStream raw;
        if (juce::Base64::convertFromBase64(raw, drum.getProperty("patterns", "").toString())
            && raw.getDataSize() == patterns_.size()) {
            std::memcpy(patterns_.data(), raw.getData(), patterns_.size());
            for (auto& b : patterns_) if (b > 2) b = 2;
        }
        juce::StringArray chainStr;
        chainStr.addTokens(drum.getProperty("chain", "").toString(), ",", "");
        std::vector<int> c;
        for (const auto& s : chainStr)
            if (s.trim().isNotEmpty())
                c.push_back(juce::jlimit(0, DR_NPATTERNS - 1, s.getIntValue()));
        chain_ = c.empty() ? std::vector<int>{0} : std::move(c);
        juce::StringArray names;
        names.addLines(drum.getProperty("padNames", "").toString());
        for (int i = 0; i < DR_NPADS && i < names.size(); ++i)
            padNames_[(size_t)i] = names[i];
        selectedPad_ = juce::jlimit(0, DR_NPADS - 1, (int)drum.getProperty("selectedPad", 0));
        editPattern_ = juce::jlimit(0, DR_NPATTERNS - 1, (int)drum.getProperty("editPattern", 0));
        pushCmd(CmdSelect, selectedPad_, 0);
    }
    shareSeqState(true, true);

    // 3. Parameters last (their table indices now resolve).
    if (params.isValid())
        apvts.replaceState(params);

    selectionBroadcaster.sendChangeMessage();
}

// ---- editor -------------------------------------------------------------------

juce::AudioProcessorEditor* DrumAudioProcessor::createEditor() {
    return new DrumEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new DrumAudioProcessor();
}
