// Pure sequence-editing helpers shared by WT-1, BL-1 and DR-1 step editors —
// C++ port of src/shared/seqEdit.ts. Range copy/paste/clear/shift over the
// packed pattern byte buffers, plus whole-pattern block ops and a bounded
// undo/redo history template. Everything is immutable-by-convention: every
// op returns a *new* buffer and never mutates its input, so callers route
// the result through their model's single mutation chokepoint (mirrors the
// web's _setPatterns pattern).
//
// A StepLayout describes how (pattern, step) maps into the flat buffer:
//   offset = pat * patternSize + laneOffset + step * stride
// WT-1/BL-1: stride 3, 16 steps, patternSize 48, laneOffset 0.
// DR-1: a pad's row is a lane — stride 1, 16 steps, patternSize
// PAD_COUNT*16, laneOffset padIndex*16. Whole-pattern ops always move the
// full patternSize block (all pads for DR-1), ignoring laneOffset.
//
// Header-only and JUCE-free on purpose: shared by every step-sequencer view
// model and the headless test harness.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace fable {

struct StepLayout {
    int stride = 1;          // bytes per step
    int stepsPerPattern = 16;
    int patternSize = 16;    // bytes per whole pattern block
    int laneOffset = 0;      // byte offset of the lane within a pattern (DR-1 pad rows)
};

using StepBytes = std::vector<uint8_t>;

inline int stepOffset(const StepLayout& l, int pat, int step) {
    return pat * l.patternSize + l.laneOffset + step * l.stride;
}

inline int clampStep(const StepLayout& l, int step) {
    return std::min(l.stepsPerPattern - 1, std::max(0, step));
}

// Normalize an inclusive [from, to] selection to lo <= hi, clamped in-pattern.
inline void normRange(const StepLayout& l, int from, int to, int& lo, int& hi) {
    const int a = clampStep(l, from);
    const int b = clampStep(l, to);
    if (a <= b) { lo = a; hi = b; } else { lo = b; hi = a; }
}

// Bytes for the inclusive step range [from, to] of one pattern (lane).
inline StepBytes copyRange(const StepBytes& p, const StepLayout& l, int pat, int from, int to) {
    int lo, hi;
    normRange(l, from, to, lo, hi);
    const int begin = stepOffset(l, pat, lo);
    const int end = stepOffset(l, pat, hi) + l.stride;
    return StepBytes(p.begin() + begin, p.begin() + end);
}

// Paste range bytes starting at step `at`; steps past the pattern end are
// dropped (clamped), never wrapped. A paste whose start is already past the
// end is dropped entirely (returns an unchanged copy) — it must never be
// pulled back into the pattern. Returns a new buffer.
inline StepBytes pasteRange(const StepBytes& p, const StepLayout& l, int pat, int at, const StepBytes& data) {
    StepBytes next = p;
    if (at >= l.stepsPerPattern) return next;
    const int start = clampStep(l, at);
    const int steps = std::min((int)(data.size() / (size_t)l.stride), l.stepsPerPattern - start);
    if (steps > 0) {
        const int dst = stepOffset(l, pat, start);
        std::copy(data.begin(), data.begin() + (ptrdiff_t)(steps * l.stride), next.begin() + dst);
    }
    return next;
}

// Clear the inclusive range [from, to]. `emptyStep` is the byte pattern of a
// cleared step (stride bytes); pass an empty vector for all-zero clears
// (DR-1's default). WT-1/BL-1 pass {1<<2, 0, 1} (duration 1, neutral octave).
inline StepBytes clearRange(const StepBytes& p, const StepLayout& l, int pat, int from, int to,
                             const StepBytes& emptyStep = {}) {
    StepBytes next = p;
    int lo, hi;
    normRange(l, from, to, lo, hi);
    for (int s = lo; s <= hi; s++) {
        const int o = stepOffset(l, pat, s);
        for (int b = 0; b < l.stride; b++)
            next[(size_t)(o + b)] = emptyStep.empty() ? (uint8_t)0 : emptyStep[(size_t)b];
    }
    return next;
}

