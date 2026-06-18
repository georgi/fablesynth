// Plugin-boundary verification: instantiates the real FableAudioProcessor and
// drives it the way a DAW would — set a preset (APVTS), prepareToPlay, feed a
// MIDI chord, process blocks — confirming the parameter bridge + MIDI handling
// + engine + FX all work together through the JUCE plugin surface.
#include "../source/PluginProcessor.h"
#include "../source/PluginEditor.h"
#include "../source/WavetableView.h"
#include <cmath>
#include <cstdio>

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
    ed->setSize(640, 860);
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
    juce::File dir(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                            : juce::File::getCurrentWorkingDirectory());
    dir.createDirectory();
    snapshotViews(proc, dir.getChildFile("wavetable_view.png"));
    snapshotEditor(proc, dir.getChildFile("plugin_editor.png"));

    printf("%s\n", fail == 0 ? "PLUGIN CHECKS PASSED" : "PLUGIN CHECKS FAILED");
    return fail == 0 ? 0 : 1;
}
