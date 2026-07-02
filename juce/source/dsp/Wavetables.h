// Procedural wavetable generation with per-mip band-limiting.
// C++ port of src/engine/wavetables.ts. Each table is
// FRAMES frames x SIZE samples x MIPS mip levels. Mip m keeps harmonics
// 1..(1024 >> m); the engine selects a mip per note so no partial crosses
// Nyquist (Serum-style anti-aliasing).
#pragma once

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

// Build a band-limited table from raw single-cycle time-domain frames (each
// SIZE samples). Runs the identical band-limit / mip pipeline as the procedural
// tables, so imported/drawn tables anti-alias identically. Mirrors buildUserTable.
GeneratedTable buildUserTable(const std::string& name, const std::vector<std::vector<float>>& frames);

} // namespace fable
