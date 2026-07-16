#pragma once

#include "../../bass/ui/BassUiModel.h"
#include "../../ui/DeviceParameterBank.h"

#include <juce_events/juce_events.h>

class SeqAudioProcessor;

namespace fui {

// Message-thread adapter that presents SQ-4 track 1 as a native BL-1 surface.
// The parameter bank is editor-owned; its listener only marks it dirty and the
// timer performs the session + command-FIFO write on the message thread.
class HostedBassModel final : public BassUiModel, private juce::Timer {
public:
    explicit HostedBassModel(SeqAudioProcessor&);
    ~HostedBassModel() override;

    void setTargetScene(int scene);
    int targetScene() const { return scene_; }
    void flushPendingPatch();
    void reloadPatchFromSession();

    ParameterSource parameters() override;
    DeviceUiCapabilities capabilities() const override;

    int currentProgram() const override;
    int numPrograms() const override;
    juce::String programName(int) const override;
    void selectProgram(int) override;

    const fable::GeneratedTable* tableAt(int) const override;
    float vizPosition() const override;
    float vizCutoff() const override;
    void readScope(float*, int) const override;
    bool midiActive() const override;
    bool hostSynced() const override;
    double hostBpm() const override;

    void noteOn(int semitone, float velocity) override;
    void noteOff(int semitone) override;
    int currentSemitone() const override;

    bool sequencerPlaying() const override;
    void setSequencerPlaying(bool) override;
    int currentStep() const override;
    int currentPattern() const override;
    int editPattern() const override { return editPattern_; }
    void setEditPattern(int) override;
    fable::BassSeqStep sequenceStep(int pattern, int step) const override;
    void setSequenceStep(int pattern, int step, const fable::BassSeqStep&) override;
    const std::vector<int>& chain() const override { return chain_; }
    void setChain(std::vector<int>) override;

    bool hasTargetClip() const override;
    void createTargetClip() override;
    int clipBars() const override;

private:
    static constexpr int kTrack = 1;

    void timerCallback() override;
    void reloadParameters();
    bool validScene() const;

    SeqAudioProcessor& proc_;
    DeviceParameterBank parameterBank_;
    int scene_ = -1;
    int editPattern_ = 0;
    std::vector<int> chain_ { 0 };
};

} // namespace fui
