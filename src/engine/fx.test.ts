import { describe, expect, it } from 'vitest';
import { bootWt } from './workletHarness';
import { fft } from './wavetables';
import type { ParamValues } from '../params';

// Coverage for the FX chain that moved from native WebAudio nodes into the
// worklet (audio-engine review, finding W6) and for the LP24 resonance retaper
// (finding B3). Both are pure DSP inside worklet.js, so both are testable here
// for the first time — the native graph never was.

const FX_OFF: Partial<ParamValues> = {
  'fx.eq.on': 0, 'fx.drive.on': 0, 'fx.chorus.on': 0,
  'fx.delay.on': 0, 'fx.reverb.on': 0, 'fx.comp.on': 0,
};

// A single steady voice with the filters bypassed.
const TONE: Partial<ParamValues> = {
  ...FX_OFF,
  'oscA.on': 1, 'oscA.table': 0, 'oscA.pos': 0.5, 'oscA.unison': 1,
  'oscA.level': 1, 'oscA.detune': 0, 'oscA.spread': 0, 'oscA.pan': 0,
  'oscB.on': 0, 'sub.on': 0, 'noise.on': 0,
  'filter.on': 0, 'filter2.on': 0,
  'env1.a': 0.001, 'env1.d': 0.005, 'env1.s': 1, 'env1.r': 0.1,
  'master.volume': 0.4,
};

const CEILING = 0.8912509381337456; // -1 dBFS

function maxAbs(x: Float32Array, from = 0, to = x.length): number {
  let m = 0;
  for (let i = from; i < to; i++) m = Math.max(m, Math.abs(x[i]));
  return m;
}

// ---------------------------------------------------------------- latency

describe('FX chain latency', () => {
  it('reports drive FIR group delay + limiter lookahead (99 samples at 48 kHz)', () => {
    const h = bootWt(TONE);
    const lat = h.sent.find((m) => m.t === 'latency');
    expect(lat).toBeDefined();
    // 47-tap half-band (23) + 17-tap at 4x (4) = 27, plus round(0.0015 * 48000) = 72.
    expect(lat!.n).toBe(99);
  });

  it('scales the lookahead with the sample rate', () => {
    const h = bootWt(TONE, 96000);
    // 27 (rate-relative FIRs) + round(0.0015 * 96000) = 144.
    expect(h.sent.find((m) => m.t === 'latency')!.n).toBe(171);
  });

  it('delays the signal by exactly the reported amount', () => {
    const h = bootWt({ ...TONE, 'master.volume': 0.2 });
    // Silence until the note starts, so the first non-zero output sample marks
    // the chain delay. The note is scheduled at the top of the second block.
    const lat = h.sent.find((m) => m.t === 'latency')!.n as number;
    h.render(1);
    h.send({ t: 'on', n: 60, v: 1 });
    const { L } = h.render(4);
    let first = -1;
    for (let i = 0; i < L.length; i++) if (Math.abs(L[i]) > 1e-9) { first = i; break; }
    expect(first).toBe(lat);
  });
});

// ---------------------------------------------------------------- limiter

describe('lookahead limiter', () => {
  it('never lets a sample exceed the -1 dBFS ceiling', () => {
    // Everything loud at once: max master volume, drive hard, the compressor's
    // makeup on top. The old web master stage was a DynamicsCompressor with
    // ratio 14 and no ceiling at all, so this was not true before.
    const h = bootWt({
      ...TONE,
      'master.volume': 1,
      'fx.drive.on': 1, 'fx.drive.amt': 1, 'fx.drive.mix': 1,
      'fx.comp.on': 1, 'fx.comp.gain': 24,
    });
    for (let i = 0; i < 8; i++) h.send({ t: 'on', n: 36 + i * 4, v: 1 });
    const { L, R } = h.render(400);
    expect(maxAbs(L)).toBeLessThanOrEqual(CEILING + 1e-6);
    expect(maxAbs(R)).toBeLessThanOrEqual(CEILING + 1e-6);
    // and it is actually being driven into limiting, not just quiet
    expect(maxAbs(L)).toBeGreaterThan(0.5);
  });

  it('holds the ceiling through a step transient (the lookahead is real)', () => {
    const h = bootWt({ ...TONE, 'master.volume': 1 });
    h.render(20);
    for (let i = 0; i < 8; i++) h.send({ t: 'on', n: 48 + i * 3, v: 1 });
    const { L } = h.render(40); // attack edge lands inside this window
    expect(maxAbs(L)).toBeLessThanOrEqual(CEILING + 1e-6);
  });
});

// ---------------------------------------------------------------- drive