struct ShiftOpts {
    bool copy = false;
    StepBytes emptyStep {};
};

// Drag-move (or Alt-drag copy) of the range [from, to] to start at `dest`.
// The source bytes are captured first so overlapping moves are safe; on a
// move the vacated source steps are cleared with `emptyStep`.
inline StepBytes shiftRange(const StepBytes& p, const StepLayout& l, int pat, int from, int to, int dest,
                             const ShiftOpts& opts = {}) {
    int lo, hi;
    normRange(l, from, to, lo, hi);
    const StepBytes data = copyRange(p, l, pat, lo, hi);
    StepBytes next = opts.copy ? p : clearRange(p, l, pat, lo, hi, opts.emptyStep);
    return pasteRange(next, l, pat, dest, data);
}

// The DR-1 RAND button: sparse hits with occasional accents on one lane only
// (web parity: randomizePadPattern in src/drum/seq.ts — same density/flavor
// as BL-1's randomPattern, scoped to a single lane since a drum pad has no
// per-step pitch to randomize). Rewrites every step byte of the lane; other
// lanes/patterns are untouched. `rng` returns [0,1) and is injected so tests
// are deterministic; the call order (one draw per step, a second only when
// the step lands on) matches the web function exactly.
template <typename Rng>
inline StepBytes randomizeLane(const StepBytes& p, const StepLayout& l, int pat, Rng&& rng) {
    StepBytes next = p;
    for (int s = 0; s < l.stepsPerPattern; s++) {
        const bool on = rng() < 0.4;
        const bool accent = on && rng() < 0.3;
        next[(size_t)stepOffset(l, pat, s)] = (uint8_t)(on ? (accent ? 2 : 1) : 0);
    }
    return next;
}

// ---------- rectangle (step × note-lane) selection ----------
// C++ port of src/shared/seqEdit.ts's rect verbs. WT-1/BL-1 note grids only:
// stride-3 steps where byte0 bit0 = on and byte1 bits0..6 = note lane
// (0..11; BL-1 keeps its slide flag in bit7, which rides along untouched).
// Verbs touch only lit cells whose lane falls inside the rect's pitch band;
// paste/move DROPS cells that land outside the pattern or the lane range —
// never wraps or clamps.

// A step×note rectangle. Steps are absolute timeline steps for the chain-aware
// verbs below (0 .. chain.size()*stepsPerPattern-1) and in-pattern steps for
// the single-pattern verbs; either endpoint may exceed the other.
struct RectSel {
    int stepFrom = 0, stepTo = 0, noteFrom = 0, noteTo = 0;
};

struct RectNorm { int stepLo, stepHi, noteLo, noteHi; };

inline RectNorm rectNorm(const RectSel& r) {
    return { std::min(r.stepFrom, r.stepTo), std::max(r.stepFrom, r.stepTo),
             std::min(r.noteFrom, r.noteTo), std::max(r.noteFrom, r.noteTo) };
}

// One captured cell: its stride bytes plus its step offset from the rect's
// left edge (dStep), so paste can re-anchor the block anywhere.
struct RectCell { int dStep = 0; StepBytes bytes; };

struct RectCells {
    int wSteps = 0;  // width of the source rect in steps
    int noteLo = 0;  // normalized source pitch band
    int noteHi = 0;
    std::vector<RectCell> cells;
};

inline bool rectCellOn(const StepBytes& p, int o) { return (p[(size_t)o] & 1) != 0; }
inline int rectCellNote(const StepBytes& p, int o) { return p[(size_t)(o + 1)] & 0x7f; }

inline RectCells copyRect(const StepBytes& p, const StepLayout& l, int pat, const RectSel& rect) {
    const auto n = rectNorm(rect);
    RectCells out; out.wSteps = n.stepHi - n.stepLo + 1; out.noteLo = n.noteLo; out.noteHi = n.noteHi;
    for (int s = n.stepLo; s <= n.stepHi; ++s) {
        const int o = stepOffset(l, pat, s);
        const int note = rectCellNote(p, o);
        if (rectCellOn(p, o) && note >= n.noteLo && note <= n.noteHi)
            out.cells.push_back({ s - n.stepLo, StepBytes(p.begin() + o, p.begin() + o + l.stride) });
    }
    return out;
}

