// BL-1 worklet fidelity tests — docs/audio-engine-review.md §5.
// These cover the parity fixes the JUCE bass got first: the intra-chunk cutoff
// ramp (B1), the latched/ramped accent (B2), the two-sided sub polyBLEP (B4),
// the drift-free standalone clock (B7), the mono filter path and the
// filter-type state clear (B8), plus sample-rate invariance and a seeded RNG.
import { describe, it, expect } from 'vitest';
import { makeBassProcessor, type BassHarness } from './bassHarness';
import { generateTables } from '../../engine/wavetables';
import { defaultBassParams } from '../params';
import { makeEmptyPatterns, setStep, stepDurSamples } from '../seq';
import type { ParamValues } from '../../params';

const tables = generateTables();
const tableMsg = {
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};

// The master FX rack now runs inside the worklet, so every render passes
// through it. These tests measure the voice, so boot() switches the six wet
// stages off; the rack has its own file (worklet-bass.fx.test.ts). Master gain,
// the DC block and the lookahead limiter always run, exactly as in the plugin.
const FX_OFF = {
  'fx.drive.on': 0, 'fx.comp.on': 0, 'fx.ott.on': 0,
  'fx.chorus.on': 0, 'fx.delay.on': 0, 'fx.reverb.on': 0,
};

function boot(params: ParamValues, sr = 48000): BassHarness {
  const h = makeBassProcessor(sr);
  h.send({ t: 'init', params: { ...params, ...FX_OFF } });
  h.send(tableMsg);
  return h;
}

const peak = (x: Float32Array): number => x.reduce((m, v) => Math.max(m, Math.abs(v)), 0);

// Windowed single-frequency power (Blackman-Harris), so leakage does not
// swamp a sideband 300 Hz away from a strong fundamental.
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

// A sine sub through a resonant sweeping LP24. The filter is linear (drive
// off), so the only thing that can put energy 300-450 Hz away from the
// fundamental is the parameter update rate — 48000/128 = 375 Hz.
function sweepSinePatch(): ParamValues {
  const p = defaultBassParams();
  p['osc.level'] = 0;
  p['sub.level'] = 1;
  p['sub.shape'] = 0;
  p['sub.oct'] = -1;
  p['flt.type'] = 1;
  p['flt.res'] = 0.9;
  p['flt.env'] = 0.85;
  p['flt.cut'] = 150;
  p['flt.drive'] = 0;
  p['fenv.att'] = 0.001;
  p['fenv.dec'] = 0.5;
  p['aenv.att'] = 0.001;
  p['aenv.sus'] = 1;
  p['lfo.depth'] = 0;
  return p;
}

const SUB_F0 = 440 * Math.pow(2, (36 + 24 - 12 - 69) / 12); // ≈ 130.8 Hz

describe('BL-1 filter sweep is free of chunk-rate zipper (B1)', () => {
  it('puts no chunk-rate sideband on a swept resonant filter', () => {
    const h = boot(sweepSinePatch());
    h.send({ t: 'noteon', semi: 24, vel: 1 });
    h.render(8); // skip the attack
    const x = h.render(64).L; // 8192 samples of the sweep
    let side = 0;
    const binF = 48000 / x.length;
    for (let f = SUB_F0 + 300; f <= SUB_F0 + 450; f += binF) side += binPower(x, f, 48000);
    const db = 10 * Math.log10(side / binPower(x, SUB_F0, 48000));
    // Held-per-chunk cutoff measures -34 dB here — a plainly audible buzz.
    expect(db).toBeLessThan(-60);
  });

  it('keeps the sample-to-sample slope bounded through the sweep', () => {
    const h = boot(sweepSinePatch());
    h.send({ t: 'noteon', semi: 24, vel: 1 });
    h.render(8);
    const x = h.render(64).L;
    let maxD = 0;
    for (let i = 1; i < x.length; i++) maxD = Math.max(maxD, Math.abs(x[i] - x[i - 1]));
    // A band-limited sine at SUB_F0 cannot move faster than 2*pi*f/sr per
    // sample; allow 2x for the resonant overshoot, no more.
    const bound = 2 * ((2 * Math.PI * SUB_F0) / 48000) * peak(x);
    expect(maxD).toBeLessThan(bound);
  });
});

