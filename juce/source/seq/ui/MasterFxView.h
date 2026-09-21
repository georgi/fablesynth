#pragma once

#include "../../ui/FxChain.h"
#include "../SeqProcessor.h"

#include <deque>

namespace fui {

class MasterFxUiModel final : public DeviceUiModel {
public:
    explicit MasterFxUiModel(SeqAudioProcessor& proc) : proc_(proc) {}
    ParameterSource parameters() override {
        return { [this](const juce::String& id) { return proc_.masterFxParameter(id); } };
    }
    fable::FxTelemetry fxTelemetry(int = 0, int = 0) const override { return proc_.masterFxTelemetry(); }
    std::array<float, 3> limiterTelemetry() const { return proc_.masterLimiterTelemetry(); }
    DeviceUiCapabilities capabilities() const override { return { false, false, false, false, false }; }
private:
    SeqAudioProcessor& proc_;
};

class MasterLimiterView final : public juce::Component, private juce::Timer {
public:
    explicit MasterLimiterView(MasterFxUiModel&);
    ~MasterLimiterView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    MasterFxUiModel& model_;
    PowerButton power_;
    Knob ceiling_;
    std::deque<std::array<float, 3>> history_;
    juce::Rectangle<int> plot_, readouts_;
};

// The SQ-4 bus exposes only the stages that actually exist on its post-fader
// path. The module painters are the same advanced EQ/OTT/COMP modules used by
// the instruments, not a separate mixer-style approximation.
class MasterFxView final : public juce::Component {
public:
    explicit MasterFxView(SeqAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    MasterFxUiModel model_;
    FxModuleView eq_, ott_, comp_;
    MasterLimiterView limiter_;
};

} // namespace fui
