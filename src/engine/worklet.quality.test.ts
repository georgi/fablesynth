import { describe, expect, it } from 'vitest';
import { bootWt, makeWtProcessor } from './workletHarness';
import { fft } from './wavetables';
import { defaultParams, type ParamValues } from '../params';

// Signal-quality tests for the WT-1 worklet (review §6). These are the tests
// that would have caught the fidelity gap between the worklet and the JUCE
// engine: interpolation images, sample-rate-dependent constants, block-rate
// parameter holds, and discontinuities at release / voice steal.

// A steady single voice, one unison, both filters bypassed: the oscillator and
// nothing else, so the only spectral content besides the harmonics is what the
// table read invents.
// Every FX stage off and the master volume low enough that the safety limiter
// never engages, so what these tests measure is the oscillator and not the FX
// chain that now runs inside the worklet (finding W6). With the stages gated
// the chain is still a fixed 99-sample delay plus the DC blocker and the
// limiter's spec makeup gain — all linear, none of which invents spectrum.
const FX_OFF: Partial<ParamValues> = {
  'fx.eq.on': 0, 'fx.drive.on': 0, 'fx.chorus.on': 0,
  'fx.delay.on': 0, 'fx.reverb.on': 0, 'fx.comp.on': 0,
  'master.volume': 0.4,
};

const PURE: Partial<ParamValues> = {
  ...FX_OFF,
  'oscA.on': 1, 'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.unison': 1,
  'oscA.level': 1, 'oscA.detune': 0, 'oscA.spread': 0, 'oscA.pan': 0,
  'oscB.on': 0, 'sub.on': 0, 'noise.on': 0,
  'filter.on': 0, 'filter2.on': 0,
  'env1.a': 0.001, 'env1.d': 0.005, 'env1.s': 1, 'env1.r': 0.1,
};

// 4-term Blackman-Harris. The shipped C++ alias test uses a Hann window with a
// ±6-bin mask, whose leakage floor (~-57 dB) sits far above the images being
// measured — it cannot tell a linear read from a cubic one.
function blackmanHarris(n: number): Float64Array {
  const w = new Float64Array(n);
  const a = [0.35875, 0.48829, 0.14128, 0.01168];
  for (let i = 0; i < n; i++) {
    const t = (2 * Math.PI * i) / (n - 1);
    w[i] = a[0] - a[1] * Math.cos(t) + a[2] * Math.cos(2 * t) - a[3] * Math.cos(3 * t);
  }
  return w;
}

// Worst non-harmonic spectral peak, in dB relative to the fundamental. Bins
// within ±12 of a harmonic (and of DC) are masked out so the window's own
// skirts are not what gets measured.
function aliasFloorDb(x: Float32Array, sampleRate: number, f0: number, n = 16384): number {
  const w = blackmanHarris(n);
  const re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < n; i++) re[i] = x[i] * w[i];
  fft(re, im, false);

  const half = n >> 1;
  const mag = new Float64Array(half);
  for (let k = 0; k < half; k++) mag[k] = Math.hypot(re[k], im[k]);

  const binsPerHz = n / sampleRate;
  const masked = new Uint8Array(half);
  const mark = (center: number) => {
    for (let k = Math.max(0, Math.ceil(center) - 12); k <= Math.min(half - 1, Math.floor(center) + 12); k++) masked[k] = 1;
  };
  mark(0);
  let ref = 0;
  for (let h = 1; h * f0 < sampleRate / 2; h++) {
    const c = h * f0 * binsPerHz;
    mark(c);
    if (h === 1) {
      for (let k = Math.max(0, Math.ceil(c) - 12); k <= Math.min(half - 1, Math.floor(c) + 12); k++) ref = Math.max(ref, mag[k]);
    }
  }
  let worst = 0;
  for (let k = 0; k < half; k++) if (!masked[k]) worst = Math.max(worst, mag[k]);
  return 20 * Math.log10(worst / Math.max(1e-30, ref));
}

function steadyTone(note: number, sampleRate = 48000, n = 16384): Float32Array {
  const h = bootWt(PURE, sampleRate);
  h.send({ t: 'on', n: note, v: 1 });
  const blocks = Math.ceil((n + 4096) / 128);
  const { L } = h.render(blocks);
  return L.subarray(4096, 4096 + n) as Float32Array;
}