// Worst non-harmonic peak, in dB relative to the fundamental.
function aliasDb(x: Float32Array, sr: number, f0: number, n = 16384): number {
  const re = new Float64Array(n), im = new Float64Array(n);
  const a = [0.35875, 0.48829, 0.14128, 0.01168];
  for (let i = 0; i < n; i++) {
    const t = (2 * Math.PI * i) / (n - 1);
    re[i] = x[i] * (a[0] - a[1] * Math.cos(t) + a[2] * Math.cos(2 * t) - a[3] * Math.cos(3 * t));
  }
  fft(re, im, false);
  const half = n >> 1;
  const mag = new Float64Array(half);
  for (let k = 0; k < half; k++) mag[k] = Math.hypot(re[k], im[k]);
  const bins = n / sr;
  const masked = new Uint8Array(half);
  const mark = (c: number) => {
    for (let k = Math.max(0, Math.ceil(c) - 12); k <= Math.min(half - 1, Math.floor(c) + 12); k++) masked[k] = 1;
  };
  mark(0);
  let ref = 0;
  for (let hh = 1; hh * f0 < sr / 2; hh++) {
    const c = hh * f0 * bins;
    mark(c);
    if (hh === 1) for (let k = Math.max(0, Math.ceil(c) - 12); k <= Math.min(half - 1, Math.floor(c) + 12); k++) ref = Math.max(ref, mag[k]);
  }
  let worst = 0;
  for (let k = 0; k < half; k++) if (!masked[k]) worst = Math.max(worst, mag[k]);
  return 20 * Math.log10(worst / Math.max(1e-30, ref));
}

describe('drive stage', () => {
  it('keeps its alias floor low on a high, hard-driven tone', () => {
    // SINE table, note 96 (~2093 Hz) at full drive: the tanh generates
    // harmonics well past Nyquist, so anything the 4x oversampler misses folds
    // back as a non-harmonic peak. The old path was a 513-point WaveShaper
    // table at 2x.
    const h = bootWt({
      ...TONE, 'oscA.table': 0, 'oscA.pos': 0,
      'master.volume': 0.3,
      'fx.drive.on': 1, 'fx.drive.amt': 1, 'fx.drive.mix': 1,
    });
    h.send({ t: 'on', n: 96, v: 1 });
    const { L } = h.render(Math.ceil((16384 + 4096) / 128));
    const f0 = 440 * Math.pow(2, (96 - 69) / 12);
    expect(aliasDb(L.subarray(4096, 4096 + 16384) as Float32Array, 48000, f0)).toBeLessThan(-60);
  });

  it('is time-aligned with its dry path (mix sweeps do not comb)', () => {
    // At MIX 0 with the stage ON, the output must equal the gated-off output
    // sample for sample: the dry path runs through the same DRIVE_LATENCY delay.
    const run = (on: number, mix: number) => {
      const h = bootWt({ ...TONE, 'master.volume': 0.2, 'fx.drive.on': on, 'fx.drive.amt': 0.7, 'fx.drive.mix': mix });
      h.send({ t: 'on', n: 60, v: 1 });
      return h.render(60).L;
    };
    const off = run(0, 0), wetZero = run(1, 0);
    let worst = 0, peak = 0;
    for (let i = 0; i < off.length; i++) {
      worst = Math.max(worst, Math.abs(off[i] - wetZero[i]));
      peak = Math.max(peak, Math.abs(off[i]));
    }
    expect(peak).toBeGreaterThan(0.01);
    // EXACTLY equal, not approximately: entering silence snaps the dry gain to
    // 1, which is its exact target for OFF and for MIX 0 alike, so the skipped
    // path multiplies by one rather than by 1 - eps. Without that snap this
    // reads ~2e-6.
    expect(worst).toBe(0);
  });
});

