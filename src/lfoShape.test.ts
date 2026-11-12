import { describe, expect, it } from 'vitest';
import OTT_SRC from './engine/ott-worklet.js?raw';
import { bootWt, type WtHarness } from './engine/workletHarness';
import { LFO_CUSTOM, LFO_HOLD, LFO_MAX_POINTS, MOD_DESTS, PARAMS, defaultParams, type ParamValues } from './params';
import {
  LFO_LANE, addLfoPoint, bendLfoSegment, evalLfoShape, lfoShapeParams, moveLfoPoint, paintLfoStep, readLfoShape,
  removeLfoPoint, seedLfoShape, toggleLfoHold,
} from './lfoShape';
import { autoParamDef, type AutoPoint } from './seq/clipAutomation';

// The engine's copy of the shape evaluation (ott-worklet.js), read from the
// flat parameter store exactly as the worklet reads it.
const engineValue = ((): (p: number[], b: number, n: number, ph: number) => number => {
  class AWP { port = { onmessage: null, postMessage: () => {} }; }
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', OTT_SRC)(48000, AWP, () => {});
  return (globalThis as unknown as { FableDsp: { lfoShapeValue: (p: number[], b: number, n: number, ph: number) => number } }).FableDsp.lfoShapeValue;
})();
function engineEval(points: AutoPoint[], phase: number): number {
  const params = lfoShapeParams(1, points);
  const flat: number[] = [];
  for (let k = 1; k <= LFO_MAX_POINTS; k++) flat.push(params[`lfo1.p${k}.x`]!, params[`lfo1.p${k}.y`]!, params[`lfo1.p${k}.c`]!);
  return engineValue(flat, 0, params['lfo1.pn']!, phase);
}
const PHASES = Array.from({ length: 200 }, (_, i) => i / 200);

describe('drawn LFO shape: storage', () => {
  it('round-trips through the point params', () => {
    const shape: AutoPoint[] = [{ t: 0, v: 0.2, c: 0.5 }, { t: 16, v: 1, hold: true }, { t: 40.25, v: 0.6 }];
    const params = { ...defaultParams(), ...lfoShapeParams(3, shape) };
    expect(readLfoShape(params, 3)).toEqual(shape);
    expect(params['lfo3.pn']).toBe(3);
    expect(params['lfo3.p2.c']).toBe(LFO_HOLD);
    expect(params['lfo3.p4.y']).toBe(PARAMS['lfo3.p4.y']!.def); // unused slots rest at the default
    expect(readLfoShape(defaultParams(), 3)).toEqual([]);
  });

  it('keeps the point params out of the automation targets', () => {
    expect(autoParamDef('WT1', 'lfo1.p1.y')).toBeNull();
    expect(autoParamDef('WT1', 'lfo1.pn')).toBeNull();
    expect(autoParamDef('WT1', 'lfo1.rate')).not.toBeNull();
  });
});

