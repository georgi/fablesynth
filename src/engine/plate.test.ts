import { describe, expect, it } from 'vitest';
import OTT_SRC from './ott-worklet.js?raw';
import { REVERB_TYPES, type ParamValues } from '../params';
import { bootWt } from './workletHarness';

// PLATE reverb: the network itself (impulse response measurements against the
// ROOM comb network) and its place in the WT-1 FX chain.

const SR = 48000;
interface Line { process(x: number): number; feedback: number; damp1: number; damp2: number }
interface Plate { process(x: number): void; setParams(size: number, damp: number): void; reset(): void; outL: number; outR: number }
interface Dsp {
  FvComb: new (n: number) => Line; FvAllpass: new (n: number) => Line;
  PlateReverb: new (sr: number) => Plate; plateDecay(size: number): number;
}
const dsp = ((): Dsp => {
  class AWP { port = { onmessage: null, postMessage: () => {} }; }
  new Function('sampleRate', 'AudioWorkletProcessor', 'registerProcessor', OTT_SRC)(SR, AWP, () => {});
  return (globalThis as unknown as { FableDsp: Dsp }).FableDsp;
})();

/** Impulse response of the ROOM network (left channel), as the FX chain runs it. */
function roomIr(size: number, seconds: number): Float64Array {
  const scale = SR / 44100;
  const combs = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617].map((n) => new dsp.FvComb((n * scale) | 0));
  const aps = [556, 441, 341, 225].map((n) => new dsp.FvAllpass((n * scale) | 0));
  for (const c of combs) { c.feedback = 0.7 + size * 0.28; c.damp1 = 0.4 - size * 0.2; c.damp2 = 1 - c.damp1; }
  const out = new Float64Array(Math.round(seconds * SR));
  for (let i = 0; i < out.length; i++) {
    let o = 0;
    for (const c of combs) o += c.process(i === 0 ? 1 : 0);
    for (const a of aps) o = a.process(o);
    out[i] = o;
  }
  return out;
}
function plateIr(size: number, damp: number, seconds: number): { l: Float64Array; r: Float64Array } {
  const plate = new dsp.PlateReverb(SR);
  plate.setParams(size, damp);
  const n = Math.round(seconds * SR);
  const l = new Float64Array(n), r = new Float64Array(n);
  for (let i = 0; i < n; i++) { plate.process(i === 0 ? 1 : 0); l[i] = plate.outL; r[i] = plate.outR; }
  return { l, r };
}

const energy = (x: Float64Array, t0: number, t1: number) => {
  let e = 0;
  for (let i = Math.floor(t0 * SR); i < Math.floor(t1 * SR); i++) e += x[i]! * x[i]!;
  return e;
};
/** Seconds until the backward-integrated energy has dropped 60 dB (from the -5..-25 dB slope). */
function rt60(x: Float64Array): number {
  const edc = new Float64Array(x.length);
  let acc = 0;
  for (let i = x.length - 1; i >= 0; i--) { acc += x[i]! * x[i]!; edc[i] = acc; }
  const at = (db: number) => edc.findIndex((e) => e < edc[0]! * Math.pow(10, db / 10)) / SR;
  return (at(-25) - at(-5)) * 3;
}
/** Normalized echo density: 1 for Gaussian noise, lower for sparse echoes. */
function echoDensity(x: Float64Array, t0: number, t1: number): number {
  const a = Math.floor(t0 * SR), b = Math.floor(t1 * SR);
  const sd = Math.sqrt(energy(x, t0, t1) / (b - a));
  let outside = 0;
  for (let i = a; i < b; i++) if (Math.abs(x[i]!) > sd) outside++;
  return outside / (b - a) / 0.3173;
}
/** RMS of the tail's normalized autocorrelation over 4..80 ms: periodic flutter. */
function tailRipple(x: Float64Array): number {
  const seg = x.slice(Math.floor(0.4 * SR), Math.floor(1.4 * SR));
  const win = 2400; // flatten the decay in 50 ms windows
  for (let k = 0; k < seg.length; k += win) {
    let e = 0;
    for (let i = k; i < k + win; i++) e += seg[i]! * seg[i]!;
    const g = 1 / (Math.sqrt(e / win) || 1e-12);
    for (let i = k; i < k + win; i++) seg[i]! *= g;
  }
  let e0 = 0, sum = 0, count = 0;
  for (const v of seg) e0 += v * v;
  for (let lag = Math.floor(0.004 * SR); lag < Math.floor(0.08 * SR); lag += 3) {
    let s = 0;
    for (let i = 0; i + lag < seg.length; i++) s += seg[i]! * seg[i + lag]!;
    sum += (s / e0) * (s / e0); count++;
  }
  return Math.sqrt(sum / count);
}

