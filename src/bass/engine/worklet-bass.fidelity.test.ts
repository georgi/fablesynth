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

function boot(params: ParamValues, sr = 48000): BassHarness {
  const h = makeBassProcessor(sr);
  h.send({ t: 'init', params });
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
