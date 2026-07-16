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

    ParameterSource() = default;
    ParameterSource(ParameterLookup parameterLookup, InfoLookup infoLookup = {});

    juce::RangedAudioParameter* parameter(const juce::String& id) const;
    const fable::ParamInfo* info(const juce::String& id) const;
    explicit operator bool() const { return (bool)parameterLookup_; }

    // Convenience adapter for existing standalone processors. The catalog
    // points at one of the canonical, process-lifetime parameter tables.
    static ParameterSource fromApvts(juce::AudioProcessorValueTreeState&,
                                     const fable::ParamInfo* catalog = nullptr,
                                     std::size_t catalogSize = 0);

private:
    ParameterLookup parameterLookup_;
    InfoLookup infoLookup_;
};

} // namespace fui
