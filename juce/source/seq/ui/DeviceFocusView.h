#pragma once

#include "HostedBassModel.h"
#include "HostedDrumModel.h"
#include "HostedWtModel.h"
#include "../../bass/BassDeviceBody.h"
#include "../../drum/DrumDeviceBody.h"
#include "../../ui/WtDeviceBody.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace fui {

// Hosts the four native SQ-4 machine surfaces. Models deliberately precede
// bodies in declaration order because every body retains references into its
// model and parameter bank.
class DeviceFocusView final : public juce::Component, private juce::Timer {
public:
    enum class ActiveBody { none, drum, bass, wt2, wt3 };

    explicit DeviceFocusView(SeqAudioProcessor&);
    ~DeviceFocusView() override;

    void setTarget(int scene, int track);
    int targetScene() const { return targetScene_; }
    int targetTrack() const { return targetTrack_; }
    ActiveBody activeBody() const { return activeBody_; }
    juce::Component* activeBodyComponent();
    const juce::Component* activeBodyComponent() const;

    HostedDrumModel& drumModelForTest() { return drumModel_; }
    HostedBassModel& bassModelForTest() { return bassModel_; }
    HostedWtModel& wt2ModelForTest() { return wt2Model_; }
    HostedWtModel& wt3ModelForTest() { return wt3Model_; }

    DrumDeviceBody& drumBodyForTest() { return drumBody_; }
    BassDeviceBody& bassBodyForTest() { return bassBody_; }
    WtDeviceBody& wt2BodyForTest() { return wt2Body_; }
    WtDeviceBody& wt3BodyForTest() { return wt3Body_; }

    void resized() override;

private:
    static constexpr int kDrumWidth = 1460, kDrumHeight = 880;
    static constexpr int kBassWidth = 1460, kBassHeight = 931;

    void timerCallback() override;
    void flushActiveModel();
    bool activeHasTargetClip() const;
    void createActiveClip();
    void updateCreateClipButton();
    void layoutBody(juce::Component&, int logicalWidth, int logicalHeight, int contentTop = 0);

    HostedDrumModel drumModel_;
    HostedBassModel bassModel_;
    HostedWtModel wt2Model_;
    HostedWtModel wt3Model_;

    DrumDeviceBody drumBody_;
    BassDeviceBody bassBody_;
    WtDeviceBody wt2Body_;
    WtDeviceBody wt3Body_;

    juce::TextButton createClipButton_ { "CREATE CLIP" };
    int targetScene_ = -1;
    int targetTrack_ = -1;
    ActiveBody activeBody_ = ActiveBody::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceFocusView)
};

} // namespace fui
