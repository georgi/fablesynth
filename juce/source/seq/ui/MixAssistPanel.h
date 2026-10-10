#pragma once
#include "../../ui/AutoMix.h"
#include "../../ui/VisualTimer.h"
#include "../../ui/LiveSpectrum.h"
#include "../../dsp/automix/MixAssistant.h"

#include <juce_gui_basics/juce_gui_basics.h>

class SeqAudioProcessor;

// SQ-4 master-bus assistant (src/seq/components/MixAssistPanel.tsx): listen
// to the running session, compare its tonal balance with a target and
// propose EQ, dynamics, loudness and fader moves. The plan is a preview until
// APPLY; REVERT restores the snapshot taken then.
namespace fui {

class MixAssistSession {
public:
    static constexpr double kListenSeconds = 12, kTimeoutSeconds = 45, kMinSeconds = 3;
    static constexpr int kFftSize = 8192;
    struct Snapshot {
        std::map<std::string, float> masterFx;
        std::vector<double> trackVol;
        double masterVol = 0.75;
    };

    explicit MixAssistSession(SeqAudioProcessor&);
    bool playing() const;
    bool listening() const { return listener_ != nullptr; }
    double progress() const { return progress_; }
    // LISTEN / STOP toggle.
    void listen();
    void poll();
    void stop(bool finish);

    void setReference(const std::string&);
    const std::string& reference() const { return reference_; }
    void setLoudness(int);
    int loudness() const { return loudness_; }
    void setAmount(double);
    double amount() const { return amount_; }
    void setRole(int track, std::optional<fable::automix::MixRole>);
    std::optional<fable::automix::MixRole> role(int track) const;

    const fable::automix::MixAnalysis* analysis() const { return analysis_ ? &*analysis_ : nullptr; }
    const fable::automix::AssistPlan* plan() const { return plan_ ? &*plan_ : nullptr; }
    bool applied() const { return snapshot_.has_value(); }
    bool canApply() const { return plan_.has_value() && !listening(); }
    void apply();
    void revert();
    juce::String caption() const;
    Snapshot current() const;

private:
    void rebuildPlan();
    void write(const juce::String& id, float value);
    SeqAudioProcessor& proc_;
    std::string reference_ = "mix";
    int loudness_ = -11;
    double amount_ = 0.7, progress_ = 0, started_ = 0;
    std::unique_ptr<fable::automix::MixListener> listener_;
    std::vector<uint64_t> cursors_;
    std::vector<std::vector<float>> pending_;
    std::optional<fable::automix::MixAnalysis> analysis_;
    std::optional<Snapshot> base_, snapshot_;
    std::vector<std::optional<fable::automix::MixRole>> roles_;
    std::optional<fable::automix::AssistPlan> plan_;
};

class MixAssistPanel : public juce::Component, private fui::VisualTimer {
public:
    explicit MixAssistPanel(SeqAudioProcessor&);
    ~MixAssistPanel() override;
    MixAssistSession& session() { return session_; }
    void refresh();
    // Reads the post-FX output once (the panel timer does this every frame).
    void updateLive(double nowMs);
    const LiveSpectrum& liveSpectrum() const { return live_; }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kLevelRow = 18, kLevelName = 56, kLevelRole = 108;
    void timerCallback() override;
    void visualVisibilityChanged(bool visible) override { if (!visible) live_.clear(); }
    void rebuildRoleMenus();
    SeqAudioProcessor& proc_;
    MixAssistSession session_;
    juce::ComboBox reference_;
    std::array<ChipButton, 3> targets_;
    AmountSlider amount_{0, 1, 0.05};
    LearnButton listen_{"LISTEN"};
    ChipButton apply_{"APPLY"}, revert_{"REVERT"};
    juce::OwnedArray<juce::ComboBox> roles_;
    std::vector<int> roleTracks_;
    juce::Rectangle<int> head_, plot_, legend_, meters_, moves_, levels_;
    // Live post-FX spectrum: third-octave powers with ~250 ms smoothing.
    LiveSpectrum live_;
};

} // namespace fui
