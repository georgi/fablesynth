import { describe, it, expect } from 'vitest';
import { MOD_DESTS, DEST_OF_PARAM, dstTarget, PARAMS, type GlobalDst } from './params';

// Canonical dst index -> target, copied straight from the C++ contract
// (juce/source/dsp/Params.h dstTarget / MOD_DESTS). This table is the single
// source of truth the two engines must agree on, so it is duplicated here on
// purpose: if either side drifts, this test fails.
const EXPECTED: Array<[number, string, ReturnType<typeof dstTarget>]> = [
  [0, '—', 'none'],
  [1, 'A POS', 'oscA.pos'],
  [2, 'B POS', 'oscB.pos'],
  [3, 'F1 CUT', 'filter.cutoff'],
  [4, 'PITCH', 'pitch'],
  [5, 'AMP', 'amp'],
  [6, 'PAN', 'pan'],
  [7, 'A LVL', 'oscA.level'],
  [8, 'B LVL', 'oscB.level'],
  [9, 'F2 CUT', 'filter2.cutoff'],
  [10, 'F2 RES', 'filter2.res'],
  [11, 'A DETUNE', 'oscA.detune'],
  [12, 'A SPREAD', 'oscA.spread'],
  [13, 'A PAN', 'oscA.pan'],
  [14, 'B DETUNE', 'oscB.detune'],
  [15, 'B SPREAD', 'oscB.spread'],
  [16, 'B PAN', 'oscB.pan'],
  [17, 'F1 RES', 'filter.res'],
  [18, 'F1 DRIVE', 'filter.drive'],
  [19, 'F1 ENV', 'filter.env'],
  [20, 'F1 KEY', 'filter.key'],
  [21, 'F2 DRIVE', 'filter2.drive'],
  [22, 'F2 ENV', 'filter2.env'],
  [23, 'F2 KEY', 'filter2.key'],
  [24, 'SUB LVL', 'sub.level'],
  [25, 'NOISE LVL', 'noise.level'],
  [26, 'A BLEND', 'oscA.blend'],
  [27, 'B BLEND', 'oscB.blend'],
];

const GLOBALS: GlobalDst[] = ['none', 'pitch', 'amp', 'pan'];

describe('MOD_DESTS', () => {
  it('has exactly 28 entries (0..27) with the contract labels', () => {
    expect(MOD_DESTS).toHaveLength(28);
    expect(MOD_DESTS).toEqual(EXPECTED.map(([, label]) => label));
  });

  it('keeps the original 0..10 destinations bit-identical (preset compatibility)', () => {
    expect(MOD_DESTS.slice(0, 11)).toEqual([
      '—', 'A POS', 'B POS', 'F1 CUT', 'PITCH', 'AMP', 'PAN', 'A LVL', 'B LVL', 'F2 CUT', 'F2 RES',
    ]);
  });
});

describe('dstTarget', () => {
  it('maps every index to the contract target (index-for-index parity)', () => {
    for (const [idx, , target] of EXPECTED) {
      expect(dstTarget(idx)).toBe(target);
    }
  });

  it('treats out-of-range / negative indices as "none"', () => {
    expect(dstTarget(28)).toBe('none');
    expect(dstTarget(-1)).toBe('none');
    expect(dstTarget(999)).toBe('none');
  });

  it('every per-param target resolves to a real ParamDef', () => {
    for (const [, , target] of EXPECTED) {
      if (GLOBALS.includes(target as GlobalDst)) continue;
      expect(PARAMS[target as string]).toBeDefined();
    }
  });
});

describe('DEST_OF_PARAM', () => {
  it('is the exact reverse of dstTarget for every per-param dest', () => {
    // forward: dstTarget(DEST_OF_PARAM[id]) === id
    for (const [id, idx] of Object.entries(DEST_OF_PARAM)) {
      expect(dstTarget(idx)).toBe(id);
    }
    // backward: every non-global dst has a DEST_OF_PARAM entry
    for (const [idx, , target] of EXPECTED) {
      if (idx === 0 || GLOBALS.includes(target as GlobalDst)) continue;
      expect(DEST_OF_PARAM[target as string]).toBe(idx);
    }
  });

  it('points only at lin/log Float params (modulatable continuous controls)', () => {
    for (const id of Object.keys(DEST_OF_PARAM)) {
      const def = PARAMS[id];
      expect(def.curve === 'lin' || def.curve === 'log').toBe(true);
    }
  });
});

// The generic curve rules the worklet/engine apply when folding a route sum x into
// a param value. Lin: p + x*(hi-lo); Log: p * 2^(x*D), D=5. These reproduce the
// legacy hard-coded scalings exactly (POS/LEVEL/RES width-1 add; CUTOFF *2^(x*5)).
describe('mod curve rules', () => {
  const D = 5;
  const lin = (p: number, x: number, lo: number, hi: number) => p + x * (hi - lo);
  const log = (p: number, x: number) => p * Math.pow(2, x * D);

  it('Lin width-1 reproduces native += x (POS/LEVEL/RES/DETUNE/SPREAD)', () => {
    const pos = PARAMS['oscA.pos'];
    expect(lin(0.5, 0.2, pos.min as number, pos.max as number)).toBeCloseTo(0.7, 12);
  });

  it('Lin width-2 doubles the swing for bipolar params (PAN/ENV)', () => {
    const pan = PARAMS['oscA.pan'];
    expect(pan.min).toBe(-1);
    expect(pan.max).toBe(1);
    // a +0.25 route shifts a width-2 param by +0.5
    expect(lin(0, 0.25, pan.min as number, pan.max as number)).toBeCloseTo(0.5, 12);
  });

  it('Log D=5 reproduces CUTOFF *2^(x*5): full route = +5 octaves', () => {
    expect(log(1000, 1)).toBeCloseTo(32000, 6); // 1000 * 2^5
    expect(log(1000, -1)).toBeCloseTo(31.25, 6); // 1000 * 2^-5
    expect(log(440, 0)).toBeCloseTo(440, 12); // identity at x=0
  });
});

describe('unison + blend (Serum-style unison)', () => {
  it('raises the unison voice cap to 16 on both oscillators', () => {
    expect(PARAMS['oscA.unison'].max).toBe(16);
    expect(PARAMS['oscB.unison'].max).toBe(16);
    expect(PARAMS['oscA.unison'].min).toBe(1);
  });

  it('adds a per-osc BLEND param defaulting to 1.0 over 0..1 (lin)', () => {
    for (const id of ['oscA.blend', 'oscB.blend']) {
      const def = PARAMS[id];
      expect(def, id).toBeDefined();
      expect(def.def).toBe(1.0);
      expect(def.min).toBe(0);
      expect(def.max).toBe(1);
      expect(def.curve).toBe('lin');
    }
  });

  it('wires BLEND as mod dest 26 (A) / 27 (B), both directions', () => {
    expect(dstTarget(26)).toBe('oscA.blend');
    expect(dstTarget(27)).toBe('oscB.blend');
    expect(DEST_OF_PARAM['oscA.blend']).toBe(26);
    expect(DEST_OF_PARAM['oscB.blend']).toBe(27);
  });
});
