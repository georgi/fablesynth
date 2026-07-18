#include "HostedWtModel.h"

#include "../SeqProcessor.h"
#include "../../dsp/NoteSeq.h"
#include "../../dsp/Params.h"
#include "../../dsp/Presets.h"
#include "../../dsp/UserTables.h"
#include "../../dsp/Wavetables.h"
#include "../dsp/SeqProtocol.h"

#include <algorithm>

namespace fui {
namespace {

const std::vector<fable::UserTable>& noUserTables() {
    static const std::vector<fable::UserTable> empty;
    return empty;
}

const std::vector<std::shared_ptr<const fable::GeneratedTable>>& noEditableFactoryTables() {
    static const std::vector<std::shared_ptr<const fable::GeneratedTable>> empty;
    return empty;
}

} // namespace

HostedWtModel::HostedWtModel(SeqAudioProcessor& proc, int track)
    : proc_(proc), track_(track),
      parameters_(fable::paramInfo().data(), fable::paramInfo().size()) {
    jassert(track_ == 2 || track_ == 3);
    parameters_.load(proc_.trackParameterValues(track_));
    startTimerHz(30);
}

HostedWtModel::~HostedWtModel() {
    stopTimer();
    flushPendingPatch();
}

void HostedWtModel::setTarget(int scene) {
    flushPendingPatch();
    scene_ = scene;
    editPattern_ = 0;
    chain_.clear();
    for (int bar = 0; bar < clipBars(); ++bar) chain_.push_back(bar);
}

ParameterSource HostedWtModel::parameters() { return parameters_.source(); }

DeviceUiCapabilities HostedWtModel::capabilities() const {
    const auto* target = targetClip();
    return {
        true,  // hosted
        false, // ownsTransport
        target == nullptr || target->bars <= fable::SQ_HOSTED_MAX_BARS,
        false, // supportsUserTables
        true   // supportsAudition
    };
}

void HostedWtModel::timerCallback() { flushPendingPatch(); }

void HostedWtModel::flushPendingPatch() {
    if (parameters_.consumeDirty())
        proc_.setTrackInlineParams(track_, parameters_.snapshot());
}

void HostedWtModel::reloadPatchFromSession() {
    parameters_.load(proc_.trackParameterValues(track_));
}

int HostedWtModel::currentProgram() const {
    const auto& tracks = proc_.conductor().session().tracks;
    if (track_ < 0 || track_ >= (int)tracks.size()) return -1;
    const auto& patch = tracks[(size_t)track_].patch;
    return patch.factory ? patch.index : -1;
}
int HostedWtModel::numPrograms() const { return (int)fable::factoryPresets().size(); }

juce::String HostedWtModel::programName(int index) const {
    const auto& presets = fable::factoryPresets();
    return index >= 0 && index < (int)presets.size() ? juce::String(presets[(size_t)index].name)
                                                     : juce::String("CUSTOM");
}

void HostedWtModel::selectProgram(int index) {
    if (index < 0 || index >= numPrograms()) return;
    flushPendingPatch();
    proc_.setTrackFactoryPatch(track_, index);
    reloadPatchFromSession();
}

int HostedWtModel::numTables() const { return proc_.deviceNumTables(track_); }
const fable::GeneratedTable* HostedWtModel::tableAt(int index) const {
    return proc_.deviceTableAt(track_, index);
}
juce::String HostedWtModel::tableName(int index) const {
    return proc_.deviceTableName(track_, index);
}
float HostedWtModel::vizPosition(int oscillator) const {
    return proc_.wtVizPosition(track_, oscillator);
}
int HostedWtModel::voiceCount() const { return proc_.wtVoiceCount(track_); }
bool HostedWtModel::midiActive() const { return voiceCount() > 0; }
double HostedWtModel::sampleRate() const { return proc_.preparedSampleRate(); }
void HostedWtModel::readScope(float* dest, int n) const { proc_.readScope(dest, n); }

double HostedWtModel::hostBpm() const {
    return proc_.conductor().session().bpm;
}

bool HostedWtModel::sequencerPlaying() const {
    return scene_ >= 0 && proc_.conductor().ownerOf(track_) == scene_;
}

int HostedWtModel::currentStep() const {
    return sequencerPlaying() ? proc_.trackStep[track_].load() : -1;
}

int HostedWtModel::currentPattern() const {
    return sequencerPlaying() ? std::max(0, proc_.trackBar[track_].load()) : 0;
}

void HostedWtModel::setEditPattern(int pattern) {
    const int last = std::max(0, clipBars() - 1);
    editPattern_ = juce::jlimit(0, last, pattern);
}

const fable::ClipData* HostedWtModel::targetClip() const {
    if (scene_ < 0) return nullptr;
    const auto& session = proc_.conductor().session();
    if (track_ < 0 || track_ >= (int)session.tracks.size()
        || session.tracks[(size_t)track_].machine != fable::Machine::WT1
        || scene_ >= (int)session.scenes.size())
        return nullptr;
    const auto& scene = session.scenes[(size_t)scene_];
    if (track_ >= (int)scene.hasClip.size() || !scene.hasClip[(size_t)track_]
        || track_ >= (int)scene.clips.size())
        return nullptr;
    return &scene.clips[(size_t)track_];
}

fable::NoteSeqStep HostedWtModel::sequenceStep(int pattern, int step) const {
    const auto* clip = targetClip();
    if (clip == nullptr || pattern < 0 || pattern >= clip->bars
        || step < 0 || step >= fable::SEQ_STEPS)
        return {};
    int offset = -1;
    for (int lane = 0; lane < fable::SQ_WT_POLY_LANES; ++lane) {
        const int candidate = fable::sqWtNoteIdx(pattern, step, lane);
        if (candidate + 2 < (int)clip->bytes.size() && (clip->bytes[(size_t)candidate] & 1)) { offset = candidate; break; }
    }
    if (offset < 0) offset = fable::sqWtNoteIdx(pattern, step, 0);
    if (offset + 2 >= (int)clip->bytes.size()) return {};
    const auto flags = clip->bytes[(size_t)offset];
    const int duration = std::max(1, std::min(fable::SEQ_MAX_NOTE_STEPS,
                                              (int)((flags >> 2) & 0x3f)));
    return { (flags & 1) != 0, (int)clip->bytes[(size_t)offset + 1],
        (int)clip->bytes[(size_t)offset + 2] - 1, (flags & 2) != 0, duration };
}

void HostedWtModel::setSequenceStep(int pattern, int step, const fable::NoteSeqStep& value) {
    const auto* clip = targetClip();
    if (clip == nullptr || clip->bars > fable::SQ_HOSTED_MAX_BARS
        || pattern < 0 || pattern >= clip->bars
        || step < 0 || step >= fable::SEQ_STEPS)
        return;
    auto bytes = clip->bytes;
    // The hosted note editor is monophonic, whereas a WT-1 clip can contain
    // eight voices at a step.  Select the same first active lane that
    // sequenceStep() projects into the editor, then clear all sibling lanes
    // so a changed/rested chord cannot leave invisible notes sounding.
    int lane = 0;
    for (int candidateLane = 0; candidateLane < fable::SQ_WT_POLY_LANES; ++candidateLane) {
        const int candidate = fable::sqWtNoteIdx(pattern, step, candidateLane);
        if (candidate + 2 < (int)bytes.size() && (bytes[(size_t)candidate] & 1)) {
            lane = candidateLane;
            break;
        }
    }
    for (int candidateLane = 0; candidateLane < fable::SQ_WT_POLY_LANES; ++candidateLane) {
        if (candidateLane == lane) continue;
        const int candidate = fable::sqWtNoteIdx(pattern, step, candidateLane);
        if (candidate + 2 >= (int)bytes.size()) return;
        bytes[(size_t)candidate] = 1 << 2;
        bytes[(size_t)candidate + 1] = 0;
        bytes[(size_t)candidate + 2] = 1;
    }
    const auto offset = fable::sqWtNoteIdx(pattern, step, lane);
    if (offset + 2 >= (int)bytes.size()) return;
    const int duration = std::min(fable::SEQ_MAX_NOTE_STEPS, std::max(1, value.duration));
    bytes[(size_t)offset] = (uint8_t)((value.on ? 1 : 0) | (value.acc ? 2 : 0) | (duration << 2));
    bytes[(size_t)offset + 1] = (uint8_t)juce::jlimit(0, 11, value.note);
    bytes[(size_t)offset + 2] = (uint8_t)juce::jlimit(0, 2, value.oct + 1);
    proc_.conductor().updateClipBytes(scene_, track_, std::move(bytes), clip->bars);
}

void HostedWtModel::setChain(std::vector<int> sequence) {
    const auto* current = targetClip();
    if (current == nullptr || current->bars > fable::SQ_HOSTED_MAX_BARS) return;
    const int bars = juce::jlimit(1, fable::SQ_HOSTED_MAX_BARS, (int)sequence.size());
    auto bytes = fable::sqEmptyClip(fable::Machine::WT1, bars);
    std::copy_n(current->bytes.begin(), juce::jmin(current->bytes.size(), bytes.size()), bytes.begin());
    proc_.conductor().updateClipBytes(scene_, track_, std::move(bytes), bars);
    editPattern_ = juce::jmin(editPattern_, bars - 1);
    chain_.clear();
    for (int bar = 0; bar < bars; ++bar) chain_.push_back(bar);
}

const std::vector<fable::UserTable>& HostedWtModel::userTables() const {
    return noUserTables();
}

const std::vector<std::shared_ptr<const fable::GeneratedTable>>& HostedWtModel::factoryTables() const {
    return noEditableFactoryTables();
}

bool HostedWtModel::hasTargetClip() const { return targetClip() != nullptr; }

void HostedWtModel::createTargetClip() {
    if (scene_ < 0 || hasTargetClip()) return;
    proc_.conductor().createClip(scene_, track_);
    setEditPattern(0);
}

int HostedWtModel::clipBars() const {
    const auto* clip = targetClip();
    return clip != nullptr ? juce::jlimit(1, fable::SQ_HOSTED_MAX_BARS, clip->bars) : 1;
}

void HostedWtModel::auditionNoteOn(int midiNote, float velocity) {
    proc_.auditionWtOn(track_, midiNote, velocity);
}

void HostedWtModel::auditionNoteOff(int midiNote) {
    proc_.auditionWtOff(track_, midiNote);
}

} // namespace fui
