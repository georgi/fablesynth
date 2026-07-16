#pragma once

#include "../../ui/DeviceParameterBank.h"
#include "../../ui/WtUiModel.h"
#include "../../dsp/UserTables.h"
#include "../../dsp/UserTables.h"
#include "../dsp/SeqModel.h"

#include <juce_events/juce_events.h>

class SeqAudioProcessor;

namespace fable { struct ClipData; }

namespace fui {

// Message-thread adapter that presents one of SQ-4's WT-1 tracks to the
// reusable native WT device body. The track is fixed for the model's lifetime;
// only the focused scene (and therefore the edited clip) is retargeted.
class HostedWtModel final : public WtUiModel, private juce::Timer {
public:
    HostedWtModel(SeqAudioProcessor&, int track);
    ~HostedWtModel() override;

    void setTarget(int scene);
    int targetScene() const { return scene_; }
    int trackIndex() const { return track_; }
    void flushPendingPatch();
    void reloadPatchFromSession();

    ParameterSource parameters() override;
    DeviceUiCapabilities capabilities() const override;

    int currentProgram() const override;
    int numPrograms() const override;
    juce::String programName(int) const override;
    void selectProgram(int) override;

    int numTables() const override;
    const fable::GeneratedTable* tableAt(int) const override;
    juce::String tableName(int) const override;
    int tablesGeneration() const override { return 0; }
    float vizPosition(int oscillator) const override;
    int voiceCount() const override;
    bool midiActive() const override;
    double sampleRate() const override;
    void readScope(float*, int) const override;
    bool hostSynced() const override { return true; }
    double hostBpm() const override;

    bool sequencerPlaying() const override;
    void setSequencerPlaying(bool) override {}
    int currentStep() const override;
    int currentPattern() const override;
    int editPattern() const override { return editPattern_; }
    void setEditPattern(int) override;
    fable::NoteSeqStep sequenceStep(int pattern, int step) const override;
    void setSequenceStep(int pattern, int step, const fable::NoteSeqStep&) override;
    const std::vector<int>& chain() const override { return chain_; }
    void setChain(std::vector<int>) override;

    const std::vector<fable::UserTable>& userTables() const override;
    const std::vector<std::shared_ptr<const fable::GeneratedTable>>& factoryTables() const override;
    int maxUserTables() const override { return 0; }
    int addUserTable(fable::UserTable) override { return -1; }
    void deleteUserTable(int) override {}
    void renameUserTable(int, std::string) override {}
    void updateUserTable(int, fable::UserTable) override {}
    int duplicateUserTable(int) override { return -1; }
    int duplicateFactoryTable(int) override { return -1; }

    bool hasTargetClip() const override;
    void createTargetClip() override;
    int clipBars() const override;

    // Kept available for a future hosted keyboard component. These always use
    // the processor FIFO; the UI model never touches the WT engine directly.
    void auditionNoteOn(int midiNote, float velocity);
    void auditionNoteOff(int midiNote);

private:
    void timerCallback() override;
    const fable::ClipData* targetClip() const;

    SeqAudioProcessor& proc_;
    const int track_;
    int scene_ = -1;
    int editPattern_ = 0;
    std::vector<int> chain_ { 0 };
    DeviceParameterBank parameters_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedWtModel)
};

} // namespace fui
