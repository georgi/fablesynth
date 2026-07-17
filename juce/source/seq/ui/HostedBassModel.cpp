#include "HostedBassModel.h"

#include "../SeqProcessor.h"
#include "../../bass/dsp/BassParams.h"
#include "../../bass/dsp/BassPatches.h"
#include "../dsp/SeqProtocol.h"

#include <algorithm>

namespace fui {

namespace {

const fable::ParamInfo* bassCatalogData() {
    return fable::bassParamInfo().data();
}

std::size_t bassCatalogSize() {
    return fable::bassParamInfo().size();
}

} // namespace

HostedBassModel::HostedBassModel(SeqAudioProcessor& processor)
    : proc_(processor), parameterBank_(bassCatalogData(), bassCatalogSize()) {
    reloadParameters();
    startTimerHz(30);
}

HostedBassModel::~HostedBassModel() {
    stopTimer();
    flushPendingPatch();
}

void HostedBassModel::setTargetScene(int scene) {
    flushPendingPatch();
    scene_ = scene;
    editPattern_ = std::clamp(editPattern_, 0, std::max(0, clipBars() - 1));
    chain_.clear();
    for (int bar = 0; bar < clipBars(); ++bar) chain_.push_back(bar);
}

void HostedBassModel::flushPendingPatch() {
    if (parameterBank_.consumeDirty())
        proc_.setTrackInlineParams(kTrack, parameterBank_.snapshot());
}

ParameterSource HostedBassModel::parameters() {
    return parameterBank_.source();
}

DeviceUiCapabilities HostedBassModel::capabilities() const {
    DeviceUiCapabilities result;
    result.hosted = true;
    result.ownsTransport = false;
    result.supportsPatternChain = !hasTargetClip()
        || proc_.conductor().session().scenes[(size_t)scene_].clips[(size_t)kTrack].bars
            <= fable::SQ_HOSTED_MAX_BARS;
    result.supportsUserTables = false;
    result.supportsAudition = true;
    return result;
}

int HostedBassModel::currentProgram() const {
    return proc_.trackFactoryProgram(kTrack);
}

int HostedBassModel::numPrograms() const {
    return (int)fable::bassFactoryPatches().size();
}

juce::String HostedBassModel::programName(int index) const {
    const auto& programs = fable::bassFactoryPatches();
    return index >= 0 && index < (int)programs.size()
        ? juce::String(programs[(size_t)index].name)
        : juce::String();
}

void HostedBassModel::selectProgram(int index) {
    if (index < 0 || index >= numPrograms()) return;
    flushPendingPatch();
    proc_.setTrackFactoryPatch(kTrack, index);
    reloadParameters();
}

const fable::GeneratedTable* HostedBassModel::tableAt(int index) const {
    return proc_.deviceTableAt(kTrack, index);
}

float HostedBassModel::vizPosition() const { return proc_.bassVizPosition(); }
float HostedBassModel::vizCutoff() const { return proc_.bassVizCutoff(); }

void HostedBassModel::readScope(float* destination, int count) const {
    proc_.readScope(destination, count);
}

bool HostedBassModel::midiActive() const { return currentSemitone() != -100; }
bool HostedBassModel::hostSynced() const { return true; }
double HostedBassModel::hostBpm() const { return proc_.conductor().session().bpm; }

void HostedBassModel::noteOn(int semitone, float velocity) {
    proc_.auditionBassOn(semitone, velocity);
}

void HostedBassModel::noteOff(int semitone) {
    proc_.auditionBassOff(semitone);
}

int HostedBassModel::currentSemitone() const { return proc_.bassCurrentSemitone(); }

bool HostedBassModel::sequencerPlaying() const {
    return validScene() && proc_.conductor().ownerOf(kTrack) == scene_;
}

void HostedBassModel::setSequencerPlaying(bool) {
    // SQ-4 owns launch/stop transport. The reusable body advertises that fact
    // through capabilities(); a defensive no-op keeps accidental calls safe.
}

int HostedBassModel::currentStep() const {
    return sequencerPlaying() ? proc_.trackStep[kTrack].load() : -1;
}

int HostedBassModel::currentPattern() const {
    return sequencerPlaying() ? std::max(0, proc_.trackBar[kTrack].load()) : 0;
}

void HostedBassModel::setEditPattern(int pattern) {
    editPattern_ = std::clamp(pattern, 0, std::max(0, clipBars() - 1));
}

fable::BassSeqStep HostedBassModel::sequenceStep(int pattern, int step) const {
    if (!hasTargetClip() || pattern < 0 || pattern >= clipBars()
        || step < 0 || step >= fable::BL_STEPS)
        return {};

    const auto& clip = proc_.conductor().session().scenes[(size_t)scene_].clips[(size_t)kTrack];
    return fable::getBassStep(clip.bytes.data(), pattern, step);
}

void HostedBassModel::setSequenceStep(int pattern, int step,
                                      const fable::BassSeqStep& value) {
    if (!hasTargetClip() || pattern < 0 || pattern >= clipBars()
        || step < 0 || step >= fable::BL_STEPS)
        return;

    const auto& clip = proc_.conductor().session().scenes[(size_t)scene_].clips[(size_t)kTrack];
    if (clip.bars > fable::SQ_HOSTED_MAX_BARS) return;
    auto bytes = clip.bytes;
    fable::setBassStep(bytes.data(), pattern, step, value);
    proc_.conductor().updateClipBytes(scene_, kTrack, std::move(bytes), clip.bars);
}

std::vector<uint8_t> HostedBassModel::patternBytes() const {
    if (!hasTargetClip()) return {};
    return proc_.conductor().session().scenes[(size_t)scene_].clips[(size_t)kTrack].bytes;
}

void HostedBassModel::setPatternBytes(std::vector<uint8_t> bytes) {
    if (!hasTargetClip()) return;
    const auto& clip = proc_.conductor().session().scenes[(size_t)scene_].clips[(size_t)kTrack];
    if (clip.bars > fable::SQ_HOSTED_MAX_BARS) return;
    // A buffer whose length disagrees with the clip's bar count would ship a
    // short/long clip to the audio thread (out-of-bounds step reads) and break
    // validateSession on the next save. Reject it outright — callers must
    // resize via setChain first.
    if ((int)bytes.size() != clip.bars * fable::sqBytesPerBar(fable::Machine::BL1)) return;
    proc_.conductor().updateClipBytes(scene_, kTrack, std::move(bytes), clip.bars);
}

void HostedBassModel::setChain(std::vector<int> chain) {
    if (!hasTargetClip()) return;
    const auto& current = proc_.conductor().session().scenes[(size_t)scene_].clips[(size_t)kTrack];
    if (current.bars > fable::SQ_HOSTED_MAX_BARS) return;
    const int bars = std::clamp((int)chain.size(), 1, fable::SQ_HOSTED_MAX_BARS);
    auto bytes = fable::sqEmptyClip(fable::Machine::BL1, bars);
    std::copy_n(current.bytes.begin(), std::min(current.bytes.size(), bytes.size()), bytes.begin());
    proc_.conductor().updateClipBytes(scene_, kTrack, std::move(bytes), bars);
    editPattern_ = std::min(editPattern_, bars - 1);
    chain_.clear();
    for (int bar = 0; bar < bars; ++bar) chain_.push_back(bar);
}

bool HostedBassModel::hasTargetClip() const {
    return validScene()
        && kTrack < (int)proc_.conductor().session().scenes[(size_t)scene_].hasClip.size()
        && proc_.conductor().session().scenes[(size_t)scene_].hasClip[(size_t)kTrack];
}

void HostedBassModel::createTargetClip() {
    if (!validScene()) return;
    proc_.conductor().createClip(scene_, kTrack);
    editPattern_ = 0;
    chain_.assign(1, 0);
}

int HostedBassModel::clipBars() const {
    if (!hasTargetClip()) return 1;
    const int bars = proc_.conductor().session().scenes[(size_t)scene_].clips[(size_t)kTrack].bars;
    return std::clamp(bars, 1, fable::SQ_HOSTED_MAX_BARS);
}

void HostedBassModel::timerCallback() {
    flushPendingPatch();
}

void HostedBassModel::reloadParameters() {
    parameterBank_.load(proc_.trackParameterValues(kTrack));
}

void HostedBassModel::reloadPatchFromSession() {
    reloadParameters();
}

bool HostedBassModel::validScene() const {
    return scene_ >= 0 && scene_ < (int)proc_.conductor().session().scenes.size();
}

} // namespace fui
