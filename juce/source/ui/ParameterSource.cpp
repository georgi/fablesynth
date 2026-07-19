#include "ParameterSource.h"

#include <limits>

namespace fui {

ParameterSource::ParameterSource(ParameterLookup parameterLookup, InfoLookup infoLookup)
    : parameterLookup_(std::move(parameterLookup)), infoLookup_(std::move(infoLookup)) {}

juce::RangedAudioParameter* ParameterSource::parameter(const juce::String& id) const {
    return parameterLookup_ ? parameterLookup_(id) : nullptr;
}

const fable::ParamInfo* ParameterSource::info(const juce::String& id) const {
    return infoLookup_ ? infoLookup_(id) : nullptr;
}

float ParameterSource::liveMod(int dest) const {
    return liveModLookup_ ? liveModLookup_(dest) : std::numeric_limits<float>::quiet_NaN();
}

ParameterSource ParameterSource::fromApvts(juce::AudioProcessorValueTreeState& apvts,
                                            const fable::ParamInfo* catalog,
                                            std::size_t catalogSize) {
    return {
        [&apvts](const juce::String& id) {
            return dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
        },
        [catalog, catalogSize](const juce::String& id) -> const fable::ParamInfo* {
            if (catalog == nullptr) return nullptr;
            const auto key = id.toStdString();
            for (std::size_t i = 0; i < catalogSize; ++i)
                if (catalog[i].pid == key) return catalog + i;
            return nullptr;
        }
    };
}

} // namespace fui