inline StepBytes clearRect(const StepBytes& p, const StepLayout& l, int pat, const RectSel& rect,
                           const StepBytes& emptyStep = {}) {
    StepBytes next = p;
    const auto n = rectNorm(rect);
    for (int s = n.stepLo; s <= n.stepHi; ++s) {
        const int o = stepOffset(l, pat, s);
        const int note = rectCellNote(next, o);
        if (rectCellOn(next, o) && note >= n.noteLo && note <= n.noteHi)
            for (int b = 0; b < l.stride; ++b)
                next[(size_t)(o + b)] = emptyStep.empty() ? (uint8_t)0 : emptyStep[(size_t)b];
    }
    return next;
}

// Paste captured cells with the rect's left edge at `atStep` and its notes
// transposed by `dNote`. Cells that fall outside the pattern or the [0,maxNote]
// lane range are dropped, never wrapped/clamped. The slide flag (byte1 bit7)
// is preserved on the destination.
inline StepBytes pasteRect(const StepBytes& p, const StepLayout& l, int pat, int atStep, int dNote,
                           const RectCells& data, int maxNote = 11) {
    StepBytes next = p;
    for (const auto& c : data.cells) {
        const int s = atStep + c.dStep;
        if (s < 0 || s >= l.stepsPerPattern) continue;
        const int note = (c.bytes[1] & 0x7f) + dNote;
        if (note < 0 || note > maxNote) continue;
        const int o = stepOffset(l, pat, s);
        for (int b = 0; b < l.stride; ++b) next[(size_t)(o + b)] = c.bytes[(size_t)b];
        next[(size_t)(o + 1)] = (uint8_t)((c.bytes[1] & 0x80) | note);
    }
    return next;
}

struct RectMoveOpts { bool copy = false; StepBytes emptyStep {}; int maxNote = 11; };

inline StepBytes moveRect(const StepBytes& p, const StepLayout& l, int pat, const RectSel& rect,
                          int dStep, int dNote, const RectMoveOpts& opts = {}) {
    const auto n = rectNorm(rect);
    const RectCells data = copyRect(p, l, pat, rect);
    StepBytes base = opts.copy ? p : clearRect(p, l, pat, rect, opts.emptyStep);
    return pasteRect(base, l, pat, n.stepLo + dStep, dNote, data, opts.maxNote);
}

// ---------- chain-aware rect ops (absolute timeline steps) ----------
// The same four verbs over the *visible timeline*: rect steps are absolute
// (0 .. chain.size()*stepsPerPattern-1) and each bar b maps to pattern
// chain[b], so a selection may span bar boundaries. Cells landing past the
// timeline end are dropped, never wrapped. Returns -1 for an out-of-range step.
inline int chainOffset(const StepLayout& l, const std::vector<int>& chain, int absStep) {
    if (absStep < 0) return -1;
    const int bar = absStep / l.stepsPerPattern;
    if (bar >= (int)chain.size()) return -1;
    return stepOffset(l, chain[(size_t)bar], absStep % l.stepsPerPattern);
}

inline RectCells copyRectChain(const StepBytes& p, const StepLayout& l, const std::vector<int>& chain,
                               const RectSel& rect) {
    const auto n = rectNorm(rect);
    RectCells out; out.wSteps = n.stepHi - n.stepLo + 1; out.noteLo = n.noteLo; out.noteHi = n.noteHi;
    for (int s = n.stepLo; s <= n.stepHi; ++s) {
        const int o = chainOffset(l, chain, s);
        if (o < 0) continue;
        const int note = rectCellNote(p, o);
        if (rectCellOn(p, o) && note >= n.noteLo && note <= n.noteHi)
            out.cells.push_back({ s - n.stepLo, StepBytes(p.begin() + o, p.begin() + o + l.stride) });
    }
    return out;
}

