#pragma once
#include <algorithm>
#include <cmath>

namespace fable {

// Chain-level activity gate shared by the Fx, BassFx and DrumFx chains (the
// DR-1 Finding D8 pattern). Once the input, the output and the tail (echo and
// the reverb tank, measured before the wet mix) have all sat below kIdleLevel
// for kIdleHold plus a buffer horizon, the owner skips its whole chain and
// freezes its state. The criterion is measured level, so tails never truncate.
struct ActivityGate {
    static constexpr float  kIdleLevel = 1.0e-5f; // -100 dBFS
    static constexpr double kIdleHold  = 0.25;

    bool   idle = false;
    double silent = 0;  // samples since input, output and tail were last audible
    double skipped = 0; // samples skipped while idle, for the free-running clocks

    void reset(bool startIdle = false) { idle = startIdle; silent = skipped = 0; }

    // A non-finite sample counts as loud, so it wakes the chain.
    static bool loud(const float* L, const float* R, int n) {
        for (int i = 0; i < n; i++)
            if (!(std::abs(L[i]) <= kIdleLevel) || !(std::abs(R[i]) <= kIdleLevel)) return true;
        return false;
    }

    // Call first in process(). True: the block is silent and the chain idle, so
    // return now. False: process the block. On a wake, wokeAfter is the span of
    // samples that were skipped, else 0.
    bool begin(bool inLoud, int n, double& wokeAfter) {
        wokeAfter = 0;
        if (!idle) return false;
        if (!inLoud) { skipped += n; return true; }
        wokeAfter = skipped;
        idle = false;
        silent = skipped = 0;
        return false;
    }

    // Call last in process(). horizon is the extra time in seconds (longest
    // buffer) that must stay silent before the chain may idle.
    void end(bool inLoud, float outPk, float tailPk, int n, double horizon, double sr) {
        if (!inLoud && outPk <= kIdleLevel && tailPk <= kIdleLevel) silent += n;
        else silent = 0;
        if (silent >= (kIdleHold + horizon) * sr) idle = true;
    }
};

} // namespace fable
