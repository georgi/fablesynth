#pragma once

#include "../../drum/ui/DrumUiModel.h"
#include "../../ui/DeviceParameterBank.h"
#include "../SeqProcessor.h"

namespace fui {

class HostedDrumModel final : public DrumUiModel, private juce::Timer {
public:
    explicit HostedDrumModel(SeqAudioProcessor&);
    ~HostedDrumModel() override { stopTimer(); }

    void setTargetScene(int scene);
    int targetScene() const { return scene_; }
    void flushPendingPatch();

    ParameterSource parameters() override { return bank_.source(); }
    DeviceUiCapabilities capabilities() const override;
    int selectedPad() const override { return selectedPad_; }
    void selectPad(int) override;
    juce::ChangeBroadcaster& selectionChanges() override { return selectionChanges_; }
    juce::String padName(int) const override;
    uint32_t patchContextRevision() const override { return patchRevision_; }
    void applyFactoryPadPatch(int) override;
    int currentProgram() const override;
    int numPrograms() const override;
    juce::String programName(int) const override;
    void selectProgram(int) override;
    int numTables() const override;
    const fable::GeneratedTable* tableAt(int) const override;
    juce::String tableName(int) const override;
    int tablesGeneration() const override { return 0; }
    int addUserTableForPad(int, fable::UserTable) override;
    void triggerPad(int, float) override;
    uint32_t consumeHitFlags() override;
    float vizPosition(int) const override;
    float vizEnvelope() const override;
    void readScope(float*, int) const override;
    bool midiActive() const override { return false; }
    bool hostSynced() const override { return true; }
    double hostBpm() const override;
    bool sequencerPlaying() const override;
    void setSequencerPlaying(bool) override {}
    int currentStep() const override;
    int currentPattern() const override;
    int editPattern() const override { return editBar_; }
    void setEditPattern(int) override;
    uint8_t step(int pattern, int pad, int step) const override;
    void setStep(int pattern, int pad, int step, uint8_t) override;
    const std::vector<int>& chain() const override { return chain_; }
    void setChain(std::vector<int>) override {}
    bool hasTargetClip() const override;
    void createTargetClip() override;
    int clipBars() const override;

private:
    void timerCallback() override { flushPendingPatch(); }
    const fable::ClipData* clip() const;

    SeqAudioProcessor& proc_;
    DeviceParameterBank bank_;
    int scene_ = 0, selectedPad_ = 0, editBar_ = 0;
    uint32_t patchRevision_ = 0;
    std::vector<int> chain_ { 0 };
    juce::ChangeBroadcaster selectionChanges_;
};

} // namespace fui
