#include "Conductor.h"
#include <algorithm>
#include <cstdint>

namespace fable {

namespace {
constexpr int kNone = -2;
} // namespace

Conductor::Conductor(SessionData session, ConductorIO& io, double sampleRate)
    : session_(std::move(session)), io_(io), sr_(sampleRate) {
    quant_ = session_.quant;
    swing_ = session_.swing;
    sceneMute_.assign(session_.scenes.size(), false);
    trackMute_.assign(session_.tracks.size(), false);
    solo_.assign(session_.tracks.size(), false);
    trackVol_.reserve(session_.tracks.size());
    for (auto& t : session_.tracks) trackVol_.push_back(t.gain);
}

double Conductor::boundary() const {
    return sqBoundaryFrame(quant_, io_.now(), anchor_, session_.bpm, sr_);
}

void Conductor::applyGains() {
    for (int t = 0; t < (int)session_.tracks.size(); t++) {
        const bool open = isTrackOpen(t, owner_, trackMute_, sceneMute_, solo_);
        io_.ioSetTrackGain(t, open ? gainCurve(trackVol_[(size_t)t]) : 0.0f);
    }
}

void Conductor::powerOn() {
    anchor_ = io_.now() + 256;
    io_.ioSendTempo(session_.bpm, swing_, anchor_);
    applyGains();
}

void Conductor::startTransport() {
    if (playing_) return;
    anchor_ = io_.now() + 256;
    io_.ioSendTempo(session_.bpm, swing_, anchor_);
    playing_ = true;
}

void Conductor::stopTransport() {
    if (!playing_) return;
    for (int t = 0; t < (int)session_.tracks.size(); ++t) {
        if (!owner_.count(t) && !queue_.count(t)) continue;
        io_.ioScheduleStop(t, 0.0);
        queue_[t] = SQ_STOP;
    }
    playing_ = false;
}

void Conductor::launch(int t, int s) {
    auto& sc = session_.scenes[(size_t)s];
    if (!sc.hasClip[(size_t)t]) return;
    startTransport();
    // Stamp the scene as the clip's launch identity: it rides the command to the
    // device and comes back on the Start ack, so onClipStart can attribute the
    // ack to this scene rather than whatever was scheduled last (Finding 1).
    io_.ioScheduleClip(t, sc.clips[(size_t)t].bytes, sc.clips[(size_t)t].bars, boundary(), s);
    queue_[t] = s;
}

void Conductor::stopTrack(int t) {
    if (!owner_.count(t) && !queue_.count(t)) return;
    io_.ioScheduleStop(t, boundary());
    queue_[t] = SQ_STOP;
}

// Empty cells are stop buttons (Ableton semantics): launching a scene stops
// uncovered tracks unless the cell is marked pass-through.
void Conductor::launchScene(int s) {
    startTransport();
    auto& sc = session_.scenes[(size_t)s];
    for (int t = 0; t < (int)sc.clips.size(); t++) {
        if (sc.hasClip[(size_t)t]) {
            launch(t, s);
        } else {
            bool pass = false;
            for (int p : sc.pass) if (p == t) { pass = true; break; }
            if (!pass) stopTrack(t);
        }
    }
}

void Conductor::stopScene(int s) {
    for (int t = 0; t < (int)session_.tracks.size(); t++) {
        auto oit = owner_.find(t);
        auto qit = queue_.find(t);
        if ((oit != owner_.end() && oit->second == s) || (qit != queue_.end() && qit->second == s))
            stopTrack(t);
    }
}

void Conductor::stopAll() {
    for (int t = 0; t < (int)session_.tracks.size(); t++) stopTrack(t);
}

void Conductor::togglePassThrough(int s, int t) {
    auto& pass = session_.scenes[(size_t)s].pass;
    auto it = std::find(pass.begin(), pass.end(), t);
    if (it != pass.end()) pass.erase(it);
    else pass.push_back(t);
}

void Conductor::updateClipBytes(int s, int t, std::vector<uint8_t> bytes, int bars) {
    auto& sc = session_.scenes[(size_t)s];
    if (!sc.hasClip[(size_t)t]) return;
    sc.clips[(size_t)t].bars = bars;
    sc.clips[(size_t)t].bytes = bytes;
    // The worklet applies clipupdate to its pending clip when one exists,
    // else the live clip — send only when the edited scene is that target.
    auto qit = queue_.find(t);
    const int q = qit != queue_.end() ? qit->second : kNone;
    const int owner = ownerOf(t);
    const int target = (q != kNone && q != SQ_STOP) ? q : owner;
    if (target == s) io_.ioUpdateClip(t, bytes, bars);
}

void Conductor::createClip(int s, int t) {
    auto& sc = session_.scenes[(size_t)s];
    if (sc.hasClip[(size_t)t]) return;
    auto bytes = sqEmptyClip(session_.tracks[(size_t)t].machine, 1);
    sc.clips[(size_t)t] = ClipData{"NEW", 1, bytes};
    sc.hasClip[(size_t)t] = true;
}

bool Conductor::loadLibraryClip(int s, int t, const ClipLibraryEntry& entry,
                                int transposeSemitones) {
    if (s < 0 || s >= (int)session_.scenes.size()
        || t < 0 || t >= (int)session_.tracks.size()
        || !validateClipLibraryEntry(entry).empty()
        || entry.machine != session_.tracks[(size_t)t].machine)
        return false;

    std::vector<uint8_t> bytes = entry.bytes;
    if (transposeSemitones != 0) {
        if (!entry.transpose || entry.machine == Machine::DR1) return false;
        const int lanes = entry.machine == Machine::WT1 ? SQ_WT_POLY_LANES : 1;
        for (int bar = 0; bar < entry.bars; ++bar) {
            for (int step = 0; step < SQ_STEPS_PER_BAR; ++step) for (int lane = 0; lane < lanes; ++lane) {
                const int o = entry.machine == Machine::WT1 ? sqWtNoteIdx(bar, step, lane) : sqNoteIdx(bar, step);
                if ((bytes[(size_t)o] & 1u) == 0) continue;
                const uint8_t slide = bytes[(size_t)o + 1] & 0x80;
                int64_t shifted = (int)(bytes[(size_t)o + 1] & 0x7f)
                                + 12 * ((int)bytes[(size_t)o + 2] - 1)
                                + (int64_t)transposeSemitones;
                // The wire format spans three octaves. Fold by octaves (not
                // clamp) so pitch class survives even at either boundary,
                // matching the web library transform.
                if (shifted > 23) shifted -= ((shifted - 23 + 11) / 12) * 12;
                if (shifted < -12) shifted += ((-12 - shifted + 11) / 12) * 12;
                // C++ division truncates toward zero, so offset to a
                // non-negative 0..35 representation before splitting it.
                const int encoded = (int)shifted + 12;
                bytes[(size_t)o + 1] = (uint8_t)(slide | (encoded % 12));
                bytes[(size_t)o + 2] = (uint8_t)(encoded / 12);
            }
        }
    }

    auto& scene = session_.scenes[(size_t)s];
    scene.clips[(size_t)t] = ClipData { entry.name, entry.bars, bytes };
    scene.hasClip[(size_t)t] = true;

    // Same write target as focused edits: a pending clip wins over the
    // outgoing live owner because ClipHost has only one pending slot.
    const auto qit = queue_.find(t);
    const int q = qit != queue_.end() ? qit->second : kNone;
    const int target = (q != kNone && q != SQ_STOP) ? q : ownerOf(t);
    if (target == s) io_.ioUpdateClip(t, bytes, entry.bars);
    return true;
}

bool Conductor::deleteClip(int s, int t) {
    if (s < 0 || s >= (int)session_.scenes.size()
        || t < 0 || t >= (int)session_.tracks.size())
        return false;
    auto& sc = session_.scenes[(size_t)s];
    if (!sc.hasClip[(size_t)t]) return false;
    // Stop-and-clear when the track owns or queues this cell: the clip is
    // gone, so stop immediately (not launch-quantized) like stopTransport's
    // per-track stops, and let the Stop ack clear the owner.
    if (ownerOf(t) == s || queueOf(t) == s) {
        io_.ioScheduleStop(t, 0.0);
        queue_[t] = SQ_STOP;
    }
    sc.clips[(size_t)t] = ClipData{};
    sc.hasClip[(size_t)t] = false;
    return true;
}

bool Conductor::pasteClip(int s, int t, const ClipData& clip) {
    if (s < 0 || s >= (int)session_.scenes.size()
        || t < 0 || t >= (int)session_.tracks.size())
        return false;
    // Machine-compat gate, same shape as loadLibraryClip/validateSession:
    // pattern bytes are machine-specific, so a payload whose byte count does
    // not match this track's machine is rejected (no partial corruption).
    if (!(clip.bars >= 1 && clip.bars <= SQ_MAX_BARS)) return false;
    if ((int)clip.bytes.size() != clip.bars * sqBytesPerBar(session_.tracks[(size_t)t].machine))
        return false;

    auto& scene = session_.scenes[(size_t)s];
    scene.clips[(size_t)t] = clip;
    scene.hasClip[(size_t)t] = true;

    // Same write target as loadLibraryClip: a pending clip wins over the
    // outgoing live owner because ClipHost has only one pending slot.
    const auto qit = queue_.find(t);
    const int q = qit != queue_.end() ? qit->second : kNone;
    const int target = (q != kNone && q != SQ_STOP) ? q : ownerOf(t);
    if (target == s) io_.ioUpdateClip(t, clip.bytes, clip.bars);
    return true;
}

bool Conductor::moveClip(int fromS, int fromT, int toS, int toT, bool copy) {
    if (fromS < 0 || fromS >= (int)session_.scenes.size()
        || fromT < 0 || fromT >= (int)session_.tracks.size()
        || toS < 0 || toS >= (int)session_.scenes.size()
        || toT < 0 || toT >= (int)session_.tracks.size())
        return false;
    if (fromS == toS && fromT == toT) return false;
    if (!session_.scenes[(size_t)fromS].hasClip[(size_t)fromT]) return false;
    if (session_.tracks[(size_t)fromT].machine != session_.tracks[(size_t)toT].machine)
        return false;
    const ClipData clip = session_.scenes[(size_t)fromS].clips[(size_t)fromT];
    if (!copy) deleteClip(fromS, fromT);
    return pasteClip(toS, toT, clip);
}

void Conductor::setTrackPatch(int t, PatchRef patch) {
    session_.tracks[(size_t)t].patch = std::move(patch);
}

void Conductor::toggleSceneMute(int s) {
    sceneMute_[(size_t)s] = !sceneMute_[(size_t)s];
    applyGains();
}

void Conductor::toggleTrackMute(int t) {
    trackMute_[(size_t)t] = !trackMute_[(size_t)t];
    applyGains();
}

void Conductor::toggleSolo(int t) {
    solo_[(size_t)t] = !solo_[(size_t)t];
    applyGains();
}

void Conductor::cycleQuant(int d) {
    static const Quant kOrder[3] = {Quant::Bar, Quant::Quarter, Quant::Off};
    int ix = 0;
    for (int i = 0; i < 3; i++) if (kOrder[i] == quant_) { ix = i; break; }
    ix = ((ix + d) % 3 + 3) % 3;
    quant_ = kOrder[ix];
    session_.quant = quant_;
}

void Conductor::setTrackVol(int t, float v) {
    trackVol_[(size_t)t] = v;
    session_.tracks[(size_t)t].gain = v;
    applyGains();
}

// Swing is safe to change live: it only shifts intra-step offsets, never the
// anchor math (docs §3/§6).
void Conductor::setSwing(double v) {
    swing_ = v;
    session_.swing = v;
    io_.ioSendTempo(session_.bpm, swing_, anchor_);
}

void Conductor::setBpm(double bpm) {
    if (!owner_.empty() || !queue_.empty()) return;
    session_.bpm = bpm;
    anchor_ = io_.now() + 256;
    io_.ioSendTempo(session_.bpm, swing_, anchor_);
}

void Conductor::onClipStart(int t, int scene) {
    // The device-delivered `scene` is the clip that actually started; make it
    // the owner. Clear the queue only when it still names this same scene — if a
    // newer clip was queued while this ack was in flight, that later launch must
    // stay pending (its own Start ack will promote it). This is the divergence
    // from the web's identity-free acks documented in the header.
    // A transport stop may overtake an in-flight Start acknowledgement on the
    // message thread. The immediate Stop command is already queued for the
    // device, so do not briefly resurrect ownership while transport is down.
    if (!playing_) return;
    owner_[t] = scene;
    if (queue_.count(t) && queue_[t] == scene) queue_.erase(t);
    applyGains();
}

void Conductor::onClipStop(int t) {
    owner_.erase(t);
    if (queue_.count(t) && queue_[t] == SQ_STOP) queue_.erase(t);
}

int Conductor::ownerOf(int t) const {
    auto it = owner_.find(t);
    return it != owner_.end() ? it->second : kNone;
}

int Conductor::queueOf(int t) const {
    auto it = queue_.find(t);
    return it != queue_.end() ? it->second : kNone;
}

bool Conductor::sceneMuted(int s) const { return sceneMute_[(size_t)s]; }
bool Conductor::trackMuted(int t) const { return trackMute_[(size_t)t]; }
bool Conductor::soloed(int t) const { return solo_[(size_t)t]; }
float Conductor::trackVol(int t) const { return trackVol_[(size_t)t]; }

SqSongPos Conductor::songPos() const {
    if (!playing_) return {};
    return sqSongPosition(io_.now(), anchor_, session_.bpm, sr_);
}

} // namespace fable