describe('WT-1 oscillator aliasing', () => {
  // Measured on this metric: cubic Hermite reads -96.0 / -104.1 / -105.0 dB at
  // notes 36 / 48 / 60, linear reads -83.7 / -89.2 / -96.0 dB. Each threshold
  // sits between the two, so a regression to a linear table read fails here
  // even at the notes where linear still clears the review's -85 dB target.
  const LIMIT: Record<number, number> = { 36: -92, 48: -98, 60: -100 };
  for (const note of [36, 48, 60]) {
    it(`stays below ${LIMIT[note]} dB at note ${note}`, () => {
      const f0 = 440 * Math.pow(2, (note - 69) / 12);
      expect(aliasFloorDb(steadyTone(note), 48000, f0)).toBeLessThan(LIMIT[note]);
    });
  }
});

// Same patch, same musical result, different host conditions. Both of these
// used to change the sound: fixed per-block smoothing coefficients made the
// engine rate-dependent, and block-rate events/holds made it block-size
// dependent.
const SR_PATCH: Partial<ParamValues> = {
  ...PURE,
  'filter.on': 1, 'filter.type': 1, 'filter.cutoff': 3000, 'filter.res': 0.3,
  'noise.on': 0, 'sub.on': 1, 'sub.level': 0.4,
};

// Energy in octave bands from 60 Hz to 16 kHz, in dB. Octave bands (rather
// than third-octave) keep the comparison meaningful across sample rates: a
// fixed 16384-point window has a different bin width at 44.1 and 96 kHz, so a
// narrow band sitting on a partial's skirt would move energy across its edge
// without the engine having changed anything.
function bandLevels(x: Float32Array, sampleRate: number, n = 16384): number[] {
  const w = blackmanHarris(n);
  const re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < n; i++) re[i] = (x[i] || 0) * w[i];
  fft(re, im, false);
  const out: number[] = [];
  for (let f = 60; f < 16000; f *= 2) {
    const lo = Math.floor((f / Math.SQRT2) * n / sampleRate);
    const hi = Math.ceil(f * Math.SQRT2 * n / sampleRate);
    let e = 1e-20;
    for (let k = lo; k <= hi && k < n / 2; k++) e += re[k] * re[k] + im[k] * im[k];
    out.push(10 * Math.log10(e));
  }
  return out;
}

function renderTone(sampleRate: number, blockSize: number, note = 57, secs = 0.6): Float32Array {
  const h = bootWt(SR_PATCH, sampleRate);
  h.send({ t: 'on', n: note, v: 1 });
  const blocks = Math.ceil((secs * sampleRate) / blockSize);
  return h.render(blocks, blockSize).L;
}

describe('WT-1 sample-rate and block-size invariance', () => {
  it('renders the same spectrum at 44.1, 48 and 96 kHz', () => {
    const ref = bandLevels(renderTone(48000, 128).subarray(8192) as Float32Array, 48000);
    // Bands more than 60 dB under the loudest one carry no audible signal —
    // comparing them measures the numeric floor, not the engine.
    const peak = Math.max(...ref);
    for (const sr of [44100, 96000]) {
      const got = bandLevels(renderTone(sr, 128).subarray(Math.round(8192 * sr / 48000)) as Float32Array, sr);
      for (let b = 0; b < ref.length; b++) {
        if (ref[b] < peak - 60) continue;
        expect(Math.abs(got[b] - ref[b]), `${sr} Hz, band ${b}`).toBeLessThan(3.5);
      }
    }
  });

  it('renders the same spectrum in 480, 512 and 4096-sample host blocks', () => {
    const ref = bandLevels(renderTone(48000, 128).subarray(8192) as Float32Array, 48000);
    const peak = Math.max(...ref);
    for (const bs of [480, 512, 4096]) {
      const got = bandLevels(renderTone(48000, bs).subarray(8192) as Float32Array, 48000);
      for (let b = 0; b < ref.length; b++) {
        if (ref[b] < peak - 60) continue;
        expect(Math.abs(got[b] - ref[b]), `${bs}-sample blocks, band ${b}`).toBeLessThan(0.5);
      }
    }
  });
});


