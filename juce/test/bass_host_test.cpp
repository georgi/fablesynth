// BL-1 plugin-boundary verification: instantiates the real BassAudioProcessor
// and drives it the way a DAW would — APVTS parameter surface, MIDI audition,
// the internal sequencer via patch program 0 (ACID LINE), host-tempo sync and
// transport lock through a mock AudioPlayHead, state round-trip, and a
// headless editor render. Modeled on drum_host_test.cpp.
#include "../source/bass/BassProcessor.h"
#include "../source/bass/BassEditor.h"
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
    double ppqInc = 0.0;
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

    BassAudioProcessor proc;
    const double sr = 48000;
    const int block = 512;

    printf("\n== BL-1 plugin-boundary test (BassAudioProcessor) ==\n");

    // ---- 1. parameter surface: 45 params in the APVTS ----
    const int nParams = (int)proc.getParameters().size();
    check(nParams == fable::BL_NUM_PARAMS || nParams == fable::BL_NUM_PARAMS + 1, // + host bypass
          "45 host parameters", nParams);
    check(proc.getName() == "FableSynth BL-1", "plugin name");
    check(proc.acceptsMidi(), "accepts MIDI");

    // ---- 2. bus layout: one stereo output ----
    check(proc.getBusCount(false) == 1, "1 output bus", proc.getBusCount(false));
    check(proc.getTotalNumOutputChannels() == 2, "stereo out",
          proc.getTotalNumOutputChannels());

    // ---- boot state: ACID LINE ----
    check(proc.getNumPrograms() == 3, "3 patch programs", proc.getNumPrograms());
    check(proc.getProgramName(0) == "ACID LINE", "program 0 is ACID LINE");
    float bpm0 = proc.apvts.getRawParameterValue("seq.bpm")->load();
    check(std::abs(bpm0 - 138.0f) < 0.5f, "boot applies seq.bpm=138", bpm0);
    auto boot0 = proc.getSeqStep(0, 0);
    check(boot0.on && boot0.acc, "boot pattern carries the acid line");

    proc.prepareToPlay(sr, block);
    juce::AudioBuffer<float> buf(2, block);
    bool allFinite = true;

    check(proc.getLatencySamples() > 0, "FX latency reported to the host",
          proc.getLatencySamples());

    // Sample-accurate MIDI: a note-on at offset K on a clean processor must
    // leave [0, K) exactly silent, with signal appearing at/after K (the FX
    // lookahead latency shifts it further right). Scan two blocks so a slow
    // attack can't false-fail the "signal exists" half.
    {
        const int kOffset = 100;
        int firstNonZero = -1;
        juce::MidiBuffer om;
        om.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), kOffset);
        for (int b = 0; b < 2 && firstNonZero < 0; ++b) {
            buf.clear();
            juce::MidiBuffer empty;
            proc.processBlock(buf, b == 0 ? om : empty);
            for (int i = 0; i < block && firstNonZero < 0; ++i)
                if (std::fpclassify(buf.getSample(0, i)) != FP_ZERO
                    || std::fpclassify(buf.getSample(1, i)) != FP_ZERO)
                    firstNonZero = b * block + i;
        }
        check(firstNonZero >= kOffset, "note-on at offset 100: silence before the offset",
              firstNonZero);
        check(firstNonZero >= 0, "note-on at offset 100: signal after the offset", firstNonZero);
        // release + settle so the audition test below starts clean
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
        buf.clear();
        proc.processBlock(buf, off);
        for (int b = 0; b < 60; ++b) {
            buf.clear();
            juce::MidiBuffer empty;
            proc.processBlock(buf, empty);
        }
    }

    int stepChanges = 0, lastStep = -1;
    auto render = [&](int nBlocks, int note, float vel, bool noteOff = false) {
        double sumSq = 0;
        long cnt = 0;
        for (int b = 0; b < nBlocks; ++b) {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0 && note >= 0) {
                if (noteOff) midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                else         midi.addEvent(juce::MidiMessage::noteOn(1, note, vel), 0);
            }
            proc.processBlock(buf, midi);
            int s = proc.getCurrentStep();
            if (s != lastStep) { stepChanges++; lastStep = s; }
            for (int ch = 0; ch < 2; ++ch) {
                const float* d = buf.getReadPointer(ch);
                for (int i = 0; i < block; ++i) {
                    if (!std::isfinite(d[i])) allFinite = false;
                    sumSq += (double)d[i] * d[i];
                }
            }
            cnt += 2 * block;
        }
        return std::sqrt(sumSq / (double)cnt);
    };

    // ---- 3. MIDI audition: note 48 (root + 12) -> audio, note-off -> decay ----
    auto r3 = render(4, 48, 0.9f);
    check(r3 > 1e-5, "MIDI note 48 audible within 4 blocks", r3);
    check(proc.getMidiActive(), "MIDI activity LED glows");
    check(proc.getCurrentSemi() == 12, "current semi published (12)", proc.getCurrentSemi());
    check(proc.getVizGate(), "viz gate high while held");
    render(2, 48, 0, true);        // note off
    render(120, -1, 0);            // ~1.3 s: release + reverb tail dies
    auto r3b = render(30, -1, 0);  // then the output must be silent
    check(r3b < 1e-4, "note-off decays to silence", r3b);
    check(!proc.getVizGate(), "viz gate low after release");

    // ---- 4. internal sequencer: play the acid line ----
    proc.setSeqPlaying(true);
    stepChanges = 0; lastStep = -1;
    // one bar at 138 bpm = 16 * (60/138/4) * 48000 ~= 83478 samples ~= 164 blocks
    auto r4 = render(164, -1, 0);
    check(proc.isSeqPlaying(), "sequencer reports playing");
    check(r4 > 1e-4, "ACID LINE bar produces audio", r4);
    check(stepChanges >= 12, "step counter advances across the bar", stepChanges);
    check(proc.getCurrentPattern() == 0, "current pattern is chain[0] = A",
          proc.getCurrentPattern());
    proc.setSeqPlaying(false);
    render(4, -1, 0);
    check(proc.getCurrentStep() == -1, "stopped: currentStep reads -1", proc.getCurrentStep());
    check(!proc.isHostSynced(), "no playhead -> not host-synced");
    render(120, -1, 0); // decay

    // ---- 5. UI audition path (command FIFO) ----
    proc.noteOn(0, 0.85f);
    auto r5 = render(4, -1, 0);
    check(r5 > 1e-5, "keyboard noteOn audible", r5);
    proc.noteOff(0);
    render(120, -1, 0);

    // ---- 6. host tempo: mock playhead at 90 bpm drives the step clock ----
    MockPlayHead ph;
    proc.setPlayHead(&ph);
    proc.setSeqPlaying(true);
    stepChanges = 0; lastStep = -1;
    // 4 s at 90 bpm = 6 steps/s -> ~24 step advances (vs 36.8 at 138).
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
    render(120, -1, 0);

    // ---- 6b. host transport lock: isPlaying + ppq slave the sequencer ----
    MockPlayHead tph;
    tph.bpm = 120.0; tph.reportPpq = true;
    tph.ppqInc = (double)block / sr * (120.0 / 60.0);
    proc.setPlayHead(&tph);
    stepChanges = 0; lastStep = -1;
    auto r6b = render((int)(2.0 * sr / block), -1, 0);   // 2 s, transport rolling
    check(proc.isSeqPlaying(), "host transport -> sequencer reports playing");
    check(r6b > 1e-4, "host transport drives the line without internal play", r6b);
    // 2 s at 120 bpm = 16 step advances (8 steps/s)
    check(stepChanges >= 13 && stepChanges <= 19,
          "step count follows host position (~16 in 2 s)", stepChanges);
    tph.playing = false;                                  // host hits stop
    render(4, -1, 0);
    check(!proc.isSeqPlaying(), "host stop -> sequencer stops");
    check(proc.getCurrentStep() == -1, "host stop resets the step readout");
    proc.setPlayHead(nullptr);
    render(120, -1, 0);

    // ---- 7. everything rendered finite ----
    check(allFinite, "all output finite");

    // ---- 8. full state round-trip into a fresh processor ----
    printf("\n== state round-trip ==\n");
    if (auto* p = proc.apvts.getParameter("flt.cut"))
        p->setValueNotifyingHost(p->convertTo0to1(1234.0f));
    fable::BassSeqStep edited;
    edited.on = true; edited.note = 9; edited.oct = 1; edited.acc = true; edited.slide = true;
    proc.setSeqStep(2, 11, edited);
    proc.setChain({ 0, 2 });
    proc.setEditPattern(2);

    juce::MemoryBlock state;
    proc.getStateInformation(state);
    check(state.getSize() > 0, "state serialises", (double)state.getSize());

    BassAudioProcessor proc2;
    check(!proc2.getSeqStep(2, 11).on, "fresh instance differs before restore");
    proc2.setStateInformation(state.getData(), (int)state.getSize());
    float cut2 = proc2.apvts.getRawParameterValue("flt.cut")->load();
    check(std::abs(cut2 - 1234.0f) < 2.0f, "flt.cut round-trips", cut2);
    auto rs = proc2.getSeqStep(2, 11);
    check(rs.on && rs.acc && rs.slide && rs.note == 9 && rs.oct == 1,
          "edited step round-trips");
    check(proc2.getChain() == std::vector<int>({ 0, 2 }), "chain {A,C} round-trips");
    check(proc2.getEditPattern() == 2, "edit pattern round-trips", proc2.getEditPattern());

    // ---- 9. patch programs beyond ACID LINE ----
    printf("\n== patch programs ==\n");
    check(proc2.getProgramName(1) == "RUBBER SUB", "program 1 is RUBBER SUB");
    check(proc2.getProgramName(2) == "NEON SQUELCH", "program 2 is NEON SQUELCH");
    proc2.setCurrentProgram(1);
    check(proc2.getCurrentProgram() == 1, "current program tracks", proc2.getCurrentProgram());
    float cut1 = proc2.apvts.getRawParameterValue("flt.cut")->load();
    check(std::abs(cut1 - 210.0f) < 2.0f, "RUBBER SUB applies flt.cut=210", cut1);
    proc2.setCurrentProgram(2);
    check(proc2.getChain() == std::vector<int>({ 0, 1 }), "NEON SQUELCH restores chain A->B");

    // ---- 10. legacy tolerance: a bare APVTS tree loads without crashing ----
    printf("\n== legacy state ==\n");
    {
        BassAudioProcessor proc3;
        auto bare = proc3.apvts.copyState(); // no BL1STATE wrapper, no BASS child
        bare.setProperty(juce::Identifier("nonsense"), 42, nullptr);
        juce::MemoryBlock legacy;
        if (auto xml = bare.createXml()) BinPacker::pack(*xml, legacy);
        check(legacy.getSize() > 0, "legacy blob built", (double)legacy.getSize());
        proc3.setStateInformation(legacy.getData(), (int)legacy.getSize());
        check(proc3.getParameters().size() > 0, "processor alive after legacy load");
        const char junk[] = "not a state blob";
        proc3.setStateInformation(junk, (int)sizeof(junk));
        check(proc3.getName() == "FableSynth BL-1", "processor alive after garbage load");
    }

    // ---- 11. editor: headless full-rack render to PNG ----
    printf("\n== editor snapshot ==\n");
    {
        juce::File dir(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                                : juce::File::getCurrentWorkingDirectory());
        proc.setCurrentProgram(0); // snapshot the boot patch, not test residue
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        auto* bassEd = dynamic_cast<BassEditor*>(ed.get());
        check(bassEd != nullptr, "createEditor returns BassEditor");
        ed->setSize(BassRack::LW, BassRack::LH); // logical rack size — 1:1 render
        juce::Image img = ed->createComponentSnapshot(ed->getLocalBounds());
        check(img.isValid() && img.getWidth() == BassRack::LW && img.getHeight() == BassRack::LH,
              "snapshot rendered at 1460x931", img.getWidth());
        const juce::File out = dir.getChildFile("bass_editor.png");
        writePng(img, out);
        check(out.existsAsFile() && out.getSize() > 0, "bass_editor.png written",
              (double)out.getSize());
        // Non-blank: section-centre pixels must differ from the bare editor
        // background sampled at the rack's outer padding (x=8 is left of every
        // panel, which starts at x=18).
        struct Probe { const char* name; int x, y; };
        const Probe probes[] = {
            { "header",        700,  54 },
            { "osc terrain",   220, 200 },
            { "sub panel",     580, 180 },
            { "filter view",   860, 200 },
            { "env view",     1240, 200 },
            { "lfo view",      120, 420 },
            { "accent knobs",  420, 430 },
            { "keys",         1000, 440 },
            { "seq lanes",     700, 620 },
            { "fx rack",       400, 845 },
        };
        for (const auto& p : probes) {
            const juce::Colour bg = img.getPixelAt(8, p.y);
            check(img.getPixelAt(p.x, p.y) != bg, p.name, (double)p.x);
        }

        // ---- 12. pitch seq view: web store semantics ----
        printf("\n== pitch seq view ==\n");
        auto& seq = bassEd->getRack().pitchSeq();
        proc.setEditPattern(0);
        proc.setChain({ 0 });

        // toggleCell: tap = set note, tap same lane again = rest
        fable::BassSeqStep offStep;
        proc.setSeqStep(0, 4, offStep);
        seq.toggleCell(4, 7);
        auto s = proc.getSeqStep(0, 4);
        check(s.on && s.note == 7, "tap lane sets the note", s.note);
        seq.toggleCell(4, 3);
        s = proc.getSeqStep(0, 4);
        check(s.on && s.note == 3, "tap other lane moves the note", s.note);
        seq.toggleCell(4, 3);
        s = proc.getSeqStep(0, 4);
        check(!s.on, "tap active lane rests the step");

        // acc/slide only latch on active steps
        seq.toggleStepAcc(4);
        check(!proc.getSeqStep(0, 4).acc, "accent ignored on a rest");
        seq.toggleCell(4, 0);
        seq.toggleStepAcc(4);
        seq.toggleStepSlide(4);
        s = proc.getSeqStep(0, 4);
        check(s.acc && s.slide, "accent + slide latch on an active step");
        seq.cycleStepOct(4);
        check(proc.getSeqStep(0, 4).oct == 1, "oct cycles 0 -> +1", proc.getSeqStep(0, 4).oct);
        seq.cycleStepOct(4);
        check(proc.getSeqStep(0, 4).oct == -1, "oct cycles +1 -> -1", proc.getSeqStep(0, 4).oct);

        // pattern click outside chain mode resets the chain (store.setEditPattern)
        seq.patternClick(1);
        check(proc.getEditPattern() == 1, "pattern click selects B", proc.getEditPattern());
        check(proc.getChain() == std::vector<int>({ 1 }), "pattern click resets chain to {B}");

        // chain builder: first click replaces, later clicks append, toggle-off commits
        seq.setChaining(true);
        check(seq.isChaining(), "CHAIN toggle latches on");
        seq.patternClick(0);
        check(proc.getChain() == std::vector<int>({ 0 }), "first chained click starts fresh");
        seq.patternClick(3);
        check(proc.getChain() == std::vector<int>({ 0, 3 }), "second chained click appends");
        check(proc.getEditPattern() == 3, "edit pattern follows chained clicks",
              proc.getEditPattern());
        seq.setChaining(false);
        check(!seq.isChaining(), "CHAIN toggle latches off");
        check(proc.getChain() == std::vector<int>({ 0, 3 }), "chain A->D survives toggle-off");

        // RAND rewrites the edit pattern in place
        proc.setEditPattern(1);
        bool changed = false;
        seq.randomize();
        for (int i = 0; i < fable::BL_STEPS && !changed; ++i)
            if (proc.getSeqStep(1, i).on) changed = true;
        // (a fully-empty random pattern is possible but astronomically unlikely
        //  to repeat across two tries)
        if (!changed) { seq.randomize();
            for (int i = 0; i < fable::BL_STEPS && !changed; ++i)
                if (proc.getSeqStep(1, i).on) changed = true; }
        check(changed, "RAND writes a pattern");
    }

    printf("%s\n", g_fail == 0 ? "BASS PLUGIN CHECKS PASSED" : "BASS PLUGIN CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
