#include "ParameterSource.h"

#include <limits>
#include <cmath>

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

float ParameterSource::liveAutomation(const juce::String& id) const {
    return liveAutomationLookup_ ? liveAutomationLookup_(id) : std::numeric_limits<float>::quiet_NaN();
}

float ParameterSource::effectiveValue(const juce::String& id, float fallback) const {
    auto* p = parameter(id);
    if (!p) return fallback;
    if (liveEffectiveLookup_) {
        const float live = liveEffectiveLookup_(id);
        if (std::isfinite(live)) return live;
    }
    float value = liveAutomation(id);
    if (!std::isfinite(value)) value = p->convertFrom0to1(p->getValue());
    // Only WT sources expose the WT destination feed. Other machine catalogs
    // can share parameter names without sharing these destination indices.
    if (liveModLookup_) {
        const auto& catalog = fable::paramInfo();
        for (int d = 1; d < fable::NUM_MOD_DESTS; ++d) {
            const int target = fable::dstTarget(d);
            if (target < 0 || catalog[(size_t)target].pid != id.toStdString()) continue;
            const float x = liveMod(d);
            if (std::isfinite(x)) {
                const auto& info = catalog[(size_t)target];
                value = info.curve == fable::Curve::Log
                    ? value * std::exp2(x * 5.0f)
                    : value + x * (info.max - info.min);
                value = juce::jlimit(info.min, info.max, value);
            }
            break;
        }
    }
    return value;
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
