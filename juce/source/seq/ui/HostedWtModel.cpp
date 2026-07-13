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
    chain_.assign(1, 0);
}

ParameterSource HostedWtModel::parameters() { return parameters_.source(); }

DeviceUiCapabilities HostedWtModel::capabilities() const {
    return {
        true,  // hosted
        false, // ownsTransport
        false, // supportsPatternChain
        false, // supportsUserTables
        true   // supportsAudition
    };
}

void HostedWtModel::timerCallback() { flushPendingPatch(); }

void HostedWtModel::flushPendingPatch() {
    if (parameters_.consumeDirty())
        proc_.setTrackInlineParams(track_, parameters_.snapshot());
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
    parameters_.load(proc_.trackParameterValues(track_));
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
    chain_.assign(1, editPattern_);
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
    const auto offset = fable::sqNoteIdx(pattern, step);
    if (offset + 2 >= (int)clip->bytes.size()) return {};
    return fable::getNoteSeqStep(clip->bytes.data(), pattern, step);
}

void HostedWtModel::setSequenceStep(int pattern, int step, const fable::NoteSeqStep& value) {
    const auto* clip = targetClip();
    if (clip == nullptr || clip->bars > fable::SQ_HOSTED_MAX_BARS
        || pattern < 0 || pattern >= clip->bars
        || step < 0 || step >= fable::SEQ_STEPS)
        return;
    auto bytes = clip->bytes;
    const auto offset = fable::sqNoteIdx(pattern, step);
    if (offset + 2 >= (int)bytes.size()) return;
    fable::setNoteSeqStep(bytes.data(), pattern, step, value);
    proc_.conductor().updateClipBytes(scene_, track_, std::move(bytes), clip->bars);
}

void HostedWtModel::setChain(std::vector<int>) {
    // Hosted bars are selected directly; an SQ clip owns its playback order.
    chain_.assign(1, editPattern_);
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
