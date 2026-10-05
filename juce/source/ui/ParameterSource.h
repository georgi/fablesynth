#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Params.h"

#include <cstddef>
#include <functional>

namespace fui {

// Lightweight, copyable parameter lookup used by shared device controls.
//
// The lookup callbacks are intentionally message-thread/UI-facing. They never
// run from an engine render path. Captured backing stores (an APVTS or a hosted
// DeviceParameterBank) must outlive every component holding this source.
class ParameterSource {
public:
    using ParameterLookup = std::function<juce::RangedAudioParameter*(const juce::String&)>;
    using InfoLookup = std::function<const fable::ParamInfo*(const juce::String&)>;
    // Live route sum for a MOD_DESTS index, NaN while idle (processor atomics).
    using LiveAutomationLookup = std::function<float(const juce::String&)>;
    using LiveModLookup = std::function<float(int dest)>;

    ParameterSource() = default;
    ParameterSource(ParameterLookup parameterLookup, InfoLookup infoLookup = {});

    juce::RangedAudioParameter* parameter(const juce::String& id) const;
    const fable::ParamInfo* info(const juce::String& id) const;
    explicit operator bool() const { return (bool)parameterLookup_; }

    // Optional live-modulation feed for controls that are mod targets (the knob
    // dots). Unset (sources without an engine feed, e.g. DR-1) reads as NaN =
    // no data, so those controls simply never show a dot.
    void setLiveModLookup(LiveModLookup lookup) { liveModLookup_ = std::move(lookup); }
    float liveMod(int dest) const;
    void setLiveAutomationLookup(LiveAutomationLookup lookup) { liveAutomationLookup_=std::move(lookup); }
    // Real parameter value from the processor, NaN when no lane is active.
    float liveAutomation(const juce::String& id) const;

    // Applied automation plus the live per-voice modulation route, in real units.
    void setLiveEffectiveLookup(LiveAutomationLookup lookup) { liveEffectiveLookup_=std::move(lookup); }
    float effectiveValue(const juce::String& id, float fallback = 0.0f) const;

    // Convenience adapter for existing standalone processors. The catalog
    // points at one of the canonical, process-lifetime parameter tables.
    static ParameterSource fromApvts(juce::AudioProcessorValueTreeState&,
                                     const fable::ParamInfo* catalog = nullptr,
                                     std::size_t catalogSize = 0);

private:
    ParameterLookup parameterLookup_;
    InfoLookup infoLookup_;
    LiveModLookup liveModLookup_;
    LiveAutomationLookup liveAutomationLookup_;
    LiveAutomationLookup liveEffectiveLookup_;
};

} // namespace fui
