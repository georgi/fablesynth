// Pure sequence-editing helpers shared by WT-1, BL-1 and DR-1: range
// copy/paste/clear/shift over the packed Uint8Array pattern buffers, plus
// whole-pattern block ops and a bounded undo/redo history. Everything is
// immutable — callers get fresh Uint8Arrays and route them through their
// store's _setPatterns chokepoint.
//
// A SeqLayout describes how (pattern, step) maps into the flat buffer:
//   offset = pat * patternSize + laneOffset + step * stride
// WT-1/BL-1: stride 3, 16 steps, patternSize 48, laneOffset 0.
// DR-1: a pad's row is a lane — stride 1, 16 steps, patternSize
// PAD_COUNT*16, laneOffset padIndex*16. Whole-pattern ops always move the
// full patternSize block (all pads for DR-1), ignoring laneOffset.

export interface SeqLayout {
  stride: number; // bytes per step
  stepsPerPattern: number;
  patternSize: number; // bytes per whole pattern block
  laneOffset?: number; // byte offset of the lane within a pattern (DR-1 pad rows)
}

export type Patterns = Uint8Array;

const stepOffset = (l: SeqLayout, pat: number, step: number): number =>
  pat * l.patternSize + (l.laneOffset ?? 0) + step * l.stride;

const clampStep = (l: SeqLayout, step: number): number =>
  Math.min(l.stepsPerPattern - 1, Math.max(0, step | 0));

// Normalize an inclusive [from, to] selection to lo <= hi, clamped in-pattern.
const normRange = (l: SeqLayout, from: number, to: number): [number, number] => {
  const a = clampStep(l, from);
  const b = clampStep(l, to);
  return a <= b ? [a, b] : [b, a];
};

// Bytes for the inclusive step range [from, to] of one pattern (lane).
export function copyRange(p: Patterns, l: SeqLayout, pat: number, from: number, to: number): Uint8Array {
  const [lo, hi] = normRange(l, from, to);
  return p.slice(stepOffset(l, pat, lo), stepOffset(l, pat, hi) + l.stride);
}

// Paste range bytes starting at step `at`; steps past the pattern end are
// dropped (clamped), never wrapped. A paste whose start is already past the
// end is dropped entirely (returns an unchanged copy) — it must never be
// pulled back into the pattern. Returns a new buffer.
export function pasteRange(p: Patterns, l: SeqLayout, pat: number, at: number, data: Uint8Array): Patterns {
  const next = p.slice();
  if (at >= l.stepsPerPattern) return next;
  const start = clampStep(l, at);
  const steps = Math.min((data.length / l.stride) | 0, l.stepsPerPattern - start);
  if (steps > 0) next.set(data.subarray(0, steps * l.stride), stepOffset(l, pat, start));
  return next;
}

// Clear the inclusive range [from, to]. `emptyStep` is the byte pattern of a
// cleared step (stride bytes) — WT-1/BL-1 pass [1<<2, 0, 1] (duration 1,
// neutral octave); DR-1's cleared step is all zeros (the default).
export function clearRange(
  p: Patterns, l: SeqLayout, pat: number, from: number, to: number, emptyStep?: Uint8Array,
): Patterns {
  const next = p.slice();
  const [lo, hi] = normRange(l, from, to);
  for (let s = lo; s <= hi; s++) {
    const o = stepOffset(l, pat, s);
    for (let b = 0; b < l.stride; b++) next[o + b] = emptyStep ? emptyStep[b] : 0;
  }
  return next;
}

// Drag-move (or Alt-drag copy) of the range [from, to] to start at `dest`.
// The source bytes are captured first so overlapping moves are safe; on a
// move the vacated source steps are cleared with `emptyStep`.
export function shiftRange(
  p: Patterns, l: SeqLayout, pat: number, from: number, to: number, dest: number,
  opts: { copy?: boolean; emptyStep?: Uint8Array } = {},
): Patterns {
  const [lo, hi] = normRange(l, from, to);
  const data = copyRange(p, l, pat, lo, hi);
  const next = opts.copy ? p.slice() : clearRange(p, l, pat, lo, hi, opts.emptyStep);
  return pasteRange(next, l, pat, dest, data);
}

// Whole-pattern block ops — the full patternSize bytes (all pads for DR-1).
export function copyPattern(p: Patterns, l: SeqLayout, pat: number): Uint8Array {
  return p.slice(pat * l.patternSize, (pat + 1) * l.patternSize);
}

export function pastePattern(p: Patterns, l: SeqLayout, pat: number, data: Uint8Array): Patterns {
  const next = p.slice();
  next.set(data.subarray(0, l.patternSize), pat * l.patternSize);
  return next;
}

// ---------- history ----------
// Bounded snapshot stack for undo/redo. Verbs call push(before) with the
// pre-mutation state; undo(current)/redo(current) return the state to restore
// (or null) and shuffle `current` onto the opposite stack. Snapshots are
// whatever the caller stores (Uint8Array patterns, session docs, …) — the
// helper never mutates them.

export interface History<T> {
  push: (snapshot: T) => void;
  undo: (current: T) => T | null;
  redo: (current: T) => T | null;
  canUndo: () => boolean;
  canRedo: () => boolean;
  clear: () => void;
}

export function makeHistory<T>(limit = 50): History<T> {
  let past: T[] = [];
  let future: T[] = [];
  return {
    push: (snapshot) => {
      past.push(snapshot);
      if (past.length > limit) past = past.slice(past.length - limit);
      future = [];
    },
    undo: (current) => {
      const prev = past.pop();
      if (prev === undefined) return null;
      future.push(current);
      return prev;
    },
    redo: (current) => {
      const nxt = future.pop();
      if (nxt === undefined) return null;
      past.push(current);
      return nxt;
    },
    canUndo: () => past.length > 0,
    canRedo: () => future.length > 0,
    clear: () => { past = []; future = []; },
  };
}
