// Hosted clip transport — the SQ-4 two-slot state machine each engine embeds
// in hosted mode. C++ port of the worklet hostTick/clipPhase/clipFire
// (src/engine/worklet.js:478-517, contract in docs/sq4-clips.md §6):
// one playing slot, one pending slot, frame-stamped commands, phase-locked
// entry derived from the shared anchor. Header-only, JUCE-free.
#pragma once

#include "../seq/dsp/SeqModel.h"
#include "../seq/dsp/SeqProtocol.h"
#include "Arp.h"
#include "Scales.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <limits>

namespace fable {

struct HostEvent {
    enum class T { Start, Stop, Pos };
    // `tag` is the launch identity of the clip this event belongs to (the scene
    // index the conductor stamped on scheduleClip). Start/Stop acks carry it so
    // the conductor can attribute an ack to the clip that actually started at
    // the device, not to whatever was scheduled most recently (Finding 1). This
    // diverges deliberately from the web protocol, whose acks carry no identity
    // (docs/sq4-clips.md §6) — see Conductor.h's header comment.
    T t; double frame; int step = 0, bar = 0, tag = 0;
};

class ClipHost {
public:
    std::vector<HostEvent> events; // drained by the embedding engine per block

    void setTempo(double bpm, double swing, double sr, double anchor) {
        const bool anchorMoved = anchor != anchor_;
        bpm_ = bpm; swing_ = swing; sr_ = sr; anchor_ = anchor;
        // Melodic POLY (worklet polyClipArm/polyClipRetime): a moved anchor
        // re-arms the clock; a tempo/swing change only re-times the next step.
        if (!playing_ || !polyActive()) return;
        if (anchorMoved || clipStep_ < 0) polyArm(lastEnd_);
        else toNext_ = anchor_ + polyFrame(clipStep_ + 1) - lastEnd_;
    }

    // Reserve steady-state capacity so nothing here allocates on the audio
    // thread once audio is running. Call once, before the first tick (the
    // embedding engine does this from setHostClipMode(true)). maxBytes covers
    // the largest possible clip (SQ_MAX_BARS * DR1 bytes-per-bar = 4096);
    // maxEvents is the per-drain event headroom (Start/Stop plus one Pos per
    // fired step — far above the worst case, since events are drained every
    // block). After this, clip_ AND pend_ keep their capacity for the object's
    // lifetime: scheduleClip/updateClip only ever assign() within reserve
    // (assign never shrinks capacity), and the pending->playing handoff is a
    // std::swap (see tick) that trades the two buffers without freeing either.
    void prepare(int maxBytes, int maxEvents) {
        clip_.reserve((size_t)maxBytes);
        pend_.reserve((size_t)maxBytes);
        events.reserve((size_t)maxEvents);
        maxEvents_ = (size_t)maxEvents;
    }

    // Replaces any pending clip and disarms a pending stop (a re-launch
    // before the boundary re-targets; docs §6 rule 1). `tag` is the clip's
    // launch identity (scene index), carried through to the Start ack so the
    // conductor attributes it correctly (Finding 1).
    void scheduleClip(const uint8_t* d, size_t n, int bars, double at, int tag = 0, ArpPattern arp = {}, SequenceRhythm rhythm = {}, const ScaleMap& scale = {}) {
        pend_.assign(d, d + n); pendBars_ = bars; pendAt_ = at; hasPend_ = true;
        pendArp_ = arp; pendRhythm_ = rhythm; pendScale_ = scale;
        pendTag_ = tag;
        hasStop_ = false;
    }

    // Cancels a pending clip if one waits; otherwise stops the playing clip.
    // Stop still acks so a pending-only launch clears in the conductor.
    void scheduleStop(double at) { hasPend_ = false; stopAt_ = at; hasStop_ = true; }

    // Hot-swap bytes: pending slot when one exists, else the live clip.
    // Position is derived arithmetic, so the playhead never moves; a bars
    // change re-derives the entry under floor() (mid-flight resize).
    void updateClip(const uint8_t* d, size_t n, int bars, ArpPattern arp = {}, SequenceRhythm rhythm = {}, const ScaleMap& scale = {}) {
        if (hasPend_) { pend_.assign(d, d + n); pendBars_ = bars; pendArp_ = arp; pendRhythm_ = rhythm; pendScale_ = scale; return; }
        if (!playing_) return;
        const bool rephase = arp.enabled != arp_.enabled || (arp.enabled && arp.rate != arp_.rate) || rhythmRephases(rhythm);
        const bool resized = bars != clipBars_ || (arp.enabled && arp.length != arp_.length);
        arp_ = arp; rhythm_ = rhythm; scale_ = scale;
        clip_.assign(d, d + n);
        clipBars_ = bars;
        if (rephase) rephase_ = true;
        else if (resized && clipStep_ >= 0)
            clipStep_ = phaseStep(lastFrame_, totalSteps(), /*roundNearest*/ false);
    }

