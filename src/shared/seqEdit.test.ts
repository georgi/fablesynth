import { describe, it, expect } from 'vitest';
import {
  copyRange, pasteRange, clearRange, shiftRange, copyPattern, pastePattern,
  makeHistory, type SeqLayout,
} from './seqEdit';
import {
  makeEmptyPatterns as makeNotePatterns, setStep, getStep,
  STEPS, STEP_STRIDE, NPATTERNS,
} from '../noteseq';
import { makeEmptyPatterns as makeDrumPatterns, patIdx } from '../drum/seq';
import { PAD_COUNT } from '../drum/params';

// WT-1/BL-1 twins: 3 bytes/step, 16 steps, 4 patterns.
const NOTE_LAYOUT: SeqLayout = {
  stride: STEP_STRIDE,
  stepsPerPattern: STEPS,
  patternSize: STEPS * STEP_STRIDE,
};
// A cleared WT-1/BL-1 step: duration 1, neutral octave (mirrors makeEmptyPatterns).
const NOTE_EMPTY_STEP = Uint8Array.of(1 << 2, 0, 1);

// DR-1: a pad's row is a lane; whole patterns span all pads.
const drumLane = (padI: number): SeqLayout => ({
  stride: 1,
  stepsPerPattern: 16,
  patternSize: PAD_COUNT * 16,
  laneOffset: padI * 16,
});

describe('copyRange / pasteRange (WT-1/BL-1 layout)', () => {
  it('round-trips a range between patterns without touching the source', () => {
    let p = makeNotePatterns();
    p = setStep(p, 0, 4, { on: true, note: 7, acc: true, duration: 3 });
    p = setStep(p, 0, 5, { on: true, note: 2, oct: 1 });
    const data = copyRange(p, NOTE_LAYOUT, 0, 4, 5);
    expect(data.length).toBe(2 * STEP_STRIDE);
    const next = pasteRange(p, NOTE_LAYOUT, 1, 10, data);
    expect(next).not.toBe(p);
    expect(getStep(next, 1, 10)).toMatchObject({ on: true, note: 7, acc: true, duration: 3 });
    expect(getStep(next, 1, 11)).toMatchObject({ on: true, note: 2, oct: 1 });
    // source pattern untouched, and the input buffer never mutated
    expect(getStep(next, 0, 4)).toMatchObject({ on: true, note: 7 });
    expect(getStep(p, 1, 10).on).toBe(false);
  });

  it('copyRange normalizes a reversed range and clamps out-of-bounds steps', () => {
    let p = makeNotePatterns();
    p = setStep(p, 0, 15, { on: true, note: 9 });
    const reversed = copyRange(p, NOTE_LAYOUT, 0, 15, 12);
    expect(reversed.length).toBe(4 * STEP_STRIDE);
    const clamped = copyRange(p, NOTE_LAYOUT, 0, 14, 99);
    expect(clamped.length).toBe(2 * STEP_STRIDE);
    expect(copyRange(p, NOTE_LAYOUT, 0, -5, 0).length).toBe(1 * STEP_STRIDE);
  });

  it('pasteRange clamps at the pattern end (drops overflow, never wraps)', () => {
    let p = makeNotePatterns();
    p = setStep(p, 0, 0, { on: true, note: 1 });
    p = setStep(p, 0, 1, { on: true, note: 2 });
    p = setStep(p, 0, 2, { on: true, note: 3 });
    const data = copyRange(p, NOTE_LAYOUT, 0, 0, 2);
    const next = pasteRange(p, NOTE_LAYOUT, 1, 14, data);
    expect(getStep(next, 1, 14).note).toBe(1);
    expect(getStep(next, 1, 15).note).toBe(2);
    // third step dropped — pattern 2 starts clean
    expect(getStep(next, 2, 0).on).toBe(false);
  });

  it('pasteRange clamps a negative anchor and drops an out-of-range paste entirely', () => {
    let p = makeNotePatterns();
    p = setStep(p, 0, 3, { on: true, note: 5 });
    const data = copyRange(p, NOTE_LAYOUT, 0, 3, 3);
    expect(getStep(pasteRange(p, NOTE_LAYOUT, 1, -4, data), 1, 0).note).toBe(5);
    // Anchor past the pattern end: nothing is written — the paste must never
    // be pulled back onto the last step.
    const dropped = pasteRange(p, NOTE_LAYOUT, 1, 16, data);
    expect(dropped).not.toBe(p);
    expect(Array.from(dropped)).toEqual(Array.from(p));
    expect(Array.from(pasteRange(p, NOTE_LAYOUT, 1, 99, data))).toEqual(Array.from(p));
  });
});

