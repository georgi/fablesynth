#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Theme.h"
#include "../PluginProcessor.h"

// Visualization views — ports of the canvas displays in src/components/displays:
//   EnvView.tsx, LFOView.tsx, FilterView.tsx, ScopeView.tsx, SpectrumView.tsx
namespace fui {

// ADSR envelope shape.
class EnvView : public juce::Component, private juce::Timer {
public:
    EnvView(juce::AudioProcessorValueTreeState&, const juce::String& base, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    float p(const char* sfx) const;
    juce::AudioProcessorValueTreeState& apvts;
    juce::String base;
    juce::Colour accent;
    float last[4] = {-1, -1, -1, -1};
};

// LFO waveform + free-running phase dot.
class LfoView : public juce::Component, private juce::Timer {
public:
    LfoView(juce::AudioProcessorValueTreeState&, const juce::String& shapeId,
            const juce::String& rateId, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    juce::AudioProcessorValueTreeState& apvts;
    juce::String shapeId, rateId;
    juce::Colour accent;
    juce::uint32 t0;
};

// Combined filter response for the two per-voice filters + routing.
class FilterView : public juce::Component, private juce::Timer {
public:
    FilterView(juce::AudioProcessorValueTreeState&, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    juce::AudioProcessorValueTreeState& apvts;
    juce::Colour accent;
    float sig = -1;
};

// Oscilloscope (post-FX time domain).
class ScopeView : public juce::Component, private juce::Timer {
public:
    ScopeView(FableAudioProcessor&, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    FableAudioProcessor& proc;
    juce::Colour accent;
};

// Spectrum analyser (post-FX, FFT with smoothing).
class SpectrumView : public juce::Component, private juce::Timer {
public:
    SpectrumView(FableAudioProcessor&, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    FableAudioProcessor& proc;
    juce::Colour accent;
    static constexpr int kOrder = 11, kFFT = 1 << kOrder; // 2048
    juce::dsp::FFT fft{kOrder};
    juce::dsp::WindowingFunction<float> window{kFFT, juce::dsp::WindowingFunction<float>::hann};
    std::array<float, kFFT> smoothed{};
};

} // namespace fui
