import { describe, expect, it } from 'vitest';
import { bootWt } from './workletHarness';
import { SynthEngine } from './synth';
import type { DynamicsMessage } from './dynamics';

const TONE = {
  'oscA.on': 1, 'oscA.table': 0, 'oscA.pos': 0, 'oscA.unison': 1, 'oscA.level': 0.6,
  'oscB.on': 0, 'sub.on': 0, 'noise.on': 0, 'filter.on': 0, 'filter2.on': 0,
  'env1.a': 0.001, 'env1.d': 0.005, 'env1.s': 1, 'env1.r': 0.01,
  'fx.eq.on': 0, 'fx.ott.on': 1, 'fx.ott.depth': 0.5, 'fx.comp.on': 1, 'fx.comp.thr': -40,
  'fx.drive.on': 0, 'fx.chorus.on': 0, 'fx.delay.on': 0, 'fx.reverb.on': 0,
};
const messages = (h: ReturnType<typeof bootWt>) => h.sent.filter(m => m.t === 'dynamics') as unknown as DynamicsMessage[];

describe('WT-1 dynamics metering', () => {
  it('is opt-in, reports real gain activity, and does not change rendered audio', () => {
    const dry = bootWt(TONE); dry.send({ t: 'on', n: 60, v: 1 });
    const reference = dry.render(400);
    expect(messages(dry)).toHaveLength(0);
    const metered = bootWt(TONE); metered.send({ t: 'dynamics', on: true });
    metered.send({ t: 'on', n: 60, v: 1 });
    const actual = metered.render(400);
    expect(actual.L).toEqual(reference.L); expect(actual.R).toEqual(reference.R);
    const data = messages(metered);
    expect(data.length).toBeGreaterThan(25); expect(data.length).toBeLessThanOrEqual(32);
    const last = data[data.length - 1];
    expect(last.ott.input).toBeGreaterThan(-60); expect(last.ott.output).toBeGreaterThan(-60);
    expect(last.ott.gains.some(g => Math.abs(g) > 1)).toBe(true);
    expect(last.comp.reduction).toBeGreaterThan(1);
    expect(last.comp.makeup).toBeGreaterThan(1);
    for (const m of data) {
      expect([...Object.values(m.comp), m.ott.input, m.ott.output, m.ott.makeup,
        ...m.ott.levels, ...m.ott.gains].every(Number.isFinite)).toBe(true);
    }
    metered.send({ t: 'dynamics', on: false }); metered.render(30);
    expect(messages(metered)).toHaveLength(data.length);
  });

  it('measures stereo RMS at both stages and reports no reduction while bypassed', () => {
    const h = bootWt({ ...TONE, 'fx.ott.on': 0, 'fx.comp.on': 0 });
    h.render(1); // Apply FX parameters.
    const fx = (h.proc as unknown as { fx: {
      metering: boolean; meterEnergy: Float64Array; meterSamples: number;
      process(l: Float32Array, r: Float32Array, n: number): void;
    } }).fx;
    fx.metering = true;
    const l = Float32Array.from({ length: 128 }, (_, i) => 0.2 * Math.sin(i * 0.15)), r = l.map(v => v * 0.3);
    const energy = l.reduce((sum, v, i) => sum + 0.5 * (v * v + r[i] * r[i]), 0);
    fx.process(l, r, l.length);
    expect(fx.meterSamples).toBe(128);
    for (const sum of fx.meterEnergy) expect(sum).toBeCloseTo(energy, 10);
    h.send({ t: 'dynamics', on: true }); h.send({ t: 'on', n: 60, v: 1 }); h.render(60);
    const data = messages(h), last = data[data.length - 1];
    expect(last.comp.reduction).toBe(0); expect(last.comp.makeup).toBe(0);
    expect(last.ott.gains).toEqual([0, 0, 0]);
    expect(last.comp.output).toBeCloseTo(last.comp.input, 8);
  });

  it('returns to silence after note release', () => {
    const h = bootWt(TONE); h.send({ t: 'dynamics', on: true });
    h.send({ t: 'on', n: 60, v: 1 }); h.render(80);
    h.send({ t: 'off', n: 60 }); h.render(1600);
    const data = messages(h), last = data[data.length - 1];
    expect(last.comp.input).toBe(-90); expect(last.comp.output).toBe(-90);
    expect(last.comp.reduction).toBe(0); expect(last.ott.gains).toEqual([0, 0, 0]);
  });

  it('shares one stream between the two panels and disables it after the final unsubscribe', () => {
    const engine = new SynthEngine(), sent: unknown[] = [];
    engine.ready = true;
    engine.node = { port: { postMessage: (m: unknown) => sent.push(m) } } as unknown as AudioWorkletNode;
    const a = engine.subscribeDynamics(() => {}), b = engine.subscribeDynamics(() => {});
    expect(sent).toEqual([{ t: 'dynamics', on: true }]);
    a(); expect(sent).toHaveLength(1);
    b(); expect(sent).toEqual([{ t: 'dynamics', on: true }, { t: 'dynamics', on: false }]);
  });
});