describe('clearRange', () => {
  it('restores the empty-step template and leaves neighbors alone', () => {
    let p = makeNotePatterns();
    p = setStep(p, 0, 2, { on: true, note: 4, acc: true, duration: 5, oct: 1 });
    p = setStep(p, 0, 3, { on: true, note: 6 });
    p = setStep(p, 0, 4, { on: true, note: 8 });
    const next = clearRange(p, NOTE_LAYOUT, 0, 2, 3, NOTE_EMPTY_STEP);
    expect(next).not.toBe(p);
    expect(getStep(next, 0, 2)).toMatchObject({ on: false, acc: false, note: 0, oct: 0, duration: 1 });
    expect(getStep(next, 0, 3).on).toBe(false);
    expect(getStep(next, 0, 4)).toMatchObject({ on: true, note: 8 });
    expect(getStep(p, 0, 2).on).toBe(true); // input untouched
  });

  it('defaults to zeroed bytes (DR-1 lane)', () => {
    const p = makeDrumPatterns();
    p[patIdx(0, 3, 5)] = 2;
    p[patIdx(0, 3, 6)] = 1;
    const next = clearRange(p, drumLane(3), 0, 5, 6);
    expect(next[patIdx(0, 3, 5)]).toBe(0);
    expect(next[patIdx(0, 3, 6)]).toBe(0);
    expect(p[patIdx(0, 3, 5)]).toBe(2);
  });
});

describe('shiftRange', () => {
  it('moves a range and clears the vacated source with the template', () => {
    let p = makeNotePatterns();
    p = setStep(p, 0, 0, { on: true, note: 3 });
    p = setStep(p, 0, 1, { on: true, note: 4 });
    const next = shiftRange(p, NOTE_LAYOUT, 0, 0, 1, 8, { emptyStep: NOTE_EMPTY_STEP });
    expect(getStep(next, 0, 8).note).toBe(3);
    expect(getStep(next, 0, 9).note).toBe(4);
    expect(getStep(next, 0, 0)).toMatchObject({ on: false, duration: 1, oct: 0 });
    expect(getStep(next, 0, 1).on).toBe(false);
  });

  it('copy keeps the source; overlapping move keeps the captured bytes', () => {
    let p = makeNotePatterns();
    p = setStep(p, 0, 0, { on: true, note: 3 });
    const copied = shiftRange(p, NOTE_LAYOUT, 0, 0, 0, 4, { copy: true });
    expect(getStep(copied, 0, 0).on).toBe(true);
    expect(getStep(copied, 0, 4).note).toBe(3);

    let q = makeNotePatterns();
    q = setStep(q, 0, 2, { on: true, note: 1 });
    q = setStep(q, 0, 3, { on: true, note: 2 });
    const moved = shiftRange(q, NOTE_LAYOUT, 0, 2, 3, 3, { emptyStep: NOTE_EMPTY_STEP });
    expect(getStep(moved, 0, 3).note).toBe(1);
    expect(getStep(moved, 0, 4).note).toBe(2);
    expect(getStep(moved, 0, 2).on).toBe(false);
  });

  it('drops steps shifted past the pattern end', () => {
    let p = makeNotePatterns();
    p = setStep(p, 0, 0, { on: true, note: 1 });
    p = setStep(p, 0, 1, { on: true, note: 2 });
    const next = shiftRange(p, NOTE_LAYOUT, 0, 0, 1, 15, { emptyStep: NOTE_EMPTY_STEP });
    expect(getStep(next, 0, 15).note).toBe(1);
    expect(getStep(next, 1, 0).on).toBe(false); // no bleed into pattern 2
  });
});

