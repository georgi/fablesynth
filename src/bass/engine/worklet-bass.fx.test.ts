// BL-1 master FX rack — the port of juce/source/bass/dsp/BassFx.cpp into the
// worklet (audio-engine review B5 + W6). The rack used to be a graph of native
// WebAudio nodes, so none of it could be measured offline; these tests cover
// the four things the native graph got wrong.
import { describe, it, expect } from 'vitest';
import { makeBassProcessor, type BassHarness } from './bassHarness';
import { generateTables } from '../../engine/wavetables';
import { defaultBassParams } from '../params';
import { makeEmptyPatterns, setStep } from '../seq';
import type { ParamValues } from '../../params';

const tables = generateTables();
const tableMsg = {
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};

const CEILING = 0.8912509381337456; // -1 dBFS
const FX_OFF = { 'fx.drive.on': 0, 'fx.chorus.on': 0, 'fx.delay.on': 0, 'fx.reverb.on': 0 };

function boot(params: ParamValues, sr = 48000): BassHarness {
  const h = makeBassProcessor(sr);
  h.send({ t: 'init', params });
  h.send(tableMsg);
  return h;
}

const peak = (x: Float32Array): number => x.reduce((m, v) => Math.max(m, Math.abs(v)), 0);

// Windowed single-frequency power (Blackman-Harris).
function binPower(x: Float32Array, f: number, sr: number): number {
  const N = x.length;
  const w = (2 * Math.PI * f) / sr;
  let re = 0, im = 0;
  for (let i = 0; i < N; i++) {
    const t = (2 * Math.PI * i) / (N - 1);
    const win = 0.35875 - 0.48829 * Math.cos(t) + 0.14128 * Math.cos(2 * t) - 0.01168 * Math.cos(3 * t);
    const v = x[i] * win;
    re += v * Math.cos(w * i);
    im += v * Math.sin(w * i);
  }
  return re * re + im * im;
}

// Non-harmonic energy relative to harmonic energy, in dB.
function aliasDb(x: Float32Array, f0: number, sr: number): number {
  const binF = sr / x.length;
  let sig = 0, alias = 0;
  for (let f = binF; f < sr / 2; f += binF) {
    const n = Math.round(f / f0);
    if (n >= 1 && Math.abs(f - n * f0) < 6 * binF) sig += binPower(x, f, sr);
    else alias += binPower(x, f, sr);
  }
  return 10 * Math.log10(alias / sig);
}

// A steady sub sine, so the only spectrum the FX rack sees is one partial.
function sinePatch(): ParamValues {
  const p = defaultBassParams();
  p['osc.level'] = 0;
  p['sub.level'] = 1;
  p['sub.shape'] = 0;
  p['sub.oct'] = -1;
  p['flt.type'] = 0;
  p['flt.cut'] = 20000;
  p['flt.res'] = 0;
  p['flt.env'] = 0;
  p['flt.track'] = 0;
  p['flt.drive'] = 0;
  p['lfo.depth'] = 0;
  p['aenv.att'] = 0.001;
  p['aenv.sus'] = 1;
  Object.assign(p, FX_OFF);
  return p;
}

const subF = (semi: number): number => 440 * Math.pow(2, (36 + semi - 12 - 69) / 12);

describe('BL-1 FX drive is 4x oversampled (W6)', () => {
  it('keeps the shaper harmonics from folding back', () => {
    // 1.2 kHz into a hard tanh: the plugin's 4x Kaiser half-band pair holds the
    // fold-back down, where the WaveShaperNode's 2x table did not. Everything
    // downstream is off and the master runs quiet, so the only non-linearity in
    // the measurement is the shaper itself.
    const semi = 48;
    const f0 = subF(semi);
    const p = sinePatch();
    p['fx.drive.on'] = 1;
    p['fx.drive.amt'] = 1;
    p['fx.drive.mix'] = 1;
    p['master.volume'] = 0.3;
    const h = boot(p);
    h.send({ t: 'noteon', semi, vel: 1 });
    h.render(30);
    const db = aliasDb(h.render(64).L, f0, 48000);
    expect(db).toBeLessThan(-60);
  }, 120_000);

  it('reports the chain latency the plugin reports', () => {
    // drive FIR group delay (27) + limiter lookahead (72 at 48 kHz) = 99.
    const h = boot(defaultBassParams());
    const msg = h.sent.find((m) => m.t === 'latency');
    expect(msg?.n).toBe(99);
    // 44.1 kHz: the lookahead is 1.5 ms, the FIR delay is rate-relative.
    const h44 = boot(defaultBassParams(), 44100);
    expect(h44.sent.find((m) => m.t === 'latency')?.n).toBe(27 + 66);
  });

  it('delays the signal by the reported latency', () => {
    const p = sinePatch();
    p['aenv.att'] = 0.0001;
    const h = boot(p);
    h.send({ t: 'noteon', semi: 24, vel: 1 });
    const { L } = h.render(4);
    let onset = -1;
    for (let i = 0; i < L.length && onset < 0; i++) if (Math.abs(L[i]) > 1e-9) onset = i;
    // The sub sine starts at phase 0, so the voice's own first non-zero sample
    // is index 1; everything after that is the chain's delay.
    expect(onset - 1).toBe(99);
  });

  it('keeps the drive path aligned with the dry path', () => {
    // The dry path runs through the same FIR delay as the shaper, so engaging
    // drive must not move the signal. Onset cannot measure this — a
    // linear-phase FIR pre-rings ~11 samples ahead of its group delay — so
    // align the two runs by cross-correlation instead. Drive is set light, to
    // keep the comparison close to linear.
    const run = (driveOn: number): Float32Array => {
      const p = sinePatch();
      p['fx.drive.on'] = driveOn;
      p['fx.drive.amt'] = 0.05;
      p['fx.drive.mix'] = 1;
      const h = boot(p);
      h.send({ t: 'noteon', semi: 24, vel: 1 });
      h.render(20);
      return h.render(16).L;
    };
    const a = run(0), b = run(1);
    let best = -Infinity, bestLag = 0;
    for (let lag = -30; lag <= 30; lag++) {
      let c = 0;
      for (let i = 40; i < a.length - 40; i++) c += a[i] * b[i + lag];
      if (c > best) { best = c; bestLag = lag; }
    }
    expect(bestLag).toBe(0);
  });
});

