import { describe, expect, it } from 'vitest';
import { DEST_OF_PARAM, DST_ROUTE_AMT, MOD_DESTS, dstTarget, type ParamValues } from '../params';
import { bootWt, type WtHarness } from './workletHarness';

// The destinations from 30 up: LFO rates, envelope times, FX controls and the
// amount of another route. The same cases as juce/test/ModDestChecks.h.

const D = (name: string) => MOD_DESTS.indexOf(name);
const MACRO1 = 9, VELO = 4, LFO1 = 1;
const modValues = (h: WtHarness, dst: number) =>
  (h.sent.filter((m) => m.t === 'mod' && m.d) as unknown as { d: Float32Array }[]).map((m) => m.d[dst]!);
const rms = (x: Float32Array, from = 0, to = x.length) => {
  let e = 0;
  for (let i = from; i < to; i++) e += x[i]! * x[i]!;
  return Math.sqrt(e / (to - from));
};

describe('destination table', () => {
  it('every knob destination maps back to its param', () => {
    for (const [id, dst] of Object.entries(DEST_OF_PARAM)) expect(dstTarget(dst), id).toBe(id);
    expect(DEST_OF_PARAM['fx.delay.mix']).toBe(D('DELAY MIX'));
    expect(DEST_OF_PARAM['mat1.amt']).toBe(DST_ROUTE_AMT);
    expect(D('MOD 1 AMT')).toBe(DST_ROUTE_AMT);
  });
});

describe('LFO rate destination', () => {
  // LFO 1 (saw, 1 Hz) -> A POS shows the LFO; a macro on LFO1 RATE speeds it up.
  const base: Partial<ParamValues> = {
    'lfo1.shape': 2, 'lfo1.rate': 1, 'mat1.src': LFO1, 'mat1.dst': D('A POS'), 'mat1.amt': 1,
    'mat2.src': MACRO1, 'mat2.dst': D('LFO1 RATE'), 'mat2.amt': 0.2,
  };
  // Cycles of the saw in one second: each wrap jumps up by about 2.
  const wraps = (params: Partial<ParamValues>) => {
    const h = bootWt({ ...base, ...params });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(375);
    const v = modValues(h, D('A POS'));
    return v.filter((x, i) => i > 0 && x > v[i - 1]! + 1).length;
  };

  it('scales a per-voice LFO by 2^(5x)', () => {
    expect(wraps({ macro1: 0 })).toBe(0);            // 1 Hz: no wrap inside the first second
    expect(wraps({ macro1: 1 })).toBe(1);            // 2 Hz
    expect(wraps({ macro1: 1, 'mat2.amt': 0.4 })).toBe(3); // 4 Hz
  });

  it('scales a free-running LFO too', () => {
    expect(wraps({ macro1: 1, 'mat2.amt': 0.4, 'lfo1.retrig': 0 })).toBeGreaterThanOrEqual(3);
    expect(wraps({ macro1: 0, 'mat2.amt': 0.4, 'lfo1.retrig': 0 })).toBeLessThanOrEqual(1);
  });
});

describe('envelope time destinations', () => {
  const DRY: Partial<ParamValues> = { 'filter.on': 0, 'fx.comp.on': 0, 'env1.s': 0, 'env1.a': 0.001 };
  // Level 150..200 ms after the note start: a longer decay leaves more.
  const late = (params: Partial<ParamValues>, vel = 1) => {
    const h = bootWt({ ...DRY, 'env1.d': 0.05, ...params });
    h.send({ t: 'on', n: 60, v: vel });
    return rms(h.render(80).L, 7200, 9600);
  };

  it('AMP DEC follows its route', () => {
    const plain = late({});
    const longer = late({ macro1: 1, 'mat1.src': MACRO1, 'mat1.dst': D('AMP DEC'), 'mat1.amt': 0.4 }); // x4
    expect(longer).toBeGreaterThan(plain * 20);
    // The route equals the same decay set on the knob.
    expect(longer).toBeCloseTo(late({ 'env1.d': 0.2 }), 4);
  });

  it('a routed time stays inside the knob range', () => {
    const x = late({ macro1: 1, 'mat1.src': MACRO1, 'mat1.dst': D('AMP ATK'), 'mat1.amt': -1 });
    expect(Number.isFinite(x)).toBe(true);
  });

  it('MENV DEC changes the mod envelope the same way', () => {
    // MOD ENV -> A POS shows the envelope level; velocity shortens its decay.
    const h = (vel: number) => {
      const w = bootWt({ 'env2.a': 0.001, 'env2.d': 0.4, 'env2.s': 0, 'mat1.src': 3, 'mat1.dst': D('A POS'), 'mat1.amt': 1,
        'mat2.src': VELO, 'mat2.dst': D('MENV DEC'), 'mat2.amt': -0.4 });
      w.send({ t: 'on', n: 60, v: vel });
      w.render(64);
      return modValues(w, D('A POS'))[2]!; // about 130 ms in
    };
    expect(h(1)).toBeLessThan(h(0.1) * 0.5);
  });
});

