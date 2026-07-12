#include "Conductor.h"
#include <algorithm>

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

void Conductor::launch(int t, int s) {
    auto& sc = session_.scenes[(size_t)s];
    if (!sc.hasClip[(size_t)t]) return;
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
}

void Conductor::setTrackVol(int t, float v) {
    trackVol_[(size_t)t] = v;
    applyGains();
}

// Swing is safe to change live: it only shifts intra-step offsets, never the
// anchor math (docs §3/§6).
void Conductor::setSwing(double v) {
    swing_ = v;
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
    return sqSongPosition(io_.now(), anchor_, session_.bpm, sr_);
}

} // namespace fable
