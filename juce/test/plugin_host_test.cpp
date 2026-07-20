// Plugin-boundary verification: instantiates the real FableAudioProcessor and
// drives it the way a DAW would — set a preset (APVTS), prepareToPlay, feed a
// MIDI chord, process blocks — confirming the parameter bridge + MIDI handling
// + engine + FX all work together through the JUCE plugin surface.
#include "../source/PluginProcessor.h"
#include "../source/PluginEditor.h"
#include "../source/WavetableView.h"
#include "../source/ui/WavetableEditor.h"
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

// Minimal host playhead (bass_host_test scheme). bpm-only by default (tempo
// sync); with reportPpq it also reports song position + isPlaying, advancing
// ppq once per getPosition() call (= once per processBlock) like a rolling DAW.
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

// Render the full plugin editor (preset bar + both wavetable views + parameter
// panel) to a PNG via JUCE's headless software renderer.
static void snapshotEditor(FableAudioProcessor& proc, const juce::File& out) {
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(Rack::LW, Rack::LH); // logical rack size — full-resolution render
    writePng(ed->createComponentSnapshot(ed->getLocalBounds()), out);
}

static juce::Button* findButtonNamed(juce::Component& root, const juce::String& name) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        auto* child = root.getChildComponent(i);
        if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr
            && button->getName() == name)
            return button;
        if (auto* nested = findButtonNamed(*child, name)) return nested;
    }
    return nullptr;
}

// Exercise the native filter-tab interaction headlessly. The full F1/matrix
// snapshots remain the visual fixtures; this check covers the F2 state change
// without relying on JUCE's invalidated-descendant repaint behaviour.
static bool selectEditorFilter2(FableAudioProcessor& proc) {
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(Rack::LW, Rack::LH);
    auto* f2 = findButtonNamed(*ed, "F2");
    if (f2 != nullptr && f2->onClick) f2->onClick();
    return f2 != nullptr && f2->getToggleState();
}

// Render the two live wavetable views to a PNG (headless software renderer),
// proving the visualization actually draws.
static void snapshotViews(FableAudioProcessor& proc, const juce::File& out) {
    WavetableView a(proc, 0, juce::Colour(0xff4de8ff));
    WavetableView b(proc, 1, juce::Colour(0xffffa14d));
    juce::Component panel;
    panel.setSize(560, 380);
    a.setBounds(0, 0, 560, 184);
    b.setBounds(0, 196, 560, 184);
    panel.addAndMakeVisible(a);
    panel.addAndMakeVisible(b);
    writePng(panel.createComponentSnapshot(panel.getLocalBounds()), out);
}