describe('BL-1 drive mono fast path', () => {
  it('resyncs the skipped channel when the voice opens up', () => {
    // A 303 patch is mono through the shaper whenever uni = 1 or spread = 0
    // (review B8), so the right oversampler is skipped and its state mirrored
    // from the left. The moment the channels diverge that state has to be what
    // it would have been had it run all along, or the right channel steps.
    const p = sinePatch();
    p['osc.level'] = 0.9;
    p['sub.level'] = 0.5;
    p['osc.unison'] = 1;
    p['osc.spread'] = 0;
    p['fx.drive.on'] = 1;
    p['fx.drive.amt'] = 1;
    p['fx.drive.mix'] = 1;
    p['master.volume'] = 0.5;
    const h = boot(p);
    h.send({ t: 'noteon', semi: 24, vel: 1 });
    const before = h.render(60);
    // The fast path is only engaged if the two channels really are identical.
    for (let i = 0; i < before.L.length; i++) expect(before.R[i]).toBe(before.L[i]);
    h.send({ t: 'p', k: 'osc.unison', v: 7 });
    h.send({ t: 'p', k: 'osc.spread', v: 0.9 });
    const after = h.render(60);

    const jump = (x: Float32Array): number => {
      let m = 0;
      for (let i = 1; i < x.length; i++) m = Math.max(m, Math.abs(x[i] - x[i - 1]));
      return m;
    };
    const join = (b: Float32Array, a: Float32Array): Float32Array => {
      const j = new Float32Array(512);
      j.set(b.subarray(b.length - 256), 0);
      j.set(a.subarray(0, 256), 256);
      return j;
    };
    // Opening unison and spread is a real, abrupt change of the voice, so both
    // channels step. What matters is that the RIGHT one — the skipped one —
    // steps no harder than the left, which ran throughout. A stale right-hand
    // oversampler shows up here and nowhere else.
    const jL = jump(join(before.L, after.L));
    const jR = jump(join(before.R, after.R));
    expect(jL).toBeGreaterThan(0);
    expect(jR).toBeLessThan(1.2 * jL);
    // ...and the channels really did diverge, so the resync was exercised.
    let diverged = false;
    for (let i = 0; i < after.L.length; i++) if (after.R[i] !== after.L[i]) { diverged = true; break; }
    expect(diverged).toBe(true);
  }, 120_000);
});

