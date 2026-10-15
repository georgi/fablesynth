#pragma once
#include "AutoMixSources.h"
#include "ParameterSource.h"
#include "../dsp/automix/AutoComp.h"
#include "../dsp/automix/AutoEq.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Native ports of the web auto-mix controls: AUTO for the EQ panel
// (src/components/panels/AutoEqStrip.tsx), AUTO for the COMP panel
// (AutoCompStrip.tsx) and the COMP side-chain row (DynamicsPanel.tsx).
// The learners hold the logic and read the lock-free analysis taps on the
// message thread; the strips only draw them and forward clicks.
namespace fui {

juce::String toFixed(double value, int digits);

// Reads and writes plain parameter values (one gesture per write).
float readParam(const ParameterSource&, const juce::String& id, float fallback = 0);
void writeParam(const ParameterSource&, const juce::String& id, float value);

class AutoEqLearner {
public:
    static constexpr double kLearnSeconds = 8, kTimeoutSeconds = 30;
    static constexpr int kFftSize = 16384;

    AutoEqLearner(AutoEqSource, ParameterSource, juce::String prefix, double sampleRate);
    ~AutoEqLearner();

    bool available() const { return source_.tap != nullptr; }
    bool learning() const { return learning_; }
    double progress() const { return progress_; }
    // LEARN / STOP toggle.
    void learn();
    // Pull new tap frames; finishes on 8 s of sound or the timeout.
    void poll();
    void stop();

    void setReference(const std::string&);
    const std::string& reference() const { return reference_; }
    void setAmount(double);
    double amount() const { return amount_; }
    const fable::automix::EqFit* fit() const { return fit_ ? &*fit_ : nullptr; }
    bool learned() const { return levels_.has_value(); }
    bool canApply() const { return fit_.has_value() && !learning_; }
    bool canUndo() const { return !undo_.empty(); }
    void apply();
    void undo();
    juce::String status() const;
    juce::String learnLabel() const;
    double sampleRate() const { return sampleRate_; }
    static juce::StringArray eqKeys();

private:
    void refit();
    juce::String id(const juce::String& key) const { return prefix_ + key; }
    AutoEqSource source_;
    ParameterSource params_;
    juce::String prefix_;
    double sampleRate_;
    std::string reference_;
    double amount_ = 0.8, progress_ = 0, started_ = 0;
    bool learning_ = false;
    std::unique_ptr<fable::automix::SpectrumCapture> capture_;
    std::vector<fable::automix::EqBand> colour_;
    bool removeColour_ = false;
    uint64_t cursor_ = 0;
    std::vector<float> pending_, spectrum_;
    int filled_ = 0;
    std::optional<std::vector<double>> levels_;
    std::optional<fable::automix::EqFit> fit_;
    std::map<juce::String, float> undo_;
};

class AutoCompLearner {
public:
    static constexpr double kLearnSeconds = 8, kSparseSeconds = 12, kTimeoutSeconds = 30;

    AutoCompLearner(AutoCompSource, ParameterSource, juce::String prefix, double sampleRate,
                    std::function<std::optional<double>()> tempo);
    ~AutoCompLearner();

    bool available() const { return source_.trace != nullptr; }
    bool learning() const { return learning_; }
    double progress() const { return progress_; }
    void learn();
    void poll();
    void stop();

