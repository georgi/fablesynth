#pragma once
#include "ui/VisualTimer.h"

#include "ui/WtUiModel.h"

class FableAudioProcessor;

// Live 3D wavetable terrain — JUCE port of src/components/displays/WavetableView.tsx.
// Draws every frame of the selected table in perspective and highlights the
// frame currently being played, tracking the modulated position the DSP thread
// publishes (falls back to the POS knob when idle).
class WavetableView : public juce::Component, private fui::VisualTimer {
public:
    WavetableView(fui::WtUiModel&, int oscIndex, juce::Colour accent);
    WavetableView(FableAudioProcessor&, int oscIndex, juce::Colour accent);
    ~WavetableView() override = default;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    int  tableIndex() const;
    float knobPos() const;

    std::unique_ptr<fui::WtUiModel> ownedModel;
    fui::WtUiModel& model;
    int        osc;
    juce::Colour accent;
    juce::String label;
    float lastShown = -2.0f;
    int   lastTable = -1;
    // Warp preview state: the near frames draw the warped waveform. FM/AM/RM
    // read the other oscillator's table, frame and tuning ratio.
    struct Warp { int mode = 0; float amount = 0; int modTable = -1; float modPos = 0; float ratio = 1;
                  bool operator==(const Warp& o) const {
                      return mode == o.mode && modTable == o.modTable
                          && std::abs(amount - o.amount) < 1e-4f && std::abs(modPos - o.modPos) < 1e-3f
                          && std::abs(ratio - o.ratio) < 1e-4f; } };
    Warp warpState() const;
    Warp lastWarp;
    juce::Image farCache;
    int cacheTable = -1, cacheGen = -1, cacheW = 0, cacheH = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableView)
};
