import { describe, it, expect } from 'vitest';
import { defaultParams, type ParamValues } from '../params';
import {
  MOD_MATRIX_SIZE,
  findFreeSlot,
  getActiveSlots,
  getModsByDest,
  setMatSlot,
  clearSlot,
} from './slotHelpers';

const base = (): ParamValues => defaultParams();

describe('findFreeSlot', () => {
  it('returns 1 on a fresh (all-zero) param map', () => {
    expect(findFreeSlot(base())).toBe(1);
  });

  it('returns the first fully-empty slot, skipping configured ones', () => {
    const p = base();
    setMatSlot(p, 1, { src: 1, dst: 3 });
    setMatSlot(p, 2, { src: 2, dst: 5 });
    expect(findFreeSlot(p)).toBe(3);
  });

  it('skips a half-configured src-only ADD-ROUTE row (src set, dst==0)', () => {
    const p = base();
    setMatSlot(p, 1, { src: 1, dst: 0 }); // ADD ROUTE: visible but inactive
    expect(findFreeSlot(p)).toBe(2);
  });

  it('returns 0 when the pool is full', () => {
    const p = base();
    for (let n = 1; n <= MOD_MATRIX_SIZE; n++) setMatSlot(p, n, { src: 1, dst: 3 });
    expect(findFreeSlot(p)).toBe(0);
  });
});

describe('getActiveSlots', () => {
  it('only returns rows with both src!=0 AND dst!=0, carrying absolute slot numbers', () => {
    const p = base();
    setMatSlot(p, 2, { src: 1, dst: 3, amt: 0.4 });
    setMatSlot(p, 5, { src: 4, dst: 1, amt: -0.2 });
    setMatSlot(p, 7, { src: 2, dst: 0, amt: 0.5 }); // src-only -> not active
    const active = getActiveSlots(p);
    expect(active).toEqual([
      { slot: 2, src: 1, dst: 3, amt: 0.4 },
      { slot: 5, src: 4, dst: 1, amt: -0.2 },
    ]);
  });
});

describe('getModsByDest', () => {
  it('filters active routes by destination', () => {
    const p = base();
    setMatSlot(p, 1, { src: 1, dst: 3, amt: 0.3 });
    setMatSlot(p, 2, { src: 2, dst: 3, amt: 0.5 });
    setMatSlot(p, 3, { src: 3, dst: 1, amt: 0.2 });
    const byCutoff = getModsByDest(p, 3);
    expect(byCutoff.map((r) => r.slot)).toEqual([1, 2]);
    expect(getModsByDest(p, 1).map((r) => r.slot)).toEqual([3]);
  });
});

describe('clearSlot', () => {
  it('zeros src, dst and amt', () => {
    const p = base();
    setMatSlot(p, 4, { src: 2, dst: 6, amt: 0.7 });
    clearSlot(p, 4);
    expect(p['mat4.src']).toBe(0);
    expect(p['mat4.dst']).toBe(0);
    expect(p['mat4.amt']).toBe(0);
  });
});
