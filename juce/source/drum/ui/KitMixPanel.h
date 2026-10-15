#pragma once
#include "DrumUiModel.h"
#include "../../ui/AutoMix.h"
#include "../../ui/LiveSpectrum.h"
#include "../../dsp/automix/KitMix.h"

// DR-1 kit auto-mix for the group strip (src/drum/components/KitMixPanel.tsx):
// LEARN captures every sounding pad's post-FX signal, the fit moves the pad
// levels so the kit follows the chosen reference curve, APPLY writes them and
// UNDO restores the levels from before APPLY.
namespace fui {

class KitMixLearner {
public:
    static constexpr double kLearnSeconds = 8, kTimeoutSeconds = 30;
    static constexpr int kBlock = 4096, kPads = 16;

    explicit KitMixLearner(DrumUiModel&);
    ~KitMixLearner();
    bool learning() const { return learning_; }
    double progress() const { return progress_; }
    void learn();
    void poll();
    void stop();
    void setReference(const std::string&);
    const std::string& reference() const { return reference_; }
    void setAmount(double);
    double amount() const { return amount_; }
    bool learned() const { return learned_; }
    const fable::automix::KitMixFit* fit() const { return fit_ ? &*fit_ : nullptr; }
    bool canApply() const { return fit_.has_value() && !learning_; }
    bool canUndo() const { return !undo_.empty(); }
    void apply();
    void undo();
    juce::String status() const;
    juce::String learnLabel() const;

private:
    void refit();
    static juce::String lvl(int pad) { return "pad" + juce::String(pad) + ".lvl"; }
    DrumUiModel& model_;
    ParameterSource params_;
    std::string reference_ = "drums";
    double amount_ = 0.8, progress_ = 0, started_ = 0;
    bool learning_ = false, learned_ = false;
    std::unique_ptr<fable::automix::KitCapture> capture_;
    std::array<uint64_t, kPads> cursors_{};
    std::array<std::vector<float>, kPads> pending_;
    std::array<int, kPads> blocks_{};
    std::vector<double> lvl_, learnedLvl_;
    std::vector<fable::automix::Bands> bands_;
    std::optional<fable::automix::KitMixFit> fit_;
    std::vector<float> undo_;
};

class KitMixPanel : public juce::Component, private juce::Timer {
public:
    explicit KitMixPanel(DrumUiModel&);
    ~KitMixPanel() override;
    KitMixLearner& learner() { return learner_; }
    const LiveSpectrum& liveSpectrum() const { return live_; }
    // Reads the kit output once (the panel does this at ~30 Hz while visible).
    void updateLiveSpectrum(double nowMs);
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    DrumUiModel& model_;
    KitMixLearner learner_;
    juce::ComboBox reference_;
    AmountSlider amount_{0, 1, 0.05};
    LearnButton learn_{"LEARN"};
    ChipButton apply_{"APPLY"}, undo_{"UNDO"};
    juce::Rectangle<int> head_, plot_, pads_;
    LiveSpectrum live_;
    VisualTick liveTick_;
};

} // namespace fui