// The smoothing constants that used to be fixed per-block/per-sample numbers
// (POS 0.35, cutoff 0.5, steal fade 0.12) all set a TIME, and a 96 kHz context
// halved every one of them. The steal fade is the one with an audible,
// directly measurable duration: a same-note retrigger of a sounding voice fades
// it to silence before the new note starts.
function stealFadeMs(sampleRate: number): number {
  const note = 96; // ~2093 Hz: one period is short enough to track the fade
  const h = bootWt({
    ...PURE, 'oscA.pos': 0.4, 'env1.a': 0.001, 'env1.d': 0.005, 'env1.s': 1, 'env1.r': 1,
  }, sampleRate);
  h.send({ t: 'on', n: note, v: 1 });
  h.render(Math.ceil(sampleRate / 128)); // ~1 s, envelope at sustain
  h.send({ t: 'on', n: note, v: 1 });    // retrigger -> steal fade, then restart
  const { L } = h.render(Math.ceil((sampleRate * 0.02) / 128));
  // The FX chain delays the output by a fixed number of samples (drive FIR +
  // limiter lookahead); the worklet reports it at init. Subtract it so this
  // measures the fade and not the chain.
  const lat = (h.sent.find((m) => m.t === 'latency')!.n as number);
  // Envelope: peak over a one-period sliding window, so the trough between the
  // fade-out and the pending note's attack is located to the sample.
  const per = Math.round(sampleRate / (440 * Math.pow(2, (note - 69) / 12)));
  const span = Math.round(sampleRate * 0.006);
  let best = Infinity, at = 0;
  for (let i = 0; i < span; i++) {
    let m = 0;
    for (let j = i; j < i + per; j++) m = Math.max(m, Math.abs(L[j]));
    if (m < best) { best = m; at = i; }
  }
  return ((at - lat) / sampleRate) * 1000;
}

describe('WT-1 steal fade', () => {
  it('takes the same time in milliseconds at 48 and 96 kHz', () => {
    const a = stealFadeMs(48000), b = stealFadeMs(96000);
    expect(a).toBeGreaterThan(0.5);
    expect(a).toBeLessThan(4);
    expect(b / a).toBeGreaterThan(0.8);
    expect(b / a).toBeLessThan(1.25);
  });
});

// A click is a sample-to-sample step much larger than the waveform's own
// slope. Measure the largest step in the suspect region against the largest
// step while the note is simply sounding.
function maxStep(x: Float32Array, from: number, to: number): number {
  let m = 0;
  for (let i = from + 1; i < to; i++) m = Math.max(m, Math.abs(x[i] - x[i - 1]));
  return m;
}

describe('WT-1 click detector', () => {
  const CLICKY: Partial<ParamValues> = {
    ...PURE,
    'oscA.pos': 0.4,
    'env1.a': 0.005, 'env1.d': 0.2, 'env1.s': 0.9, 'env1.r': 0.2,
  };

  it('does not step on note release', () => {
    const h = bootWt(CLICKY);
    h.send({ t: 'on', n: 55, v: 1 });
    h.render(40);
    h.send({ t: 'off', n: 55 });
    const { L } = h.render(60);
    // The release starts at sample 0 of this render; compare the first 256
    // samples of the tail against the steady slope right after it.
    const atRelease = maxStep(L, 0, 256);
    const steady = maxStep(L, 512, 4096);
    expect(atRelease).toBeLessThan(steady * 1.5 + 1e-4);
  });

  it('does not step when a voice is stolen', () => {
    const h = bootWt(CLICKY);
    for (let i = 0; i < 8; i++) h.send({ t: 'on', n: 40 + i * 2, v: 1 });
    h.render(40);
    const before = (() => { const { L } = h.render(4); return maxStep(L, 0, 512); })();
    h.send({ t: 'on', n: 72, v: 1 }); // 9th note into an 8-voice pool -> steal
    const { L } = h.render(20);
    expect(maxStep(L, 0, 1024)).toBeLessThan(before * 4 + 1e-3);
  });
});

describe('WT-1 determinism', () => {
  it('renders bit-identically twice (seeded RNG, finding W5)', () => {
    const run = () => {
      const h = makeWtProcessor();
      h.send({ t: 'init', params: { ...defaultParams(), 'noise.on': 1, 'noise.type': 1, 'noise.level': 0.5 } });
      h.send({ t: 'on', n: 60, v: 1 });
      return h.render(20).L;
    };
    // Two harnesses evaluate the worklet independently; a seeded RNG makes the
    // renders identical, which is what a cross-engine parity test needs.
    const a = run(), b = run();
    for (let i = 0; i < a.length; i++) expect(a[i]).toBe(b[i]);
  });
});