    // True when swapping in `r` would re-phase the live POLY clock (worklet
    // clipupdate: enabled flag, mode, cycleBeats, or steps in fit mode).
    bool rhythmRephases(const SequenceRhythm& r) const {
        return rhythm_.enabled != r.enabled
            || (r.enabled && (rhythm_.fit != r.fit || rhythm_.cycleBeats != r.cycleBeats || (r.fit && rhythm_.steps != r.steps)));
    }
    // Melodic POLY clock active for the live clip (the arp keeps its own clock).
    bool polyActive() const { return rhythm_.enabled && !arp_.enabled; }
    // Samples per POLY step — the unit written note durations scale by.
    double polyDuration() const { return stepDuration(); }
    // Source step (0..steps-1) the NEXT POLY fire will read, for tie/slide lookahead.
    int polyNextSource() const { return polySource(clipStep_ + 1); }

    void clear() { playing_ = false; hasPend_ = false; hasStop_ = false; clipStep_ = -1; }

    bool isPlaying() const { return playing_; }
    bool hasPending() const { return hasPend_; }
    int  playingBars() const { return hasPend_ ? pendBars_ : clipBars_; }
    // Bars of the currently loaded (live) clip — unlike playingBars(), never
    // shadowed by a still-pending clip; the fire callback needs this to wrap
    // its tie lookahead within the clip that's actually sounding.
    int  clipBars() const { return clipBars_; }
    const uint8_t* clipData() const { return clip_.data(); }
    const ArpPattern& arp() const { return arp_; }
    // Scale mode (Scales.h): the written note of the step being fired, moved
    // into the scale the live clip's map holds at that anchor-grid sixteenth.
    int scaleNote(int note) const { return fable::scaleNote(scale_, firePos16_, note); }
    const ScaleMap& scaleMap() const { return scale_; }
    double stepInterval(int) const { return std::max(1.0, toNext_); }
    int  clipStep() const { return clipStep_; } // last fired absolute step

    // Embedders with independent lane clocks can tick the first sample, split
    // before the next transport/grid boundary, then consume the remaining span.
    double nextBoundary(double frame) const {
        double next = playing_ ? frame + std::max(0.0, toNext_) : std::numeric_limits<double>::infinity();
        if (hasPend_) next = std::min(next, pendAt_);
        if (hasStop_) next = std::min(next, stopAt_);
        return next;
    }
    void elapse(int samples) { if (playing_) toNext_ -= samples; }

    // Test hooks: reserved buffer capacities, to assert prepare()'s
    // allocation-free steady state (no realloc across launch/update/stop).
    size_t clipCapacity()   const { return clip_.capacity(); }
    size_t pendCapacity()   const { return pend_.capacity(); }
    size_t eventsCapacity() const { return events.capacity(); }

    // One render quantum: [frame, frame+n). fire(absStep) triggers the step's
    // voices in the embedding engine. Order matters: stop, swap, fire —
    // usually one fire per call (the caller drives consecutive quanta and a
    // step rarely spans more than one block on the shared timebase), but a
    // quantum spanning more than one step's duration (large host buffers,
    // e.g. offline render) fires every step actually due, catching up any
    // backlog instead of dropping it.
    //
    // 2-arg overload: no swap hook (existing callers/tests unaffected).
    template <typename FireFn>
    void tick(double frame, int n, FireFn&& fire) {
        tick(frame, n, std::forward<FireFn>(fire), [](bool) {});
    }