describe('drawn LFO shape: value', () => {
  it('ramps, holds and wraps', () => {
    const tri: AutoPoint[] = [{ t: 0, v: 0 }, { t: 32, v: 1 }];
    expect(evalLfoShape(tri, 0)).toBe(-1);
    expect(evalLfoShape(tri, 0.25)).toBeCloseTo(0, 9);
    expect(evalLfoShape(tri, 0.5)).toBe(1);
    expect(evalLfoShape(tri, 0.75)).toBeCloseTo(0, 9); // the last point runs on to the first
    const steps: AutoPoint[] = [{ t: 0, v: 1, hold: true }, { t: 32, v: 0.25, hold: true }];
    expect(evalLfoShape(steps, 0.49)).toBe(1);
    expect(evalLfoShape(steps, 0.5)).toBe(-0.5);
    expect(evalLfoShape(steps, 0.99)).toBe(-0.5);
    // Before the first point the shape continues from the last one.
    expect(evalLfoShape([{ t: 16, v: 0 }, { t: 48, v: 1 }], 0)).toBeCloseTo(0, 9);
    expect(evalLfoShape([], 0.3)).toBe(0);
  });

  it('bends a segment: c > 0 starts slow, c < 0 starts fast', () => {
    const at = (c: number) => evalLfoShape([{ t: 0, v: 0, c }, { t: 32, v: 1, hold: true }], 0.25);
    expect(at(0)).toBeCloseTo(0, 9);
    expect(at(1)).toBeCloseTo(2 / (1 + Math.exp(3)) - 1, 9);
    expect(at(-1)).toBeCloseTo(-at(1), 9);
  });

  it('the engine evaluates the same value', () => {
    const shapes: AutoPoint[][] = [0, 1, 2, 3, 4].map(seedLfoShape);
    shapes.push([{ t: 5.25, v: 0.3, c: -0.7 }, { t: 20, v: 0.9, hold: true }, { t: 20.25, v: 0.1, c: 1 }, { t: 61, v: 0.5 }]);
    shapes.push([{ t: 12, v: 0.4 }]);
    for (const shape of shapes) for (const ph of PHASES) expect(engineEval(shape, ph)).toBeCloseTo(evalLfoShape(shape, ph), 6);
    expect(engineEval([], 0.5)).toBe(0);
  });

  it('the seeds draw the standard shapes', () => {
    const fns = [
      (p: number) => Math.sin(2 * Math.PI * p), (p: number) => 1 - 4 * Math.abs(p - 0.5),
      (p: number) => 1 - 2 * p, (p: number) => (p < 0.5 ? 1 : -1),
    ];
    fns.forEach((fn, shape) => {
      for (const ph of PHASES.filter((p) => p < 0.99)) {
        // The drawn sine is four exponential arcs; the saw ends one grid tick early.
        expect(Math.abs(evalLfoShape(seedLfoShape(shape), ph) - fn(ph)), `shape ${shape} at ${ph}`).toBeLessThan(shape === 0 ? 0.05 : 0.01);
      }
    });
  });
});

describe('drawn LFO shape: edits', () => {
  it('adds, moves, bends and removes points in x order', () => {
    let { points, index } = addLfoPoint([], 0.5, 0.8);
    ({ points, index } = addLfoPoint(points, 0.25, 0.1));
    expect(index).toBe(0);
    expect(points.map((p) => p.t)).toEqual([16, 32]);
    points = moveLfoPoint(points, 0, 0.9, 0.4); // cannot pass its neighbour
    expect(points[0]).toMatchObject({ t: 32, v: 0.4 });
    points = bendLfoSegment(points, 0, 3);
    expect(points[0]!.c).toBe(1);
    points = toggleLfoHold(points, 1);
    expect(points[1]!.hold).toBe(true);
    expect(removeLfoPoint(points, 0)).toHaveLength(1);
  });

  it('paints sixteen steps into the sixteen points', () => {
    let points: AutoPoint[] = [];
    for (let s = 0; s < 16; s++) points = paintLfoStep(points, (s + 0.5) / 16, s % 2 ? 0.2 : 0.9, 16);
    expect(points).toHaveLength(16);
    for (let s = 0; s < 16; s++) expect(evalLfoShape(points, (s + 0.5) / 16)).toBeCloseTo(s % 2 ? -0.6 : 0.8, 9);
  });

  it('refuses a point beyond the limit', () => {
    const full = Array.from({ length: LFO_MAX_POINTS }, (_, i) => ({ t: i * 4, v: i / 16 }));
    expect(addLfoPoint(full, 0.99, 0.5)).toEqual({ points: full, index: -1 });
    expect(paintLfoStep(full, 0.53, 0.5, 32)).toBe(full); // 32 steps cannot fit
    expect(LFO_LANE / 0.25).toBe(256); // positions per cycle
  });

  it('a paint stroke over a full curve turns it into sixteen steps', () => {
    // Sixteen ramp points: one painted cell would need pin points beside it.
    const full = Array.from({ length: LFO_MAX_POINTS }, (_, i) => ({ t: i * 4 + 1, v: i / 16 }));
    const painted = paintLfoStep(full, 0.53, 0.9, 16);
    expect(painted).toHaveLength(16);
    expect(painted.every((p) => p.hold)).toBe(true);
    expect(evalLfoShape(painted, 0.53)).toBeCloseTo(0.8, 9);           // the painted cell
    expect(evalLfoShape(painted, 0.22)).toBeCloseTo(evalLfoShape(full, 3.5 / 16), 9); // the others sample the curve
  });
});

