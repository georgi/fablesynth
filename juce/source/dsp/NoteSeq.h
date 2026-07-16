// WT-1 note-sequencer data model — C++ port of src/noteseq.ts.
// A step is { on, note (0..11 within the lane octave), oct (-1/0/+1), acc,
// duration }; patterns are packed 3 bytes per step so all 4 patterns travel
// as one byte blob — byte-identical to the web build, so persisted state and
// SQ-4 clip bytes stay interchangeable across TypeScript and C++.
//
// Header-only and JUCE-free on purpose: shared by the engine, the plugin
// processor and the headless test harness.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace fable {

constexpr int SEQ_STEPS       = 16;
constexpr int SEQ_NPATTERNS   = 4;
constexpr int SEQ_NOTE_LANES  = 12;   // one octave of lanes, C at the bottom
constexpr int SEQ_OCT_MIN     = -1, SEQ_OCT_MAX = 1;
// byte 0: bit0 on, bit1 acc, bits2..7 duration (1..63 steps) · byte 1: note 0..11 · byte 2: oct+1
constexpr int SEQ_STEP_STRIDE = 3;
constexpr int SEQ_PATTERN_BYTES = SEQ_NPATTERNS * SEQ_STEPS * SEQ_STEP_STRIDE;

constexpr float  SEQ_ACCENT_VEL = 1.0f, SEQ_PLAIN_VEL = 0.72f;
constexpr int    SEQ_MAX_NOTE_STEPS = 63;   // matches noteseq.ts MAX_NOTE_STEPS
// Swing: odd 16ths are delayed by swing * SEQ_SWING_MAX of a step (1.0 -> triplet feel).
constexpr double SEQ_SWING_MAX  = 0.667;

// One unpacked step (noteseq.ts SeqStep) — the UI-facing shape. A step's
// gate is `duration` 16th-steps; the web worklet's seqFire/clipFire use that
// directly for note-off timing (no separate gate/tie flag).
struct NoteSeqStep {
    bool on = false;
    int  note = 0;      // 0..11
    int  oct = 0;       // -1 | 0 | 1
    bool acc = false;
    int  duration = 1;  // 1..63 (16th-note steps)
};

inline int seqStepOff(int pat, int step) {
    return (pat * SEQ_STEPS + step) * SEQ_STEP_STRIDE;
}

// Empty patterns read back as neutral rests: bit2..7 = duration 1 (flags byte
// 1<<2) and oct byte 1 (= oct 0), matching noteseq.ts makeEmptyPatterns.
inline std::vector<uint8_t> makeEmptySeqPatterns() {
    std::vector<uint8_t> p(SEQ_PATTERN_BYTES, 0);
    for (int i = 0; i < (int)p.size(); i += SEQ_STEP_STRIDE) {
        p[(size_t)i]     = 1 << 2;   // duration 1
        p[(size_t)i + 2] = 1;        // oct 0
    }
    return p;
}

inline NoteSeqStep getNoteSeqStep(const uint8_t* p, int pat, int step) {
    const int o = seqStepOff(pat, step);
    const uint8_t flags = p[o];
    NoteSeqStep s;
    s.on   = (flags & 1) != 0;
    s.note = std::min(SEQ_NOTE_LANES - 1, (int)p[o + 1]);
    s.oct  = std::min(SEQ_OCT_MAX, std::max(SEQ_OCT_MIN, (int)p[o + 2] - 1));
    s.acc  = (flags & 2) != 0;
    s.duration = std::max(1, std::min(SEQ_MAX_NOTE_STEPS, (int)((flags >> 2) & 0x3f)));
    return s;
}

inline void setNoteSeqStep(uint8_t* p, int pat, int step, const NoteSeqStep& s) {
    const int o = seqStepOff(pat, step);
    const int duration = std::min(SEQ_MAX_NOTE_STEPS, std::max(1, s.duration));
    p[o]     = (uint8_t)((s.on ? 1 : 0) | (s.acc ? 2 : 0) | (duration << 2));
    p[o + 1] = (uint8_t)std::min(SEQ_NOTE_LANES - 1, std::max(0, s.note));
    p[o + 2] = (uint8_t)(std::min(SEQ_OCT_MAX, std::max(SEQ_OCT_MIN, s.oct)) + 1);
}

// Semitone offset from the root for a step (lane note + octave switch).
inline int seqStepSemi(const NoteSeqStep& s) { return s.note + 12 * s.oct; }

} // namespace fable
