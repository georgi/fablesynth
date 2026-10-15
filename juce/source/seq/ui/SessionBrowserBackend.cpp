#include "SessionBrowserBackend.h"

namespace fui {

SessionBrowserBackend::SessionBrowserBackend(SeqAudioProcessor& p, int track, Hooks hooks)
    : proc_(p), track_(track), hooks_(std::move(hooks)) {}

float SessionBrowserBackend::value(int id) const {
    // The flat parameter values of the session patch, recomputed only when the patch changes.
    const auto revision = proc_.trackPatchRevision(track_);
    if (revision != cachedRevision_ || cached_.empty()) {
        cached_ = proc_.patchParamValues(track_, proc_.trackPatch(track_));
        cachedRevision_ = revision;
    }
    return id >= 0 && id < (int)cached_.size() ? cached_[(std::size_t)id] : 0.0f;
}

void SessionBrowserBackend::load(int item) {
    if (hooks_.flush) hooks_.flush();
    proc_.setTrackFactoryPatch(track_, item);
    if (hooks_.reload) hooks_.reload();
}

std::unique_ptr<BrowserSnapshot> SessionBrowserBackend::capture() const {
    if (hooks_.flush) hooks_.flush();
    auto s = std::make_unique<Snap>();
    s->patch = proc_.trackPatch(track_);
    s->index = s->patch.index;
    s->dirty = !s->patch.factory;
    return s;
}

void SessionBrowserBackend::restore(const BrowserSnapshot& snap) {
    if (hooks_.flush) hooks_.flush();
    proc_.setTrackPatchRef(track_, static_cast<const Snap&>(snap).patch);
    if (hooks_.reload) hooks_.reload();
}

void SessionBrowserBackend::applyVariant(int, const std::vector<float>& values) {
    if (hooks_.flush) hooks_.flush();
    fable::PatchRef patch;
    patch.factory = false;
    patch.index = proc_.trackPatch(track_).index;   // keep the current base
    for (const auto& d : proc_.trackParamCatalog(track_))
        if (d.id >= 0 && d.id < (int)values.size()) patch.params[d.pid] = values[(std::size_t)d.id];
    proc_.setTrackPatchRef(track_, std::move(patch));
    if (hooks_.reload) hooks_.reload();
}

void SessionBrowserBackend::noteOn(int id, float velocity) {
    if (track_ == 0) proc_.auditionDrum(id, velocity);
    else if (track_ == 1) proc_.auditionBassOn(id, velocity);
    else proc_.auditionWtOn(track_, id, velocity);
}

void SessionBrowserBackend::noteOff(int id) {
    if (track_ == 1) proc_.auditionBassOff(id);
    else if (track_ >= 2) proc_.auditionWtOff(track_, id);
}

juce::String SessionBrowserBackend::blockedHint(const juce::String& noun) const {
    return "Stop SQ-4 to hear " + (noun == "pad" ? juce::String("pad patches") : noun == "patch" ? juce::String("patches") : noun + "s") + ".";
}

} // namespace fui