describe('FX destinations', () => {
  const BASE: Partial<ParamValues> = {
    'filter.on': 0, 'fx.comp.on': 0, 'env1.a': 0.001, 'env1.s': 1, 'env1.r': 0.005,
    'fx.delay.on': 1, 'fx.delay.time': 0.25, 'fx.delay.fb': 0, 'fx.delay.mix': 0,
    'mat1.src': MACRO1, 'mat1.dst': D('DELAY MIX'), 'mat1.amt': 1,
  };
  // A 100 ms note, then the level around the first echo (250..350 ms).
  const echo = (h: WtHarness) => {
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(38);
    h.send({ t: 'off', n: 60 });
    return rms(h.render(94).L, 7200, 12000);
  };

  it('a macro opens the delay mix', () => {
    const dry = echo(bootWt({ ...BASE, macro1: 0 }));
    const wet = echo(bootWt({ ...BASE, macro1: 0.6 }));
    expect(dry).toBeLessThan(1e-4);
    expect(wet).toBeGreaterThan(0.005);
    // The route equals the same mix set on the knob.
    const knob = echo(bootWt({ ...BASE, macro1: 0, 'fx.delay.mix': 0.6 }));
    expect(wet).toBeCloseTo(knob, 3);
  });

  it('the macro still moves the mix after the last voice ended', () => {
    const h = bootWt({ ...BASE, macro1: 0, 'fx.delay.fb': 0.8 });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(38);
    h.send({ t: 'off', n: 60 });
    h.render(60); // the voice is gone; the delay line still circulates
    const before = rms(h.render(150).L);
    h.send({ t: 'p', k: 'macro1', v: 0.7 });
    h.render(20);
    const after = rms(h.render(150).L);
    expect(before).toBeLessThan(1e-4);
    expect(after).toBeGreaterThan(0.002);
  });

  it('reports the FX route in the live-mod telemetry, for the knob ring', () => {
    const h = bootWt({ ...BASE, macro1: 0.5 });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(64);
    expect(modValues(h, D('DELAY MIX'))[0]).toBeCloseTo(0.5, 6);
  });

  it('leaves the FX alone without an FX route', () => {
    const plain = bootWt({ ...BASE, 'mat1.dst': 0, 'fx.delay.mix': 0.5 }), other = bootWt({ ...BASE, 'mat1.dst': D('A POS'), macro1: 0, 'fx.delay.mix': 0.5 });
    plain.send({ t: 'on', n: 60, v: 1 }); other.send({ t: 'on', n: 60, v: 1 });
    expect(other.render(100).L).toEqual(plain.render(100).L);
  });
});

describe('route amount destination (mod of mod)', () => {
  // Route 1: VELO -> A POS, amount 0.2. Route 2: MACRO 1 -> MOD 1 AMT.
  const base: Partial<ParamValues> = {
    'mat1.src': VELO, 'mat1.dst': D('A POS'), 'mat1.amt': 0.2,
    'mat2.src': MACRO1, 'mat2.dst': D('MOD 1 AMT'), 'mat2.amt': 0.25,
  };
  const pos = (params: Partial<ParamValues>) => {
    const h = bootWt({ ...base, ...params });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(32);
    return modValues(h, D('A POS'))[0]!;
  };

  it('moves the amount of the target route', () => {
    expect(pos({ macro1: 0 })).toBeCloseTo(0.2, 6);
    expect(pos({ macro1: 1 })).toBeCloseTo(0.7, 6);       // 0.2 + 0.25 * 2
    expect(pos({ macro1: 1, 'mat2.amt': 1 })).toBeCloseTo(1, 6);   // the amount stops at 1
    expect(pos({ macro1: 1, 'mat2.amt': -1 })).toBeCloseTo(-1, 6); // and at -1
  });

  it('does not depend on the slot order', () => {
    const swapped = pos({ macro1: 1, 'mat1.src': MACRO1, 'mat1.dst': D('MOD 2 AMT'), 'mat1.amt': 0.25,
      'mat2.src': VELO, 'mat2.dst': D('A POS'), 'mat2.amt': 0.2 });
    expect(swapped).toBeCloseTo(0.7, 6);
  });

  it('a route on a route amount is not itself modulated by a third', () => {
    // Route 3 targets route 2 (the modulating route): no effect on route 1.
    expect(pos({ macro1: 1, 'mat3.src': VELO, 'mat3.dst': D('MOD 2 AMT'), 'mat3.amt': 1 })).toBeCloseTo(0.7, 6);
  });
});
