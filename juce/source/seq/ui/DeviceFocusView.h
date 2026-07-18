#pragma once

#include "HostedBassModel.h"
#include "HostedDrumModel.h"
#include "HostedWtModel.h"
#include "../../bass/BassDeviceBody.h"
#include "../../drum/DrumDeviceBody.h"
#include "../../ui/WtDeviceBody.h"
#include "../dsp/ClipLibrary.h"
#include "../ClipLibraryStorage.h"

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
    void reloadPatchesFromSession();
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
    juce::ComboBox& patchSelectorForTest() { return patchSelector_; }
    juce::ComboBox& clipSelectorForTest() { return clipSelector_; }
    juce::ComboBox& clipSourceForTest() { return clipSourceSelector_; }
    juce::ComboBox& clipActionsForTest() { return clipActionsSelector_; }
    fable::ClipLibraryStorage& clipStorageForTest() { return clipStorage_; }
    const juce::String clipTargetForTest() const { return clipTargetLabel_.getText(); }
    const juce::String clipMetadataForTest() const { return clipMetadataLabel_.getText(); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kDrumWidth = 1460, kDrumHeight = 880;
    static constexpr int kBassWidth = 1460, kBassHeight = 931;

    void timerCallback() override;
    void flushActiveModel();
    bool activeHasTargetClip() const;
    void createActiveClip();
    void updateCreateClipButton();
    void refreshPatchSelector();
    void refreshClipBrowser(bool rebuild);
    void refreshClipMetadata();
    void loadSelectedClip();
    void saveCurrentClip();
    void saveSelectedTransform(int transformIndex);
    void chooseImportClip();
    void chooseExportClip();
    void retargetModelsAfterLoad();
    const fable::ClipLibraryEntry* selectedClip() const;
    void stepActiveProgram(int direction);
    int activeCurrentProgram() const;
    int activeNumPrograms() const;
    juce::String activeProgramName(int index) const;
    void selectActiveProgram(int index);
    void layoutBody(juce::Component&, int logicalWidth, int logicalHeight, int contentTop = 0);

    SeqAudioProcessor& proc_;
    HostedDrumModel drumModel_;
    HostedBassModel bassModel_;
    HostedWtModel wt2Model_;
    HostedWtModel wt3Model_;

    DrumDeviceBody drumBody_;
    BassDeviceBody bassBody_;
    WtDeviceBody wt2Body_;
    WtDeviceBody wt3Body_;

    juce::Label patchLabel_;
    juce::TextButton previousPatchButton_ { "<" };
    juce::ComboBox patchSelector_;
    juce::TextButton nextPatchButton_ { ">" };

    juce::Label clipTargetLabel_;
    juce::ComboBox clipSourceSelector_;
    juce::ComboBox clipSelector_;
    juce::Label clipMetadataLabel_;
    juce::ComboBox clipActionsSelector_;
    juce::TextButton createClipButton_ { "CREATE CLIP" };
    int shownPatchTrack_ = -1;
    int shownPatchProgram_ = -2;
    int selectedClipIndex_ = -1;
    int targetScene_ = -1;
    int targetTrack_ = -1;
    ActiveBody activeBody_ = ActiveBody::none;
    fable::ClipLibraryStorage clipStorage_;
    std::vector<fable::SourcedClip> visibleClips_;
    std::unique_ptr<juce::FileChooser> clipChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceFocusView)
};

} // namespace fui
