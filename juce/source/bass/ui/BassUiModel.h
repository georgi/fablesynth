#pragma once

#include "../../ui/DeviceUiModel.h"

#include <cstdint>
#include <vector>

namespace fable { struct BassSeqStep; struct GeneratedTable; }

namespace fui {

class BassUiModel : public DeviceUiModel {
public:
    virtual int currentProgram() const = 0;
    virtual int numPrograms() const = 0;
    virtual juce::String programName(int) const = 0;
    virtual void selectProgram(int) = 0;

    virtual const fable::GeneratedTable* tableAt(int) const = 0;
    virtual float vizPosition() const = 0;
    virtual float vizCutoff() const = 0;
    virtual void readScope(float*, int) const = 0;
    virtual bool midiActive() const = 0;
    virtual bool hostSynced() const = 0;
    virtual double hostBpm() const = 0;

    virtual void noteOn(int semitone, float velocity) = 0;
    virtual void noteOff(int semitone) = 0;
    virtual int currentSemitone() const = 0;

    virtual bool sequencerPlaying() const = 0;
    virtual void setSequencerPlaying(bool) = 0;
    virtual int currentStep() const = 0;
    virtual int currentPattern() const = 0;
    virtual int editPattern() const = 0;
    virtual void setEditPattern(int) = 0;
    virtual fable::BassSeqStep sequenceStep(int pattern, int step) const = 0;
    virtual void setSequenceStep(int pattern, int step, const fable::BassSeqStep&) = 0;
    virtual const std::vector<int>& chain() const = 0;
    virtual void setChain(std::vector<int>) = 0;

    // Decision-6 (BL-1 step editing) foundation: the whole pattern byte
    // buffer (BL_PATTERN_BYTES for the standalone plugin; bars*BL_STEPS*
    // BL_STEP_STRIDE for a hosted SQ-4 clip), read/written in bulk so
    // StepEditOps.h's range/pattern ops and undo history can operate without
    // a per-step round trip. patternSourceId() changes whenever the hosted
    // clip/pattern source is swapped (SQ-4 focus switching); the view clears
    // its undo history and selection when it observes a change.
    virtual std::vector<uint8_t> patternBytes() const = 0;
    virtual void setPatternBytes(std::vector<uint8_t>) = 0;
    virtual int patternSourceId() const { return 0; }

    virtual bool hasTargetClip() const { return true; }
    virtual void createTargetClip() {}
    virtual int clipBars() const { return 1; }
};

} // namespace fui
