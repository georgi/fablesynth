#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Params.h"

#include <vector>

// Plain-value access to a machine's APVTS parameters, shared by the preset
// browser snapshots (WT-1, BL-1, DR-1 all describe parameters with fable::ParamInfo).
namespace fui {

// Same normalisation the processors use when a program loads.
inline void writeParamPlain(juce::AudioProcessorValueTreeState& apvts, const fable::ParamInfo& d, float v) {
    if (auto* param = apvts.getParameter(d.pid)) {
        const float norm = (d.kind == fable::Kind::Bool) ? (v != 0.0f ? 1.0f : 0.0f)
                         : (d.kind == fable::Kind::Enum) ? param->convertTo0to1(v)
                         : fable::valueToNorm(d, v);
        param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
    }
}

template <class Infos>
std::vector<float> readAllParams(juce::AudioProcessorValueTreeState& apvts, const Infos& infos) {
    std::vector<float> out(infos.size(), 0.0f);
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const auto* raw = apvts.getRawParameterValue(infos[i].pid);
        out[i] = raw ? raw->load() : infos[i].def;
    }
    return out;
}

template <class Infos>
void writeAllParams(juce::AudioProcessorValueTreeState& apvts, const Infos& infos, const std::vector<float>& values) {
    for (std::size_t i = 0; i < infos.size() && i < values.size(); ++i) writeParamPlain(apvts, infos[i], values[i]);
}

} // namespace fui
