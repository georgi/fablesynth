#include "DeviceParameterBank.h"

namespace fui {
namespace {

std::unique_ptr<juce::RangedAudioParameter> makeParameter(const fable::ParamInfo& d) {
    const juce::ParameterID pid(d.pid, 1);
    const auto name = juce::String(d.pid).replaceCharacter('.', ' ').toUpperCase();
    if (d.kind == fable::Kind::Bool)
        return std::make_unique<juce::AudioParameterBool>(pid, name, d.def != 0.0f);
    if (d.kind == fable::Kind::Enum) {
        juce::StringArray choices;
        if (d.options != nullptr)
            for (const auto& choice : *d.options) choices.add(choice);
        return std::make_unique<juce::AudioParameterChoice>(pid, name, choices, (int)d.def);
    }

    const auto descriptor = d;
    juce::NormalisableRange<float> range(
        d.min, d.max,
        [descriptor](float, float, float n) { return fable::normToValue(descriptor, n); },
        [descriptor](float, float, float v) { return fable::valueToNorm(descriptor, v); });
    if (d.curve == fable::Curve::Int) range.interval = 1.0f;
    return std::make_unique<juce::AudioParameterFloat>(pid, name, range, d.def);
}

} // namespace

DeviceParameterBank::DeviceParameterBank(const fable::ParamInfo* catalog,
                                         std::size_t catalogSize)
    : info_(catalog, catalog + catalogSize) {
    parameters_.reserve(catalogSize);
    for (std::size_t i = 0; i < info_.size(); ++i) {
        auto parameter = makeParameter(info_[i]);
        parameter->addListener(this);
        byId_.emplace(info_[i].pid, i);
        parameters_.push_back(std::move(parameter));
    }
}

DeviceParameterBank::~DeviceParameterBank() {
    for (auto& parameter : parameters_) parameter->removeListener(this);
}

ParameterSource DeviceParameterBank::source() {
    return {
        [this](const juce::String& id) { return parameter(id); },
        [this](const juce::String& id) { return info(id); }
    };
}

juce::RangedAudioParameter* DeviceParameterBank::parameter(const juce::String& id) const {
    const auto it = byId_.find(id.toStdString());
    return it == byId_.end() ? nullptr : parameters_[it->second].get();
}

const fable::ParamInfo* DeviceParameterBank::info(const juce::String& id) const {
    const auto it = byId_.find(id.toStdString());
    return it == byId_.end() ? nullptr : &info_[it->second];
}

std::unordered_map<std::string, float> DeviceParameterBank::snapshot() const {
    std::unordered_map<std::string, float> values;
    values.reserve(parameters_.size());
    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        auto* parameter = parameters_[i].get();
        values.emplace(info_[i].pid, parameter->convertFrom0to1(parameter->getValue()));
    }
    return values;
}

void DeviceParameterBank::load(const std::unordered_map<std::string, float>& values) {
    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        const auto found = values.find(info_[i].pid);
        const float real = found == values.end() ? info_[i].def : found->second;
        parameters_[i]->setValueNotifyingHost(parameters_[i]->convertTo0to1(real));
    }
    dirty_.store(false);
}

} // namespace fui
