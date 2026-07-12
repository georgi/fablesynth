// Hosted clip transport — the SQ-4 two-slot state machine each engine embeds
// in hosted mode. C++ port of the worklet hostTick/clipPhase/clipFire
// (src/engine/worklet.js:478-517, contract in docs/sq4-clips.md §6):
// one playing slot, one pending slot, frame-stamped commands, phase-locked
// entry derived from the shared anchor. Header-only, JUCE-free.
#pragma once

#include "../seq/dsp/SeqProtocol.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fable {

struct HostEvent {
    enum class T { Start, Stop, Pos };
    T t; double frame; int step = 0, bar = 0;
};

class ClipHost {
public:
    std::vector<HostEvent> events; // drained by the embedding engine per block

    void setTempo(double bpm, double swing, double sr, double anchor) {
        bpm_ = bpm; swing_ = swing; sr_ = sr; anchor_ = anchor;
    }

    // Replaces any pending clip and disarms a pending stop (a re-launch
    // before the boundary re-targets; docs §6 rule 1).
    void scheduleClip(const uint8_t* d, size_t n, int bars, double at) {
        pend_.assign(d, d + n); pendBars_ = bars; pendAt_ = at; hasPend_ = true;
        hasStop_ = false;
    }

    // Cancels a pending clip if one waits; otherwise stops the playing clip.
    // Stop still acks so a pending-only launch clears in the conductor.
    void scheduleStop(double at) { hasPend_ = false; stopAt_ = at; hasStop_ = true; }

    // Hot-swap bytes: pending slot when one exists, else the live clip.
    // Position is derived arithmetic, so the playhead never moves; a bars
    // change re-derives the entry under floor() (mid-flight resize).
    void updateClip(const uint8_t* d, size_t n, int bars) {
        if (hasPend_) { pend_.assign(d, d + n); pendBars_ = bars; return; }
        if (!playing_) return;
        clip_.assign(d, d + n);
        const bool resized = bars != clipBars_;
        clipBars_ = bars;
        if (resized && clipStep_ >= 0)
            clipStep_ = phaseStep(lastFrame_, bars * SQ_STEPS_PER_BAR, /*roundNearest*/ false);
    }

    void clear() { playing_ = false; hasPend_ = false; hasStop_ = false; clipStep_ = -1; }

    bool isPlaying() const { return playing_; }
    int  playingBars() const { return hasPend_ ? pendBars_ : clipBars_; }
    // Bars of the currently loaded (live) clip — unlike playingBars(), never
    // shadowed by a still-pending clip; the fire callback needs this to wrap
    // its tie lookahead within the clip that's actually sounding.
    int  clipBars() const { return clipBars_; }
    const uint8_t* clipData() const { return clip_.data(); }
    int  clipStep() const { return clipStep_; } // last fired absolute step

    // One render quantum: [frame, frame+n). fire(absStep) triggers the step's
    // voices in the embedding engine. Order matters: stop, swap, fire — and
    // (matching hostTick) at most one fire per call, since the caller drives
    // consecutive quanta and a step never spans more than one block in
    // practice on the shared timebase.
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
            events.push_back({ HostEvent::T::Stop, frame });
        }
        if (hasPend_ && pendAt_ < end) {
            hasPend_ = false;
            const bool wasPlaying = playing_;
            clip_ = std::move(pend_); clipBars_ = pendBars_;
            playing_ = true;
            // Phase-locked entry: enter at the global grid position, -1 so
            // the immediate fire below lands ON that step (worklet clipPhase).
            clipStep_ = phaseStep(frame, clipBars_ * SQ_STEPS_PER_BAR, true) - 1;
            toNext_ = 0;
            onSwap(wasPlaying);
            events.push_back({ HostEvent::T::Start, frame });
        }
        if (playing_) {
            if (toNext_ <= 0) fireStep(frame, fire);
            toNext_ -= n;
        }
    }

private:
    double clampBpm() const { return std::max(60.0, std::min(200.0, bpm_)); }
    double clampSwing() const { return std::max(0.0, std::min(1.0, swing_)); }

    // round: activation snaps to the boundary step; floor: mid-flight resize.
    int phaseStep(double frame, int total, bool roundNearest) const {
        const double dur = sqSamplesPerStep(clampBpm(), sr_);
        const double pos = std::max(0.0, frame - anchor_) / dur;
        const long idx = roundNearest ? (long)std::lround(pos) : (long)std::floor(pos);
        return (int)(((idx % total) + total) % total);
    }

    // Advances clipStep_, fires it, and reschedules the NEXT step at its
    // absolute anchor-grid time (anchor + (idx+1)*dur, plus swing offset).
    // This must stay anchor-absolute, not a free-running countdown: a
    // countdown drops the block-quantization residue on every step and
    // drifts late without bound, desyncing devices launched at different
    // times (docs/sq4-clips.md §6 rule 2; worklet.js clipFire).
    template <typename FireFn>
    void fireStep(double frame, FireFn&& fire) {
        const double dur = sqSamplesPerStep(clampBpm(), sr_);
        const double sw = clampSwing();
        const int total = clipBars_ * SQ_STEPS_PER_BAR;
        const int abs = (clipStep_ + 1) % total;
        const int s = abs % SQ_STEPS_PER_BAR;
        clipStep_ = abs;
        fire(abs);
        events.push_back({ HostEvent::T::Pos, frame, s, abs / SQ_STEPS_PER_BAR });

        const double offNow  = (s % 2 == 1) ? sw * SQ_SWING_MAX * dur : 0.0;
        const int sNext = (s + 1) % SQ_STEPS_PER_BAR;
        const double offNext = (sNext % 2 == 1) ? sw * SQ_SWING_MAX * dur : 0.0;
        const long idx = (long)std::lround((frame - anchor_ - offNow) / dur);
        toNext_ = anchor_ + (double)(idx + 1) * dur + offNext - frame;
    }

    double bpm_ = 120, swing_ = 0, sr_ = 48000, anchor_ = 0;
    std::vector<uint8_t> clip_, pend_;
    int clipBars_ = 0, pendBars_ = 0;
    double pendAt_ = -1, stopAt_ = -1, toNext_ = 0, lastFrame_ = 0;
    bool hasPend_ = false, hasStop_ = false, playing_ = false;
    int clipStep_ = -1;
};

} // namespace fable
