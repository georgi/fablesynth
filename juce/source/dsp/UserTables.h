// User wavetables — audio import, single-cycle draw, and the band-limited
// pyramid build. C++ port of the analysis half of src/engine/usertables.ts.
//
// A user table is described by its *source* single-cycle frames (frames x SIZE
// samples). The 11-level band-limited mip pyramid the engine plays is rebuilt
// from those frames with buildUserTable (see Wavetables.h) — so only the raw
// frames need persisting, and they always anti-alias with the current code.
//
// This file is deliberately JUCE-independent: the analysis is plain C++ so it
// can be unit-tested headlessly, exactly like the rest of the DSP core.
#pragma once

#include "Wavetables.h"
#include <string>
#include <vector>

namespace fable {

constexpr int MAX_FRAMES = 64; // cap on imported frame count (memory + UI)

struct UserTable {
    std::string        name;
    int                frames = 0;
    std::vector<float> wave;   // length frames*SIZE — source single-cycle frames
    GeneratedTable     table;  // rebuilt band-limited pyramid (runtime only)
};

// Build a runtime UserTable from a flat list of single-cycle frames.
UserTable makeUserTable(const std::string& name, const std::vector<std::vector<float>>& frames);

// Rebuild a UserTable from a flat frame-major wave buffer (length frames*SIZE),
// e.g. when restoring from persisted plugin state.
UserTable userTableFromWave(const std::string& name, int frames, const std::vector<float>& wave);

// Pull a GeneratedTable's source single-cycle frames (mip-0, full-band) back
// out as SIZE-sample frames — used to duplicate a factory table into an
// editable user table that re-band-limits identically via makeUserTable.
std::vector<std::vector<float>> framesFromGenerated(const GeneratedTable& t);

// ---------- audio analysis ----------
// Average all channels to mono. channels[c] points to n samples.
std::vector<float> mixToMono(const float* const* channels, int numChannels, int n);

// Estimate the fundamental cycle length (in samples) by autocorrelation.
double detectCycleLength(const std::vector<float>& x, double sampleRate);

// Slice x into consecutive segments of cycleLen samples, each resampled to SIZE.
std::vector<std::vector<float>> sliceToFrames(const std::vector<float>& x, double cycleLen);

// Single-cycle import: the whole clip is one cycle -> one frame.
std::vector<std::vector<float>> singleCycleFrame(const std::vector<float>& x);

// Resample drawn y-values (any length, one cycle, range ~[-1,1]) to one frame.
std::vector<float> frameFromDrawing(const std::vector<float>& points);

} // namespace fable