describe('PLATE network', () => {
  const room = roomIr(0.84, 2), plate = plateIr(0.84, 0.5, 2);

  it('is denser than the ROOM network from the first reflections on', () => {
    for (const [t0, t1] of [[0.02, 0.07], [0.07, 0.15], [0.15, 0.3], [0.4, 0.6]] as const) {
      expect(echoDensity(plate.l, t0, t1), `${t0}-${t1} s`).toBeGreaterThan(echoDensity(room, t0, t1));
    }
    expect(echoDensity(plate.l, 0.4, 0.6)).toBeGreaterThan(0.9);
  });

  it('has less periodic ripple in the tail', () => {
    expect(tailRipple(plate.l)).toBeLessThan(0.6 * tailRipple(room));
  });

  it('decorrelates left and right', () => {
    let cross = 0;
    for (let i = 0; i < plate.l.length; i++) cross += plate.l[i]! * plate.r[i]!;
    const corr = cross / Math.sqrt(energy(plate.l, 0, 2) * energy(plate.r, 0, 2));
    expect(Math.abs(corr)).toBeLessThan(0.1);
  });

  it('SIZE sets the same decay time as the ROOM network', () => {
    for (const size of [0.3, 0.6, 0.84]) {
      const want = rt60(roomIr(size, 6)), got = rt60(plateIr(size, 0.5, 6).l);
      expect(got / want, `size ${size}: ${got.toFixed(2)} s vs ${want.toFixed(2)} s`).toBeGreaterThan(0.75);
      expect(got / want, `size ${size}: ${got.toFixed(2)} s vs ${want.toFixed(2)} s`).toBeLessThan(1.3);
    }
    expect(dsp.plateDecay(1)).toBeLessThan(0.9); // the tank stays stable
  });

  it('DAMP shortens the high-frequency tail', () => {
    const hf = (x: Float64Array) => { // energy of the first difference: a high-pass
      let e = 0;
      for (let i = Math.floor(0.5 * SR) + 1; i < x.length; i++) e += (x[i]! - x[i - 1]!) ** 2;
      return e / energy(x, 0.5, 2);
    };
    const bright = hf(plateIr(0.84, 0, 2).l), dark = hf(plateIr(0.84, 1, 2).l);
    expect(dark).toBeLessThan(0.1 * bright);
  });

  it('stays bounded at full size', () => {
    const p = new dsp.PlateReverb(SR);
    p.setParams(1, 0);
    let seed = 7, peak = 0;
    for (let i = 0; i < SR * 20; i++) {
      seed = (seed * 1664525 + 1013904223) >>> 0;
      p.process(seed / 2147483648 - 1);
      peak = Math.max(peak, Math.abs(p.outL), Math.abs(p.outR));
    }
    expect(peak).toBeLessThan(20);
    expect(Number.isFinite(p.outL)).toBe(true);
  });
});

describe('PLATE in the WT-1 FX chain', () => {
  const BASE: Partial<ParamValues> = {
    'fx.comp.on': 0, 'fx.reverb.on': 1, 'fx.reverb.size': 0.8, 'fx.reverb.mix': 0.5,
    'filter.on': 0, 'env1.a': 0.001, 'env1.s': 1, 'env1.r': 0.01, 'noise.on': 1, 'noise.level': 1, 'oscA.on': 0,
  };
  const PLATE = REVERB_TYPES.indexOf('PLATE');
  const rms = (x: Float32Array, from = 0, to = x.length) => {
    let e = 0;
    for (let i = from; i < to; i++) e += x[i]! * x[i]!;
    return Math.sqrt(e / (to - from));
  };
  /** A 2 s noise note, then 1 s of tail. */
  function run(params: Partial<ParamValues>) {
    const h = bootWt({ ...BASE, ...params });
    h.send({ t: 'on', n: 60, v: 1 });
    const held = h.render(750);
    h.send({ t: 'off', n: 60 });
    return { held, tail: h.render(375) };
  }

  it('ROOM stays the default and ignores DAMP', () => {
    const a = run({}), b = run({ 'fx.reverb.type': 0, 'fx.reverb.damp': 1 });
    expect(b.held.L).toEqual(a.held.L);
    expect(b.tail.R).toEqual(a.tail.R);
  });

  it('PLATE returns a wet level within 1.5 dB of ROOM', () => {
    const room = run({}), plate = run({ 'fx.reverb.type': PLATE });
    // Under the held note (the last second; the dry signal is the same in both)...
    const held = 20 * Math.log10(rms(plate.held.L, 48000) / rms(room.held.L, 48000));
    expect(Math.abs(held)).toBeLessThan(1.5);
    // ...and 50..250 ms after the release, where only the reverb sounds.
    const tail = 20 * Math.log10(rms(plate.tail.L, 2400, 12000) / rms(room.tail.L, 2400, 12000));
    expect(Math.abs(tail)).toBeLessThan(1.5);
  });

  it('PLATE has a wide tail', () => {
    const { tail } = run({ 'fx.reverb.type': PLATE });
    let cross = 0, l = 0, r = 0;
    for (let i = 2400; i < tail.L.length; i++) { cross += tail.L[i]! * tail.R[i]!; l += tail.L[i]! ** 2; r += tail.R[i]! ** 2; }
    expect(Math.abs(cross / Math.sqrt(l * r))).toBeLessThan(0.2);
  });

  it('a type change starts from silence', () => {
    const h = bootWt({ ...BASE, 'fx.reverb.type': PLATE });
    h.send({ t: 'on', n: 60, v: 1 }); h.render(400);
    h.send({ t: 'off', n: 60 }); h.render(20);
    h.send({ t: 'p', k: 'fx.reverb.type', v: 0 });
    h.render(40); // the ROOM network has nothing to play
    h.send({ t: 'p', k: 'fx.reverb.type', v: PLATE });
    const back = h.render(40);
    expect(rms(back.L, 1280)).toBeLessThan(1e-5); // the old PLATE tail is gone
  });
});
