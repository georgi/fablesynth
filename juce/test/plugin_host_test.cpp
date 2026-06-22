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
    ed->setSize(1400, 1053); // logical rack size — full-resolution render
    writePng(ed->createComponentSnapshot(ed->getLocalBounds()), out);
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
    double rms = std::sqrt(sumSq / count);

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

    juce::File dir(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                            : juce::File::getCurrentWorkingDirectory());
    dir.createDirectory();
    snapshotViews(proc, dir.getChildFile("wavetable_view.png"));
    snapshotEditor(proc, dir.getChildFile("plugin_editor.png"));
    snapshotEditor(proc, dir.getChildFile("plugin_editor_mat5.png")); // editor with mat5 route assigned
    snapshotEditor(proc, dir.getChildFile("plugin_editor_newdest.png")); // editor with mat6 -> SUB LVL (new dest)
    snapshotWavetableEditor(proc, dir.getChildFile("wavetable_editor.png"));

    printf("%s\n", fail == 0 ? "PLUGIN CHECKS PASSED" : "PLUGIN CHECKS FAILED");
    return fail == 0 ? 0 : 1;
}