describe('BL-1 is sample-rate invariant', () => {
  // The cutoff smoother, the morph smoother and the DC pole all used literal
  // per-call coefficients, so the same patch swept at a different rate.
  const cutTrace = (sr: number): number[] => {
    const h = boot(sweepSinePatch(), sr);
    h.send({ t: 'noteon', semi: 24, vel: 1 });
    const blocks = Math.ceil((sr * 0.25) / 128);
    const every = Math.round(sr * 0.005);
    const out: number[] = [];
    let f = 0;
    for (let b = 0; b < blocks; b++) {
      h.proc.process([], [[new Float32Array(128), new Float32Array(128)]]);
      f += 128;
      if (f >= out.length * every) out.push((h.proc as unknown as { curCut: number }).curCut);
    }
    return out;
  };

  it('sweeps the cutoff along the same curve at 44.1, 48 and 96 kHz', () => {
    const ref = cutTrace(48000);
    for (const sr of [44100, 96000]) {
      const t = cutTrace(sr);
      let dev = 0;
      // Skip the first 40 ms: the 1 ms filter attack is steeper than the 5 ms
      // sampling grid, so the comparison there measures the grid, not the curve.
      for (let i = 8; i < Math.min(ref.length, t.length); i++) {
        dev = Math.max(dev, Math.abs(t[i] - ref[i]) / ref[i]);
      }
      expect(dev, `${sr} Hz`).toBeLessThan(0.05); // was 9 % at 96 kHz
    }
  });

  it('maps the DC blocker pole to the device rate', () => {
    for (const sr of [44100, 48000, 96000]) {
      const h = boot(defaultBassParams(), sr);
      const dcR = (h.proc as unknown as { dcR: number }).dcR;
      expect(dcR).toBeCloseTo(Math.pow(0.9998, 48000 / sr), 12);
      // same corner frequency everywhere
      expect(((1 - dcR) / (2 * Math.PI)) * sr).toBeCloseTo(1.5279, 2);
    }
  });

  it('renders the same amplitude envelope at 44.1, 48 and 96 kHz', () => {
    const env = (sr: number): number[] => {
      const h = boot(sweepSinePatch(), sr);
      h.send({ t: 'noteon', semi: 24, vel: 1 });
      const x = h.render(Math.ceil((sr * 0.4) / 128)).L;
      const w = Math.round(sr * 0.04);
      const out: number[] = [];
      for (let i = 0; i + w <= x.length && out.length < 8; i += w) {
        let s = 0;
        for (let j = i; j < i + w; j++) s += x[j] * x[j];
        out.push(Math.sqrt(s / w));
      }
      return out;
    };
    const ref = env(48000);
    for (const sr of [44100, 96000]) {
      const e = env(sr);
      for (let i = 1; i < ref.length; i++) {
        expect(Math.abs(e[i] - ref[i]) / ref[i], `${sr} Hz window ${i}`).toBeLessThan(0.08);
      }
    }
  });
});

describe('BL-1 renders deterministically', () => {
  const shPatch = (): ParamValues => {
    const p = defaultBassParams();
    p['lfo.shape'] = 4; // sample & hold — the only RNG consumer
    p['lfo.rate'] = 8;
    p['lfo.depth'] = 1;
    p['flt.res'] = 0.6;
    p['aenv.sus'] = 1;
    return p;
  };

  const playPattern = (): Float32Array => {
    const h = boot(shPatch());
    const data = setStep(makeEmptyPatterns(), 0, 0, { on: true, note: 0 }).buffer as ArrayBuffer;
    h.send({ t: 'pats', data });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    return h.render(200).L;
  };

  it('gives bit-identical output for two runs of the same patch', () => {
    const a = playPattern();
    const b = playPattern();
    expect(Array.from(a)).toEqual(Array.from(b));
    expect(peak(a)).toBeGreaterThan(0.01); // the S&H really did modulate
  });

  it('never calls Math.random on the render path', () => {
    const real = Math.random;
    Math.random = (): number => { throw new Error('Math.random in the render path'); };
    try {
      expect(() => playPattern()).not.toThrow();
    } finally {
      Math.random = real;
    }
  });
});

