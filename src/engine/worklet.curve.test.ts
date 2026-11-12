import { describe, expect, it } from 'vitest';
import { MOD_DESTS, defaultParams, routeCurve, type ParamValues } from '../params';
import { clearSlot, setMatSlot } from '../store/slotHelpers';
import { bootWt } from './workletHarness';

// Response curve per modulation route (mat<n>.crv): the source value is bent
// before the amount. The same numbers as juce/test/RouteCurveChecks.h.

const SLOW = 1 / (1 + Math.exp(3)); // bend(0.5, +1)
const DST = MOD_DESTS.indexOf('A POS');

describe('routeCurve', () => {
  it('bends a unipolar value and keeps both ends', () => {
    expect(routeCurve(0.5, 0, false)).toBe(0.5);
    expect(routeCurve(0.5, 1, false)).toBeCloseTo(SLOW, 9);      // starts slow
    expect(routeCurve(0.5, -1, false)).toBeCloseTo(1 - SLOW, 9); // starts fast
    for (const c of [-1, -0.3, 0.6, 1]) {
      expect(routeCurve(0, c, false)).toBeCloseTo(0, 9);
      expect(routeCurve(1, c, false)).toBeCloseTo(1, 9);
    }
    expect(routeCurve(1.7, -1, false)).toBeCloseTo(1, 9); // sizes above 1 stop at 1
  });

  it('keeps the sign of a bipolar value', () => {
    expect(routeCurve(-0.5, 1, true)).toBeCloseTo(-SLOW, 9);
    expect(routeCurve(0.5, 1, true)).toBeCloseTo(SLOW, 9);
    expect(routeCurve(-1, -0.4, true)).toBeCloseTo(-1, 9);
  });

  it('a cleared slot is linear again', () => {
    const p = defaultParams();
    setMatSlot(p, 3, { src: 1, dst: 3, amt: 0.5, crv: 0.7 });
    expect(p['mat3.crv']).toBe(0.7);
    clearSlot(p, 3);
    expect(p['mat3.crv']).toBe(0);
  });
});

describe('route curve in the engine', () => {
  // The route sum at full depth is the curved source value (mod telemetry).
  function routed(params: Partial<ParamValues>, note: number, vel: number): number {
    const h = bootWt({ 'mat1.dst': DST, 'mat1.amt': 1, ...params });
    h.send({ t: 'on', n: note, v: vel });
    h.render(32);
    const msg = h.sent.find((m) => m.t === 'mod' && m.d) as unknown as { d: Float32Array };
    return msg.d[DST]!;
  }

  it('applies the curve to a unipolar source', () => {
    expect(routed({ 'mat1.src': 4 }, 60, 0.5)).toBeCloseTo(0.5, 6);                      // VELO
    expect(routed({ 'mat1.src': 4, 'mat1.crv': 1 }, 60, 0.5)).toBeCloseTo(SLOW, 6);
    expect(routed({ 'mat1.src': 4, 'mat1.crv': -1 }, 60, 0.5)).toBeCloseTo(1 - SLOW, 6);
  });

  it('applies the curve to the size of a bipolar source', () => {
    expect(routed({ 'mat1.src': 5 }, 48, 1)).toBeCloseTo(-0.5, 6);                       // NOTE: C3 is -0.5
    expect(routed({ 'mat1.src': 5, 'mat1.crv': 1 }, 48, 1)).toBeCloseTo(-SLOW, 6);
  });
});
