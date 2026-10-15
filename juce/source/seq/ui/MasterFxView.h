#pragma once
#include "../../ui/VisualTimer.h"

#include "../../ui/FxChain.h"
#include "MixAssistPanel.h"
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
    // The master learns from the pre-FX sum (before its EQ) and its own comp input.
    AutoEqSource autoEqSource(int) const override { return { &proc_.masterInputTap(), true, "mix", {} }; }
    AutoCompSource autoCompSource(int) const override { return { &proc_.compTrace(-1), "glue", {} }; }
    // The live spectrum shows what leaves the master, not the sum it learns from.
    const fable::ProgramTap* liveSpectrumTap(int) const override { return &proc_.masterOutputTap(); }
    SideChainInfo sideChain() const override {
        std::vector<std::string> names;
        if (proc_.preparedSampleRate() > 0)   // the conductor exists after prepareToPlay
            for (const auto& t : proc_.conductor().session().tracks) names.push_back(t.name);
        return { fable::SC_SOURCES, true, trackKeyLabels(names) };
    }
    double analysisSampleRate() const override {
        return proc_.preparedSampleRate() > 0 ? proc_.preparedSampleRate() : 48000;
    }
    std::optional<double> analysisTempo() const override {
        if (proc_.preparedSampleRate() <= 0) return std::nullopt;
        return proc_.conductor().session().bpm;
    }
private:
    SeqAudioProcessor& proc_;
};

class MasterLimiterView final : public juce::Component, private fui::VisualTimer {
public:
    explicit MasterLimiterView(MasterFxUiModel&);
    ~MasterLimiterView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void visualVisibilityChanged(bool showing) override { if (!showing) history_.clear(); }
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
    MixAssistPanel assist_;
public:
    FxModuleView& eqModule() { return eq_; }
    FxModuleView& compModule() { return comp_; }
    MixAssistPanel& assistPanel() { return assist_; }
};

} // namespace fui
