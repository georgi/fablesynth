// DR-1 plugin-boundary verification: instantiates the real DrumAudioProcessor
// and drives it the way a DAW would — APVTS parameter surface, the 5-bus
// multi-out layout, MIDI pad triggers, per-pad AUX routing, the internal
// sequencer via kit program 0 (TR-VOID), and host-tempo sync through a mock
// AudioPlayHead. Modeled on plugin_host_test.cpp.
#include "../source/drum/DrumProcessor.h"
#include "../source/drum/DrumEditor.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_fail = 0;
static void check(bool c, const char* msg, double val = 0) {
    printf("  [%s] %s (%.5f)\n", c ? "PASS" : "FAIL", msg, val);
    if (!c) g_fail++;
}

// Exposes AudioProcessor's protected static XML<->binary packing so the
// legacy-state test can hand-craft a bare-APVTS blob (never instantiated).
struct BinPacker : juce::AudioProcessor {
    static void pack(const juce::XmlElement& xml, juce::MemoryBlock& dest) {
        copyXmlToBinary(xml, dest);
    }
};

// Minimal host playhead. bpm-only by default (tempo sync); with reportPpq it
// also reports song position + isPlaying, auto-advancing ppq once per
// getPosition() call (= once per processBlock) like a rolling DAW transport.
struct MockPlayHead : juce::AudioPlayHead {
    double bpm = 90.0;
    bool   playing = true;
    bool   reportPpq = false;
    mutable double ppq = 0.0;
    double ppqInc = 0.0;          // ppq advance per block while playing
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override {
        juce::AudioPlayHead::PositionInfo pos;
        pos.setBpm(bpm);
        pos.setIsPlaying(playing);
        if (reportPpq) {
            pos.setPpqPosition(ppq);
            if (playing) ppq += ppqInc;
        }
        return pos;
    }
};

