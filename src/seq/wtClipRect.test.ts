import { describe, expect, it } from 'vitest';
import { emptyClipBytes, wtNoteIdx } from './protocol';
import { clearRectWt, copyRectWt, moveRectWt, pasteRectWt } from './wtClipRect';

// Light voice `lane` of absolute step `s` with `note`.
const lit = (bytes: Uint8Array, s: number, note: number, lane = 0): void => {
  const o = wtNoteIdx(Math.floor(s / 16), s % 16, lane);
  bytes[o] = 1 | (1 << 2); bytes[o + 1] = note; bytes[o + 2] = 1;
};
const voice = (bytes: Uint8Array, s: number, lane: number) => {
  const o = wtNoteIdx(Math.floor(s / 16), s % 16, lane);
  return { on: (bytes[o] & 1) !== 0, note: bytes[o + 1] & 0x7f };
};

describe('wtClipRect (hosted WT-1 poly rect verbs)', () => {
  it('copy/clear touch every voice of a chord inside the band, none outside', () => {
    const b = emptyClipBytes('WT1', 1);
    lit(b, 2, 5, 0); lit(b, 2, 7, 1); lit(b, 2, 11, 2); // chord, note 11 out of band
    const rect = { stepFrom: 2, stepTo: 3, noteFrom: 4, noteTo: 8 };
    const c = copyRectWt(b, rect);
    expect(c.cells.map((x) => x.bytes[1])).toEqual([5, 7]);
    const cleared = clearRectWt(b, rect);
    expect(voice(cleared, 2, 0).on).toBe(false);
    expect(voice(cleared, 2, 1).on).toBe(false);
    expect(voice(cleared, 2, 2)).toEqual({ on: true, note: 11 }); // out of band survives
  });

  it('paste stacks chord voices into free slots, transposed, across bars', () => {
    const b = emptyClipBytes('WT1', 2);
    lit(b, 15, 5, 0); lit(b, 15, 7, 1); // chord on bar 1's last step
    const data = copyRectWt(b, { stepFrom: 15, stepTo: 15, noteFrom: 0, noteTo: 11 });
    const out = pasteRectWt(emptyClipBytes('WT1', 2), 2, 16, 1, data); // drop on bar 2 step 0, +1
    expect(voice(out, 16, 0)).toEqual({ on: true, note: 6 });
    expect(voice(out, 16, 1)).toEqual({ on: true, note: 8 });
  });

  it('paste drops voices past the clip end or lane range, and when the chord is full', () => {
    const b = emptyClipBytes('WT1', 1);
    lit(b, 0, 11, 0); lit(b, 1, 3, 0);
    const data = copyRectWt(b, { stepFrom: 0, stepTo: 1, noteFrom: 0, noteTo: 11 });
    const out = pasteRectWt(emptyClipBytes('WT1', 1), 1, 15, 0, data);
    expect(voice(out, 15, 0)).toEqual({ on: true, note: 11 });
    // dStep 1 → abs 16 ≥ 1 bar × 16 → dropped, no wrap
    expect(voice(out, 0, 0).on).toBe(false);
    const full = emptyClipBytes('WT1', 1);
    for (let lane = 0; lane < 8; lane++) lit(full, 5, lane, lane);
    const out2 = pasteRectWt(full, 1, 5, 0, copyRectWt(b, { stepFrom: 0, stepTo: 0, noteFrom: 0, noteTo: 11 }));
    expect(Array.from({ length: 8 }, (_, lane) => voice(out2, 5, lane).note)).toEqual([0, 1, 2, 3, 4, 5, 6, 7]);
  });

  it('move clears the vacated chord and re-stacks it at the target', () => {
    const b = emptyClipBytes('WT1', 1);
    lit(b, 2, 5, 0); lit(b, 2, 7, 1);
    const out = moveRectWt(b, 1, { stepFrom: 2, stepTo: 2, noteFrom: 0, noteTo: 11 }, 3, -2);
    expect(voice(out, 2, 0).on).toBe(false);
    expect(voice(out, 2, 1).on).toBe(false);
    expect(voice(out, 5, 0)).toEqual({ on: true, note: 3 });
    expect(voice(out, 5, 1)).toEqual({ on: true, note: 5 });
  });
});