    // onSwap(wasPlaying) runs at the pending->playing swap, after the new
    // clip's state (clip_/clipBars_/clipStep_) is committed but before the
    // Start event and the entry step's fire — so the embedding engine can
    // end the outgoing clip's sounding note in the same block, before the
    // new clip's first trigger (docs/sq4-clips.md §6 rule 4; worklet.js
    // hostTick: "the old clip's tail note ends where the new clip starts").
    template <typename FireFn, typename SwapFn>
    void tick(double frame, int n, FireFn&& fire, SwapFn&& onSwap) {
        lastFrame_ = frame;
        const double end = frame + n;
        if (hasStop_ && stopAt_ < end) {
            hasStop_ = false;
            playing_ = false; clipStep_ = -1;
            // Stop carries the stopped clip's tag (harmless, and lets the
            // conductor attribute the stop if it ever needs to).
            pushEvent({ HostEvent::T::Stop, frame, 0, 0, clipTag_ });
        }
        if (hasPend_ && pendAt_ < end) {
            hasPend_ = false;
            const bool wasPlaying = playing_;
            // Capacity-preserving handoff: swap (not move-assign) so both
            // clip_ and pend_ retain their reserved buffers — pend_ inherits
            // the outgoing clip's storage for the next scheduleClip's assign().
            std::swap(clip_, pend_); clipBars_ = pendBars_; clipTag_ = pendTag_;
            arp_ = pendArp_; rhythm_ = pendRhythm_; scale_ = pendScale_;
            playing_ = true;
            rephase_ = false;
            // Phase-locked entry: enter at the global grid position, -1 so
            // the immediate fire below lands ON that step (worklet clipPhase).
            clipStep_ = phaseStep(frame, totalSteps(), true) - 1;
            toNext_ = 0;
            if (polyActive()) polyArm(frame);
            onSwap(wasPlaying);
            pushEvent({ HostEvent::T::Start, frame, 0, 0, clipTag_ });
        }
        if (playing_) {
            if (rephase_) {
                clipStep_ = phaseStep(frame, totalSteps(), false) - 1;
                toNext_ = 0; rephase_ = false;
                if (polyActive()) polyArm(frame);
            }
            // A large host quantum (offline render, high bpm) can leave more
            // than one grid step already due as of this block's start.
            // fireStep reschedules anchor-absolute from the frame it's given
            // (self-correcting — see its comment — not a running countdown),
            // so a *fixed* `frame` on every iteration would recompute the
            // same due time and only ever fire once, no matter how deep the
            // backlog. Instead advance a local `due` cursor to each fired
            // step's own scheduled time and keep the original firing
            // condition (due <= frame, i.e. "already due as of this block's
            // start" — exactly what the old single-fire `if` checked) so a
            // block with no backlog fires at the identical time it always
            // did; only a genuinely overdue backlog loops to catch up,
            // instead of leaking the excess as ever-growing, never-fired
            // backlog (the original bug).
            double due = frame + toNext_;
            int guard = 0; // pathological-swing safety net; never hit in practice
            while (due <= frame && guard++ < 100000) {
                fireStep(due, fire);
                due += toNext_;
            }
            toNext_ = due - end;
        }
        lastEnd_ = end;
    }

private:
    // Append a host event. prepare() reserves maxEvents_ up front and the
    // buffer is drained every block, so a prepared host must never exceed that
    // reserve here (doing so would allocate on the audio thread) — the assert
    // is a debug-only safety net if the worst-case sizing is ever wrong.
    // maxEvents_ == 0 means unprepared (the direct-construction unit tests),
    // which grow freely.
    void pushEvent(const HostEvent& e) {
        assert(maxEvents_ == 0 || events.size() < maxEvents_);
        events.push_back(e);
    }

    double clampBpm() const { return std::max(60.0, std::min(200.0, bpm_)); }
    double clampSwing() const { return std::max(0.0, std::min(1.0, swing_)); }
    double stepDuration() const {
        if (polyActive()) return 60.0 / clampBpm() * sr_ * (rhythm_.fit ? (double)rhythm_.cycleBeats / rhythm_.steps : .25);
        return 60.0 / clampBpm() * (arp_.enabled ? arp_.rate : .25) * sr_;
    }
    int totalSteps() const { return arp_.enabled ? std::max(16, arp_.length) : clipBars_ * SQ_STEPS_PER_BAR; }

    // round: activation snaps to the boundary step; floor: mid-flight resize.
    int phaseStep(double frame, int total, bool roundNearest) const {
        if (polyActive()) return (int)polyPhase(std::max(0.0, frame - anchor_), roundNearest ? Quant::Round : Quant::Floor);
        const double dur = stepDuration();
        const double pos = std::max(0.0, frame - anchor_) / dur;
        const long idx = roundNearest ? (long)std::lround(pos) : (long)std::floor(pos);
        return (int)(((idx % total) + total) % total);
    }