static void writePng(const juce::Image& img, const juce::File& out) {
    if (auto stream = out.createOutputStream()) {
        stream->setPosition(0); stream->truncate();
        juce::PNGImageFormat png;
        png.writeImageToStream(img, *stream);
        printf("  wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(),
               img.getWidth(), img.getHeight());
    }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for the processor

    DrumAudioProcessor proc;
    const double sr = 48000;
    const int block = 512;

    printf("\n== DR-1 plugin-boundary test (DrumAudioProcessor) ==\n");

    // ---- 1. parameter surface: all per-pad + global params in the APVTS ----
    const int nParams = (int)proc.getParameters().size();
    check(nParams == fable::DR_NUM_PARAMS || nParams == fable::DR_NUM_PARAMS + 1, // + host bypass
          "DR-1 host parameter catalog", nParams);
    check(proc.getName() == "FableSynth DR-1", "plugin name");

    // ---- 2. bus layout: 5 stereo output buses ----
    check(proc.getBusCount(false) == 5, "5 output buses declared", proc.getBusCount(false));
    proc.enableAllBuses();
    check(proc.getTotalNumOutputChannels() == 10, "10 channels with all buses enabled",
          proc.getTotalNumOutputChannels());

    proc.prepareToPlay(sr, block);
    juce::AudioBuffer<float> buf(10, block);
    bool allFinite = true;

    check(proc.getLatencySamples() > 0, "FX latency reported to the host",
          proc.getLatencySamples());

    // Sample-accurate MIDI: a pad trigger at offset K on a clean processor
    // must leave MAIN [0, K) exactly silent, with signal appearing at/after K
    // (the FX lookahead latency shifts it further right).
    {
        const int kOffset = 100;
        int firstNonZero = -1;
        juce::MidiBuffer om;
        om.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), kOffset);
        for (int b = 0; b < 2 && firstNonZero < 0; ++b) {
            buf.clear();
            juce::MidiBuffer empty;
            proc.processBlock(buf, b == 0 ? om : empty);
            auto bb = proc.getBusBuffer(buf, false, 0);
            for (int i = 0; i < block && firstNonZero < 0; ++i)
                if (std::fpclassify(bb.getSample(0, i)) != FP_ZERO
                    || std::fpclassify(bb.getSample(1, i)) != FP_ZERO)
                    firstNonZero = b * block + i;
        }
        check(firstNonZero >= kOffset, "pad trigger at offset 100: silence before the offset",
              firstNonZero);
        check(firstNonZero >= 0, "pad trigger at offset 100: signal after the offset",
              firstNonZero);
        // let the hit decay so the routing tests below start clean
        for (int b = 0; b < 60; ++b) {
            buf.clear();
            juce::MidiBuffer empty;
            proc.processBlock(buf, empty);
        }
    }

    // Render nBlocks, optionally firing a MIDI note at block 0; returns the
    // per-bus RMS and tracks step-counter changes + finiteness.
    int stepChanges = 0, lastStep = -1;
    auto render = [&](int nBlocks, int note, float vel) {
        std::array<double, 5> sumSq{};
        long cnt = 0;
        for (int b = 0; b < nBlocks; ++b) {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0 && note >= 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, vel), 0);
            proc.processBlock(buf, midi);
            int s = proc.getCurrentStep();
            if (s != lastStep) { stepChanges++; lastStep = s; }
            for (int bus = 0; bus < 5; ++bus) {
                auto bb = proc.getBusBuffer(buf, false, bus);
                for (int ch = 0; ch < bb.getNumChannels(); ++ch) {
                    const float* d = bb.getReadPointer(ch);
                    for (int i = 0; i < block; ++i) {
                        if (!std::isfinite(d[i])) allFinite = false;
                        sumSq[(size_t)bus] += (double)d[i] * d[i];
                    }
                }
            }
            cnt += 2 * block;
        }
        std::array<double, 5> rms{};
        for (int bus = 0; bus < 5; ++bus) rms[(size_t)bus] = std::sqrt(sumSq[(size_t)bus] / (double)cnt);
        return rms;
    };

    // ---- 3. MIDI note 36 (pad 0) -> audio on MAIN within 2 blocks ----
    proc.consumeHitFlags(); // clear
    auto r3 = render(2, 36, 1.0f);
    check(r3[0] > 1e-5, "MIDI note 36 -> MAIN audible within 2 blocks", r3[0]);
    check((proc.consumeHitFlags() & 1u) != 0, "hit flag published for pad 0");
    check(proc.getMidiActive(), "MIDI activity LED glows");
    render(30, -1, 0); // let the pad decay

    // ---- 4. multi-out: pad 0 -> AUX 2 renders on bus 2, silent on MAIN ----
    // Reverb off while checking MAIN silence: test 3's MAIN hit leaves a long
    // Freeverb tail that would mask the routing assertion otherwise.
    if (auto* p = proc.apvts.getParameter("fx.reverb.on"))
        p->setValueNotifyingHost(0.0f);
    render(60, -1, 0); // flush the wet tail (equal-power dry ramp ~0.02 s)
    if (auto* p = proc.apvts.getParameter("pad0.out"))
        p->setValueNotifyingHost(p->convertTo0to1(2.0f)); // AUX 2
    auto r4 = render(20, 36, 1.0f);
    check(r4[2] > 1e-4, "pad 0 routed to AUX 2 audible on bus 2", r4[2]);
    check(r4[0] < 1e-6, "MAIN silent while pad routed to AUX 2", r4[0]);
    if (auto* p = proc.apvts.getParameter("pad0.out"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f)); // back to MAIN
    render(40, -1, 0); // decay (program 0 below restores fx.reverb.on)

    // ---- 5. sequencer with TR-VOID (program 0) ----
    check(proc.getNumPrograms() == 18, "18 kit programs", proc.getNumPrograms());
    check(proc.getProgramName(0) == "TR-VOID", "program 0 is TR-VOID");
    const auto patchRevisionBeforeKit = proc.getPatchContextRevision();
    proc.setCurrentProgram(0);
    check(proc.getPatchContextRevision() != patchRevisionBeforeKit,
          "reloading current kit invalidates patch readout");
    float bpmParam = proc.apvts.getRawParameterValue("seq.bpm")->load();
    check(std::abs(bpmParam - 126.0f) < 0.5f, "TR-VOID applies seq.bpm=126 to APVTS", bpmParam);

    proc.setSeqPlaying(true);
    stepChanges = 0; lastStep = -1;
    // one bar at 126 bpm = 16 * (60/126/4) * 48000 ~= 91429 samples ~= 179 blocks
    auto r5 = render(180, -1, 0);
    check(proc.isSeqPlaying(), "sequencer reports playing");
    check(r5[0] > 1e-4, "TR-VOID bar produces audio on MAIN", r5[0]);
    check(stepChanges >= 12, "step counter advances across the bar", stepChanges);
    check(proc.getCurrentPattern() == 0, "current pattern is chain[0] = A", proc.getCurrentPattern());
    proc.setSeqPlaying(false);
    render(4, -1, 0);
    check(proc.getCurrentStep() == -1, "stopped: currentStep reads -1", proc.getCurrentStep());
    check(!proc.isHostSynced(), "no playhead -> not host-synced");

    // ---- 6. host tempo: mock playhead at 90 bpm drives the step clock ----
    MockPlayHead ph;
    proc.setPlayHead(&ph);
    proc.setSeqPlaying(true);
    stepChanges = 0; lastStep = -1;
    // 4 s at 90 bpm = 90/60*4 = 6 steps/s -> ~24 step advances (vs 33.6 at 126).
    const int blocks4s = (int)(4.0 * sr / block);
    render(blocks4s, -1, 0);
    check(proc.isHostSynced(), "host tempo reported -> isHostSynced()");
    check(std::abs(proc.getHostBpm() - 90.0) < 0.01, "getHostBpm() = 90", proc.getHostBpm());
    check(stepChanges >= 21 && stepChanges <= 27,
          "step spacing follows host 90 bpm (~24 steps in 4 s)", stepChanges);
    proc.setSeqPlaying(false);
    proc.setPlayHead(nullptr);
    render(2, -1, 0);
    check(!proc.isHostSynced(), "playhead removed -> sync drops");
    render(60, -1, 0); // let tails die before the transport-lock checks

    // ---- 6b. host transport lock: isPlaying + ppq slave the sequencer ----
    // The host rolls -> DR-1 plays TR-VOID from song position with NO internal
    // play; the host stops -> the sequencer stops.
    MockPlayHead tph;
    tph.bpm = 120.0; tph.reportPpq = true;
    tph.ppqInc = (double)block / sr * (120.0 / 60.0);
    proc.setPlayHead(&tph);
    stepChanges = 0; lastStep = -1;
    auto r6b = render((int)(2.0 * sr / block), -1, 0);   // 2 s, transport rolling
    check(proc.isSeqPlaying(), "host transport -> sequencer reports playing");
    check(r6b[0] > 1e-4, "host transport drives TR-VOID without internal play", r6b[0]);
    // 2 s at 120 bpm = 16 step advances (8 steps/s)
    check(stepChanges >= 13 && stepChanges <= 19,
          "step count follows host position (~16 in 2 s)", stepChanges);
    tph.playing = false;                                  // host hits stop
    render(4, -1, 0);
    check(!proc.isSeqPlaying(), "host stop -> sequencer stops");
    check(proc.getCurrentStep() == -1, "host stop resets the step readout");
    proc.setPlayHead(nullptr);
    render(60, -1, 0); // decay

    // ---- 7. everything rendered finite ----
    check(allFinite, "all bus outputs finite");

    // ---- 8. full state round-trip into a fresh processor ----
    // Mutate params + every piece of non-param session state, save, restore
    // into a second instance, and verify all of it comes back.
    printf("\n== state round-trip ==\n");
    if (auto* p = proc.apvts.getParameter("pad3.flt.cut"))
        p->setValueNotifyingHost(p->convertTo0to1(1234.0f));
    proc.setStep(0, 3, 7, 2);
    proc.setChain({ 0, 1 });
    proc.setPadName(3, "ZAP");
    proc.setSelectedPad(3);
    std::vector<float> sine((size_t)fable::SIZE);
    for (int i = 0; i < fable::SIZE; ++i)
        sine[(size_t)i] = (float)std::sin(2.0 * juce::MathConstants<double>::pi * i / fable::SIZE);
    const int userIdx = proc.addUserTableForPad(2, fable::makeUserTable("IMP", { sine }));
    check(userIdx >= 10, "user table appended after the 10 factory tables", userIdx);
    float padTable = proc.apvts.getRawParameterValue("pad2.oscA.table")->load();
    check((int)std::lround(padTable) == userIdx, "import points pad2 oscA.table at it", padTable);

    juce::MemoryBlock state;
    proc.getStateInformation(state);
    check(state.getSize() > 0, "state serialises", (double)state.getSize());

    DrumAudioProcessor proc2;
    // Fresh instance boots on TR-VOID; prove the restore actually changes it.
    check(proc2.getPadName(3) != "ZAP", "fresh instance differs before restore");
    check(proc2.numTables() == 15, "fresh instance has no user tables", proc2.numTables());
    const auto patchRevisionBeforeRestore = proc2.getPatchContextRevision();
    proc2.setStateInformation(state.getData(), (int)state.getSize());
    check(proc2.getPatchContextRevision() != patchRevisionBeforeRestore,
          "state restore invalidates patch readout");

    float cut2 = proc2.apvts.getRawParameterValue("pad3.flt.cut")->load();
    check(std::abs(cut2 - 1234.0f) < 2.0f, "pad3.flt.cut round-trips", cut2);
    check(proc2.getStep(0, 3, 7) == 2, "step (pat 0, pad 3, step 7) = accent round-trips",
          proc2.getStep(0, 3, 7));
    check(proc2.getChain() == std::vector<int>({ 0, 1 }), "chain {0,1} round-trips");
    check(proc2.getPadName(3) == "ZAP", "pad name ZAP round-trips");
    check(proc2.getSelectedPad() == 3, "selected pad 3 round-trips", proc2.getSelectedPad());
    check(proc2.numTables() == proc.numTables(), "user table pool size round-trips",
          proc2.numTables());
    check(proc2.tableName(userIdx) == "IMP", "user table name IMP round-trips");
    float padTable2 = proc2.apvts.getRawParameterValue("pad2.oscA.table")->load();
    check((int)std::lround(padTable2) == userIdx, "pad2 oscA.table index round-trips", padTable2);
    check(proc2.tableAt(userIdx) != nullptr, "restored user table resolves in the engine list");

    // ---- 9. kit programs beyond TR-VOID ----
    printf("\n== kit programs ==\n");
    check(proc2.getProgramName(1) == "ROOM ONE", "program 1 is ROOM ONE");
    check(proc2.getProgramName(2) == "BITCRUSH", "program 2 is BITCRUSH");
    proc2.setCurrentProgram(1);
    check(proc2.getCurrentProgram() == 1, "current program tracks", proc2.getCurrentProgram());
    float bpm1 = proc2.apvts.getRawParameterValue("seq.bpm")->load();
    check(std::abs(bpm1 - 116.0f) < 0.5f, "ROOM ONE applies seq.bpm=116", bpm1);

    // ---- 10. legacy tolerance: a bare APVTS tree loads without crashing ----
    printf("\n== legacy state ==\n");
    {
        DrumAudioProcessor proc3;
        juce::ValueTree bare(proc3.apvts.state.getType()); // no DR1STATE wrapper, no DRUM child
        bare.setProperty(juce::Identifier("nonsense"), 42, nullptr);
        juce::ValueTree legacyFx("PARAM");
        legacyFx.setProperty("id", "fx.delay.mix", nullptr);
        legacyFx.setProperty("value", 0.42f, nullptr);
        bare.appendChild(legacyFx, nullptr);
        juce::MemoryBlock legacy;
        if (auto xml = bare.createXml()) BinPacker::pack(*xml, legacy);
        check(legacy.getSize() > 0, "legacy blob built", (double)legacy.getSize());
        proc3.setStateInformation(legacy.getData(), (int)legacy.getSize());
        check(proc3.getParameters().size() > 0, "processor alive after legacy load");
        bool broadcast = true;
        for (int pad = 0; pad < fable::DR_NPADS; ++pad)
            broadcast = broadcast && std::abs(proc3.apvts.getRawParameterValue(
                "pad" + juce::String(pad) + ".fx.delay.mix")->load() - 0.42f) < 0.001f;
        check(broadcast, "legacy top-level FX broadcasts to every pad");
        // Garbage bytes must not crash either.
        const char junk[] = "not a state blob";
        proc3.setStateInformation(junk, (int)sizeof(junk));
        check(proc3.getName() == "FableSynth DR-1", "processor alive after garbage load");
    }

    // ---- 11. editor: headless full-rack render to PNG (Task 9 shell) ----
    printf("\n== editor snapshot ==\n");
    {
        juce::File dir(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                                : juce::File::getCurrentWorkingDirectory());
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        auto* drumEd = dynamic_cast<DrumEditor*>(ed.get());
        check(drumEd != nullptr, "createEditor returns DrumEditor");
        ed->setSize(DrumRack::LW, DrumRack::LH); // logical rack size — 1:1 render
        juce::Image img = ed->createComponentSnapshot(ed->getLocalBounds());
        check(img.isValid() && img.getWidth() == DrumRack::LW && img.getHeight() == DrumRack::LH,
              "snapshot rendered at 1460x880", img.getWidth());
        // The full-rack device body also occupies the header's coordinates.
        // The header must remain above it or the kit stepper cannot be clicked.
        // (The "next kit" button sits right of the KIT label + prev + name well;
        // the KIT mini-head shifted the stepper ~24px right of the old layout.)
        const int programBeforeClick = proc.getCurrentProgram();
        auto* nextKitHit = drumEd->getRack().getComponentAt(480, 54);
        check(dynamic_cast<juce::Button*>(nextKitHit) != nullptr,
              "kit stepper is the header hit target");
        if (auto* button = dynamic_cast<juce::Button*>(nextKitHit)) button->onClick();
        check(proc.getCurrentProgram() == (programBeforeClick + 1) % proc.getNumPrograms(),
              "kit stepper changes program");
        const juce::File out = dir.getChildFile("drum_editor.png");
        writePng(img, out);
        check(out.existsAsFile() && out.getSize() > 0, "drum_editor.png written",
              (double)out.getSize());
        // Non-blank: section-centre pixels must differ from the bare editor
        // background sampled at the rack's outer padding (x=8 is left of every
        // panel, which starts at x=18).
        struct Probe { const char* name; int x, y; };
        const Probe probes[] = {
            { "header",    700,  54 },
            { "pad grid",  194, 287 },
            { "osc row",   910, 264 },
            { "fx rack",   730, 665 },
            { "step seq",  730, 805 },
            // Task 11 pad editor panels (centres of view/knob areas)
            { "osc A terrain",   560, 230 },
            { "noise view",     1344, 230 },
            { "pitch env view",  490, 458 },
            { "amp env view",    742, 458 },
            { "filter view",    1010, 458 },
            { "mod rows",       1200, 452 },
            // Task 13 FX rack + OUT panel (DRIVE knob body / MAIN route dot)
            { "fx drive knob",    89, 673 },
            { "out main dot",   1256, 658 },
        };
        for (const auto& p : probes) {
            const juce::Colour bg = img.getPixelAt(8, p.y);
            check(img.getPixelAt(p.x, p.y) != bg, p.name, (double)p.x);
        }

        // Task 11: selection rebinding — switching pads destroys and recreates
        // every pad-bound control headlessly, then the rack still renders.
        // (setSelectedPad broadcasts async; deliver it now, no dispatch loop.)
        proc.setSelectedPad(7);
        proc.selectionBroadcaster.dispatchPendingMessages();
        check(proc.getSelectedPad() == 7, "pad 7 selected for rebind");
        juce::Image img2 = ed->createComponentSnapshot(ed->getLocalBounds());
        check(img2.isValid() && img2.getWidth() == DrumRack::LW,
              "snapshot after pad-7 rebind renders");
        for (const auto& p : probes) {
            const juce::Colour bg = img2.getPixelAt(8, p.y);
            check(img2.getPixelAt(p.x, p.y) != bg,
                  (juce::String("rebound ") + p.name).toRawUTF8(), (double)p.x);
        }
        proc.setSelectedPad(3); // restore the state the round-trip section set
        proc.selectionBroadcaster.dispatchPendingMessages();
    }

    // ---- 12. pad grid: drop-WAV import + QWERTY trigger (Task 10) ----
    printf("\n== pad grid ==\n");
    {
        // Write a 440 Hz sine WAV to a temp file — the "dropped" payload.
        juce::TemporaryFile tmp(".wav");
        {
            juce::WavAudioFormat wavFmt;
            std::unique_ptr<juce::AudioFormatWriter> writer(wavFmt.createWriterFor(
                tmp.getFile().createOutputStream().release(), 48000.0, 1, 16, {}, 0));
            check(writer != nullptr, "temp WAV writer created");
            const int nsamp = 48000 / 2; // 0.5 s
            juce::AudioBuffer<float> wave(1, nsamp);
            for (int i = 0; i < nsamp; ++i)
                wave.setSample(0, i, 0.8f * (float)std::sin(2.0 * juce::MathConstants<double>::pi
                                                            * 440.0 * i / 48000.0));
            if (writer) { writer->writeFromAudioSampleBuffer(wave, 0, nsamp); writer->flush(); }
        }

        fui::PadGrid grid(proc);
        grid.setBounds(0, 0, 352, 369);
        check(grid.isInterestedInFileDrag({ tmp.getFile().getFullPathName() }),
              "grid accepts .wav drags");
        check(!grid.isInterestedInFileDrag({ "notes.txt" }), "grid rejects non-audio drags");

        const int before = proc.numTables();
        const auto tile0 = grid.tileBounds(0).getCentre();  // pad 01, bottom-left
        check(grid.padAt(tile0) == 0, "tile centre hit-tests to pad 0", grid.padAt(tile0));
        grid.filesDropped({ tmp.getFile().getFullPathName() }, tile0.x, tile0.y);
        check(proc.numTables() == before + 1, "drop grew the table pool", proc.numTables());
        const float pad0Table = proc.apvts.getRawParameterValue("pad0.oscA.table")->load();
        check((int)std::lround(pad0Table) == before,
              "pad0.oscA.table points at the imported table", pad0Table);
        check(proc.tableAt(before) != nullptr, "imported table resolves");

        // QWERTY: 'z' = pad 0 (useDrumKeys zxcv row) -> trigger reaches the engine.
        proc.consumeHitFlags();
        check(grid.keyPressed(juce::KeyPress('z', {}, 'z'), nullptr), "'z' key handled");
        render(2, -1, 0);
        check((proc.consumeHitFlags() & 1u) != 0, "'z' triggered pad 0 (hit flag)");
        check(!grid.keyPressed(juce::KeyPress('m', {}, 'm'), nullptr), "unmapped key ignored");
    }

    // ---- 13. step sequencer view: tap cycle + chain builder (Task 12) ----
    // Drives the same handlers a mouse click hits (StepSeq.tsx / store.ts port).
    printf("\n== step seq view ==\n");
    {
        fui::StepSeqView seq(proc);
        seq.setBounds(0, 0, 1424, 105);
        proc.setSelectedPad(0);
        proc.selectionBroadcaster.dispatchPendingMessages();
        proc.setEditPattern(0);

        // Tap cycle on (pattern 0, pad 0, step 3): 1 -> 2 -> 0 -> 1.
        proc.setStep(0, 0, 3, 1);
        seq.toggleStep(3);
        check(proc.getStep(0, 0, 3) == 2, "tap cycles ON -> ACCENT", proc.getStep(0, 0, 3));
        seq.toggleStep(3);
        check(proc.getStep(0, 0, 3) == 0, "tap cycles ACCENT -> OFF", proc.getStep(0, 0, 3));
        seq.toggleStep(3);
        check(proc.getStep(0, 0, 3) == 1, "tap cycles OFF -> ON", proc.getStep(0, 0, 3));
        // The tap edits the selected pad's lane in the edit pattern only.
        check(proc.getStep(0, 1, 3) == 0, "other pads untouched");
        check(seq.stepBounds(3).getCentre().y > 47, "step tiles sit in the row band",
              seq.stepBounds(3).getCentre().y);

        // Bar selection edits independently from the playback length.
        seq.patternClick(1);
        check(proc.getEditPattern() == 1, "bar click selects 2", proc.getEditPattern());
        check(proc.getChain() == std::vector<int>({ 0 }), "bar click preserves length");
        seq.setSequenceLength(3);
        check(proc.getChain() == std::vector<int>({ 0, 1, 2 }), "length 3 plays bars 1-3");
        seq.patternClick(3);
        check(proc.getEditPattern() == 3, "bar click selects 4", proc.getEditPattern());
        check(proc.getChain() == std::vector<int>({ 0, 1, 2 }), "editing bar 4 preserves length");

        // RAND: rewrites the selected pad's row in the edit pattern only.
        // A deterministic rng ("always off") clears the target lane and must
        // not touch any other pad's lane or another pattern.
        proc.setEditPattern(2);
        proc.setSelectedPad(5);
        proc.setStep(2, 6, 4, 1);  // another pad, same pattern — must survive
        proc.setStep(3, 5, 4, 2);  // same pad, another pattern — must survive
        for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(2, 5, s, 1);
        seq.randomizePad([] { return 0.9f; });  // rng always "off"
        bool laneCleared = true;
        for (int s = 0; s < fable::DR_STEPS; ++s) laneCleared = laneCleared && proc.getStep(2, 5, s) == 0;
        check(laneCleared, "RAND (rng=off) clears the target pad row");
        check(proc.getStep(2, 6, 4) == 1, "RAND leaves other pads in the pattern untouched");
        check(proc.getStep(3, 5, 4) == 2, "RAND leaves the same pad in other patterns untouched");

        // A deterministic "always accent" rng fills the lane with accents (2),
        // and every written byte is a valid step value.
        seq.randomizePad([] { return 0.0f; });  // on and accent both fire
        bool allAccent = true, allValid = true;
        for (int s = 0; s < fable::DR_STEPS; ++s) {
            const auto v = proc.getStep(2, 5, s);
            allAccent = allAccent && v == 2;
            allValid = allValid && v <= 2;
        }
        check(allAccent, "RAND (rng=0) fills the lane with accents");
        check(allValid, "RAND only ever writes valid step values");

        // RAND is undoable — undo restores the state before the last RAND
        // (the all-off lane the first RAND produced).
        seq.undo();
        bool restored = true;
        for (int s = 0; s < fable::DR_STEPS; ++s) restored = restored && proc.getStep(2, 5, s) == 0;
        check(restored, "RAND pushes an undo entry");
    }

    // ---- 14. step sequencer editing: selection, verbs, drag, undo (decision 6) ----
    printf("\n== step seq editing (decision 6) ==\n");
    {
        fui::StepSeqView seq(proc);
        seq.setBounds(0, 0, 1424, 105);
        proc.setEditPattern(0);
        proc.setChain({ 0 });
        for (int pat = 0; pat < fable::DR_NPATTERNS; ++pat)
            for (int pad = 0; pad < fable::DR_NPADS; ++pad)
                for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(pat, pad, s, 0);

        // -- selection: extend from an anchor, re-anchor on a fresh Shift-click --
        proc.setSelectedPad(2);
        seq.extendSelection(2);
        check(seq.hasSelection() && !seq.isSelectAll() && seq.selectionFrom() == 2 && seq.selectionTo() == 2,
              "extendSelection sets a 1-step anchor");
        seq.extendSelection(5);
        check(seq.selectionFrom() == 2 && seq.selectionTo() == 5, "extendSelection extends to the head");
        seq.extendSelection(1); // head crosses back past the anchor
        check(seq.selectionFrom() == 1 && seq.selectionTo() == 2,
              "extendSelection re-normalizes when the head passes the anchor");
        seq.clearSelection();
        check(!seq.hasSelection(), "Esc-equivalent clearSelection empties the selection");

        // -- Cmd-A sets the select-all flag; any narrowing clears it --
        seq.selectAllPattern();
        check(seq.hasSelection() && seq.isSelectAll(), "Cmd-A sets select-all");
        seq.extendSelection(3);
        check(seq.hasSelection() && !seq.isSelectAll() && seq.selectionFrom() == 3 && seq.selectionTo() == 3,
              "narrowing the selection clears select-all");

        // -- copy/paste a range: pad 2 steps 2..5 -> pad 3 steps 0..3 --
        proc.setStep(0, 2, 2, 1); proc.setStep(0, 2, 3, 2); proc.setStep(0, 2, 4, 1); proc.setStep(0, 2, 5, 2);
        seq.clearSelection(); // fresh anchor: extendSelection continues from the OLD anchor otherwise
        seq.extendSelection(2); seq.extendSelection(5);
        check(seq.selectionFrom() == 2 && seq.selectionTo() == 5, "range re-selected for copy");
        seq.copySelection();
        check(seq.hasClipboard(), "copySelection fills the clipboard");
        proc.setSelectedPad(3);
        seq.clearSelection(); // paste with no selection lands at step 0
        seq.pasteSelection();
        check(proc.getStep(0, 3, 0) == 1 && proc.getStep(0, 3, 1) == 2
              && proc.getStep(0, 3, 2) == 1 && proc.getStep(0, 3, 3) == 2,
              "pasteRange lands the copied range at step 0 with no selection");
        check(proc.getStep(0, 2, 2) == 1, "copy left the source pad untouched");

        // -- cut clears the source and still fills the clipboard --
        proc.setSelectedPad(2);
        seq.extendSelection(2); seq.extendSelection(5);
        seq.cutSelection();
        check(proc.getStep(0, 2, 2) == 0 && proc.getStep(0, 2, 5) == 0, "cutSelection clears the source range");
        proc.setSelectedPad(4);
        seq.clearSelection();
        seq.pasteSelection();
        check(proc.getStep(0, 4, 0) == 1 && proc.getStep(0, 4, 3) == 2, "cut payload pastes elsewhere");

        // -- paste/duplicate starting past the pattern end is a total no-op (fixed semantics) --
        proc.setSelectedPad(5);
        for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(0, 5, s, (uint8_t)((s % 2) + 1));
        seq.clearSelection();
        seq.extendSelection(fable::DR_STEPS - 2); seq.extendSelection(fable::DR_STEPS - 1);
        auto beforeNoop = std::vector<int>();
        for (int s = 0; s < fable::DR_STEPS; ++s) beforeNoop.push_back(proc.getStep(0, 5, s));
        seq.duplicateSelection(); // dest = 16, past the end -> no-op
        bool unchanged = true;
        for (int s = 0; s < fable::DR_STEPS; ++s) unchanged = unchanged && proc.getStep(0, 5, s) == beforeNoop[(size_t)s];
        check(unchanged, "duplicate/paste starting past the pattern end is a no-op (no wrap)");

        // -- duplicate within range: paste immediately after, clamped --
        proc.setSelectedPad(6);
        for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(0, 6, s, 0);
        proc.setStep(0, 6, 0, 1); proc.setStep(0, 6, 1, 2);
        seq.clearSelection();
        seq.extendSelection(0); seq.extendSelection(1);
        seq.duplicateSelection();
        check(proc.getStep(0, 6, 2) == 1 && proc.getStep(0, 6, 3) == 2,
              "duplicateSelection pastes the range right after itself");

        // -- delete only acts on an explicit selection --
        proc.setSelectedPad(7);
        proc.setStep(0, 7, 0, 1);
        seq.clearSelection();
        seq.deleteSelection();
        check(proc.getStep(0, 7, 0) == 1, "deleteSelection with nothing selected is a no-op");
        seq.extendSelection(0);
        seq.deleteSelection();
        check(proc.getStep(0, 7, 0) == 0, "deleteSelection clears the selected step");

        // -- step-range drag-shift: move (source vacated) and Alt-copy (source kept) --
        proc.setSelectedPad(8);
        for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(0, 8, s, 0);
        proc.setStep(0, 8, 0, 1); proc.setStep(0, 8, 1, 2);
        seq.clearSelection();
        seq.extendSelection(0); seq.extendSelection(1);
        seq.shiftSelection(10, false); // move to steps 10-11
        check(proc.getStep(0, 8, 0) == 0 && proc.getStep(0, 8, 1) == 0, "shiftRange move vacates the source");
        check(proc.getStep(0, 8, 10) == 1 && proc.getStep(0, 8, 11) == 2, "shiftRange move lands at dest");
        check(seq.selectionFrom() == 10 && seq.selectionTo() == 11, "selection follows the moved range");
        seq.clearSelection(); // fresh anchor for the re-select, not a continued extend
        seq.extendSelection(10); seq.extendSelection(11); // re-select the moved pair
        seq.shiftSelection(0, true); // Alt-copy back to steps 0-1
        check(proc.getStep(0, 8, 0) == 1 && proc.getStep(0, 8, 1) == 2, "shiftRange copy writes the dest");
        check(proc.getStep(0, 8, 10) == 1 && proc.getStep(0, 8, 11) == 2, "shiftRange copy leaves the source intact");

        // -- shiftRange dest-at-end: vacates the source without bleeding past the pattern --
        proc.setSelectedPad(9);
        for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(0, 9, s, 0);
        proc.setStep(0, 9, 0, 1); proc.setStep(0, 9, 1, 2); proc.setStep(0, 9, 2, 1);
        seq.clearSelection();
        seq.extendSelection(0); seq.extendSelection(2);
        seq.shiftSelection(fable::DR_STEPS - 1, false); // dest = 15, only step 0's value fits
        check(proc.getStep(0, 9, 15) == 1, "shiftRange dest-at-end keeps only what fits");
        check(proc.getStep(0, 9, 0) == 0 && proc.getStep(0, 9, 1) == 0 && proc.getStep(0, 9, 2) == 0,
              "shiftRange dest-at-end still vacates the whole source range");

        // -- whole-pattern copy/paste (Cmd-A scope) --
        proc.setEditPattern(0);
        for (int pad = 0; pad < fable::DR_NPADS; ++pad)
            for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(0, pad, s, (uint8_t)((pad + s) % 3));
        seq.selectAllPattern();
        seq.copySelection();
        proc.setEditPattern(1);
        for (int pad = 0; pad < fable::DR_NPADS; ++pad)
            for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(1, pad, s, 0);
        seq.clearSelection();
        seq.pasteSelection();
        bool wholePatternMatches = true;
        for (int pad = 0; pad < fable::DR_NPADS && wholePatternMatches; ++pad)
            for (int s = 0; s < fable::DR_STEPS && wholePatternMatches; ++s)
                wholePatternMatches = proc.getStep(1, pad, s) == proc.getStep(0, pad, s);
        check(wholePatternMatches, "select-all copy/paste moves the whole pattern (all pads)");

        // -- duplicate with no selection: copy pattern to next bar, extending length --
        proc.setEditPattern(0);
        proc.setChain({ 0 });
        proc.setStep(0, 0, 0, 2);
        seq.clearSelection();
        seq.duplicatePattern();
        check(proc.getStep(1, 0, 0) == 2, "duplicatePattern copies pattern 0 into pattern 1");
        check(proc.getChain() == std::vector<int>({ 0, 1 }), "duplicatePattern extends the sequence length");
        proc.setEditPattern(fable::DR_NPATTERNS - 1);
        auto chainBefore = proc.getChain();
        seq.duplicatePattern();
        check(proc.getChain() == chainBefore, "duplicatePattern on the last bar is a no-op (no bar 5)");

        // -- bar-chip drag: plain = swap, Alt = copy (leaves source untouched) --
        proc.setChain({ 0, 1, 2, 3 });
        for (int pat = 0; pat < fable::DR_NPATTERNS; ++pat)
            for (int pad = 0; pad < fable::DR_NPADS; ++pad)
                for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(pat, pad, s, 0);
        proc.setStep(0, 0, 0, 1); // marker A in pattern 0
        proc.setStep(2, 0, 0, 2); // marker B in pattern 2
        seq.movePattern(0, 2, false); // swap patterns 0 and 2
        check(proc.getStep(0, 0, 0) == 2 && proc.getStep(2, 0, 0) == 1,
              "movePattern(copy=false) swaps the two patterns");
        seq.movePattern(0, 1, true); // Alt-copy pattern 0 (now marker B=2) over pattern 1
        check(proc.getStep(1, 0, 0) == 2 && proc.getStep(0, 0, 0) == 2,
              "movePattern(copy=true) copies without touching the source");

        // -- undo/redo: one entry per verb, restoring the mutated cell --
        proc.setEditPattern(0);   // earlier duplicatePattern-on-last-bar test left editPattern at 3
        proc.setSelectedPad(11);
        for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(0, 11, s, 0);
        proc.setStep(0, 11, 0, 1);
        seq.clearSelection();
        seq.extendSelection(0);
        seq.deleteSelection();
        check(proc.getStep(0, 11, 0) == 0, "delete applied");
        check(seq.canUndo() && !seq.canRedo(), "delete pushed one undo entry");
        seq.undo();
        check(proc.getStep(0, 11, 0) == 1, "undo restores the deleted step");
        check(seq.canRedo(), "undo enables redo");
        seq.redo();
        check(proc.getStep(0, 11, 0) == 0, "redo re-applies the delete");

        // -- keyPressed: modifier combos only; plain Escape/Delete fall through when idle --
        seq.clearSelection();
        check(!seq.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),
              "plain Escape with no selection/drag falls through (PadGrid stop stays live)");
        check(!seq.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),
              "plain Delete with no selection falls through");
        seq.extendSelection(0);
        check(seq.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "Escape with an active selection is claimed");
        check(!seq.hasSelection(), "Escape cleared the selection");

        proc.setSelectedPad(12);
        for (int s = 0; s < fable::DR_STEPS; ++s) proc.setStep(0, 12, s, 0);
        proc.setStep(0, 12, 4, 2);
        seq.extendSelection(4);
        const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);
        const juce::ModifierKeys cmdShift(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
        check(seq.keyPressed(juce::KeyPress('c', cmd, 'c')), "Cmd-C claimed");
        check(seq.hasClipboard(), "Cmd-C copied to the clipboard");
        check(seq.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey, {}, 0)),
              "Delete claimed while a selection is active");
        check(proc.getStep(0, 12, 4) == 0, "Delete cleared the selected step");
        seq.extendSelection(4);
        check(seq.keyPressed(juce::KeyPress('v', cmd, 'v')), "Cmd-V claimed");
        check(proc.getStep(0, 12, 4) == 2, "Cmd-V pasted the clipboard back");
        check(seq.keyPressed(juce::KeyPress('a', cmd, 'a')), "Cmd-A claimed");
        check(seq.isSelectAll(), "Cmd-A selected the whole pattern");
        check(seq.keyPressed(juce::KeyPress('z', cmd, 'z')), "Cmd-Z claimed");
        check(seq.keyPressed(juce::KeyPress('z', cmdShift, 'z')), "Shift-Cmd-Z claimed");
        check(!seq.keyPressed(juce::KeyPress('c', {}, 'c')), "plain 'c' (no modifier) is not claimed");
    }

    printf("%s\n", g_fail == 0 ? "DRUM PLUGIN CHECKS PASSED" : "DRUM PLUGIN CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
