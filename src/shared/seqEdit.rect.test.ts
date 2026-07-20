import { describe, expect, it } from 'vitest';
import {
  clearRect, copyRect, moveRect, pasteRect, rectNorm,
  type RectSel, type SeqLayout,
} from './seqEdit';

const L: SeqLayout = { stride: 3, stepsPerPattern: 16, patternSize: 48 };
const EMPTY = Uint8Array.of(1 << 2, 0, 1);

// One empty pattern; set(step, note, extra?) lights a step.
const makeP = (): Uint8Array => {
  const p = new Uint8Array(48);
  for (let i = 0; i < 48; i += 3) { p[i] = 1 << 2; p[i + 2] = 1; }
  return p;
};
const lit = (p: Uint8Array, step: number, note: number, b1extra = 0): void => {
  p[step * 3] = 1 | (1 << 2); p[step * 3 + 1] = note | b1extra; p[step * 3 + 2] = 1;
};
const noteAt = (p: Uint8Array, step: number): number => p[step * 3 + 1] & 0x7f;
const onAt = (p: Uint8Array, step: number): boolean => (p[step * 3] & 1) !== 0;

describe('rectNorm', () => {
  it('normalizes reversed corners', () => {
    expect(rectNorm({ stepFrom: 7, stepTo: 2, noteFrom: 9, noteTo: 4 }))
      .toEqual({ stepLo: 2, stepHi: 7, noteLo: 4, noteHi: 9 });
  });
});

describe('copyRect / clearRect', () => {
  it('picks only lit cells inside the pitch band', () => {
    const p = makeP();
    lit(p, 2, 5); lit(p, 3, 11); lit(p, 4, 6);
    const rect: RectSel = { stepFrom: 2, stepTo: 4, noteFrom: 4, noteTo: 7 };
    const c = copyRect(p, L, 0, rect);
    expect(c.wSteps).toBe(3);
    expect(c.cells.map((x) => x.dStep)).toEqual([0, 2]); // step 3 (note 11) excluded
    const cleared = clearRect(p, L, 0, rect, EMPTY);
    expect(onAt(cleared, 2)).toBe(false);
    expect(onAt(cleared, 3)).toBe(true); // out of band → untouched
    expect(onAt(cleared, 4)).toBe(false);
  });
});

describe('pasteRect', () => {
  it('stamps with transpose, drops out-of-range, keeps bit7', () => {
    const p = makeP();
    lit(p, 0, 10, 0x80); lit(p, 1, 3);
    const c = copyRect(p, L, 0, { stepFrom: 0, stepTo: 1, noteFrom: 0, noteTo: 11 });
    const out = pasteRect(makeP(), L, 0, 14, 2, c, 11);
    expect(onAt(out, 14)).toBe(false);        // note 10+2 → out of lane range, dropped
    expect(onAt(out, 15)).toBe(true);
    expect(noteAt(out, 15)).toBe(5);          // 3 + 2
    const out2 = pasteRect(makeP(), L, 0, 15, 0, c, 11);
    expect(onAt(out2, 15)).toBe(true);
    expect(out2[15 * 3 + 1] & 0x80).toBe(0x80); // slide bit preserved
    // dStep 1 lands at step 16 → dropped, no wrap to step 0
    expect(onAt(out2, 0)).toBe(false);
  });
  it('overwrites only steps where the clipboard has a cell', () => {
    const p = makeP();
    lit(p, 5, 7);
    const src = makeP(); lit(src, 0, 2);
    const c = copyRect(src, L, 0, { stepFrom: 0, stepTo: 1, noteFrom: 0, noteTo: 11 });
    const out = pasteRect(p, L, 0, 4, 0, c, 11);
    expect(noteAt(out, 4)).toBe(2);
    expect(noteAt(out, 5)).toBe(7); // dStep 1 has no cell → untouched
  });
});

describe('moveRect', () => {
  it('moves with transpose and clears the vacated cells', () => {
    const p = makeP();
    lit(p, 2, 5);
    const out = moveRect(p, L, 0, { stepFrom: 2, stepTo: 2, noteFrom: 5, noteTo: 5 }, 3, -2, { emptyStep: EMPTY });
    expect(onAt(out, 2)).toBe(false);
    expect(onAt(out, 5)).toBe(true);
    expect(noteAt(out, 5)).toBe(3);
  });
  it('copy keeps the source; overlapping move is safe', () => {
    const p = makeP();
    lit(p, 2, 5); lit(p, 3, 6);
    const rect: RectSel = { stepFrom: 2, stepTo: 3, noteFrom: 0, noteTo: 11 };
    const copied = moveRect(p, L, 0, rect, 4, 0, { copy: true, emptyStep: EMPTY });
    expect(onAt(copied, 2)).toBe(true);
    const moved = moveRect(p, L, 0, rect, 1, 0, { emptyStep: EMPTY });
    expect(onAt(moved, 2)).toBe(false);
    expect(noteAt(moved, 3)).toBe(5);
    expect(noteAt(moved, 4)).toBe(6);
  });
});
