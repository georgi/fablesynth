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
