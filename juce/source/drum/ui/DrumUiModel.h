#pragma once

#include "../../ui/DeviceUiModel.h"
#include "../dsp/DrumRhythm.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace fable { struct GeneratedTable; struct UserTable; }
class DrumAudioProcessor;

namespace fui {

struct DrumSequence {
    std::vector<uint8_t> steps;
    std::vector<int> chain;
    bool hasRhythm = false;
    fable::DrumRhythm rhythm{};
    uint16_t configuredLanes = 0;
};

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

    virtual bool supportsPoly() const { return false; }
    virtual DrumSequence sequence() const {
        DrumSequence s; s.chain = chain(); s.steps.resize(1024);
        for (int b = 0; b < 4; ++b) for (int p = 0; p < 16; ++p) for (int i = 0; i < 16; ++i)
            s.steps[(b * 16 + p) * 16 + i] = step(b, p, i);
        return s;
    }
    virtual bool commitSequence(const DrumSequence& s) {
        if (s.hasRhythm) return false;
        setChain(s.chain);
        for (int b = 0; b < 4; ++b) for (int p = 0; p < 16; ++p) for (int i = 0; i < 16; ++i)
            setStep(b, p, i, s.steps[(b * 16 + p) * 16 + i]);
        return true;
    }
    virtual bool loadPatternPreset(int) { return false; }
    virtual int lanePosition(int) const { return -1; }

    // Hosted clip surface. Standalone implementations return false/1 and no-op.
    virtual bool hasTargetClip() const { return true; }
    virtual void createTargetClip() {}
    virtual int clipBars() const { return 1; }

    // Identity of the currently-hosted pattern/clip source. Standalone never
    // changes (always 0); the hosted model returns something that changes
    // whenever the SQ-4 focus target switches to a different scene, so the
    // step editor can clear its undo history on the swap (decision 6: cross-
    // clip undo corruption is a hazard identical to the web's).
    virtual int clipIdentity() const { return 0; }
};

std::unique_ptr<DrumUiModel> makeStandaloneDrumUiModel(DrumAudioProcessor&);

} // namespace fui