describe('BL-1 standalone clock (B7)', () => {
  it('does not drift against the step grid over hundreds of steps', () => {
    const p = defaultBassParams();
    p['master.swing'] = 0;
    p['seq.bpm'] = 138; // 5217.39 samples/step — a fat fractional residue
    const h = boot(p);
    const data = setStep(makeEmptyPatterns(), 0, 0, { on: true, note: 0 }).buffer as ArrayBuffer;
    h.send({ t: 'pats', data });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });

    const dur = stepDurSamples(138, 48000);
    const STEPS = 400;
    const fires: number[] = [];
    let seen = 0;
    for (let b = 0; fires.length < STEPS && b < 20000; b++) {
      h.proc.process([], [[new Float32Array(128), new Float32Array(128)]]);
      const now = h.sent.filter((m) => m.t === 'step').length;
      for (; seen < now; seen++) fires.push(b * 128);
    }
    expect(fires.length).toBe(STEPS);
    let worst = 0;
    for (let k = 0; k < STEPS; k++) worst = Math.max(worst, Math.abs(fires[k] - k * dur));
    // Every fire lands inside the block that contains its grid time. Dropping
    // the residue costs ~0.6 samples/step: 240 samples adrift by step 400.
    expect(worst).toBeLessThan(128);
  });
});

describe('BL-1 sub oscillator (B4)', () => {
  it('suppresses square-wave aliasing with a two-sided polyBLEP', () => {
    const p = defaultBassParams();
    p['osc.level'] = 0;
    p['sub.level'] = 1;
    p['sub.shape'] = 1;
    p['sub.oct'] = -1;
    p['flt.cut'] = 20000;
    p['flt.env'] = 0;
    p['flt.drive'] = 0;
    p['flt.res'] = 0;
    p['aenv.att'] = 0.001;
    p['aenv.sus'] = 1;
    const h = boot(p);
    h.send({ t: 'noteon', semi: 48, vel: 1 }); // sub ≈ 523 Hz
    h.render(40);
    const x = h.render(64).L;
    const f0 = 440 * Math.pow(2, (36 + 48 - 12 - 69) / 12);
    const binF = 48000 / x.length;
    let sig = 0, alias = 0;
    for (let f = binF; f < 24000; f += binF) {
      const h0 = Math.round(f / f0);
      const isHarm = h0 >= 1 && Math.abs(f - h0 * f0) < 6 * binF;
      if (isHarm) sig += binPower(x, f, 48000);
      else alias += binPower(x, f, 48000);
    }
    // One-sided polyBLEP measures -27 dB on this note.
    expect(10 * Math.log10(alias / sig)).toBeLessThan(-38);
  }, 60000);
});

describe('BL-1 accent (B2)', () => {
  it('ramps an accent that latches on a running voice', () => {
    const p = defaultBassParams();
    p['osc.level'] = 0;
    p['sub.level'] = 1;
    p['sub.shape'] = 0;
    p['flt.cut'] = 20000;
    p['flt.env'] = 0;
    p['flt.drive'] = 0;
    p['flt.res'] = 0;
    p['acc.amt'] = 1;
    p['aenv.att'] = 0.001;
    p['aenv.sus'] = 1;
    p['master.swing'] = 0;
    const h = boot(p);
    let data = makeEmptyPatterns();
    data = setStep(data, 0, 0, { on: true, note: 0 });
    data = setStep(data, 0, 1, { on: true, note: 0, slide: true, acc: true });
    h.send({ t: 'pats', data: data.buffer as ArrayBuffer });
    h.send({ t: 'chain', list: [0] });
    h.send({ t: 'play' });
    const x = h.render(120).L;

    // Quadrature envelope of the pure 32.7 Hz sub sine (quarter period 367
    // samples): an amp-gain step shows up as a one-sample envelope jump.
    const q = 367;
    let worst = 1;
    for (let i = 9 * 128; i + q + 1 < x.length; i++) {
      const e0 = Math.hypot(x[i], x[i + q]);
      const e1 = Math.hypot(x[i + 1], x[i + q + 1]);
      if (e0 > 0.05) worst = Math.max(worst, e1 / e0);
    }
    // Stepping vel*(1 + accAmt*0.7) at a chunk boundary measures 1.12 here.
    expect(worst).toBeLessThan(1.02);
    expect(peak(x)).toBeGreaterThan(0.05);
  });

  it('still makes an accented step audibly louder', () => {
    const play = (acc: boolean): number => {
      const p = defaultBassParams();
      p['acc.amt'] = 1;
      const h = boot(p);
      const data = setStep(makeEmptyPatterns(), 0, 0, { on: true, note: 0, acc }).buffer as ArrayBuffer;
      h.send({ t: 'pats', data });
      h.send({ t: 'chain', list: [0] });
      h.send({ t: 'play' });
      return peak(h.render(30).L);
    };
    expect(play(true)).toBeGreaterThan(play(false) * 1.2);
  });
});

