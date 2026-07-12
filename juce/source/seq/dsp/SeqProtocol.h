// SQ-4 protocol constants and shared-timebase math — C++ port of
// src/seq/protocol.ts (docs/sq4-clips.md §3-§5). Header-only, JUCE-free.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace fable {

enum class Machine { DR1, BL1, WT1 };
// Cycle order matches the web QUANTS = ['1 BAR', '1/4', 'OFF'].
enum class Quant { Bar, Quarter, Off };

constexpr int SQ_STEPS_PER_BAR = 16;
constexpr int SQ_DR1_PADS      = 16;
constexpr int SQ_NOTE_STRIDE   = 3;   // BL1/WT1: flags, note, oct+1 per step
constexpr int SQ_MAX_BARS      = 16;
constexpr int SQ_HOSTED_MAX_BARS = 4; // hosted editor cap (= device NPATTERNS)
constexpr int SQ_STOP          = -1;  // queue sentinel: "stop the track"

inline int sqBytesPerBar(Machine m) {
    return m == Machine::DR1 ? SQ_DR1_PADS * SQ_STEPS_PER_BAR
                             : SQ_STEPS_PER_BAR * SQ_NOTE_STRIDE;
}
inline int sqDr1Idx(int bar, int pad, int step) {
    return (bar * SQ_DR1_PADS + pad) * SQ_STEPS_PER_BAR + step;
}
inline int sqNoteIdx(int bar, int step) {
    return (bar * SQ_STEPS_PER_BAR + step) * SQ_NOTE_STRIDE;
}

// A silent clip payload; note machines get the neutral oct byte (=1).
inline std::vector<uint8_t> sqEmptyClip(Machine m, int bars) {
    std::vector<uint8_t> out((size_t)(bars * sqBytesPerBar(m)), 0);
    if (m != Machine::DR1)
        for (size_t i = 2; i < out.size(); i += SQ_NOTE_STRIDE) out[i] = 1;
    return out;
}

inline double sqSamplesPerBeat(double bpm, double sr) { return sr * 60.0 / bpm; }
inline double sqSamplesPerStep(double bpm, double sr) { return sqSamplesPerBeat(bpm, sr) / 4.0; }
inline double sqBarFrames(double bpm, double sr)      { return sqSamplesPerBeat(bpm, sr) * 4.0; }

// Next quantize boundary at-or-after `now`, in context frames. Quant OFF
// returns 0 — devices treat atFrame <= currentFrame as "this block".
inline double sqBoundaryFrame(Quant q, double now, double anchor, double bpm, double sr) {
    if (q == Quant::Off) return 0.0;
    const double step = q == Quant::Bar ? sqBarFrames(bpm, sr) : sqSamplesPerBeat(bpm, sr);
    const double delta = std::max(0.0, now - anchor);
    return anchor + std::ceil(delta / step) * step;
}

struct SqSongPos { int beat = 0; int bar = 1; };

inline SqSongPos sqSongPosition(double now, double anchor, double bpm, double sr) {
    const double beats = std::max(0.0, now - anchor) / sqSamplesPerBeat(bpm, sr);
    return { (int)std::floor(beats) % 4, (int)std::floor(beats / 4.0) + 1 };
}

inline double sqBarSeconds(double bpm) { return 240.0 / bpm; }

} // namespace fable