    void setStyle(const std::string&);
    const std::string& style() const { return style_; }
    void setDepth(double);
    double depth() const { return depth_; }
    const fable::automix::CompFit* fit() const { return fit_ ? &*fit_ : nullptr; }
    bool learned() const { return learned_; }
    bool canApply() const { return fit_.has_value() && !learning_; }
    bool canUndo() const { return !undo_.empty(); }
    void apply();
    void undo();
    juce::String status() const;
    juce::String learnLabel() const;
    // 30-bin histogram of the fitted block levels (-60..0 dB), max-normalised.
    std::vector<float> histogram() const;

private:
    void refit();
    juce::String id(const juce::String& key) const { return prefix_ + "fx.comp." + key; }
    AutoCompSource source_;
    ParameterSource params_;
    juce::String prefix_;
    double sampleRate_;
    std::function<std::optional<double>()> tempo_;
    std::string style_;
    double depth_ = 1, progress_ = 0, started_ = 0;
    bool learning_ = false, learned_ = false;
    uint64_t cursor_ = 0;
    std::vector<float> pending_;
    int audible_ = 0;
    std::vector<double> blocks_, learnedBlocks_;
    std::vector<std::vector<float>> traces_, learnedTraces_;
    std::optional<double> learnedTempo_;
    std::optional<fable::automix::CompFit> fit_;
    std::map<juce::String, float> undo_;
};

// Horizontal amount slider with a percentage readout (the web range inputs).
class AmountSlider : public juce::Component {
public:
    AmountSlider(double min, double max, double step);
    void setValue(double v) { value_ = juce::jlimit(min_, max_, v); repaint(); }
    double value() const { return value_; }
    std::function<void(double)> onChange;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    void setFromX(float x);
    double min_, max_, step_, value_ = 0;
};

// Small 8 px action button of the auto-mix strips (web .eq-auto button).
// A toggled chip fills with the accent; `progress` >= 0 fills from the left
// (the web --p gradient on LEARN / LISTEN).
class ChipButton : public juce::TextButton {
public:
    explicit ChipButton(const juce::String& text = {}) : juce::TextButton(text) { setName(text); }
    void setProgress(double p) { if (std::abs(p - progress_) > 1e-9) { progress_ = p; repaint(); } }
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    double progress_ = -1;
};
using LearnButton = ChipButton;

class AutoEqStrip : public juce::Component, private juce::Timer {
public:
    AutoEqStrip(AutoEqSource, ParameterSource, juce::String prefix, double sampleRate);
    ~AutoEqStrip() override;
    AutoEqLearner& learner() { return learner_; }
    void refresh();
    void resized() override;
    void paint(juce::Graphics&) override;
    std::function<void()> onFitChanged;
private:
    void timerCallback() override;
    AutoEqLearner learner_;
    juce::ComboBox reference_;
    AmountSlider amount_{0, 1, 0.05};
    LearnButton learn_{"LEARN"};
    ChipButton apply_{"APPLY"}, undo_{"UNDO"};
    juce::Rectangle<int> status_;
};

class AutoCompStrip : public juce::Component, private juce::Timer {
public:
    AutoCompStrip(AutoCompSource, ParameterSource, juce::String prefix, double sampleRate,
                  std::function<std::optional<double>()> tempo);
    ~AutoCompStrip() override;
    AutoCompLearner& learner() { return learner_; }
    void refresh();
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    AutoCompLearner learner_;
    juce::ComboBox style_;
    AmountSlider depth_{0.25, 2, 0.05};
    LearnButton learn_{"LEARN"};
    ChipButton apply_{"APPLY"}, undo_{"UNDO"};
    juce::Rectangle<int> hist_, status_;
};

// SIDECHAIN source menu. The COMP panel adds the KEY HP knob once keyed.
class SideChainRow : public juce::Component {
public:
    SideChainRow(ParameterSource, juce::String prefix, SideChainInfo);
    ~SideChainRow() override;
    void refresh();
    // Re-reads the param after outside edits (automation, undo, a preset).
    void poll() { if (value() != shown_) refresh(); }
    void resized() override;
    void paint(juce::Graphics&) override;
    int value() const;
    juce::ComboBox& menu() { return menu_; }
    // Called after the source changes, from the menu or an outside edit.
    std::function<void(int)> onValueChanged;
private:
    ParameterSource params_;
    juce::String prefix_;
    SideChainInfo info_;
    juce::ComboBox menu_;
    juce::Rectangle<int> label_, note_;
    int shown_ = -1;
};

} // namespace fui