describe('drive mono fast path', () => {
  // While L and R carry identical samples the drive runs once and the
  // right-hand filters are left idle, resynced from the left the moment the
  // channels diverge. WT-1 sits here whenever unison is 1 and SPREAD and PAN
  // are 0, which is most single-oscillator patches.
  const MONO_DRIVEN: Partial<ParamValues> = {
    ...TONE, 'master.volume': 0.5,
    'oscA.unison': 1, 'oscA.spread': 0, 'oscA.pan': 0,
    'fx.drive.on': 1, 'fx.drive.amt': 1, 'fx.drive.mix': 1,
  };

  it('keeps the channels bit-identical while the input is centred', () => {
    const h = bootWt(MONO_DRIVEN);
    h.send({ t: 'on', n: 55, v: 0.95 });
    const { L, R } = h.render(120);
    let worst = 0, peak = 0;
    for (let i = 0; i < L.length; i++) {
      worst = Math.max(worst, Math.abs(L[i] - R[i]));
      peak = Math.max(peak, Math.abs(L[i]));
    }
    expect(peak).toBeGreaterThan(0.1); // the drive is genuinely working
    expect(worst).toBe(0);
  });

  it('resyncs the right channel to exactly what a running filter would hold', () => {
    // Render the same patch twice, mono for 60 blocks and then panned hard one
    // way or the other. In the +0.8 run the right-hand drive filters idled
    // through the mono stretch and are resynced from the left; in the -0.8 run
    // the LEFT filters ran throughout. The pan law is symmetric and the rest of
    // the chain is off, so the two must agree sample for sample — which is the
    // resync invariant stated directly rather than inferred from a step size.
    const run = (pan: number) => {
      const h = bootWt(MONO_DRIVEN);
      h.send({ t: 'on', n: 55, v: 0.95 });
      h.render(60);
      h.send({ t: 'p', k: 'oscA.pan', v: pan });
      return h.render(40);
    };
    const a = run(0.8), b = run(-0.8);
    let worst = 0, peak = 0;
    for (let i = 0; i < a.R.length; i++) {
      worst = Math.max(worst, Math.abs(a.R[i] - b.L[i]));
      peak = Math.max(peak, Math.abs(b.L[i]));
    }
    expect(peak).toBeGreaterThan(0.1);
    expect(worst).toBe(0); // stale state instead of a resync measures 0.243 here
  });
});

// ---------------------------------------------------------------- reverb

describe('reverb', () => {
  it('keeps its tail continuous across a SIZE change', () => {
    // The web build re-rendered a ConvolverNode impulse on every SIZE change
    // and the buffer swap cut the tail dead. Freeverb only moves the comb
    // feedback, so the tail bends instead of dropping out.
    const h = bootWt({
      ...TONE, 'master.volume': 0.3,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.8, 'fx.reverb.mix': 1,
    });
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(40);
    h.send({ t: 'off', n: 60 });
    h.render(60); // note released, only the tail is left
    const before = h.render(20).L;
    h.send({ t: 'p', k: 'fx.reverb.size', v: 0.2 });
    const after = h.render(40).L;
    // Measured LATE in the window, not as its peak: a tail that is cut at the
    // swap still peaks in the first samples after it, so a peak test cannot see
    // the drop. Healthy: 0.50 of the pre-change RMS. With the combs rebuilt on
    // the SIZE change, as the ConvolverNode path did: 0.0009.
    const rms = (x: Float32Array, a: number, b: number) => {
      let s2 = 0;
      for (let i = a; i < b; i++) s2 += x[i] * x[i];
      return Math.sqrt(s2 / (b - a));
    };
    const rmsBefore = rms(before, 0, before.length);
    expect(rmsBefore).toBeGreaterThan(1e-4);
    expect(rms(after, 20 * 128, after.length) / rmsBefore).toBeGreaterThan(0.1);
    // and no step discontinuity at the swap
    expect(Math.abs(after[0] - before[before.length - 1])).toBeLessThan(0.05);
  });
});

// ---------------------------------------------------------------- bypass

describe('FX bypass', () => {
  it('is linear when every stage is off', () => {
    // Gated stages must not colour the signal: a level change in must produce
    // exactly the same change out (delay, DC block and makeup gain are all
    // linear). Any residual nonlinearity would mean a stage is still running.
    const run = (vol: number) => {
      const h = bootWt({ ...TONE, 'master.volume': vol });
      h.send({ t: 'on', n: 60, v: 1 });
      return h.render(80).L;
    };
    // masterGain = vol^2 * 1.6, so halving the volume is exactly a 0.25x gain.
    const a = run(0.2), b = run(0.1);
    let worst = 0, ref = 0;
    for (let i = 0; i < a.length; i++) {
      worst = Math.max(worst, Math.abs(a[i] * 0.25 - b[i]));
      ref = Math.max(ref, Math.abs(b[i]));
    }
    expect(ref).toBeGreaterThan(1e-3);
    expect(worst).toBeLessThan(ref * 1e-4);
  });
});

// ---------------------------------------------------------------- B3

// Filter patch shared by the measurements below. The noise source is seeded, so
// two renders of the same patch produce the same samples.
function filterPatch(res: number, type: number, filterOn: number, noise = 0.02): Partial<ParamValues> {
  return {
    ...FX_OFF, 'oscA.on': 0, 'oscB.on': 0, 'sub.on': 0,
    'noise.on': 1, 'noise.type': 0, 'noise.level': noise,
    'filter.on': filterOn, 'filter.type': type, 'filter.cutoff': 1000, 'filter.res': res,
    'filter.drive': 0, 'filter.env': 0, 'filter.key': 0, 'filter2.on': 0,
    'env1.a': 0.001, 'env1.d': 0.001, 'env1.s': 1, 'env1.r': 5,
    'master.volume': 0.15,
  };
}

