#pragma once

#include "ParameterSource.h"

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fui {

// Editor-owned parameter backing for an SQ-4 hosted device UI. This is not an
// AudioProcessor and owns no DSP. It mirrors one track's PatchRef closely enough
// for the native controls to use normal JUCE RangedAudioParameter gestures.
//
// Parameter listeners only set dirty_, so callbacks are safe regardless of the
// thread that called setValueNotifyingHost. Session/engine writes are performed
// later by the hosted model on the message thread after consumeDirty().
class DeviceParameterBank final : private juce::AudioProcessorParameter::Listener {
public:
    DeviceParameterBank(const fable::ParamInfo* catalog, std::size_t catalogSize);
    ~DeviceParameterBank() override;

    ParameterSource source();
    juce::RangedAudioParameter* parameter(const juce::String& id) const;
    const fable::ParamInfo* info(const juce::String& id) const;

    // Real (denormalised) values keyed by canonical pid.
    std::unordered_map<std::string, float> snapshot() const;
    void load(const std::unordered_map<std::string, float>& values);

    bool consumeDirty() { return dirty_.exchange(false); }
    void markClean() { dirty_.store(false); }

private:
    void parameterValueChanged(int, float) override { dirty_.store(true); }
    void parameterGestureChanged(int, bool) override {}

    std::vector<fable::ParamInfo> info_;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters_;
    std::unordered_map<std::string, std::size_t> byId_;
    std::atomic<bool> dirty_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceParameterBank)
};

} // namespace fui