describe('BL-1 filter housekeeping (B8)', () => {
  it('runs a centred patch as one channel and copies it', () => {
    const p = defaultBassParams();
    p['osc.unison'] = 1;
    p['osc.spread'] = 0;
    p['sub.level'] = 0.4;
    p['aenv.sus'] = 1;
    const h = boot(p);
    h.send({ t: 'noteon', semi: 12, vel: 1 });
    const { L, R } = h.render(20);
    expect(peak(L)).toBeGreaterThan(0.01);
    for (let i = 0; i < L.length; i++) expect(R[i]).toBe(L[i]);
  });

  it('clears the dormant LP24 second stage on a filter-type switch', () => {
    // LP24 at high resonance charges the second stage hard. Switching to a
    // 1-pole type parks that stage. Compare two runs that differ only in what
    // played before the switch: with the state cleared they must converge, and
    // switching back to LP24 must not fire the stored energy into the signal.
    const run = (startLp24: boolean): Float32Array => {
      const p = defaultBassParams();
      p['osc.level'] = 0;
      p['sub.level'] = 1;
      p['sub.shape'] = 0;
      p['flt.type'] = startLp24 ? 1 : 4;
      p['flt.res'] = 0.98;
      p['flt.cut'] = startLp24 ? 60 : 12000;
      p['flt.env'] = 0;
      p['flt.drive'] = 0;
      p['aenv.sus'] = 1;
      // Keep the master limiter out of the comparison: it has 200 ms of gain
      // memory, so the loud run would still be releasing when the quiet one is
      // not. This test is about the filter state, so run below the ceiling.
      p['master.volume'] = 0.2;
      const h = boot(p);
      h.send({ t: 'noteon', semi: 24, vel: 1 });
      h.render(40); // ring the ladder up (or not)
      h.send({ t: 'p', k: 'flt.type', v: 4 });
      h.send({ t: 'p', k: 'flt.cut', v: 12000 });
      h.render(300);
      h.send({ t: 'p', k: 'flt.type', v: 1 });
      h.send({ t: 'p', k: 'flt.cut', v: 60 });
      return h.render(8).L;
    };
    const a = run(true), b = run(false);
    let e = 0, s = 0;
    for (let i = 0; i < a.length; i++) { const d = a[i] - b[i]; e += d * d; s += b[i] * b[i]; }
    // Stale state leaves the two runs 19 dB apart.
    expect(10 * Math.log10(e / s)).toBeLessThan(-80);
  });
});