// |H(f)| of the filter, exactly: render the SAME deterministic noise with the
// filter in and out of circuit and take the per-bin power ratio. The source
// cancels, so there is no periodogram-averaging resolution penalty — unlike
// svfPeakDb below, which is limited by its bin width. The power ratio is then
// averaged over +-4 bins, because the source cancels but the finite-segment
// truncation error does not. (Method from juce/test/engine_test.cpp 4b.)
function svfMagDb(res: number, type = 1): { atFc: number } {
  const N = 1 << 16;
  const settle = res >= 0.9 ? 16 : 2; // a Q 470 resonance takes ~1 s to build
  const run = (on: number) => {
    const h = bootWt(filterPatch(res, type, on));
    h.send({ t: 'on', n: 60, v: 1 });
    h.render(Math.ceil((48000 * settle) / 128));
    const { L } = h.render(Math.ceil(N / 128));
    const re = new Float64Array(N), im = new Float64Array(N);
    for (let i = 0; i < N; i++) re[i] = L[i];
    fft(re, im, false);
    const p = new Float64Array(N >> 1);
    for (let k = 0; k < N >> 1; k++) p[k] = re[k] * re[k] + im[k] * im[k];
    return p;
  };
  const a = run(1), b = run(0);
  const k0 = Math.round((1000 * N) / 48000);
  let sum = 0;
  for (let j = -4; j <= 4; j++) sum += a[k0 + j] / Math.max(b[k0 + j], 1e-300);
  return { atFc: 10 * Math.log10(sum / 9) };
}

// Seconds for the filter's resonance to decay 60 dB once the excitation stops.
// The note is held at sustain and NOISE LEVEL is taken to zero, so the voice
// stays open and what decays is the filter alone.
function ringDownSeconds(res: number): number {
  const h = bootWt(filterPatch(res, 1, 1));
  h.send({ t: 'on', n: 60, v: 1 });
  h.render(Math.ceil((48000 * 16) / 128));
  h.send({ t: 'p', k: 'noise.level', v: 0 });
  const { L } = h.render(Math.ceil((48000 * 3) / 128));
  const per = 48; // one cycle at the 1 kHz cutoff
  let peak = 0;
  for (let i = 0; i < per * 4; i++) peak = Math.max(peak, Math.abs(L[i]));
  const floor = peak / 1000; // -60 dB
  for (let i = 0; i + per < L.length; i += per) {
    let m = 0;
    for (let j = i; j < i + per; j++) m = Math.max(m, Math.abs(L[j]));
    if (m < floor) return i / 48000;
  }
  return Infinity;
}

// Averaged periodogram of the engine's output with a single filter in circuit
// and white noise as the source. The FX chain moved into the worklet, so the
// filter's response is measurable through the real render path.
function svfSpectrum(res: number, type: number, N: number): Float64Array {
  const h = bootWt({
    ...FX_OFF,
    'oscA.on': 0, 'oscB.on': 0, 'sub.on': 0,
    'noise.on': 1, 'noise.type': 0, 'noise.level': 0.02,
    'filter.on': 1, 'filter.type': type, 'filter.cutoff': 1000, 'filter.res': res,
    'filter.drive': 0, 'filter.env': 0, 'filter.key': 0, 'filter2.on': 0,
    'env1.a': 0.001, 'env1.d': 0.001, 'env1.s': 1, 'env1.r': 0.1,
    'master.volume': 0.15,
  });
  h.send({ t: 'on', n: 60, v: 1 });
  h.render(200); // settle the cutoff smoother
  const { L } = h.render(Math.ceil((N * 8) / 128));
  // 8 averaged periodograms — white noise needs the averaging to give a
  // readable peak.
  const acc = new Float64Array(N >> 1);
  for (let s = 0; s < 8; s++) {
    const re = new Float64Array(N), im = new Float64Array(N);
    for (let i = 0; i < N; i++) {
      const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (N - 1));
      re[i] = L[s * N + i] * w;
    }
    fft(re, im, false);
    for (let k = 0; k < N >> 1; k++) acc[k] += re[k] * re[k] + im[k] * im[k];
  }
  return acc;
}

