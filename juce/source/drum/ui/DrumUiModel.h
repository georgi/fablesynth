#pragma once

#include "../../ui/DeviceUiModel.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace fable { struct GeneratedTable; struct UserTable; }
class DrumAudioProcessor;

namespace fui {

class DrumUiModel : public DeviceUiModel {
public:
    virtual int selectedPad() const = 0;
    virtual void selectPad(int) = 0;
    virtual juce::ChangeBroadcaster& selectionChanges() = 0;
    virtual juce::String padName(int) const = 0;
    virtual uint32_t patchContextRevision() const = 0;
    virtual void applyFactoryPadPatch(int) = 0;

    virtual int currentProgram() const = 0;
    virtual int numPrograms() const = 0;
    virtual juce::String programName(int) const = 0;
    virtual void selectProgram(int) = 0;

    virtual int numTables() const = 0;
    virtual const fable::GeneratedTable* tableAt(int) const = 0;
    virtual juce::String tableName(int) const = 0;
    virtual int tablesGeneration() const = 0;
    virtual int addUserTableForPad(int, fable::UserTable) = 0;

    virtual void triggerPad(int, float velocity) = 0;
    virtual uint32_t consumeHitFlags() = 0;
    virtual float vizPosition(int oscillator) const = 0;
    virtual float vizEnvelope() const = 0;
    virtual void readScope(float*, int) const = 0;
    virtual bool midiActive() const = 0;
    virtual bool hostSynced() const = 0;
    virtual double hostBpm() const = 0;

    virtual bool sequencerPlaying() const = 0;
    virtual void setSequencerPlaying(bool) = 0;
    virtual int currentStep() const = 0;
    virtual int currentPattern() const = 0;
    virtual int editPattern() const = 0;
    virtual void setEditPattern(int) = 0;
    virtual uint8_t step(int pattern, int pad, int step) const = 0;
    virtual void setStep(int pattern, int pad, int step, uint8_t) = 0;
    virtual const std::vector<int>& chain() const = 0;
    virtual void setChain(std::vector<int>) = 0;

    // Hosted clip surface. Standalone implementations return false/1 and no-op.
    virtual bool hasTargetClip() const { return true; }
    virtual void createTargetClip() {}
    virtual int clipBars() const { return 1; }
};

std::unique_ptr<DrumUiModel> makeStandaloneDrumUiModel(DrumAudioProcessor&);

} // namespace fui
