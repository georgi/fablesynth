#pragma once

#include "PluginProcessor.h"

// Live 3D wavetable terrain — JUCE port of src/components/displays/WavetableView.tsx.
// Draws every frame of the selected table in perspective and highlights the
// frame currently being played, tracking the modulated position the DSP thread
// publishes (falls back to the POS knob when idle).
class WavetableView : public juce::Component, private juce::Timer {
public:
    WavetableView(FableAudioProcessor& proc, int oscIndex, juce::Colour accent);
    ~WavetableView() override = default;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    int  tableIndex() const;
    float knobPos() const;

    FableAudioProcessor& proc;
    int        osc;
    juce::Colour accent;
    juce::String label;
    float lastShown = -2.0f;
    int   lastTable = -1;
    juce::Image farCache;
    int cacheTable = -1, cacheGen = -1, cacheW = 0, cacheH = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableView)
};
