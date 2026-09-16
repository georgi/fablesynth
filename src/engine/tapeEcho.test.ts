import { describe, expect, it } from 'vitest';
import { bootWt } from './workletHarness';
import type { ParamValues } from '../params';
import { ECHO_DIVISIONS, type EchoMessage } from './echo';

const BASE = {
  'fx.eq.on': 0, 'fx.ott.on': 0, 'fx.comp.on': 0, 'fx.drive.on': 0, 'fx.chorus.on': 0, 'fx.reverb.on': 0,
  'fx.delay.on': 1, 'fx.delay.time': 0.06, 'fx.delay.fb': 0.55, 'fx.delay.mix': 1,
  'fx.delay.wow': 0, 'fx.delay.flutter': 0, 'fx.delay.sat': 0, 'fx.delay.tone': 8000,
  'fx.delay.width': 1, 'fx.delay.mode': 0, 'master.volume': 0.5,
};
interface Rack {
  process(l: Float32Array, r: Float32Array, n: number): void;
  dlTime: { target: number };
}
function rack(p: ParamValues = {}, sr = 48000) {
  const h = bootWt({ ...BASE, ...p }, sr);
  h.render(Math.ceil(sr / 128)); // Settle parameter ramps in silence.
  return { h, fx: (h.proc as unknown as { fx: Rack }).fx };
}
function impulse(p: ParamValues = {}, rightInput = 0.25) {
  const { fx } = rack(p);
  const L = new Float32Array(24000), R = new Float32Array(24000);
  for (let offset = 0; offset < L.length; offset += 128) {
    const n = Math.min(128, L.length - offset), l = new Float32Array(n), r = new Float32Array(n);
    if (!offset) { l[0] = 0.25; r[0] = rightInput; }
    fx.process(l, r, n); L.set(l, offset); R.set(r, offset);
  }
  return { L, R };
}
const energy = (x: Float32Array, seconds: number) => {
  const start = Math.round(seconds * 48000) + 99;
  return x.slice(start, start + 512).reduce((sum, v) => sum + v * v, 0);
};

describe('WT-1 stereo tape echo', () => {
  it('ping-pongs an impulse at the saved time interval, with decaying repeats', () => {
    const { L, R } = impulse();
    expect(energy(L, 0.06)).toBeGreaterThan(energy(R, 0.06) * 20);
    expect(energy(R, 0.12)).toBeGreaterThan(energy(L, 0.12) * 20);
    expect(energy(L, 0.18)).toBeLessThan(energy(L, 0.06) * 0.4);
  });
  it('preserves the input channels in stereo mode, and width zero folds returns to mono', () => {
    const { L, R } = impulse({ 'fx.delay.mode': 1, 'fx.delay.fb': 0 }, 0);
    expect(energy(L, 0.06)).toBeGreaterThan(1e-5); expect(energy(R, 0.06)).toBe(0);
    const mono = impulse({ 'fx.delay.width': 0 });
    // The width smoother approaches zero asymptotically; only roundoff remains.
    expect(mono.L.reduce((peak, v, i) => Math.max(peak, Math.abs(v - mono.R[i])), 0)).toBeLessThan(1e-8);
  });
  it('the tape tone removes high frequencies, and saturation lowers hot repeat peaks', () => {
    const bright = impulse({ 'fx.delay.tone': 12000 }), dark = impulse({ 'fx.delay.tone': 600 });
    const roughness = (x: Float32Array) => {
      const signal = x.slice(2979, 3491);
      return signal.reduce((s, v, i) => s + (i ? (v - signal[i - 1]) ** 2 : 0), 0) / energy(x, 0.06);
    };
    expect(roughness(dark.L)).toBeLessThan(roughness(bright.L) * 0.1);
    const saturated = impulse({ 'fx.delay.sat': 1 });
    expect(energy(saturated.L, 0.06)).toBeLessThan(energy(impulse().L, 0.06) * 0.8);
  });
  it('sync follows standalone tempo and SQ-4 host tempo, including dotted eighths', () => {
    const { h, fx } = rack({ 'fx.delay.sync': 1, 'fx.delay.div': 3, 'seq.bpm': 120 });
    expect(fx.dlTime.target).toBe(0.375);
    h.send({ t: 'p', k: 'seq.bpm', v: 150 }); h.render(1);
    expect(fx.dlTime.target).toBeCloseTo(0.3, 8);
    h.send({ t: 'host', on: 1 }); h.send({ t: 'tempo', bpm: 118, swing: 0, anchor: 0 }); h.render(1);
    expect(fx.dlTime.target).toBeCloseTo(45 / 118, 8);
    for (let div = 0; div < ECHO_DIVISIONS.length; div++) {
      h.send({ t: 'p', k: 'fx.delay.div', v: div }); h.render(1);
      expect(fx.dlTime.target).toBeCloseTo(60 / 118 * ECHO_DIVISIONS[div], 8);
    }
  });
  it.each([44100, 48000, 96000])('stays finite and limited through maximum feedback, modulation, and time sweeps at %i Hz', sr => {
    const h = bootWt({ ...BASE, 'fx.delay.fb': 0.92, 'fx.delay.wow': 1, 'fx.delay.flutter': 1, 'fx.delay.sat': 1,
      'oscA.unison': 1, 'filter.on': 0, 'filter2.on': 0, 'env1.a': 0.001, 'env1.s': 1 }, sr);
    h.send({ t: 'on', n: 48, v: 1 });
    for (const time of [0.02, 1.5, 0.07, 0.4]) {
      h.send({ t: 'p', k: 'fx.delay.time', v: time });
      const { L, R } = h.render(160);
      expect(L.every(v => Number.isFinite(v) && Math.abs(v) <= 0.891251)).toBe(true);
      expect(R.every(v => Number.isFinite(v) && Math.abs(v) <= 0.891251)).toBe(true);
    }
    h.send({ t: 'panic' });
    const silent = h.render(40);
    expect(silent.L.every(v => v === 0) && silent.R.every(v => v === 0)).toBe(true);
  });
  it('live returns include mix and expose real head drift without affecting audio', () => {
    const params = { ...BASE, 'fx.delay.wow': 0.8, 'fx.delay.flutter': 0.6, 'filter.on': 0, 'env1.s': 1 };
    const plain = bootWt(params); plain.send({ t: 'on', n: 60, v: 1 }); const reference = plain.render(250);
    const h = bootWt(params); h.send({ t: 'echo', on: true }); h.send({ t: 'on', n: 60, v: 1 });
    const actual = h.render(250);
    expect(actual.L).toEqual(reference.L); expect(actual.R).toEqual(reference.R);
    const messages = h.sent.filter(m => m.t === 'echo') as unknown as EchoMessage[];
    expect(messages.length).toBeGreaterThan(10);
    expect(messages.some(m => m.left > -60 && m.right > -60)).toBe(true);
    expect(messages.some(m => Math.abs(m.driftL - m.driftR) > 0.0001)).toBe(true);
    expect(messages.every(m => Math.abs(m.driftL) < 0.003 && Math.abs(m.driftR) < 0.003)).toBe(true);
    h.send({ t: 'p', k: 'fx.delay.mix', v: 0 }); h.render(200);
    const silent = h.sent.filter(m => m.t === 'echo');
    expect(silent[silent.length - 1].left).toBe(-90); expect(silent[silent.length - 1].right).toBe(-90);
    h.send({ t: 'echo', on: false }); h.render(20);
    expect(h.sent.filter(m => m.t === 'echo')).toHaveLength(silent.length);
  });
});
