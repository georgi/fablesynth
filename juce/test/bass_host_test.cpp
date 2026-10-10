// BL-1 plugin-boundary verification: instantiates the real BassAudioProcessor
// and drives it the way a DAW would — APVTS parameter surface, MIDI audition,
// the internal sequencer via patch program 0 (ACID LINE), host-tempo sync and
// transport lock through a mock AudioPlayHead, state round-trip, and a
// headless editor render. Modeled on drum_host_test.cpp.
#include "../source/bass/BassProcessor.h"
#include "FxUiChecks.h"
#include "AutoMixUiChecks.h"
#include "LiveSpectrumChecks.h"
#include "BassSeqCacheChecks.h"
#include "BassKeysCacheChecks.h"
#include "AgentProcessorChecks.h"
#include "NoteDrawChecks.h"
#include "ArpProcessorChecks.h"
#include "BrowserChecks.h"
#include "../source/bass/BassEditor.h"
#include "../source/ui/SequencePolyStrip.h"
#include "../source/bass/ui/BassStandaloneBackend.h"
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

static juce::Button* findNamedButton(juce::Component& root, const juce::String& name) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        auto* child = root.getChildComponent(i);
        if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr && button->getName() == name) return button;
        if (auto* nested = findNamedButton(*child, name)) return nested;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for the processor
    check(runBassSeqCacheChecks(), "bass sequencer cache parity and invalidation");
    check(runBassKeysCacheChecks(), "bass keys and panels cache parity and invalidation");

    BassAudioProcessor proc;
    const double sr = 48000;
    const int block = 512;
    // A boundary 240 samples into the block must fire even when unrelated
    // MIDI controller events divide the callback into 64-sample segments.
    {
        BassAudioProcessor segmented;
        segmented.apvts.getParameter("master.swing")->setValueNotifyingHost(0.0f);
        segmented.prepareToPlay(sr, 256);
        MockPlayHead clock;
        clock.bpm = 120; clock.reportPpq = true; clock.ppq = 0.24;
        clock.ppqInc = 256.0 / sr * 2.0;
        segmented.setPlayHead(&clock);
        juce::AudioBuffer<float> audio(2, 256);
        juce::MidiBuffer events;
        for (int offset : {64, 128, 192})
            events.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), offset);
        segmented.processBlock(audio, events);
        check(segmented.getCurrentStep() == 1,
              "MIDI segments preserve host sequencer boundary", segmented.getCurrentStep());
        events.clear();
        segmented.processBlock(audio, events);
        check(segmented.getCurrentStep() == 1,
              "next block does not skip the step after MIDI segments", segmented.getCurrentStep());
        segmented.setPlayHead(nullptr);
    }


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
    check(proc.getNumPrograms() == 22, "22 patch programs", proc.getNumPrograms());
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
    proc.setChain({ 0, 2 }); // legacy values now mean a two-bar length
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
    check(proc2.getChain() == std::vector<int>({ 0, 1 }), "two-bar length round-trips");
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
              "snapshot rendered at the logical rack size", img.getWidth());
        // The full-rack device body also occupies the header's coordinates.
        // The header must remain above it or the patch stepper cannot be clicked.
        const int programBeforeClick = proc.getCurrentProgram();
        auto* nextPatchHit = bassEd->getRack().getComponentAt(499, 54);
        check(dynamic_cast<juce::Button*>(nextPatchHit) != nullptr,
              "patch stepper is the header hit target");
        if (auto* button = dynamic_cast<juce::Button*>(nextPatchHit)) button->onClick();
        check(proc.getCurrentProgram() == (programBeforeClick + 1) % proc.getNumPrograms(),
              "patch stepper changes program");
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
            { "keys",         1000, 660 },
        };
        for (const auto& p : probes) {
            const juce::Colour bg = img.getPixelAt(8, p.y);
            check(img.getPixelAt(p.x, p.y) != bg, p.name, (double)p.x);
        }

        // All four page tabs share one content height, so switching pages
        // never changes the BL-1 body extent (standalone or in SQ-4).
        {
            BassDeviceBody* body = nullptr;
            for (auto* c : bassEd->getRack().getChildren())
                if (auto* b = dynamic_cast<BassDeviceBody*>(c)) body = b;
            check(body != nullptr, "rack hosts the BL-1 device body");
            if (body) {
                std::vector<int> bottoms;
                int tab = 0;
                for (auto* c : body->getChildren()) {
                    auto* button = dynamic_cast<juce::TextButton*>(c);
                    if (!button || button->getRadioGroupId() == 0) continue;
                    button->onClick();
                    int bottom = 0;
                    for (auto* child : body->getChildren())
                        if (child->isVisible() && child->getY() > button->getBottom())
                            bottom = std::max(bottom, child->getBottom());
                    bottoms.push_back(bottom);
                    writePng(ed->createComponentSnapshot(ed->getLocalBounds()),
                             dir.getChildFile("bass_page_" + juce::String(tab++) + ".png"));
                }
                bool same = bottoms.size() == 4;
                for (int b : bottoms) same = same && b == bottoms.front();
                check(same, "all four BL-1 pages share one height",
                      bottoms.empty() ? 0.0 : (double)bottoms.front());
                for (int b : bottoms) printf("    page bottom %d\n", b);
            }
        }

        // ---- 12. pitch seq view: web store semantics ----
        printf("\n== pitch seq view ==\n");
        auto& seq = bassEd->getRack().pitchSeq();
        proc.setEditPattern(0);
        proc.setChain({ 0 });

        check(checkNoteDrawing(seq, proc), "empty-grid draw: length, pitch, cancel, undo, tap, bounds", 0);

        // PATTERN PRESET: the catalog's first line (ACID ROLLER) fills all
        // four bars and sets the length; the combo returns to its prompt.
        {
            juce::ComboBox* presets = nullptr;
            for (auto* c : seq.getChildren()) if (auto* b = dynamic_cast<juce::ComboBox*>(c)) presets = b;
            check(presets != nullptr && presets->isVisible(), "pitch seq offers a pattern preset catalog");
            if (presets) {
                const int programBefore = proc.getCurrentProgram();
                presets->setSelectedId(1, juce::sendNotificationSync);
                const auto first = proc.getSeqStep(0, 0);
                check(first.on && first.note == 0 && first.acc && proc.getChain().size() == 4,
                      "pattern preset loads its notes and length", (double)proc.getChain().size());
                check(proc.getCurrentProgram() == programBefore && presets->getSelectedId() == 0,
                      "pattern preset keeps the patch and resets the prompt");
                proc.setChain({ 0 });
            }
        }

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
        for (int i = 5; i < fable::BL_STEPS; ++i) proc.setSeqStep(0, i, {});
        seq.resizeStep(4, 63);
        s = proc.getSeqStep(0, 4);
        // chain is one bar: NoteLengthHandle.tsx clamps at totalSteps - absoluteStep (16 - 4)
        check(s.duration == 12 && s.slide, "duration resize clamps at playback length, preserves slide", s.duration);
        seq.resizeStep(4, -4);
        check(proc.getSeqStep(0, 4).duration == 1, "bass duration resize clamps at 1");
        fable::BassSeqStep nextSame; nextSame.on = true; nextSame.note = 0;
        proc.setSeqStep(0, 7, nextSame);
        seq.resizeStep(4, 63);
        s = proc.getSeqStep(0, 4);
        check(s.duration == 3 && s.slide, "bass resize avoids overlap and preserves slide", s.duration);
        proc.setSeqStep(0, 7, {});
        seq.cycleStepOct(4);
        check(proc.getSeqStep(0, 4).oct == 1, "oct cycles 0 -> +1", proc.getSeqStep(0, 4).oct);
        seq.cycleStepOct(4);
        check(proc.getSeqStep(0, 4).oct == -1, "oct cycles +1 -> -1", proc.getSeqStep(0, 4).oct);

        // Bar selection edits independently from the playback length.
        seq.patternClick(1);
        check(proc.getEditPattern() == 1, "bar click selects 2", proc.getEditPattern());
        check(proc.getChain() == std::vector<int>({ 0 }), "bar click preserves length");
        seq.setSequenceLength(3);
        check(proc.getChain() == std::vector<int>({ 0, 1, 2 }), "length 3 plays bars 1-3");
        seq.patternClick(3);
        check(proc.getEditPattern() == 3, "bar click selects 4", proc.getEditPattern());
        check(proc.getChain() == std::vector<int>({ 0, 1, 2 }), "editing bar 4 preserves length");

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

        // ---- 13. decision-6: step-range selection + verbs ----
        printf("\n== pitch seq view: selection + verbs (decision 6) ==\n");
        auto stepsEqual = [](const fable::BassSeqStep& a, const fable::BassSeqStep& b) {
            return a.on == b.on && a.note == b.note && a.oct == b.oct
                && a.acc == b.acc && a.slide == b.slide && a.duration == b.duration;
        };
        auto snapshot = [&](int pat) {
            std::vector<fable::BassSeqStep> v;
            for (int i = 0; i < fable::BL_STEPS; ++i) v.push_back(proc.getSeqStep(pat, i));
            return v;
        };
        auto vecsEqual = [&](const std::vector<fable::BassSeqStep>& a,
                             const std::vector<fable::BassSeqStep>& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) if (!stepsEqual(a[i], b[i])) return false;
            return true;
        };

        // -- 2-D rectangle selection: setSelection normalizes, Cmd-A, Esc --
        proc.setEditPattern(2);
        for (int i = 0; i < fable::BL_STEPS; ++i) proc.setSeqStep(2, i, {});
        fable::BassSeqStep srcStep;
        srcStep.on = true; srcStep.note = 3; srcStep.oct = 1; srcStep.acc = true;
        srcStep.slide = true; srcStep.duration = 2;
        proc.setSeqStep(2, 0, srcStep);
        const int allLanes = fable::BL_NOTE_LANES - 1;
        // The grid is one timeline of every chained bar (PitchSeq.tsx): with
        // the chain at {0, 1, 2}, pattern 2 is bar 3 and starts at step 32.
        const int B2 = 2 * fable::BL_STEPS;
        auto selCols = [&](int a, int b) { seq.setSelection({ B2 + a, B2 + b, 0, allLanes }); };

        check(!seq.hasSelection(), "no selection by default");
        selCols(0, 0);
        {
            const auto r = seq.selection();
            check(seq.hasSelection() && r.stepLo == B2 && r.stepHi == B2 && r.noteLo == 0 && r.noteHi == allLanes,
                  "setSelection stores a step-by-note rectangle");
        }
        seq.setSelection({ 3, 0, 9, 4 }); // anchor/head unnormalized
        {
            const auto r = seq.selection();
            check(r.stepLo == 0 && r.stepHi == 3 && r.noteLo == 4 && r.noteHi == 9,
                  "selection() normalizes the rectangle");
        }
        seq.selectAllCells();
        {
            const auto r = seq.selection();
            check(r.stepLo == 0 && r.stepHi == seq.timelineSteps() - 1 && r.noteLo == 0 && r.noteHi == allLanes,
                  "Cmd-A selects the whole timeline");
        }
        seq.clearSelection();
        check(!seq.hasSelection(), "Esc/clearSelection drops the selection");

        // -- copy/paste: the slide flag survives the rect round trip --
        selCols(0, 0);
        seq.copySel();
        check(seq.hasClipboard(), "copySel fills the clipboard");
        seq.clearSelection();
        for (int i = 0; i < fable::BL_STEPS; ++i) proc.setSeqStep(2, i, {}); // wipe
        selCols(5, 5);
        seq.pasteSel();
        check(stepsEqual(proc.getSeqStep(2, 5), srcStep),
              "paste at the selection reproduces the copied step, slide flag included");

        // -- cut clears the source; the clipboard still holds the payload --
        seq.clearSelection();
        selCols(5, 5);
        const auto preCut = proc.getSeqStep(2, 5);
        seq.cutSel();
        check(!proc.getSeqStep(2, 5).on, "cut clears the source step");
        seq.undoEdit();
        check(stepsEqual(proc.getSeqStep(2, 5), preCut), "undo restores the cut step");
        seq.redoEdit();
        check(!proc.getSeqStep(2, 5).on, "redo re-applies the cut");
        seq.clearSelection();
        selCols(6, 6);
        seq.pasteSel(); // clipboard still holds step5's pre-cut content
        check(stepsEqual(proc.getSeqStep(2, 6), preCut), "cut's clipboard pastes elsewhere");

        // -- delete: clears the in-band lit cells, a no-op without a selection --
        seq.clearSelection();
        seq.deleteSel();
        check(true, "delete with no selection does not crash");
        selCols(6, 6);
        seq.deleteSel();
        check(!proc.getSeqStep(2, 6).on, "delete clears the selected cell");

        // -- duplicate with a selection: paste right after the range --
        for (int i = 0; i < fable::BL_STEPS; ++i) proc.setSeqStep(2, i, {});
        fable::BassSeqStep d0; d0.on = true; d0.note = 1;
        fable::BassSeqStep d1; d1.on = true; d1.note = 2;
        proc.setSeqStep(2, 8, d0);
        proc.setSeqStep(2, 9, d1);
        seq.clearSelection();
        selCols(8, 9);
        seq.duplicateSel();
        check(stepsEqual(proc.getSeqStep(2, 10), d0) && stepsEqual(proc.getSeqStep(2, 11), d1),
              "duplicate copies the rect right after the range");
        check(seq.selection().stepLo == B2 + 10 && seq.selection().stepHi == B2 + 11,
              "duplicate moves the selection onto the copy");

        // -- duplicate with no selection: copy pattern to next bar, extending
        //    the chain (already at 3 bars from the sequence-length test) --
        proc.setEditPattern(3);
        seq.clearSelection();
        seq.duplicateSel();
        check(proc.getEditPattern() == 3,
              "duplicate-to-next-bar is a no-op at the last pattern slot");

        proc.setEditPattern(2);
        const auto preDup = proc.getPatternBytes();
        const auto srcPat2 = snapshot(2);
        seq.duplicateSel();
        const auto postDup = proc.getPatternBytes();
        check(postDup != preDup, "duplicate-to-next-bar changes the pattern buffer");
        check(proc.getChain() == std::vector<int>({ 0, 1, 2, 3 }),
              "duplicate-to-next-bar extends the chain to 4 bars");
        check(proc.getEditPattern() == 3, "duplicate-to-next-bar moves edit focus to the new bar");
        check(vecsEqual(snapshot(3), srcPat2), "duplicated pattern content matches its source");
        seq.undoEdit();
        check(proc.getPatternBytes() == preDup, "undo restores the pre-duplicate pattern buffer");
        check(proc.getChain() == std::vector<int>({ 0, 1, 2, 3 }),
              "undo does not revert the chain-length side effect (scope: pattern bytes only)");
        seq.redoEdit();
        check(proc.getPatternBytes() == postDup, "redo restores the post-duplicate pattern buffer");

        // -- block move: shift the whole rectangle, clamped to the grid; the
        //    source vacates, Alt copies, and a zero-delta move is a no-op --
        proc.setEditPattern(2);
        for (int i = 0; i < fable::BL_STEPS; ++i) proc.setSeqStep(2, i, {});
        fable::BassSeqStep sa; sa.on = true; sa.note = 4; sa.oct = -1; sa.acc = true;
        sa.slide = true; sa.duration = 3;
        fable::BassSeqStep sb; sb.on = true; sb.note = 6;
        proc.setSeqStep(2, 2, sa);
        proc.setSeqStep(2, 3, sb);
        seq.clearSelection();
        selCols(2, 3);
        seq.commitBlockMove(+8, 0, false); // 2->10, 3->11
        check(stepsEqual(proc.getSeqStep(2, 10), sa) && stepsEqual(proc.getSeqStep(2, 11), sb),
              "block move lands the range at the destination");
        check(!proc.getSeqStep(2, 2).on && !proc.getSeqStep(2, 3).on,
              "block move vacates the source range");
        check(seq.selection().stepLo == B2 + 10 && seq.selection().stepHi == B2 + 11,
              "selection follows the moved range");

        // Overshoot: the delta clamps so the range ends flush with the
        // timeline end, crossing the bar line into pattern 3.
        const int lastStep = seq.timelineSteps() - 1;
        seq.commitBlockMove(+100, 0, false);
        check(stepsEqual(proc.getSeqStep(3, 14), sa) && stepsEqual(proc.getSeqStep(3, 15), sb),
              "block move crosses bars and clamps flush with the timeline end");
        check(seq.selection().stepLo == lastStep - 1 && seq.selection().stepHi == lastStep,
              "block move clamp keeps content and selection in agreement");

        seq.clearSelection();
        seq.setSelection({ lastStep - 1, lastStep - 1, 0, allLanes });
        seq.commitBlockMove(B2 - (lastStep - 1), 0, true); // Alt = copy back to pattern 2, step 0
        check(stepsEqual(proc.getSeqStep(2, 0), sa), "block copy writes the destination");
        check(stepsEqual(proc.getSeqStep(3, 14), sa), "block copy preserves the source");

        seq.clearSelection();
        selCols(0, 0);
        const auto beforeNoop = proc.getPatternBytes();
        seq.commitBlockMove(0, 0, false);
        check(proc.getPatternBytes() == beforeNoop, "a zero-delta block move is a no-op");

        // -- note drag: transpose + time-shift one note, clearing the source --
        proc.setEditPattern(2);
        for (int i = 0; i < fable::BL_STEPS; ++i) proc.setSeqStep(2, i, {});
        { fable::BassSeqStep n; n.on = true; n.note = 4; n.slide = true; n.acc = true; n.duration = 2; proc.setSeqStep(2, 5, n); }
        seq.commitNoteMove(B2 + 5, 4, B2 + 8, 7, false);
        check(!proc.getSeqStep(2, 5).on, "note drag clears the source step");
        {
            const auto d = proc.getSeqStep(2, 8);
            check(d.on && d.note == 7 && d.slide && d.acc && d.duration == 2,
                  "note drag lands the transposed note keeping slide / accent / duration");
        }

        // -- ghost paste (CUT): source clears only on drop, one undo entry --
        for (int i = 0; i < fable::BL_STEPS; ++i) proc.setSeqStep(2, i, {});
        { fable::BassSeqStep n; n.on = true; n.note = 5; n.slide = true; proc.setSeqStep(2, 4, n); }
        seq.setSelection({ B2 + 4, B2 + 4, 5, 5 });
        seq.beginGhostPaste(true);
        check(seq.ghostActive() && !seq.hasSelection(), "CUT begins a ghost and closes the selection");
        check(proc.getSeqStep(2, 4).on, "CUT ghost leaves the source until drop");
        seq.dropGhost(B2 + 9, 5);
        check(!seq.ghostActive() && !proc.getSeqStep(2, 4).on
              && proc.getSeqStep(2, 9).on && proc.getSeqStep(2, 9).slide,
              "CUT ghost clears the source and lands the moved cell (slide kept) on drop");
        seq.undoEdit();
        check(proc.getSeqStep(2, 4).on && !proc.getSeqStep(2, 9).on, "one undo reverts the CUT-ghost drop");

        // -- bar-chip drag: move (swap) / Alt-copy the pattern selector --
        const auto before0 = snapshot(0), before1 = snapshot(1);
        seq.moveBar(0, 1, false);
        check(vecsEqual(snapshot(0), before1) && vecsEqual(snapshot(1), before0),
              "bar-chip move swaps pattern content");
        seq.undoEdit();
        check(vecsEqual(snapshot(0), before0) && vecsEqual(snapshot(1), before1),
              "bar-chip move is undoable");
        seq.redoEdit();
        check(vecsEqual(snapshot(0), before1) && vecsEqual(snapshot(1), before0),
              "bar-chip move redo reapplies the swap");

        const auto before2 = snapshot(2);
        seq.moveBar(2, 2, true);
        check(vecsEqual(snapshot(2), before2), "bar-chip drag onto itself is a no-op");

        const auto copySrc = snapshot(0);
        seq.moveBar(0, 3, true); // Alt-copy: overwrite pattern 3, leave pattern 0 untouched
        check(vecsEqual(snapshot(0), copySrc), "bar-chip Alt-copy leaves the source untouched");
        check(vecsEqual(snapshot(3), copySrc), "bar-chip Alt-copy overwrites the destination");

        // -- keyPressed dispatch: the actual shortcut table, not just the
        //    handler methods it calls. --
        proc.setEditPattern(2);
        for (int i = 0; i < fable::BL_STEPS; ++i) proc.setSeqStep(2, i, {});
        fable::BassSeqStep kStep;
        kStep.on = true; kStep.note = 9; kStep.slide = true; kStep.acc = true; kStep.duration = 4;
        proc.setSeqStep(2, 5, kStep);
        seq.clearSelection();

        check(seq.keyPressed(juce::KeyPress('A', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 'A')),
              "Cmd-A handled");
        check(seq.selection().stepLo == 0 && seq.selection().stepHi == seq.timelineSteps() - 1,
              "Cmd-A (via keyPressed) selects the whole timeline");
        check(seq.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0)),
              "Esc handled");
        check(!seq.hasSelection(), "Esc (via keyPressed) clears the selection");

        selCols(5, 5); // narrow the selection to just the slide-flagged step
        check(seq.keyPressed(juce::KeyPress('C', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 'C')),
              "Cmd-C handled");
        seq.clearSelection();
        for (int i = 0; i < fable::BL_STEPS; ++i) proc.setSeqStep(2, i, {}); // wipe so the paste is visible
        selCols(11, 11);
        check(seq.keyPressed(juce::KeyPress('V', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 'V')),
              "Cmd-V handled");
        check(stepsEqual(proc.getSeqStep(2, 11), kStep),
              "Cmd-V (via keyPressed) pastes the slide-flagged step");
        check(seq.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey, juce::ModifierKeys(), 0)),
              "Delete handled");
        check(!proc.getSeqStep(2, 11).on, "Delete (via keyPressed) clears the selected cell");
        check(seq.keyPressed(juce::KeyPress('Z', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 'Z')),
              "Cmd-Z handled");
        check(proc.getSeqStep(2, 11).on, "undo (via keyPressed) restores the deleted cell");
        check(seq.keyPressed(juce::KeyPress('Z', juce::ModifierKeys(juce::ModifierKeys::commandModifier
                                                                     | juce::ModifierKeys::shiftModifier), 'Z')),
              "Shift-Cmd-Z handled");
        check(!proc.getSeqStep(2, 11).on, "redo (via keyPressed) re-applies the delete");
        check(!seq.keyPressed(juce::KeyPress('Q', juce::ModifierKeys(), 'q')),
              "an unclaimed plain key falls through unhandled");

        // -- timeline: every chained bar on screen, selections span bar lines --
        {
            proc.setChain({ 0, 1, 2, 3 });
            for (int pat = 0; pat < 4; ++pat) for (int i = 0; i < fable::BL_STEPS; ++i) proc.setSeqStep(pat, i, {});
            fable::BassSeqStep a; a.on = true; a.note = 2; a.acc = true;
            fable::BassSeqStep b; b.on = true; b.note = 5; b.slide = true;
            proc.setSeqStep(0, 15, a); proc.setSeqStep(1, 0, b);
            check(seq.timelineSteps() == 64, "four chained bars give a 64-step timeline", seq.timelineSteps());
            seq.setSelection({ 15, 16, 0, allLanes });
            seq.duplicateSel();
            check(stepsEqual(proc.getSeqStep(1, 1), a) && stepsEqual(proc.getSeqStep(1, 2), b),
                  "a selection across the bar line duplicates both cells");
            seq.toggleCell(40, 7);
            check(proc.getSeqStep(2, 8).on && proc.getSeqStep(2, 8).note == 7, "timeline step 40 edits bar 3, step 9");
            seq.resizeStep(40, 12);
            check(proc.getSeqStep(2, 8).duration == 12, "a note may run across the bar line", proc.getSeqStep(2, 8).duration);
            seq.setSize(1180, seq.getHeight()); seq.resized();
            const int colW = seq.colBounds(1).getX() - seq.colBounds(0).getX();
            check(colW >= 32, "64 columns keep the web 32 px pitch", colW);
            seq.patternClick(3);
            check(seq.scrollX() > 0 && seq.colBounds(48).getX() >= seq.gridBounds().getX(),
                  "a bar chip scrolls its bar into view", seq.scrollX());
            const auto vis = juce::File::getCurrentWorkingDirectory().getChildFile(".superpowers/sdd/port-sequencer");
            vis.createDirectory();
            seq.patternClick(0);
            writePng(seq.createComponentSnapshot(seq.getLocalBounds()), vis.getChildFile("bl1-timeline-4bars.png"));
            proc.setChain({ 0 });
        }

        // -- standalone melodic POLY: the strip edits the processor's rhythm,
        //    shares the note undo history and persists in plugin state --
        {
            fui::SequencePolyStrip* strip = nullptr;
            for (auto* c : seq.getChildren()) if (auto* p = dynamic_cast<fui::SequencePolyStrip*>(c)) strip = p;
            check(strip && strip->isVisible(), "standalone BL-1 shows the POLY strip");
            if (strip) {
                strip->press(fui::SequencePolyStrip::disclosure);
                strip->press(fui::SequencePolyStrip::power);
                strip->press(fui::SequencePolyStrip::stepsDown);
                check(proc.getSequenceRhythm().enabled && proc.getSequenceRhythm().steps == 15,
                      "strip writes the standalone POLY (ON, 15 steps)");
                juce::MemoryBlock saved; proc.getStateInformation(saved);
                BassAudioProcessor fresh; fresh.setStateInformation(saved.getData(), (int)saved.getSize());
                check(fresh.getSequenceRhythm().enabled && fresh.getSequenceRhythm().steps == 15,
                      "standalone POLY round-trips through plugin state");
                // Preset browser snapshot (web captureSound/restoreSound) keeps POLY.
                fui::BassStandaloneBackend backend(proc);
                const auto snap = backend.capture();
                proc.setSequenceRhythm({});
                backend.restore(*snap);
                check(proc.getSequenceRhythm().enabled && proc.getSequenceRhythm().steps == 15,
                      "browser snapshot restores the POLY rhythm");
                seq.undoEdit(); seq.undoEdit();
                check(!proc.getSequenceRhythm().enabled, "undo restores POLY OFF");
            }
        }
    }

    // ---- 12. preset browser: drawer, snapshots (params + patterns + chain), VARY, audition ----
    printf("\n== BL-1 preset browser ==\n");
    {
        const auto vis = juce::File::getCurrentWorkingDirectory().getChildFile("build/browser-visuals");
        vis.createDirectory();
        const auto favFile = vis.getChildFile("bl1-favorites.settings");
        favFile.deleteFile();
        fui::BrowserFavorites::overrideFile() = favFile;
        BassAudioProcessor bp;
        bp.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> bed(bp.createEditor());
        auto* bassEd = dynamic_cast<BassEditor*>(bed.get());
        check(bassEd != nullptr, "createEditor returns BassEditor (browser)");
        if (bassEd != nullptr) {
            bed->setSize(BassRack::LW, BassRack::LH);
            auto& rack = bassEd->getRack();
            auto* browseBtn = findNamedButton(rack, "BROWSE");
            check(browseBtn != nullptr && browseBtn->isVisible(), "the header shows BROWSE");
            if (browseBtn) browseBtn->onClick();
            check(rack.presetBrowser().isOpen() && browseBtn && browseBtn->getToggleState(), "BROWSE opens the drawer");
            rack.presetBrowser().selectItem(2);
            writePng(bed->createComponentSnapshot(bed->getLocalBounds()), vis.getChildFile("bl1-browser-open.png"));
            rack.presetBrowser().vary();
            rack.presetBrowser().toggleCharacter("gritty");
            writePng(bed->createComponentSnapshot(bed->getLocalBounds()), vis.getChildFile("bl1-browser-variant-filtered.png"));
            rack.presetBrowser().toggleCharacter("gritty");
            rack.presetBrowser().revert();
            rack.presetBrowser().close();
            check(!rack.presetBrowser().isOpen() && !browseBtn->getToggleState(), "CLOSE hides the drawer and resets BROWSE");

            browser_test::Probe probe;
            probe.label = "BL-1";
            probe.infos = fable::bassParamInfo();
            probe.values = [&] { return bp.captureParamValues(); };
            probe.extra = [&] {
                const auto bytes = bp.getPatternBytes();
                std::string out(bytes.begin(), bytes.end());
                out += "|" + std::to_string(bp.getChain().size()) + "|" + std::to_string(bp.getEditPattern());
                return out;
            };
            probe.program = [&] { return bp.getCurrentProgram(); };
            probe.dirty = [&] { return bp.isProgramDirty(); };
            probe.programName = [&](int i) { return bp.getProgramName(i); };
            for (const auto& rule : fui::BassBrowserAdapter::varyRules()) probe.varyAmounts[fable::bassIdFromString(rule.id)] = rule.amount;
            probe.setPlaying = [&](bool on) {
                bp.setSeqPlaying(on);
                juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m;
                for (int i = 0; i < 2; ++i) { b.clear(); bp.processBlock(b, m); }
            };
            browser_test::runChecks(rack.browserAdapter(), rack.presetBrowser(), probe,
                                    [](bool c, const char* m, double v) { check(c, m, v); });

            // A patch is params + patterns: REVERT undoes a pattern edit made after KEEP.
            auto& br = rack.presetBrowser();
            br.open();
            br.selectItem(1);
            br.keep();
            const auto keptPatterns = bp.getPatternBytes();
            auto step = bp.getSeqStep(0, 5);
            step.on = !step.on;
            bp.setSeqStep(0, 5, step);
            check(bp.getPatternBytes() != keptPatterns && bp.isProgramDirty(), "a pattern edit marks the patch edited");
            br.revert();
            check(bp.getPatternBytes() == keptPatterns && !bp.isProgramDirty(), "REVERT restores the patch patterns and clears the dirty flag");
            br.close();

            // The audition note reaches the audio thread through the command FIFO.
            bp.setCurrentProgram(0);
            bp.noteOn(0, 1.0f);
            juce::AudioBuffer<float> ab(2, 512); juce::MidiBuffer am;
            float peak = 0;
            for (int i = 0; i < 8; ++i) { ab.clear(); bp.processBlock(ab, am); peak = std::max(peak, ab.getMagnitude(0, 512)); }
            check(peak > 1.0e-4f, "audition note sounds through the FIFO path", peak);
            bp.noteOff(0);
        }
        fui::BrowserFavorites::overrideFile() = juce::File();
    }

    printf("%s\n", g_fail == 0 ? "BASS PLUGIN CHECKS PASSED" : "BASS PLUGIN CHECKS FAILED");
    if (!runFxUiChecks<BassAudioProcessor>("bl", BassRack::LW, BassRack::LH, false, [](const auto& p) { return p.fxTelemetry(); })) ++g_fail;
    const auto pluck = [](auto&, juce::MidiBuffer& midi, int block) {
        // A plucked line: 48/55/60/55, a note every 24 blocks (~256 ms), held ~150 ms.
        static const int notes[] = {48, 55, 60, 55};
        const int note = notes[(block / 24) % 4];
        if (block % 24 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)(block % 48 ? 80 : 120)), 0);
        if (block % 24 == 14) midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
    };
    g_fail += automixchecks::runStandalone<BassAudioProcessor>("bl1", BassRack::LW, BassRack::LH, false, pluck);
    g_fail += livespectrumchecks::runStandalone<BassAudioProcessor>("bl1", BassRack::LW, BassRack::LH, false, pluck);
    if (!runArpProcessorChecks<BassAudioProcessor>("bl", true)) ++g_fail;
    if (!runAgentProcessorChecks<BassAudioProcessor>("bl", fable::bassParamInfo().data(), fable::bassParamInfo().size(), "flt.cut", "osc.tune")) ++g_fail;
    return g_fail == 0 ? 0 : 1;
}
