#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Theme.h"
#include "ParameterSource.h"
#include "DeviceUiModel.h"

// Visualization views — ports of the canvas displays in src/components/displays:
//   EnvView.tsx, LFOView.tsx, FilterView.tsx, ScopeView.tsx, SpectrumView.tsx
namespace fui {

// ADSR envelope shape.
class EnvView : public juce::Component, private juce::Timer {
public:
    EnvView(juce::AudioProcessorValueTreeState&, const juce::String& base, juce::Colour accent);
    EnvView(ParameterSource, const juce::String& base, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    float p(const char* sfx) const;
    ParameterSource parameters;
    juce::String base;
    juce::Colour accent;
    float last[4] = {-1, -1, -1, -1};
};

// LFO waveform + free-running phase dot. The dot animates at the LFO's actual
// speed: the host-tempo-synced division when sync is on, else the free RATE.
class LfoView : public juce::Component, private juce::Timer {
public:
    LfoView(juce::AudioProcessorValueTreeState&, const juce::String& shapeId,
            const juce::String& rateId, const juce::String& syncId,
            const juce::String& syncRateId, juce::Colour accent,
            std::function<HostTransport()> transportProvider);
    LfoView(ParameterSource, const juce::String& shapeId,
            const juce::String& rateId, const juce::String& syncId,
            const juce::String& syncRateId, juce::Colour accent,
            std::function<HostTransport()> transportProvider);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    float currentRate(const HostTransport&) const;   // effective Hz (free or synced)
    bool  synced() const;
    ParameterSource parameters;
    juce::String shapeId, rateId, syncId, syncRateId;
    juce::Colour accent;
    std::function<HostTransport()> transport;
    juce::uint32 t0;
};

// Combined filter response for the two per-voice filters + routing.
class FilterView : public juce::Component, private juce::Timer {
public:
    FilterView(juce::AudioProcessorValueTreeState&, juce::Colour accent);
    FilterView(ParameterSource, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    ParameterSource parameters;
    juce::Colour accent;
    float sig = -1;
};

// Oscilloscope (post-FX time domain).
class ScopeView : public juce::Component, private juce::Timer {
public:
    ScopeView(std::function<void(float*, int)> reader, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override; // repaints only while audio is present
    std::function<void(float*, int)> readScope;
    juce::Colour accent;
    bool wasActive_ = true;
};

// Spectrum analyser (post-FX, FFT with smoothing).
class SpectrumView : public juce::Component, private juce::Timer {
public:
    SpectrumView(std::function<void(float*, int)> reader,
                 std::function<double()> sampleRateProvider,
                 juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override; // repaints while audio present or bars still falling
    std::function<void(float*, int)> readScope;
    std::function<double()> sampleRate;
    juce::Colour accent;
    static constexpr int kOrder = 11, kFFT = 1 << kOrder; // 2048
    juce::dsp::FFT fft{kOrder};
    juce::dsp::WindowingFunction<float> window{kFFT, juce::dsp::WindowingFunction<float>::hann};
    std::array<float, kFFT> smoothed{};
    bool wasActive_ = true, decaying_ = true;
};

} // namespace fui
