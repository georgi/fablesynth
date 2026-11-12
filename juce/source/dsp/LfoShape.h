// Drawn LFO shape — the value of the shape stored in the `lfoN.p*` params.
// Shared by the engine and the LFO display. Mirrors lfoShapeValue in
// src/engine/ott-worklet.js and evalLane in src/seq/clipAutomation.ts.
#pragma once

#include <cmath>

namespace fable {

// Value in -1..+1 at phase `ph` (0..1). The shape is `n` points from `p`:
// x (cycle position), y (0..1) and c per point, sorted by x. c is the bend of
// the segment that leaves the point (-1..1; c > 0 starts slow, c < 0 starts
// fast); above 1.5 the point holds its y (a step). The last point runs on to
// the first one, a cycle later. An empty shape rests at 0.
template <typename T>
inline double lfoShapeValue(const T* p, int n, double ph) {
    if (n < 1) return 0;
    int i = -1;
    for (int k = 0; k < n; k++) { if ((double)p[3 * k] <= ph) i = k; else break; }
    double x0;
    if (i < 0) { i = n - 1; x0 = (double)p[3 * i] - 1; } else x0 = (double)p[3 * i];
    const double ya = (double)p[3 * i + 1], c = (double)p[3 * i + 2];
    if (c > 1.5) return 2 * ya - 1;
    const bool wraps = i == n - 1;
    const int j = wraps ? 0 : i + 1;
    const double x1 = wraps && ph >= (double)p[3 * i] ? (double)p[3 * j] + 1 : (double)p[3 * j];
    const double yb = (double)p[3 * j + 1];
    if (!(x1 - x0 > 1e-9)) return 2 * yb - 1;
    double f = (ph - x0) / (x1 - x0);
    if (std::abs(c) >= 1e-3) {
        const double k = (c < -1 ? -1 : c > 1 ? 1 : c) * 6;
        f = (std::exp(k * f) - 1) / (std::exp(k) - 1);
    }
    return 2 * (ya + (yb - ya) * f) - 1;
}

} // namespace fable