describe('drawn LFO shape: in the engine', () => {
  const DST = MOD_DESTS.indexOf('A POS');
  // LFO 1 -> A POS at full depth: the telemetry reports the LFO value.
  const ROUTE: Partial<ParamValues> = { 'mat1.src': 1, 'mat1.dst': DST, 'mat1.amt': 1, 'lfo1.shape': LFO_CUSTOM, 'lfo1.rate': 2 };
  const values = (h: WtHarness) => (h.sent.filter((m) => m.t === 'mod' && m.d) as unknown as { d: Float32Array }[]).map((m) => m.d[DST]!);

  it('plays the drawn steps', () => {
    const h = bootWt({ ...ROUTE, ...lfoShapeParams(1, [{ t: 0, v: 1, hold: true }, { t: 32, v: 0.25, hold: true }]) });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(750); // 2 s = 4 cycles
    const seen = values(h);
    expect(seen.length).toBeGreaterThan(30);
    for (const v of seen) expect([1, -0.5]).toContain(Number(v.toFixed(4)));
    expect(new Set(seen.map((v) => v.toFixed(4))).size).toBe(2);
  });

  it('an empty drawn shape rests at zero', () => {
    const h = bootWt({ ...ROUTE, 'mat2.src': 4, 'mat2.dst': MOD_DESTS.indexOf('B POS'), 'mat2.amt': 0.5 });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(200);
    for (const v of values(h)) expect(v).toBe(0);
  });

  it('ONE-SHOT runs one cycle, then holds the end of the shape', () => {
    const ramp = lfoShapeParams(1, [{ t: 0, v: 0 }, { t: LFO_LANE - 0.25, v: 1, hold: true }]);
    const once = bootWt({ ...ROUTE, ...ramp, 'lfo1.oneshot': 1 });
    once.send({ t: 'on', n: 60, v: 1 });
    once.render(750);
    const seen = values(once);
    expect(seen[0]!).toBeLessThan(-0.7);                       // starts at the bottom
    for (let i = 1; i < seen.length; i++) expect(seen[i]!).toBeGreaterThanOrEqual(seen[i - 1]! - 1e-6); // never wraps
    expect(seen[seen.length - 1]!).toBeCloseTo(1, 4);          // holds the last point
    const loop = bootWt({ ...ROUTE, ...ramp });
    loop.send({ t: 'on', n: 60, v: 1 });
    loop.render(750);
    expect(values(loop).some((v, i, all) => i > 0 && v < all[i - 1]! - 0.5)).toBe(true); // the loop wraps
  });

  it('ONE-SHOT restarts with each note, also with TRIG off, and works for the standard shapes', () => {
    const h = bootWt({ 'mat1.src': 1, 'mat1.dst': DST, 'mat1.amt': 1, 'lfo1.shape': 2, 'lfo1.rate': 2, 'lfo1.retrig': 0, 'lfo1.oneshot': 1, 'env1.r': 0.01 });
    for (let note = 0; note < 2; note++) {
      h.sent.length = 0;
      h.send({ t: 'on', n: 60, v: 1 });
      h.render(400);
      const seen = values(h);
      expect(seen[0]!).toBeGreaterThan(0.8);                   // the saw starts at the top
      expect(seen[seen.length - 1]!).toBeCloseTo(-1, 4);       // and stops at the bottom
      h.send({ t: 'off', n: 60 });
      h.render(200);
    }
  });
});