// Render the user-wavetable editor overlay (import/draw modal) to a PNG, proving
// the new editing UI instantiates and paints headlessly.
static void snapshotWavetableEditor(FableAudioProcessor& proc, const juce::File& out) {
    fui::WavetableEditor ed(proc);
    ed.setSize(900, 700);
    ed.openFor(0);
    writePng(ed.createComponentSnapshot(ed.getLocalBounds()), out);
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for the processor

    FableAudioProcessor proc;
    const double sr = 48000;
    const int block = 512;

    const int HYPER_SAW = 4; // factory preset index (unison 7 + sub + chorus + reverb)
    proc.setCurrentProgram(HYPER_SAW);
    proc.prepareToPlay(sr, block);

    juce::AudioBuffer<float> buf(2, block);
    double sumSq = 0; long count = 0; float peak = 0; bool finite = true;

    // Sample-accurate MIDI: a note-on at offset K on a clean processor must
    // leave [0, K) exactly silent, with signal appearing at/after K (the FX
    // lookahead latency shifts it further right). Scan two blocks so a slow
    // attack can't false-fail the "signal exists" half.
    const int kOffset = 100;
    int firstNonZero = -1;
    {
        juce::MidiBuffer om;
        om.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), kOffset);
        for (int b = 0; b < 2 && firstNonZero < 0; ++b) {
            buf.clear();
            juce::MidiBuffer empty;
            proc.processBlock(buf, b == 0 ? om : empty);
            for (int i = 0; i < block && firstNonZero < 0; ++i)
                if (std::fpclassify(buf.getSample(0, i)) != FP_ZERO
                    || std::fpclassify(buf.getSample(1, i)) != FP_ZERO)
                    firstNonZero = b * block + i;
        }
        // release + settle so the main measurement below starts clean-ish
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        buf.clear();
        proc.processBlock(buf, off);
        for (int b = 0; b < (int)(1.0 * sr / block); ++b) {
            buf.clear();
            juce::MidiBuffer empty;
            proc.processBlock(buf, empty);
        }
    }

    float vizDuringPlay = -2.0f;
    const int blocks = (int)(2.0 * sr / block); // ~2 s
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0) {
            midi.addEvent(juce::MidiMessage::noteOn(1, 52, 1.0f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 59, 0.9f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);
        }
        if (b == (int)(1.5 * sr / block)) midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        proc.processBlock(buf, midi);
        if (b == 20) vizDuringPlay = proc.getVizPos(0); // voices active here
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i) {
                float v = buf.getSample(ch, i);
                if (!std::isfinite(v)) finite = false;
                peak = std::max(peak, std::abs(v));
                sumSq += (double)v * v; count++;
            }
    }
    double rms = std::sqrt(sumSq / (double)count);

    // Flush the release tail so all voices free, then the viz feed should idle.
    for (int b = 0; b < (int)(3.0 * sr / block); ++b) {
        buf.clear();
        juce::MidiBuffer empty;
        proc.processBlock(buf, empty);
    }

    int fail = 0;
    auto check = [&](bool c, const char* msg, double val) {
        printf("  [%s] %s (%.5f)\n", c ? "PASS" : "FAIL", msg, val);
        if (!c) fail++;
    };
    printf("\n== Plugin-boundary test (FableAudioProcessor, preset HYPER SAW) ==\n");
    check(proc.getName() == "FableSynth WT-1", "plugin name is FableSynth WT-1", 0);
    check(proc.getLatencySamples() > 0, "FX latency reported to the host",
          proc.getLatencySamples());
    check(firstNonZero >= kOffset, "note-on at offset 100: silence before the offset",
          firstNonZero);
    check(firstNonZero >= 0, "note-on at offset 100: signal after the offset", firstNonZero);
    check(finite, "output finite", 0);
    check(rms > 1e-3, "audio present (RMS > 0)", rms);
    check(peak < 1.5f, "output bounded by limiter", peak);
    check(vizDuringPlay >= 0.0f && vizDuringPlay <= 1.0f,
          "wavetable viz position published while playing", vizDuringPlay);
    check(proc.getVizPos(0) < 0.0f, "viz position reads idle (-1) after notes off",
          proc.getVizPos(0));

    // APVTS raw-value atomics update synchronously on setValueNotifyingHost /
    // replaceState, so the preset value and the state round-trip can be read back
    // directly. HYPER SAW sets oscA.unison = 7.
    auto uni = [&] { return proc.apvts.getRawParameterValue("oscA.unison")->load(); };
    check(std::abs(uni() - 7.0f) < 0.5f, "preset applied to APVTS (oscA.unison=7)", uni());

    juce::MemoryBlock state;
    proc.getStateInformation(state);
    proc.setCurrentProgram(0); // INIT (unison = 1)
    check(std::abs(uni() - 1.0f) < 0.5f, "program change updates APVTS (oscA.unison=1)", uni());
    proc.setStateInformation(state.getData(), (int)state.getSize());
    check(std::abs(uni() - 7.0f) < 0.5f, "state save/restore round-trips (oscA.unison=7)", uni());

    // Re-trigger a chord so the viz feed is live, then snapshot to PNG.
    proc.setCurrentProgram(1); // VELVET PAD — both oscs on, evolving table
    for (int b = 0; b < 30; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0) {
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 55, 0.9f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        }
        proc.processBlock(buf, midi);
    }
    // --- LFO shape parameter independence: lfo1.shape must not alias lfo2.shape ---
    // (set distinct shapes so the editor snapshot also shows them differently).
    {
        auto* p1 = proc.apvts.getParameter("lfo1.shape");
        auto* p2 = proc.apvts.getParameter("lfo2.shape");
        check(p1 && p2 && p1 != p2, "lfo1.shape / lfo2.shape are distinct params", (p1 && p2 && p1 != p2) ? 1 : 0);
        if (auto* c1 = dynamic_cast<juce::AudioParameterChoice*>(p1)) c1->setValueNotifyingHost(c1->convertTo0to1(2.0f)); // SAW
        if (auto* c2 = dynamic_cast<juce::AudioParameterChoice*>(p2)) c2->setValueNotifyingHost(c2->convertTo0to1(3.0f)); // SQR
        float v1 = proc.apvts.getRawParameterValue("lfo1.shape")->load();
        float v2 = proc.apvts.getRawParameterValue("lfo2.shape")->load();
        check(std::abs(v1 - 2.0f) < 0.5f, "lfo1.shape = SAW", v1);
        check(std::abs(v2 - 3.0f) < 0.5f, "lfo2.shape = SQR independently (no alias)", v2);
    }

    // Every host-facing parameter NAME must be unique. Repeated blocks share
    // short labels ("SHAPE", "RATE", ...); a DAW that keys MIDI-learn / generic
    // automation by name would alias them, making one LFO's shape appear mapped
    // to both. (The plugin's own UI keys by id and is unaffected either way.)
    {
        std::set<juce::String> seen;
        juce::String dup;
        for (auto* p : proc.getParameters()) {
            auto nm = p->getName(128);
            if (!seen.insert(nm).second && dup.isEmpty()) dup = nm;
        }
        std::string msg = dup.isEmpty() ? "all host parameter names unique"
                                        : ("duplicate parameter name: " + dup.toStdString());
        check(dup.isEmpty(), msg.c_str(), 0);
    }

    // --- Mod matrix slot > 4 via APVTS: assign mat5 -> PITCH and render ---
    // Drive a slot past the legacy 4 through the host parameter bridge (set via
    // convertTo0to1 like a DAW would), render a few blocks, then snapshot the
    // editor so the matrix-list row + control mod rings paint. No crash = pass.
    {
        auto setParamReal = [&](const char* id, float real) {
            if (auto* pr = proc.apvts.getParameter(id))
                pr->setValueNotifyingHost(pr->convertTo0to1(real));
        };
        setParamReal("mat5.src", 1.0f); // LFO 1
        setParamReal("mat5.dst", 4.0f); // PITCH
        setParamReal("mat5.amt", 0.7f); // high depth, host-automatable
        float s = proc.apvts.getRawParameterValue("mat5.src")->load();
        float d = proc.apvts.getRawParameterValue("mat5.dst")->load();
        float a = proc.apvts.getRawParameterValue("mat5.amt")->load();
        check(std::abs(s - 1.0f) < 0.5f && std::abs(d - 4.0f) < 0.5f && std::abs(a - 0.7f) < 0.05f,
              "mat5 (slot > 4) src/dst/amt set via APVTS convertTo0to1", a);

        bool matFinite = true; float matPeak = 0;
        for (int b = 0; b < 30; ++b) {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0) {
                midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
                midi.addEvent(juce::MidiMessage::noteOn(1, 55, 0.9f), 0);
            }
            proc.processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i) {
                    float v = buf.getSample(ch, i);
                    if (!std::isfinite(v)) matFinite = false;
                    matPeak = std::max(matPeak, std::abs(v));
                }
        }
        check(matFinite && matPeak < 1.5f, "renders cleanly with mat5 -> PITCH active", matPeak);
    }

    // --- New generic destination via APVTS: assign mat6 -> SUB LVL (dst 24) ---
    // Drive a NEW per-param destination (added by the generic-destinations feature)
    // through the host parameter bridge exactly as a DAW would (convertTo0to1),
    // enable the sub osc so the route actually modulates a synthesis knob, render a
    // few blocks, then snapshot the editor so the matrix row + the SUB LEVEL knob's
    // mod ring paint. No crash / finite output = pass.
    {
        auto setParamReal = [&](const char* id, float real) {
            if (auto* pr = proc.apvts.getParameter(id))
                pr->setValueNotifyingHost(pr->convertTo0to1(real));
        };
        setParamReal("sub.on", 1.0f);   // enable the sub so SUB LVL is an active synthesis path
        setParamReal("mat6.src", 1.0f); // LFO 1
        setParamReal("mat6.dst", 24.0f); // SUB LVL (new generic destination)
        setParamReal("mat6.amt", 0.6f);  // host-automatable depth
        float s = proc.apvts.getRawParameterValue("mat6.src")->load();
        float d = proc.apvts.getRawParameterValue("mat6.dst")->load();
        float a = proc.apvts.getRawParameterValue("mat6.amt")->load();
        check(std::abs(s - 1.0f) < 0.5f && std::abs(d - 24.0f) < 0.5f && std::abs(a - 0.6f) < 0.05f,
              "mat6 -> SUB LVL (new dest) src/dst/amt set via APVTS convertTo0to1", d);

        bool newFinite = true; float newPeak = 0;
        for (int b = 0; b < 30; ++b) {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0) {
                midi.addEvent(juce::MidiMessage::noteOn(1, 40, 0.9f), 0);
                midi.addEvent(juce::MidiMessage::noteOn(1, 47, 0.9f), 0);
            }
            proc.processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i) {
                    float v = buf.getSample(ch, i);
                    if (!std::isfinite(v)) newFinite = false;
                    newPeak = std::max(newPeak, std::abs(v));
                }
        }
        check(newFinite && newPeak < 1.5f, "renders cleanly with mat6 -> SUB LVL (new dest) active", newPeak);
    }

    // --- Many active routes: the mod-matrix list must scroll, not clip/overflow ---
    {
        auto setReal = [&](const juce::String& id, float real) {
            if (auto* pr = proc.apvts.getParameter(id)) pr->setValueNotifyingHost(pr->convertTo0to1(real));
        };
        // Fill 12 of the 16 slots so the row list exceeds the panel height.
        for (int slot = 1; slot <= 12; ++slot) {
            setReal("mat" + juce::String(slot) + ".src", (float)(1 + (slot % 5))); // LFO1..NOTE
            setReal("mat" + juce::String(slot) + ".dst", (float)slot);             // dsts 1..12
            setReal("mat" + juce::String(slot) + ".amt", 0.4f);
        }
        int active = 0;
        for (int slot = 1; slot <= 12; ++slot)
            if (proc.apvts.getRawParameterValue("mat" + juce::String(slot) + ".src")->load() >= 0.5f) active++;
        check(active == 12, "12 mod-matrix routes active (list overflows -> scrollable viewport)", (float)active);
    }

    // ================= note sequencer (plugin boundary) =================
    printf("\n== Note sequencer ==\n");
    {
        // seq params live in the canonical table -> APVTS
        auto* pBpm = proc.apvts.getRawParameterValue("seq.bpm");
        check(pBpm != nullptr, "seq.bpm exists in the APVTS", pBpm ? 1 : 0);
        check(pBpm && std::abs(pBpm->load() - 120.0f) < 0.5f, "seq.bpm defaults to 120",
              pBpm ? pBpm->load() : -1);
        check(proc.apvts.getRawParameterValue("seq.swing") != nullptr
              && proc.apvts.getRawParameterValue("seq.gate") != nullptr
              && proc.apvts.getRawParameterValue("seq.root") != nullptr,
              "seq.swing / seq.gate / seq.root exist", 0);

        auto renderBlocks = [&](int nBlocks) {
            double seqSumSq = 0; long cnt = 0;
            for (int b = 0; b < nBlocks; ++b) {
                buf.clear();
                juce::MidiBuffer midi;
                proc.processBlock(buf, midi);
                for (int ch = 0; ch < 2; ++ch) {
                    const float* d = buf.getReadPointer(ch);
                    for (int i = 0; i < block; ++i) seqSumSq += (double)d[i] * d[i];
                }
                cnt += 2 * block;
            }
            return std::sqrt(seqSumSq / (double)cnt);
        };

        // write a line into pattern A and play the internal clock
        proc.setEditPattern(0);
        proc.setChain({ 0 });
        for (int s = 0; s < 16; s += 2) {
            fable::NoteSeqStep st; st.on = true; st.note = (s % 4 == 0) ? 0 : 7;
            st.acc = (s == 0);
            proc.setSeqStep(0, s, st);
        }
        renderBlocks(60); // flush earlier chord tails before measuring
        proc.setSeqPlaying(true);
        int stepChanges = 0, lastStep = -1;
        double sum = 0;
        // one bar at 120 bpm = 16 * 6000 = 96000 samples ~= 188 blocks
        for (int b = 0; b < 188; ++b) {
            sum += renderBlocks(1);
            int s = proc.getCurrentStep();
            if (s != lastStep) { stepChanges++; lastStep = s; }
        }
        check(proc.isSeqPlaying(), "sequencer reports playing", 1);
        check(sum / 188.0 > 1e-4, "internal clock drives audible steps", sum / 188.0);
        check(stepChanges >= 14, "step counter advances across the bar", stepChanges);
        check(proc.getCurrentPattern() == 0, "current pattern is chain[0] = A",
              proc.getCurrentPattern());
        check(!proc.isHostSynced(), "no playhead -> not host-synced", 0);
        proc.setSeqPlaying(false);
        renderBlocks(4);
        check(proc.getCurrentStep() == -1, "stopped: currentStep reads -1",
              proc.getCurrentStep());
        renderBlocks(120); // decay

        // host tempo overrides seq.bpm for the internal clock
        MockPlayHead ph; // 90 bpm, no ppq -> tempo-sync only
        proc.setPlayHead(&ph);
        proc.setSeqPlaying(true);
        stepChanges = 0; lastStep = -1;
        const int blocks4s = (int)(4.0 * sr / block);
        for (int b = 0; b < blocks4s; ++b) {
            renderBlocks(1);
            int s = proc.getCurrentStep();
            if (s != lastStep) { stepChanges++; lastStep = s; }
        }
        check(proc.isHostSynced(), "host tempo reported -> isHostSynced()", 1);
        check(std::abs(proc.getHostBpm() - 90.0) < 0.01, "getHostBpm() = 90",
              proc.getHostBpm());
        // 4 s at 90 bpm = 6 steps/s -> ~24 step advances (vs 32 at 120)
        check(stepChanges >= 21 && stepChanges <= 27,
              "step spacing follows host 90 bpm (~24 steps in 4 s)", stepChanges);
        proc.setSeqPlaying(false);
        proc.setPlayHead(nullptr);
        renderBlocks(120);

        // host transport lock: isPlaying + ppq slave the sequencer
        MockPlayHead tph;
        tph.bpm = 120.0; tph.reportPpq = true;
        tph.ppqInc = (double)block / sr * (120.0 / 60.0);
        proc.setPlayHead(&tph);
        stepChanges = 0; lastStep = -1;
        double sum6b = 0;
        const int blocks2s = (int)(2.0 * sr / block);
        for (int b = 0; b < blocks2s; ++b) {
            sum6b += renderBlocks(1);
            int s = proc.getCurrentStep();
            if (s != lastStep) { stepChanges++; lastStep = s; }
        }
        check(proc.isSeqPlaying(), "host transport -> sequencer reports playing", 1);
        check(sum6b / blocks2s > 1e-4, "host transport drives the line without internal play",
              sum6b / blocks2s);
        // 2 s at 120 bpm = 16 step advances (8 steps/s)
        check(stepChanges >= 13 && stepChanges <= 19,
              "step count follows host position (~16 in 2 s)", stepChanges);
        tph.playing = false;                                  // host hits stop
        renderBlocks(4);
        check(!proc.isSeqPlaying(), "host stop -> sequencer stops", 0);
        check(proc.getCurrentStep() == -1, "host stop resets the step readout",
              proc.getCurrentStep());
        proc.setPlayHead(nullptr);
        renderBlocks(120);

        // patterns + chain round-trip through getState/setState (packed web layout)
        fable::NoteSeqStep edited;
        edited.on = true; edited.note = 9; edited.oct = 1; edited.acc = true; edited.duration = 5;
        proc.setSeqStep(2, 11, edited);
        proc.setChain({ 0, 2 }); // legacy values now mean a two-bar length
        proc.setEditPattern(2);
        juce::MemoryBlock seqState;
        proc.getStateInformation(seqState);
        FableAudioProcessor proc2;
        check(!proc2.getSeqStep(2, 11).on, "fresh instance differs before restore", 0);
        proc2.setStateInformation(seqState.getData(), (int)seqState.getSize());
        auto rs = proc2.getSeqStep(2, 11);
        check(rs.on && rs.acc && rs.duration == 5 && rs.note == 9 && rs.oct == 1,
              "edited step round-trips", rs.note);
        check(proc2.getChain() == std::vector<int>({ 0, 1 }), "two-bar length round-trips", 0);
        check(proc2.getEditPattern() == 2, "edit pattern round-trips", proc2.getEditPattern());

        // restore the snapshot-friendly state: pattern A, simple chain
        proc.setEditPattern(0);
        proc.setChain({ 0 });
    }

    // ---- NOTE SEQ panel: web store semantics through the real view ----
    printf("\n== Note seq view ==\n");
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        auto* fed = dynamic_cast<FableAudioProcessorEditor*>(ed.get());
        check(fed != nullptr, "createEditor returns FableAudioProcessorEditor", 0);
        auto& seq = fed->getRack().noteSeq();
        proc.setEditPattern(0);
        proc.setChain({ 0 });

        // toggleCell: tap = set note, tap same lane again = rest
        fable::NoteSeqStep offStep;
        proc.setSeqStep(0, 4, offStep);
        seq.toggleCell(4, 7);
        auto s = proc.getSeqStep(0, 4);
        check(s.on && s.note == 7, "tap lane sets the note", s.note);
        seq.toggleCell(4, 3);
        s = proc.getSeqStep(0, 4);
        check(s.on && s.note == 3, "tap other lane moves the note", s.note);
        seq.toggleCell(4, 3);
        s = proc.getSeqStep(0, 4);
        check(!s.on, "tap active lane rests the step", 0);

        // acc only latches on active steps (tie retired; duration lives in
        // the packed bits and is surfaced by the milestone-3 piano roll)
        seq.toggleStepAcc(4);
        check(!proc.getSeqStep(0, 4).acc, "accent ignored on a rest", 0);
        seq.toggleCell(4, 0);
        seq.toggleStepAcc(4);
        s = proc.getSeqStep(0, 4);
        check(s.acc && s.duration >= 1, "accent latches on an active step", s.duration);
        for (int i = 5; i < fable::SEQ_STEPS; ++i) proc.setSeqStep(0, i, {});
        seq.resizeStep(4, 63);
        // chain is one bar: NoteLengthHandle.tsx clamps at totalSteps - absoluteStep (16 - 4)
        check(proc.getSeqStep(0, 4).duration == 12, "duration resize clamps at the playback length", proc.getSeqStep(0, 4).duration);
        seq.resizeStep(4, -4);
        check(proc.getSeqStep(0, 4).duration == 1, "duration resize clamps at 1", proc.getSeqStep(0, 4).duration);
        fable::NoteSeqStep nextSame; nextSame.on = true; nextSame.note = 0;
        proc.setSeqStep(0, 7, nextSame);
        seq.resizeStep(4, 63);
        check(proc.getSeqStep(0, 4).duration == 3, "duration resize stops before same-lane note", proc.getSeqStep(0, 4).duration);
        proc.setSeqStep(0, 7, {});
        seq.cycleStepOct(4);
        check(proc.getSeqStep(0, 4).oct == 1, "oct cycles 0 -> +1", proc.getSeqStep(0, 4).oct);
        seq.cycleStepOct(4);
        check(proc.getSeqStep(0, 4).oct == -1, "oct cycles +1 -> -1", proc.getSeqStep(0, 4).oct);

        // Bar selection edits independently from the playback length.
        seq.patternClick(1);
        check(proc.getEditPattern() == 1, "bar click selects 2", proc.getEditPattern());
        check(proc.getChain() == std::vector<int>({ 0 }), "bar click preserves length", 0);
        seq.setSequenceLength(3);
        check(proc.getChain() == std::vector<int>({ 0, 1, 2 }), "length 3 plays bars 1-3", 0);
        seq.patternClick(3);
        check(proc.getEditPattern() == 3, "bar click selects 4", proc.getEditPattern());
        check(proc.getChain() == std::vector<int>({ 0, 1, 2 }), "editing bar 4 preserves length", 0);

        // RAND rewrites the edit pattern in place
        proc.setEditPattern(1);
        bool changed = false;
        seq.randomize();
        for (int i = 0; i < 16 && !changed; ++i)
            if (proc.getSeqStep(1, i).on) changed = true;
        if (!changed) { seq.randomize();
            for (int i = 0; i < 16 && !changed; ++i)
                if (proc.getSeqStep(1, i).on) changed = true; }
        check(changed, "RAND writes a pattern", 1);

        // ---- 2-D rectangle editing (rect select, note drag, ghost paste) ----
        proc.setEditPattern(0);
        proc.setChain({ 0 });
        auto clearPat0 = [&] { for (int i = 0; i < fable::SEQ_STEPS; ++i) proc.setSeqStep(0, i, {}); };
        auto setNote = [&](int step, int note) {
            fable::NoteSeqStep n; n.on = true; n.note = note; proc.setSeqStep(0, step, n);
        };

        // rectangle selection state
        clearPat0();
        setNote(2, 5); setNote(3, 8);
        seq.clearSelection();
        check(!seq.hasSelection(), "no selection by default", 0);
        seq.setSelection({ 2, 3, 4, 9 });
        {
            const auto r = seq.selection();
            check(seq.hasSelection() && r.stepLo == 2 && r.stepHi == 3 && r.noteLo == 4 && r.noteHi == 9,
                  "setSelection stores a normalized step-by-note rectangle", 0);
        }
        seq.selectAllCells();
        {
            const auto r = seq.selection();
            check(r.stepLo == 0 && r.stepHi == fable::SEQ_STEPS - 1 && r.noteLo == 0
                  && r.noteHi == fable::SEQ_NOTE_LANES - 1, "Cmd-A selects the whole grid", 0);
        }

        // copy a rect, wipe the grid, paste it back at the selection anchor
        seq.setSelection({ 2, 3, 4, 9 });
        seq.copySel();
        check(seq.hasClipboard(), "copySel fills the clipboard", 0);
        clearPat0(); // selection stays active; the anchor is its top-left (dNote 0)
        seq.pasteSel();
        check(proc.getSeqStep(0, 2).on && proc.getSeqStep(0, 2).note == 5
              && proc.getSeqStep(0, 3).on && proc.getSeqStep(0, 3).note == 8,
              "pasteSel drops the captured cells at the selection anchor", 0);

        // delete clears the in-band lit cells; undo/redo round-trips
        clearPat0();
        setNote(6, 7);
        seq.setSelection({ 6, 6, 0, fable::SEQ_NOTE_LANES - 1 });
        seq.deleteSel();
        check(!proc.getSeqStep(0, 6).on, "deleteSel clears the selected cell", 0);
        seq.undoEdit();
        check(proc.getSeqStep(0, 6).on && proc.getSeqStep(0, 6).note == 7, "undo restores the deleted cell", 0);
        seq.redoEdit();
        check(!proc.getSeqStep(0, 6).on, "redo re-applies the delete", 0);

        // duplicate a rect immediately to its right
        clearPat0();
        setNote(2, 4); setNote(3, 6);
        seq.setSelection({ 2, 3, 0, fable::SEQ_NOTE_LANES - 1 });
        seq.duplicateSel();
        check(proc.getSeqStep(0, 4).note == 4 && proc.getSeqStep(0, 5).note == 6,
              "duplicateSel copies the rect one width to the right", 0);
        check(seq.selection().stepLo == 4 && seq.selection().stepHi == 5,
              "duplicateSel moves the selection onto the copy", 0);

        // note drag: transpose + time-shift one note, clearing the source
        clearPat0();
        { fable::NoteSeqStep n; n.on = true; n.note = 6; n.acc = true; n.duration = 2; proc.setSeqStep(0, 5, n); }
        seq.commitNoteMove(5, 6, 8, 9, false);
        check(!proc.getSeqStep(0, 5).on, "note drag clears the source step", 0);
        {
            const auto d = proc.getSeqStep(0, 8);
            check(d.on && d.note == 9 && d.acc && d.duration == 2,
                  "note drag lands the transposed note keeping accent + duration", d.duration);
        }
        seq.commitNoteMove(8, 9, 8, 9, false);
        check(proc.getSeqStep(0, 8).on, "a zero-delta note drag is a no-op", 0);

        // block move: shift the whole rectangle in step and pitch
        clearPat0();
        setNote(2, 4); setNote(3, 7);
        seq.setSelection({ 2, 3, 4, 7 });
        seq.commitBlockMove(+5, +1, false);
        check(!proc.getSeqStep(0, 2).on && !proc.getSeqStep(0, 3).on, "block move clears the source cells", 0);
        check(proc.getSeqStep(0, 7).note == 5 && proc.getSeqStep(0, 8).note == 8,
              "block move lands the block shifted +5 steps / +1 semitone", 0);

        // ghost paste (COPY): pick the rect up, drop it elsewhere transposed
        clearPat0();
        setNote(4, 3);
        seq.setSelection({ 4, 4, 3, 3 });
        seq.beginGhostPaste(false);
        check(seq.ghostActive() && !seq.hasSelection(), "COPY begins a ghost and closes the selection", 0);
        seq.dropGhost(10, 7);
        check(!seq.ghostActive(), "dropping the ghost ends ghost mode", 0);
        check(proc.getSeqStep(0, 4).on && proc.getSeqStep(0, 10).note == 7,
              "COPY ghost keeps the source and lands the transposed copy", 0);

        // ghost paste (CUT): source only clears at drop, in one undo entry
        clearPat0();
        setNote(4, 5);
        seq.setSelection({ 4, 4, 5, 5 });
        seq.beginGhostPaste(true);
        check(proc.getSeqStep(0, 4).on, "CUT ghost leaves the source lit until drop", 0);
        seq.dropGhost(9, 5);
        check(!proc.getSeqStep(0, 4).on && proc.getSeqStep(0, 9).note == 5,
              "CUT ghost clears the source and lands the moved cell on drop", 0);
        seq.undoEdit();
        check(proc.getSeqStep(0, 4).on && !proc.getSeqStep(0, 9).on,
              "one undo reverts the whole CUT-ghost drop", 0);

        // back to the snapshot-friendly state
        clearPat0();
        seq.clearSelection();
        proc.setEditPattern(0);
        proc.setChain({ 0 });
    }

    juce::File dir(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                            : juce::File::getCurrentWorkingDirectory());
    dir.createDirectory();
    snapshotViews(proc, dir.getChildFile("wavetable_view.png"));
    snapshotEditor(proc, dir.getChildFile("plugin_editor.png"));
    snapshotEditor(proc, dir.getChildFile("plugin_editor_mat5.png")); // editor with mat5 route assigned
    snapshotEditor(proc, dir.getChildFile("plugin_editor_newdest.png")); // editor with mat6 -> SUB LVL (new dest)
    snapshotEditor(proc, dir.getChildFile("plugin_editor_matrixfull.png")); // 12 routes -> scrollable matrix
    check(selectEditorFilter2(proc), "F2 filter tab selects the second filter controls", 0);
    snapshotWavetableEditor(proc, dir.getChildFile("wavetable_editor.png"));

    // NOTE SEQ rectangle selection + floating CUT/COPY/DUP/DEL/X menu overlay.
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        ed->setSize(Rack::LW, Rack::LH);
        auto* fed = dynamic_cast<FableAudioProcessorEditor*>(ed.get());
        proc.setEditPattern(0); proc.setChain({ 0 });
        for (int i = 0; i < fable::SEQ_STEPS; ++i) proc.setSeqStep(0, i, {});
        fable::NoteSeqStep a; a.on = true; a.note = 4;              proc.setSeqStep(0, 2, a);
        fable::NoteSeqStep b; b.on = true; b.note = 7; b.acc = true; proc.setSeqStep(0, 4, b);
        fable::NoteSeqStep c; c.on = true; c.note = 5;              proc.setSeqStep(0, 6, c);
        if (fed) fed->getRack().noteSeq().setSelection({ 2, 6, 3, 8 });
        writePng(ed->createComponentSnapshot(ed->getLocalBounds()),
                 dir.getChildFile("plugin_editor_seqsel.png"));
        for (int i = 0; i < fable::SEQ_STEPS; ++i) proc.setSeqStep(0, i, {});
    }

    // ---- Live modulation dots: knob indicators follow the modulated value ----
    // Route a slow LFO onto F1 CUT, hold a note, and snapshot the editor at two
    // transport instants: the published live-mod value must move between them
    // (the dot paints from proc.getLiveMod via the knob's ParameterSource), and
    // must go NaN (dot hidden) once the release tail frees every voice.
    printf("\n== Live-mod knob dots ==\n");
    {
        auto setParamReal = [&](const char* id, float real) {
            if (auto* pr = proc.apvts.getParameter(id))
                pr->setValueNotifyingHost(pr->convertTo0to1(real));
        };
        setParamReal("lfo1.sync", 0.0f);
        setParamReal("lfo1.rate", 0.5f);   // slow sweep -> visibly different captures
        setParamReal("lfo1.shape", 0.0f);  // sine
        setParamReal("lfo1.retrig", 1.0f); // deterministic per-note phase
        setParamReal("mat8.src", 1.0f);    // LFO 1
        setParamReal("mat8.dst", 3.0f);    // F1 CUT
        setParamReal("mat8.amt", 0.8f);
        auto renderBlocks = [&](int count) {
            for (int b = 0; b < count; ++b) {
                buf.clear();
                juce::MidiBuffer empty;
                proc.processBlock(buf, empty);
            }
        };
        // Snapshot with the message loop pumped first so the knob timers tick
        // (rings_ rebuild there); the dot itself paints from the live atomics.
        auto snapshotLive = [&](const juce::File& out) {
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
            ed->setSize(Rack::LW, Rack::LH);
            juce::Thread::sleep(60); // let the 30 Hz control timers become due...
            juce::Timer::callPendingTimersSynchronously(); // ...then run them headlessly
            writePng(ed->createComponentSnapshot(ed->getLocalBounds()), out);
        };
        {
            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(1, 57, 0.9f), 0);
            buf.clear();
            proc.processBlock(buf, on);
        }
        renderBlocks(10);
        const float m1 = proc.getLiveMod(3);
        check(std::isfinite(m1), "live-mod feed publishes a finite F1 CUT sum while sounding", m1);
        snapshotLive(dir.getChildFile("plugin_editor_livemod_1.png"));
        renderBlocks((int)(0.6 * sr / block)); // ~0.6 s later on a 0.5 Hz LFO
        const float m2 = proc.getLiveMod(3);
        check(std::isfinite(m2) && std::abs(m2 - m1) > 0.02f,
              "live-mod value moves with the LFO between captures", std::abs(m2 - m1));
        snapshotLive(dir.getChildFile("plugin_editor_livemod_2.png"));
        {
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            buf.clear();
            proc.processBlock(buf, off);
        }
        renderBlocks((int)(3.0 * sr / block)); // flush the release tail
        check(std::isnan(proc.getLiveMod(3)), "live-mod feed idles (NaN) after release", 0);
    }

    printf("%s\n", fail == 0 ? "PLUGIN CHECKS PASSED" : "PLUGIN CHECKS FAILED");
    return fail == 0 ? 0 : 1;
}