describe('DR-1 lane layout', () => {
  it('range ops stay inside the pad row', () => {
    const p = makeDrumPatterns();
    p[patIdx(0, 5, 0)] = 1;
    p[patIdx(0, 5, 1)] = 2;
    p[patIdx(0, 6, 0)] = 2; // neighbor pad, must not travel
    const data = copyRange(p, drumLane(5), 0, 0, 1);
    expect(Array.from(data)).toEqual([1, 2]);
    const next = pasteRange(makeDrumPatterns(), drumLane(2), 1, 14, data);
    expect(next[patIdx(1, 2, 14)]).toBe(1);
    expect(next[patIdx(1, 2, 15)]).toBe(2);
    expect(next[patIdx(1, 3, 0)]).toBe(0); // clamped, no bleed into next pad
  });

  it('shiftRange moves hits along the pad row', () => {
    const p = makeDrumPatterns();
    p[patIdx(0, 0, 0)] = 2;
    const next = shiftRange(p, drumLane(0), 0, 0, 0, 8);
    expect(next[patIdx(0, 0, 0)]).toBe(0);
    expect(next[patIdx(0, 0, 8)]).toBe(2);
  });
});

describe('copyPattern / pastePattern', () => {
  it('WT-1/BL-1: whole-bar copy between patterns', () => {
    let p = makeNotePatterns();
    p = setStep(p, 2, 7, { on: true, note: 11, acc: true });
    const data = copyPattern(p, NOTE_LAYOUT, 2);
    expect(data.length).toBe(NOTE_LAYOUT.patternSize);
    const next = pastePattern(p, NOTE_LAYOUT, 0, data);
    expect(next).not.toBe(p);
    expect(getStep(next, 0, 7)).toMatchObject({ on: true, note: 11, acc: true });
    expect(getStep(next, 2, 7).on).toBe(true); // source kept
    expect(getStep(p, 0, 7).on).toBe(false); // input untouched
  });

  it('DR-1: whole-pattern block includes all pads (laneOffset ignored)', () => {
    const p = makeDrumPatterns();
    p[patIdx(1, 0, 0)] = 1;
    p[patIdx(1, 9, 15)] = 2;
    const data = copyPattern(p, drumLane(4), 1);
    expect(data.length).toBe(PAD_COUNT * 16);
    const next = pastePattern(p, drumLane(4), 3, data);
    expect(next[patIdx(3, 0, 0)]).toBe(1);
    expect(next[patIdx(3, 9, 15)]).toBe(2);
    expect(next.length).toBe(NPATTERNS * PAD_COUNT * 16);
  });
});

describe('makeHistory', () => {
  it('undo/redo shuffle snapshots and report availability', () => {
    const h = makeHistory<number>();
    expect(h.canUndo()).toBe(false);
    expect(h.undo(0)).toBeNull();
    h.push(1); // state before mutating 1 → 2
    h.push(2); // before 2 → 3
    expect(h.canUndo()).toBe(true);
    expect(h.undo(3)).toBe(2);
    expect(h.undo(2)).toBe(1);
    expect(h.canUndo()).toBe(false);
    expect(h.canRedo()).toBe(true);
    expect(h.redo(1)).toBe(2);
    expect(h.redo(2)).toBe(3);
    expect(h.redo(3)).toBeNull();
  });

  it('a fresh push clears the redo stack', () => {
    const h = makeHistory<number>();
    h.push(1);
    expect(h.undo(2)).toBe(1);
    h.push(1); // new edit branch
    expect(h.canRedo()).toBe(false);
    expect(h.redo(5)).toBeNull();
  });

  it('is bounded: oldest snapshots fall off past the limit', () => {
    const h = makeHistory<number>(3);
    for (let i = 0; i < 10; i++) h.push(i);
    expect(h.undo(10)).toBe(9);
    expect(h.undo(9)).toBe(8);
    expect(h.undo(8)).toBe(7);
    expect(h.undo(7)).toBeNull();
  });

  it('clear empties both stacks', () => {
    const h = makeHistory<number>();
    h.push(1);
    h.undo(2);
    h.clear();
    expect(h.canUndo()).toBe(false);
    expect(h.canRedo()).toBe(false);
  });
});