// ---------------------------------------------------------------------------
// B3 — LP24 used to cascade two stages with the same k, so the peak at fc was
// (1/k)^2 (two coincident resonances) and k = 2 - 1.93*res bottomed out at
// Q ~= 14, so the filter never rang. The resonance now lives in stage 1 alone
// with stage 2 critically damped, keeping k1*k2 = (2 - 1.93*resT)^2 so the peak
// magnitude — and therefore existing patches — barely move, while the single
// resonant stage's Q climbs to ~470 at the top of the knob.
// Lockstep with juce/source/dsp/Engine.cpp:738-757.
// ---------------------------------------------------------------------------
describe('BL-1 LP24 resonance (B3)', () => {
  const F0 = 440 * Math.pow(2, (36 + 24 - 12 - 69) / 12); // sub sine, semi 24

  const resPatch = (res: number, ftype: number, fc: number): ParamValues => {
    const p = defaultBassParams();
    p['osc.level'] = 0;
    // The analytical high-Q cases reach +47 dB at fc. Keep the source below
    // the production peak guards so this measures the filter transfer curve,
    // rather than deliberately exercising the FX protection path.
    p['sub.level'] = 0.05;
    p['sub.shape'] = 0;
    p['sub.oct'] = -1;
    p['flt.type'] = ftype;
    p['flt.res'] = res;
    p['flt.cut'] = fc;
    p['flt.env'] = 0;
    p['flt.track'] = 0;
    p['flt.drive'] = 0;
    p['lfo.depth'] = 0;
    p['aenv.att'] = 0.001;
    p['aenv.sus'] = 1;
    p['master.volume'] = 0.05; // stay far under the limiter ceiling
    return p;
  };

  // Amplitude of the sub partial once the filter has settled. Settling is the
  // whole difficulty here: at res = 1 the pole Q is ~470, so at fc = 130.8 Hz
  // the ring decays with tau = Q/(pi*fc) = 1.15 s. 4 s of settle reads 0.27 dB
  // low; 16 s is ~14 tau and lands on the closed form.
  const level = (res: number, ftype: number, fc: number): number => {
    const h = boot(resPatch(res, ftype, fc));
    h.send({ t: 'noteon', semi: 24, vel: 1 });
    h.render(res > 0.95 ? 6000 : res > 0.85 ? 1500 : 200);
    return Math.sqrt(binPower(h.render(32).L, F0, 48000));
  };

  // Gain at the cutoff, in dB: put fc on the partial and reference it against a
  // cutoff far above, where the filter passes unity.
  const gainAtCutDb = (res: number, ftype: number): number =>
    20 * Math.log10(level(res, ftype, F0) / level(res, ftype, F0 * 32));

  // The design target. k1*k2 = (2 - 1.93*resT)^2, so the gain at fc is
  // 1/(k1*k2) — the same closed form the old two-identical-stage filter had as
  // (1/k)^2, which is why patches keep their timbre.
  const expectedDb = (res: number): number => {
    const r = Math.min(0.999, Math.max(0, res));
    const resT = r + 0.0035 * r * r * r * r;
    const kk = 2 - 1.93 * resT;
    const k1 = Math.max(0.002, 0.5 * kk * kk);
    return -20 * Math.log10(k1 * 2);
  };

  // Measured on this build: -12.025, -0.589, +23.497, +47.430 dB, against a
  // closed form of -12.041, -0.591, +23.497, +47.430. The old
  // two-identical-stage filter, same measurement: -12.03, -0.61, +23.19,
  // +45.71 — unchanged where patches live. The point of the change is that
  // stage 1 now carries Q ~= 470 instead of two stages carrying ~14 each.
  it.each([0, 0.5, 0.9, 1.0])('matches 1/(k1*k2) at the cutoff, res %s', (res) => {
    expect(gainAtCutDb(res, 1)).toBeCloseTo(expectedDb(res), 1);
  }, 180_000);

  // The other half of the picture: max_f |H(f)|, the peak anywhere in the
  // response rather than the gain at fc. |H(fc)| above is the preset-fidelity
  // check; this is the "does the knob actually resonate" check, and it reads
  // 0 dB whenever the response has no peak at all. Only the low half of the
  // knob is measured here — above res ~0.75 the peak sits on fc and the test
  // above already covers it to three decimals.
  const peakOverFcDb = (res: number, ftype: number): number => {
    const ref = level(res, ftype, F0 * 32);
    let best = 0;
    for (let k = 0; k <= 28; k++) best = Math.max(best, level(res, ftype, F0 * Math.pow(2, (k / 28) * 5 - 0.15)));
    return 20 * Math.log10(best / ref);
  };

  it.each([
    [0, 0],      // no peak: the response is monotonic
    [0.5, 0.76], // analytic; the old two-stage cascade measured +2.11 here
  ])('peaks nowhere above the passband at res %s (max_f %s dB)', (res, want) => {
    expect(peakOverFcDb(res, 1)).toBeCloseTo(want, 1);
  }, 120_000);

  it('leaves LP12 alone', () => {
    // Only the two-stage type changed; a 12 dB patch keeps k = 2 - 1.93*res,
    // so its gain at fc stays 1/k.
    const k = 2 - 1.93 * 0.62;
    expect(gainAtCutDb(0.62, 0)).toBeCloseTo(-20 * Math.log10(k), 0);
  }, 120_000);

  it('sings at the top of the knob', () => {
    // The old filter topped out at Q ~= 14 and rang out in ~0.3 s. One stage at
    // Q ~= 470 rings for seconds. The amp env gates the filter, so the source
    // is removed mid-note rather than the note released.
    const h = boot(resPatch(1, 1, F0));
    h.send({ t: 'noteon', semi: 24, vel: 1 });
    h.render(4);
    h.send({ t: 'p', k: 'sub.level', v: 0 });
    const { L } = h.render(1200); // 3.2 s
    const win = (at: number): number => {
      let m = 0;
      for (let i = at; i < at + 2048; i++) m = Math.max(m, Math.abs(L[i]));
      return m;
    };
    const start = win(128 * 40);
    expect(start).toBeGreaterThan(1e-5);
    expect(win(L.length - 2048)).toBeGreaterThan(start / 1000); // above -60 dB at 3.2 s
  }, 120_000);
});