describe('BL-1 master limiter holds a hard ceiling (B5)', () => {
  it('never exceeds -1 dBFS on a run of accented steps', () => {
    // The DynamicsCompressorNode this replaces had no ceiling at all: it
    // engaged on most accents and let the peaks through.
    const p = defaultBassParams();
    p['osc.level'] = 1;
    p['osc.unison'] = 7;
    p['sub.level'] = 1;
    p['sub.shape'] = 1;
    p['flt.type'] = 1;
    p['flt.res'] = 0.95;
    p['flt.env'] = 1;
    p['flt.drive'] = 1;
    p['acc.amt'] = 1;
    p['aenv.sus'] = 1;
    p['master.volume'] = 1;
    p['fx.drive.on'] = 1;
    p['fx.drive.amt'] = 1;
    p['fx.drive.mix'] = 1;
    const h = boot(p);
    let pats = makeEmptyPatterns();
    for (let s = 0; s < 16; s++) pats = setStep(pats, 0, s, { on: true, note: s % 12, acc: true });
    h.send({ t: 'pats', data: pats.buffer as ArrayBuffer });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    const { L, R } = h.render(1200); // ~3.2 s
    expect(peak(L)).toBeGreaterThan(0.5); // it is genuinely loud
    expect(peak(L)).toBeLessThanOrEqual(CEILING + 1e-6);
    expect(peak(R)).toBeLessThanOrEqual(CEILING + 1e-6);
  }, 120_000);

  it('holds the ceiling with resonance, filter drive and FX drive all at max', () => {
    // BL-1's ADAA saturator sits BEFORE the filter, so nothing bounds the
    // resonance loop and the limiter is what stops it (review B3). At res = 1
    // the resonant stage is Q ~= 470 and rings for about a second, so this is
    // the patch most likely to run away.
    const p = defaultBassParams();
    p['osc.level'] = 1; p['osc.unison'] = 7; p['osc.spread'] = 0.8;
    p['sub.level'] = 1; p['sub.shape'] = 1;
    p['flt.type'] = 1; p['flt.res'] = 1; p['flt.env'] = 1; p['flt.drive'] = 1; p['flt.cut'] = 200;
    p['acc.amt'] = 1; p['aenv.sus'] = 1; p['master.volume'] = 1;
    p['fx.drive.on'] = 1; p['fx.drive.amt'] = 1; p['fx.drive.mix'] = 1;
    p['fx.chorus.on'] = 0; p['fx.delay.on'] = 0; p['fx.reverb.on'] = 0;
    const h = boot(p);
    let pats = makeEmptyPatterns();
    for (let s = 0; s < 16; s++) pats = setStep(pats, 0, s, { on: true, note: s % 12, acc: true, slide: s % 3 === 0 });
    h.send({ t: 'pats', data: pats.buffer as ArrayBuffer });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    const { L, R } = h.render(2400); // 6.4 s
    for (let i = 0; i < L.length; i++) {
      expect(Number.isFinite(L[i])).toBe(true);
      expect(Number.isFinite(R[i])).toBe(true);
    }
    expect(peak(L)).toBeGreaterThan(0.5);
    expect(peak(L)).toBeLessThanOrEqual(CEILING + 1e-6);
    expect(peak(R)).toBeLessThanOrEqual(CEILING + 1e-6);
  }, 180_000);

  it('never exceeds -1 dBFS on a self-resonant filter peak', () => {
    const p = sinePatch();
    p['flt.type'] = 1;
    p['flt.res'] = 1;
    p['flt.cut'] = subF(24);
    p['master.volume'] = 1;
    const h = boot(p);
    h.send({ t: 'noteon', semi: 24, vel: 1 });
    const { L, R } = h.render(400);
    expect(peak(L)).toBeGreaterThan(0.5);
    expect(peak(L)).toBeLessThanOrEqual(CEILING + 1e-6);
    expect(peak(R)).toBeLessThanOrEqual(CEILING + 1e-6);
  }, 120_000);
});

describe('BL-1 reverb survives a SIZE change (W6)', () => {
  it('keeps the tail continuous', () => {
    // The ConvolverNode this replaces re-rendered its impulse on every SIZE
    // edit and swapping a live buffer cut the tail dead. Freeverb just moves
    // its comb feedback, so the tail bends instead of stopping.
    const p = sinePatch();
    p['aenv.rel'] = 0.02;
    p['fx.reverb.on'] = 1;
    p['fx.reverb.mix'] = 1;
    p['fx.reverb.size'] = 0.2;
    p['master.volume'] = 0.5;
    // Two identical runs; only one gets the SIZE edit. Comparing them takes
    // the tail's own decay out of the measurement.
    const tail = (edit: boolean): { before: Float32Array; after: Float32Array } => {
      const h = boot(p);
      h.send({ t: 'noteon', semi: 24, vel: 1 });
      h.render(40);
      h.send({ t: 'noteoff', semi: 24 });
      h.render(20); // the voice is gone; only the tail is left
      const before = h.render(8).L;
      if (edit) h.send({ t: 'p', k: 'fx.reverb.size', v: 0.95 });
      return { before, after: h.render(8).L };
    };
    const rms = (x: Float32Array): number => Math.sqrt(x.reduce((s, v) => s + v * v, 0) / x.length);
    const edited = tail(true), held = tail(false);
    expect(rms(edited.before)).toBeGreaterThan(1e-4); // there is a tail to protect
    // The edit bends the tail; it must not cut it. A convolver buffer swap
    // takes this to ~0.
    expect(rms(edited.after)).toBeGreaterThan(0.5 * rms(held.after));
    // ...and it does not step: the largest sample-to-sample jump across the
    // join stays inside what the tail itself already does.
    const jump = (x: Float32Array): number => {
      let m = 0;
      for (let i = 1; i < x.length; i++) m = Math.max(m, Math.abs(x[i] - x[i - 1]));
      return m;
    };
    const join = new Float32Array(16);
    join.set(edited.before.subarray(edited.before.length - 8), 0);
    join.set(edited.after.subarray(0, 8), 8);
    expect(jump(join)).toBeLessThanOrEqual(2 * Math.max(jump(edited.before), jump(edited.after)));
  }, 120_000);
});
