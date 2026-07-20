import { describe, expect, it } from 'vitest';
import {
  clearPadRect, copyPadRect, movePadRect, padRectNorm, pastePadRect,
  type PadRectSel, type SeqLayout,
} from './seqEdit';

// DR-1 shape: stride 1, 16 steps, 4 pads for a compact test grid.
const PADS = 4;
const L: SeqLayout = { stride: 1, stepsPerPattern: 16, patternSize: PADS * 16 };

const makeP = (): Uint8Array => new Uint8Array(PADS * 16);
const at = (pad: number, step: number): number => pad * 16 + step;
const set = (p: Uint8Array, pad: number, step: number, v: number): void => { p[at(pad, step)] = v; };
const get = (p: Uint8Array, pad: number, step: number): number => p[at(pad, step)];

describe('padRectNorm', () => {
  it('normalizes reversed corners', () => {
    expect(padRectNorm({ stepFrom: 7, stepTo: 2, padFrom: 3, padTo: 1 }))
      .toEqual({ stepLo: 2, stepHi: 7, padLo: 1, padHi: 3 });
  });
});

describe('copyPadRect / clearPadRect', () => {
  it('captures only lit cells inside the pad band, positionally', () => {
    const p = makeP();
    set(p, 1, 2, 1); set(p, 2, 3, 2); set(p, 3, 3, 1); // pad 3 is out of band
    const rect: PadRectSel = { stepFrom: 2, stepTo: 4, padFrom: 1, padTo: 2 };
    const c = copyPadRect(p, L, 0, rect);
    expect(c.wSteps).toBe(3);
    expect(c.wPads).toBe(2);
    expect(c.cells.map((x) => [x.dPad, x.dStep, x.bytes[0]])).toEqual([[0, 0, 1], [1, 1, 2]]);
    const cleared = clearPadRect(p, L, 0, rect);
    expect(get(cleared, 1, 2)).toBe(0);
    expect(get(cleared, 2, 3)).toBe(0);
    expect(get(cleared, 3, 3)).toBe(1); // out of band → untouched
  });
});

describe('pastePadRect', () => {
  it('stamps at the top-left, dropping out-of-range cells', () => {
    const p = makeP();
    set(p, 0, 0, 1); set(p, 1, 1, 2);
    const c = copyPadRect(p, L, 0, { stepFrom: 0, stepTo: 1, padFrom: 0, padTo: 1 });
    // Anchor at step 15 / pad 3: dStep 1 → step 16 (dropped), dPad 1 → pad 4 (dropped).
    const out = pastePadRect(makeP(), L, 0, 15, 3, c, PADS);
    expect(get(out, 3, 15)).toBe(1);   // the top-left cell lands
    expect(get(out, 0, 0)).toBe(0);    // no wrap of the overhanging cell
  });

  it('overwrites only cells the clipboard carries', () => {
    const p = makeP();
    set(p, 2, 5, 2); // existing hit the paste must not disturb
    const src = makeP(); set(src, 0, 0, 1);
    const c = copyPadRect(src, L, 0, { stepFrom: 0, stepTo: 1, padFrom: 0, padTo: 0 });
    const out = pastePadRect(p, L, 0, 4, 2, c, PADS);
    expect(get(out, 2, 4)).toBe(1);
    expect(get(out, 2, 5)).toBe(2); // dStep 1 has no cell → untouched
  });
});

describe('movePadRect', () => {
  it('moves positionally in both axes and clears the vacated cells', () => {
    const p = makeP();
    set(p, 1, 2, 1);
    const out = movePadRect(p, L, 0, { stepFrom: 2, stepTo: 2, padFrom: 1, padTo: 1 }, 3, 1, PADS);
    expect(get(out, 1, 2)).toBe(0);
    expect(get(out, 2, 5)).toBe(1); // pad 1+1, step 2+3
  });

  it('copy keeps the source; overlapping move is safe', () => {
    const p = makeP();
    set(p, 0, 2, 1); set(p, 0, 3, 2);
    const rect: PadRectSel = { stepFrom: 2, stepTo: 3, padFrom: 0, padTo: 0 };
    const copied = movePadRect(p, L, 0, rect, 4, 0, PADS, { copy: true });
    expect(get(copied, 0, 2)).toBe(1);
    expect(get(copied, 0, 6)).toBe(1);
    const moved = movePadRect(p, L, 0, rect, 1, 0, PADS);
    expect(get(moved, 0, 2)).toBe(0);
    expect(get(moved, 0, 3)).toBe(1);
    expect(get(moved, 0, 4)).toBe(2);
  });
});