// Height of the resonant peak, in dB above the passband.
function svfPeakDb(res: number, type = 1): number {
  const N = 16384;
  const acc = svfSpectrum(res, type, N);
  const bin = (f: number) => Math.round((f * N) / 48000);
  let sum = 0, cnt = 0;
  for (let k = bin(150); k <= bin(400); k++) { sum += acc[k]; cnt++; }
  const ref = sum / cnt;
  let peak = 0;
  for (let k = bin(500); k <= bin(2000); k++) peak = Math.max(peak, acc[k]);
  return 10 * Math.log10(peak / ref);
}

// Width of the resonant peak, in FFT bins within 3 dB of it. Distinguishes one
// high-Q pole from two coincident lower-Q ones, which the height cannot.
function peakWidthBins(res: number, type = 1): number {
  const N = 16384;
  const acc = svfSpectrum(res, type, N);
  const bin = (f: number) => Math.round((f * N) / 48000);
  let peak = 0;
  for (let k = bin(500); k <= bin(2000); k++) peak = Math.max(peak, acc[k]);
  let n = 0;
  for (let k = bin(500); k <= bin(2000); k++) if (acc[k] > peak * 0.5) n++;
  return n;
}

describe('LP24 resonance (B3)', () => {
  // Analytic peak of the shipped formula (TPT SVF, fc = 1 kHz, 48 kHz):
  //   RES 0 -> 0.0 dB, 0.5 -> +0.8 dB, 0.9 -> +23.5 dB, 1.0 -> +47.3 dB.
  // The old cascade gave 0.0 / +2.1 / +23.4 / +45.7, so the magnitude is
  // deliberately almost unchanged; what changed is WHERE the resonance lives —
  // one stage at Q 470 at the top of the knob instead of two at Q 14. Measured
  // here through the engine on noise, so the tolerances are loose.
  // |H(fc)| is the "do existing presets still sound the same" measure, and the
  // taper was chosen to reproduce the old cascade's magnitude, so these numbers
  // are nearly unchanged by design. Analytic (TPT SVF, fc 1 kHz, 48 kHz):
  // -12.04 / -8.73 / -0.59 / +23.50 dB. Measured by source cancellation, which
  // lands within 0.6 dB of analytic without a bin-width compromise; measured
  // here: -11.84 / -8.52 / -0.34 / +24.02.
  const AT_FC: [number, number][] = [[0, -12.04], [0.18, -8.73], [0.5, -0.59], [0.9, 23.5]];
  for (const [res, want] of AT_FC) {
    it(`has |H(fc)| near ${want} dB at RES ${res}`, () => {
      const got = svfMagDb(res).atFc;
      expect(got).toBeGreaterThan(want - 1);
      expect(got).toBeLessThan(want + 1);
    });
  }

  it('rings for about a second at the top of the knob', () => {
    // RES 1 is where the topology change shows, and where a magnitude
    // measurement cannot follow it: a 2.1 Hz resonance is narrower than any
    // practical FFT bin, so the spectrum under-reads the +47.4 dB peak by 4 dB.
    // The ring-down is both easier to measure and the more meaningful property.
    // Analytic 1.012 s; measured 1.011 s here and 1.035 s through the plugin.
    // With the old identical-k cascade restored this reads 0.048 s.
    const t60 = ringDownSeconds(0.999);
    expect(t60).toBeGreaterThan(0.5);
    expect(t60).toBeLessThan(2);
  });

  it('rises monotonically across the knob', () => {
    const pts = [0, 0.25, 0.5, 0.75, 1].map((r) => svfPeakDb(r));
    for (let i = 1; i < pts.length; i++) expect(pts[i]).toBeGreaterThan(pts[i - 1] - 0.5);
    // and the top of the knob is where the resonance lives
    expect(pts[4] - pts[3]).toBeGreaterThan(20);
  });

  it('puts the resonance in one stage, not two (the peak is narrow)', () => {
    // The peak HEIGHT cannot tell the two formulas apart — the taper was chosen
    // to reproduce the old cascade's magnitude — but its WIDTH can. One pole at
    // Q 71 is a few bins wide at RES 0.9; two coincident poles at Q 3.8 are
    // ~19. Measured: 3 bins now, 19 with the old identical-k cascade.
    expect(peakWidthBins(0.9)).toBeLessThan(8);
  });

  it('leaves the other SVF types on the old damping', () => {
    // LP12 keeps k = 2 - 1.93*res untouched, so at RES 1 it peaks at
    // 1/0.0719 = +22.9 dB, not LP24's +39.9. A regression that applied the new
    // taper to every type would show up here.
    const lp12 = svfPeakDb(1, 0);
    expect(lp12).toBeGreaterThan(19);
    expect(lp12).toBeLessThan(27);
  });
});
