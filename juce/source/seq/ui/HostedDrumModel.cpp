#include "HostedDrumModel.h"

#include "../../drum/dsp/DrumKits.h"
#include "../../drum/dsp/DrumPatches.h"
#include "../../dsp/UserTables.h"
#include "../dsp/SeqProtocol.h"

namespace fui {

HostedDrumModel::HostedDrumModel(SeqAudioProcessor& proc)
    : proc_(proc),
      bank_(fable::drumParamInfo().data(), fable::drumParamInfo().size()) {
    bank_.load(proc_.trackParameterValues(0));
    startTimerHz(30);
}

DeviceUiCapabilities HostedDrumModel::capabilities() const {
    DeviceUiCapabilities c;
    c.hosted = true;
    c.ownsTransport = false;
    c.supportsPatternChain = false;
    c.supportsUserTables = false;
    return c;
}

void HostedDrumModel::setTargetScene(int scene) {
    flushPendingPatch();
    scene_ = juce::jmax(0, scene);
    editBar_ = juce::jlimit(0, juce::jmax(0, clipBars() - 1), editBar_);
    chain_[0] = editBar_;
}

void HostedDrumModel::flushPendingPatch() {
    if (bank_.consumeDirty()) {
        proc_.setTrackInlineParams(0, bank_.snapshot());
        ++patchRevision_;
    }
}

void HostedDrumModel::selectPad(int pad) {
    selectedPad_ = juce::jlimit(0, fable::DR_NPADS - 1, pad);
    proc_.selectDrumPad(selectedPad_);
    ++patchRevision_;
    selectionChanges_.sendChangeMessage();
}

juce::String HostedDrumModel::padName(int pad) const {
    const int program = juce::jlimit(0, numPrograms() - 1, currentProgram());
    return fable::factoryKits()[(size_t)program].padNames[(size_t)juce::jlimit(0, fable::DR_NPADS - 1, pad)];
}

void HostedDrumModel::applyFactoryPadPatch(int index) {
    const auto& patches = fable::factoryPatches();
    if (index < 0 || index >= (int)patches.size()) return;
    for (const auto& value : fable::applyPatchToPad(selectedPad_, patches[(size_t)index])) {
        const auto& info = fable::drumParamInfo()[(size_t)value.first];
        if (auto* p = bank_.parameter(info.pid))
            p->setValueNotifyingHost(p->convertTo0to1(value.second));
    }
    flushPendingPatch();
}

int HostedDrumModel::currentProgram() const { return proc_.trackFactoryProgram(0); }
int HostedDrumModel::numPrograms() const { return (int)fable::factoryKits().size(); }
juce::String HostedDrumModel::programName(int i) const {
    return i >= 0 && i < numPrograms() ? fable::factoryKits()[(size_t)i].name : "";
}
void HostedDrumModel::selectProgram(int i) {
    if (i < 0 || i >= numPrograms()) return;
    proc_.setTrackFactoryPatch(0, i);
    bank_.load(proc_.trackParameterValues(0));
    ++patchRevision_;
    selectionChanges_.sendChangeMessage();
}

int HostedDrumModel::numTables() const { return proc_.deviceNumTables(0); }
const fable::GeneratedTable* HostedDrumModel::tableAt(int i) const { return proc_.deviceTableAt(0, i); }
juce::String HostedDrumModel::tableName(int i) const { return proc_.deviceTableName(0, i); }
int HostedDrumModel::addUserTableForPad(int, fable::UserTable) { return -1; }
void HostedDrumModel::triggerPad(int pad, float velocity) { proc_.auditionDrum(pad, velocity); }
uint32_t HostedDrumModel::consumeHitFlags() { return proc_.consumeDrumHitFlags(); }
float HostedDrumModel::vizPosition(int osc) const { return proc_.drumVizPosition(osc); }
float HostedDrumModel::vizEnvelope() const { return proc_.drumVizEnvelope(); }
void HostedDrumModel::readScope(float* out, int n) const { proc_.readScope(out, n); }
double HostedDrumModel::hostBpm() const { return proc_.conductor().session().bpm; }
bool HostedDrumModel::sequencerPlaying() const { return proc_.conductor().ownerOf(0) == scene_; }
int HostedDrumModel::currentStep() const { return proc_.trackStep[0].load(); }
int HostedDrumModel::currentPattern() const { return proc_.trackBar[0].load(); }

void HostedDrumModel::setEditPattern(int bar) {
    editBar_ = juce::jlimit(0, juce::jmax(0, clipBars() - 1), bar);
    chain_[0] = editBar_;
}

const fable::ClipData* HostedDrumModel::clip() const {
    const auto& session = proc_.conductor().session();
    if (scene_ < 0 || scene_ >= (int)session.scenes.size()) return nullptr;
    const auto& scene = session.scenes[(size_t)scene_];
    return scene.hasClip[0] ? &scene.clips[0] : nullptr;
}

uint8_t HostedDrumModel::step(int pattern, int pad, int stepIndex) const {
    const auto* c = clip();
    if (!c || pattern < 0 || pattern >= c->bars || pad < 0 || pad >= fable::SQ_DR1_PADS
        || stepIndex < 0 || stepIndex >= fable::SQ_STEPS_PER_BAR) return 0;
    return c->bytes[(size_t)fable::sqDr1Idx(pattern, pad, stepIndex)];
}

void HostedDrumModel::setStep(int pattern, int pad, int stepIndex, uint8_t value) {
    const auto* c = clip();
    if (!c || c->bars > fable::SQ_HOSTED_MAX_BARS
        || pattern < 0 || pattern >= c->bars || pad < 0 || pad >= fable::SQ_DR1_PADS
        || stepIndex < 0 || stepIndex >= fable::SQ_STEPS_PER_BAR) return;
    auto bytes = c->bytes;
    bytes[(size_t)fable::sqDr1Idx(pattern, pad, stepIndex)] = value;
    proc_.conductor().updateClipBytes(scene_, 0, std::move(bytes), c->bars);
}

bool HostedDrumModel::hasTargetClip() const { return clip() != nullptr; }
void HostedDrumModel::createTargetClip() { proc_.conductor().createClip(scene_, 0); }
int HostedDrumModel::clipBars() const {
    const auto* c = clip();
    return c ? juce::jmin(c->bars, fable::SQ_HOSTED_MAX_BARS) : 1;
}

} // namespace fui
