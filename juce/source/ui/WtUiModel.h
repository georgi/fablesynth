#pragma once

#include "DeviceUiModel.h"

#include <memory>
#include <string>
#include <vector>

namespace fable {
struct GeneratedTable;
struct NoteSeqStep;
struct UserTable;
}

namespace fui {

class WtUiModel : public DeviceUiModel {
public:
    virtual int currentProgram() const = 0;
    virtual int numPrograms() const = 0;
    virtual juce::String programName(int) const = 0;
    virtual void selectProgram(int) = 0;

    virtual int numTables() const = 0;
    virtual const fable::GeneratedTable* tableAt(int) const = 0;
    virtual juce::String tableName(int) const = 0;
    virtual int tablesGeneration() const = 0;
    virtual float vizPosition(int oscillator) const = 0;
    virtual int voiceCount() const = 0;
    virtual bool midiActive() const = 0;
    virtual double sampleRate() const = 0;
    virtual void readScope(float*, int) const = 0;
    virtual bool hostSynced() const = 0;
    virtual double hostBpm() const = 0;

    virtual bool sequencerPlaying() const = 0;
    virtual void setSequencerPlaying(bool) = 0;
    virtual int currentStep() const = 0;
    virtual int currentPattern() const = 0;
    virtual int editPattern() const = 0;
    virtual void setEditPattern(int) = 0;
    virtual fable::NoteSeqStep sequenceStep(int pattern, int step) const = 0;
    virtual void setSequenceStep(int pattern, int step, const fable::NoteSeqStep&) = 0;
    virtual const std::vector<int>& chain() const = 0;
    virtual void setChain(std::vector<int>) = 0;

    virtual const std::vector<fable::UserTable>& userTables() const = 0;
    virtual const std::vector<std::shared_ptr<const fable::GeneratedTable>>& factoryTables() const = 0;
    virtual int maxUserTables() const = 0;
    virtual int addUserTable(fable::UserTable) = 0;
    virtual void deleteUserTable(int) = 0;
    virtual void renameUserTable(int, std::string) = 0;
    virtual void updateUserTable(int, fable::UserTable) = 0;
    virtual int duplicateUserTable(int) = 0;
    virtual int duplicateFactoryTable(int) = 0;

    virtual bool hasTargetClip() const { return true; }
    virtual void createTargetClip() {}
    virtual int clipBars() const { return 1; }
};

} // namespace fui
