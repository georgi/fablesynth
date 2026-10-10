// Procedural wavetable generation with per-mip band-limiting.
// C++ port of src/engine/wavetables.ts. Each table is
// FRAMES frames x SIZE samples x MIPS mip levels. Mip m keeps harmonics
// 1..(1024 >> m); the engine selects a mip per note so no partial crosses
// Nyquist (Serum-style anti-aliasing).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fable {

constexpr int SIZE   = 2048;
constexpr int FRAMES = 16;
constexpr int MIPS   = 11;  // maxHarm: 1024,512,...,1 — coarsest mip is a pure sine, so even fundamentals at the 0.45*sr pitch guard can't fold
constexpr int VIZ_N  = 128; // points per frame kept for visualization

struct GeneratedTable {
    std::string        name;
    int                frames = 0;
    int                mips   = MIPS;
    int                size   = SIZE;
    std::vector<float> data;  // frames*mips*size, frame-major then mip-major
    std::vector<float> viz;   // frames*VIZ_N (mip-0 downsample, for UI)
};

// Tables are shared (not copied) between the processor's pool and the engine:
// a table's pyramid is multiple MB, and the pool is rebuilt on every user-table
// edit / state load, so ownership passes around as pointer copies.
using TablePtr = std::shared_ptr<const GeneratedTable>;

// In-place iterative radix-2 complex FFT (re/im length must be a power of two).
void fft(double* re, double* im, int n, bool inverse);

// The six procedural tables (PRIME, BLOOM, PULSE, VOX, CHIME, GLITCH).
std::vector<GeneratedTable> generateTables();

// Process-wide shared build of generateTables(). The tables are immutable and
// sample-rate independent, but a build costs 6 x 16 x 11 iFFTs and ~8.6 MB, so
// every plugin instance must not pay it again (finding J5). Built on first use
// and never freed; thread-safe (function-local static).
const std::vector<TablePtr>& sharedFactoryTables();

// Build a band-limited table from raw single-cycle time-domain frames (each
// SIZE samples). Runs the identical band-limit / mip pipeline as the procedural
// tables, so imported/drawn tables anti-alias identically. Mirrors buildUserTable.
GeneratedTable buildUserTable(const std::string& name, const std::vector<std::vector<float>>& frames);

// ---------- band-limited square for the SUB oscillators (BL-1 and WT-1) ----------
// One MIPS x SUBSQ_STRIDE table of a square wave (odd harmonics 1/k, peak-plateau level 1,
// DC 0, positive for phase 0..0.5 like the naive square it replaces). Mip m keeps
// harmonics 1..(1024>>m), the same pyramid and mip rule as the wavetables, so the
// sub reads it with the same Hermite interpolation. The table is 2x finer than
// SIZE: the Hermite image of the top harmonics falls as (k/N)^4, and a 1/k
// spectrum has no natural roll-off to hide it. Not part of any user-visible
// table list: it does not shift table indices. Built once (thread-safe static);
// engines force the build in prepare() so the audio thread never allocates.
constexpr int SUBSQ_SIZE = 4096;
// Each mip is stored with one wrapped sample before and two after the cycle, so
// the Hermite read needs no index masking.
constexpr int SUBSQ_STRIDE = SUBSQ_SIZE + 3;
const float* subSquareTable();

struct SubSquareMip {
    int    off = 0, offFine = 0;  // coarse / one-finer mip offsets into the table
    double blend = 0;             // weight of the finer mip (mip-switch crossfade)
};

// Mip for a per-sample phase increment `inc` (same rule as the main oscillators).
inline SubSquareMip subSquareMip(double inc) {
    SubSquareMip r;
    const double mipF = std::log2((inc * 1024.0) / 0.475);
    int mip = 0;
    if (mipF > 0) {
        mip = std::min(MIPS - 1, (int)std::ceil(mipF));
        const double over = mipF - (mip - 1);
        if (over < 0.07) r.blend = 1 - over / 0.07;
    }
    r.off = mip * SUBSQ_STRIDE;
    r.offFine = (mip > 0 ? mip - 1 : 0) * SUBSQ_STRIDE;
    return r;
}

// Cubic Hermite read of one mip (`off`) at phase `ph` in [0,1).
inline double subSquareReadMip(const float* t, int off, double ph) {
    const double p = ph * SUBSQ_SIZE;
    const int idx = (int)p;
    const double f = p - idx;
    const float* q = t + off + idx;   // q[0..3] = samples idx-1 .. idx+2
    const double ym1 = q[0], y0 = q[1], y1 = q[2], y2 = q[3];
    const double c1 = 0.5 * (y1 - ym1);
    const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
    const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

// Full read: the coarse mip, crossfaded with the finer one near a mip switch.
inline double subSquareRead(const float* t, const SubSquareMip& m, double ph) {
    const double sc = subSquareReadMip(t, m.off, ph);
    if (m.blend < 0.001) return sc;
    return sc + m.blend * (subSquareReadMip(t, m.offFine, ph) - sc);
}

} // namespace fable