inline StepBytes clearRectChain(const StepBytes& p, const StepLayout& l, const std::vector<int>& chain,
                                const RectSel& rect, const StepBytes& emptyStep = {}) {
    StepBytes next = p;
    const auto n = rectNorm(rect);
    for (int s = n.stepLo; s <= n.stepHi; ++s) {
        const int o = chainOffset(l, chain, s);
        if (o < 0) continue;
        const int note = rectCellNote(next, o);
        if (rectCellOn(next, o) && note >= n.noteLo && note <= n.noteHi)
            for (int b = 0; b < l.stride; ++b)
                next[(size_t)(o + b)] = emptyStep.empty() ? (uint8_t)0 : emptyStep[(size_t)b];
    }
    return next;
}

inline StepBytes pasteRectChain(const StepBytes& p, const StepLayout& l, const std::vector<int>& chain,
                                int atStep, int dNote, const RectCells& data, int maxNote = 11) {
    StepBytes next = p;
    for (const auto& c : data.cells) {
        const int o = chainOffset(l, chain, atStep + c.dStep);
        if (o < 0) continue;
        const int note = (c.bytes[1] & 0x7f) + dNote;
        if (note < 0 || note > maxNote) continue;
        for (int b = 0; b < l.stride; ++b) next[(size_t)(o + b)] = c.bytes[(size_t)b];
        next[(size_t)(o + 1)] = (uint8_t)((c.bytes[1] & 0x80) | note);
    }
    return next;
}

inline StepBytes moveRectChain(const StepBytes& p, const StepLayout& l, const std::vector<int>& chain,
                               const RectSel& rect, int dStep, int dNote, const RectMoveOpts& opts = {}) {
    const auto n = rectNorm(rect);
    const RectCells data = copyRectChain(p, l, chain, rect);
    StepBytes base = opts.copy ? p : clearRectChain(p, l, chain, rect, opts.emptyStep);
    return pasteRectChain(base, l, chain, n.stepLo + dStep, dNote, data, opts.maxNote);
}

// Whole-pattern block ops — the full patternSize bytes (all pads for DR-1).
inline StepBytes copyPattern(const StepBytes& p, const StepLayout& l, int pat) {
    const int begin = pat * l.patternSize;
    return StepBytes(p.begin() + begin, p.begin() + begin + l.patternSize);
}

inline StepBytes pastePattern(const StepBytes& p, const StepLayout& l, int pat, const StepBytes& data) {
    StepBytes next = p;
    std::copy(data.begin(), data.begin() + l.patternSize, next.begin() + pat * l.patternSize);
    return next;
}

// ---------- history ----------
// Bounded snapshot stack for undo/redo. Verbs call push(before) with the
// pre-mutation state; undo(current)/redo(current) return the state to
// restore (or nothing) and shuffle `current` onto the opposite stack.
// Snapshots are whatever the caller stores (pattern bytes, session JSON, …)
// — the template never mutates them. One entry per gesture (coalesce
// continuous drags into a single push); clear() on clip/pattern-source swap.
template <typename T>
class StepEditHistory {
public:
    explicit StepEditHistory(int limit = 50) : limit_(limit) {}

    void push(T snapshot) {
        past_.push_back(std::move(snapshot));
        if ((int)past_.size() > limit_)
            past_.erase(past_.begin(), past_.begin() + ((int)past_.size() - limit_));
        future_.clear();
    }

    // Returns true and writes the restored state into `outRestored` if an
    // undo was available; `current` is the state being undone away from
    // (pushed onto the redo stack).
    bool undo(const T& current, T& outRestored) {
        if (past_.empty()) return false;
        outRestored = std::move(past_.back());
        past_.pop_back();
        future_.push_back(current);
        return true;
    }

    bool redo(const T& current, T& outRestored) {
        if (future_.empty()) return false;
        outRestored = std::move(future_.back());
        future_.pop_back();
        past_.push_back(current);
        return true;
    }

    bool canUndo() const { return !past_.empty(); }
    bool canRedo() const { return !future_.empty(); }
    void clear() { past_.clear(); future_.clear(); }

private:
    int limit_;
    std::vector<T> past_;
    std::vector<T> future_;
};

} // namespace fable