    // ---- Melodic POLY clock (worklet polyDuration/polyFrame/polyPhase/polySource).
    // clipStep_ is the unwrapped POLY step count k; the source step read from
    // the clip is (k - rotation) mod steps. Grid mode swings odd k.
    enum class Quant { Floor, Ceil, Round };
    double polyFrame(long k) const {
        const double dur = stepDuration();
        return (double)k * dur + (!rhythm_.fit && (k & 1) ? clampSwing() * SQ_SWING_MAX * dur : 0.0);
    }
    long polyPhase(double elapsed, Quant q) const {
        const double dur = stepDuration();
        long k = std::max(0L, (long)std::floor(elapsed / dur));
        if (polyFrame(k) > elapsed + 1e-6) k = std::max(0L, k - 1);
        if (q == Quant::Floor) return k;
        if (q == Quant::Ceil) return polyFrame(k) >= elapsed - 1e-6 ? k : k + 1;
        return elapsed - polyFrame(k) < polyFrame(k + 1) - elapsed ? k : k + 1;
    }
    int polySource(long k) const {
        const long m = (k - rhythm_.rotation) % rhythm_.steps;
        return (int)(m < 0 ? m + rhythm_.steps : m);
    }
    void polyArm(double now) {
        const long k = polyPhase(std::max(0.0, now - anchor_), Quant::Ceil);
        clipStep_ = (int)(k - 1);
        toNext_ = anchor_ + polyFrame(k) - now;
    }
    template <typename FireFn>
    void polyFireStep(double frame, FireFn&& fire) {
        const long k = (long)clipStep_ + 1;
        const int s = polySource(k);
        clipStep_ = (int)k;
        // Worklet polyEmit: the step's sixteenth on the straight grid.
        firePos16_ = (long)std::floor((double)k * stepDuration() / (60.0 / clampBpm() / 4 * sr_) + .1);
        fire(s);
        toNext_ = anchor_ + polyFrame(k + 1) - frame;
        pushEvent({ HostEvent::T::Pos, frame, s, 0, clipTag_ });
    }

    // Advances clipStep_, fires it, and reschedules the NEXT step at its
    // absolute anchor-grid time (anchor + (idx+1)*dur, plus swing offset).
    // This must stay anchor-absolute, not a free-running countdown: a
    // countdown drops the block-quantization residue on every step and
    // drifts late without bound, desyncing devices launched at different
    // times (docs/sq4-clips.md §6 rule 2; worklet.js clipFire).
    template <typename FireFn>
    void fireStep(double frame, FireFn&& fire) {
        if (polyActive()) { polyFireStep(frame, fire); return; }
        const double dur = stepDuration();
        const double sw = clampSwing();
        const int total = totalSteps();
        const int abs = (clipStep_ + 1) % total;
        const int s = abs % SQ_STEPS_PER_BAR;
        clipStep_ = abs;

        const double offNow  = (s % 2 == 1) ? sw * SQ_SWING_MAX * dur : 0.0;
        const int sNext = (s + 1) % SQ_STEPS_PER_BAR;
        const double offNext = (sNext % 2 == 1) ? sw * SQ_SWING_MAX * dur : 0.0;
        const long idx = (long)std::lround((frame - anchor_ - offNow) / dur);
        toNext_ = anchor_ + (double)(idx + 1) * dur + offNext - frame;
        firePos16_ = idx; // the anchor-grid sixteenth of this step (worklet clipFire)
        fire(abs);
        pushEvent({ HostEvent::T::Pos, frame, s, abs / SQ_STEPS_PER_BAR, clipTag_ });
    }

    double bpm_ = 120, swing_ = 0, sr_ = 48000, anchor_ = 0;
    std::vector<uint8_t> clip_, pend_;
    ArpPattern arp_, pendArp_;
    SequenceRhythm rhythm_, pendRhythm_;
    ScaleMap scale_, pendScale_;
    long firePos16_ = 0;
    int clipBars_ = 0, pendBars_ = 0;
    int clipTag_ = 0, pendTag_ = 0; // launch identity (scene) of live/pending clip
    double pendAt_ = -1, stopAt_ = -1, toNext_ = 0, lastFrame_ = 0, lastEnd_ = 0;
    bool hasPend_ = false, hasStop_ = false, playing_ = false, rephase_ = false;
    int clipStep_ = -1;
    size_t maxEvents_ = 0; // reserved event headroom, 0 until prepare() (see pushEvent)
};

} // namespace fable
