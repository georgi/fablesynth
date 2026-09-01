// Fidelity guards for the DR-1 worklet (docs/audio-engine-review.md §4):
// no step at a retrigger or at the end of the decay, behaviour that does not
// change with the device sample rate, and a reproducible noise path.
import { describe, it, expect } from 'vitest';
import { makeDrumProcessor, type DrumHarness } from './workletHarness';
import { generateDrumTables } from './drumtables';
import { defaultDrumParams, pad } from '../params';
import { makeEmptyPatterns, patIdx, stepDurSamples } from '../seq';
import type { ParamValues } from '../../params';

const tables = generateDrumTables();
const tableMsg = {
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};
const testSamples = Array.from({ length: 5 }, (_, slot) => {
  const data = new Float32Array(4800);
  for (let i = 0; i < data.length; i++) data[i] = Math.sin(i * (0.03 + slot * 0.01)) * (1 - i / data.length);
  return { sampleRate: 48000, buf: data.buffer };
});

function boot(params: ParamValues, sampleRate = 48000): DrumHarness {
  const h = makeDrumProcessor(sampleRate);
  h.send({ t: 'init', params });
  h.send(tableMsg);
  h.send({ t: 'samples', list: testSamples });
  return h;
}

// The click metric: the largest single-sample jump in a window. A hard cut
// shows up here as a step far larger than the waveform's own slew rate.
function maxDelta(x: Float32Array, a: number, b: number): number {
  let m = 0;
  for (let i = Math.max(1, a); i < Math.min(x.length, b); i++) m = Math.max(m, Math.abs(x[i] - x[i - 1]));
  return m;
}

const concat = (...parts: Float32Array[]): Float32Array => {
  const out = new Float32Array(parts.reduce((n, p) => n + p.length, 0));
  let at = 0;
  for (const p of parts) { out.set(p, at); at += p.length; }
  return out;
};

describe('drum click detector', () => {
  it('retriggering a sounding pad does not step the output', () => {
    const p = defaultDrumParams();
    p[pad(0, 'aenv.dec')] = 2; // long tail, so a hard cut would be loud
    p[pad(0, 'penv.amt')] = 0;

    const steady = boot(p);
    steady.send({ t: 'trig', pad: 0, v: 1 });
    const slew = maxDelta(steady.render(20).L, 0, 2560);

    const h = boot(p);
    h.send({ t: 'trig', pad: 0, v: 1 });
    const first = h.render(20).L;
    h.send({ t: 'trig', pad: 0, v: 1 });
    const audio = concat(first, h.render(20).L);
    // The old engine zeroed the amp, the SVF and the DC blocker in one sample
    // and measured ~8.6x the waveform's own slew here.
    expect(maxDelta(audio, 2500, 2700)).toBeLessThan(slew * 2.5);
  });

  it('the amp decay reaches zero instead of stepping down', () => {
    const p = defaultDrumParams();
    p[pad(0, 'aenv.dec')] = 0.2;
    p[pad(0, 'penv.amt')] = 0;
    const h = boot(p);
    h.send({ t: 'trig', pad: 0, v: 1 });
    const x = h.render(160).L;
    const end = Math.round((p[pad(0, 'aenv.att')] + p[pad(0, 'aenv.hold')] + 0.2) * 48000);
    const atEnd = maxDelta(x, end - 60, end + 60);
    const before = maxDelta(x, end - 2000, end - 500);
    expect(Math.abs(x[end - 1])).toBeLessThan(1e-4);
    expect(atEnd).toBeLessThan(before * 0.25); // was a -40 dB step, i.e. ~0.6x
  });
});

describe('drum filter-type crossfade', () => {
  // A type switch swaps which SVF tap is the output; without a crossfade the
  // step is larger than the waveform itself (measured 0.060 against a 0.079
  // peak). The outgoing type now runs on a copy of the state for 3 ms.
  const switchClick = (from: number, to: number): { step: number; peak: number } => {
    const p = defaultDrumParams();
    p[pad(0, 'aenv.dec')] = 3;
    p[pad(0, 'penv.amt')] = 0;
    p[pad(0, 'oscA.level')] = 0.9;
    p[pad(0, 'flt.on')] = 1;
    p[pad(0, 'flt.type')] = from;
    p[pad(0, 'flt.cut')] = 900;
    p[pad(0, 'flt.res')] = 0.5;
    const h = boot(p);
    h.send({ t: 'trig', pad: 0, v: 1 });
    const before = h.render(20).L;
    h.send({ t: 'p', k: pad(0, 'flt.type'), v: to });
    const audio = concat(before, h.render(20).L);
    const peak = (a: number, b: number) => {
      let m = 0;
      for (let i = a; i < b; i++) m = Math.max(m, Math.abs(audio[i]));
      return m;
    };
    return {
      step: maxDelta(audio, 2555, 2760),
      // the loudest of the two regimes: a fade into a much louder type still
      // ramps, and that ramp is not a click
      peak: Math.max(peak(1200, 2560), peak(3000, 4000)),
    };
  };

  it('does not step when the filter type changes', () => {
    for (const [from, to] of [[0, 3], [3, 0], [0, 1]]) {
      const { step, peak } = switchClick(from, to);
      expect(step, `type ${from} -> ${to}`).toBeLessThan(peak * 0.1);
    }
  });
});

// Goertzel magnitude of a Hann-windowed segment; the segments compared below
// hold the same duration at each rate, so the bins are directly comparable.
function bandDb(x: Float32Array, n: number, freq: number, sr: number): number {
  const w = (2 * Math.PI * freq) / sr;
  const c = 2 * Math.cos(w);
  let s1 = 0, s2 = 0;
  for (let i = 0; i < n; i++) {
    const win = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (n - 1));
    const s0 = x[i] * win + c * s1 - s2;
    s2 = s1; s1 = s0;
  }
  return 20 * Math.log10(Math.sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n + 1e-12);
}

const FREQS = [130, 190, 280, 410, 600, 880, 1300, 1900, 2800, 4100];

describe('drum sample-rate invariance', () => {
  const patch = (): ParamValues => {
    const p = defaultDrumParams();
    p[pad(0, 'oscA.level')] = 0.8;
    p[pad(0, 'penv.amt')] = 24;
    p[pad(0, 'penv.dec')] = 0.08;
    p[pad(0, 'aenv.dec')] = 0.4;
    p[pad(0, 'flt.on')] = 1;
    p[pad(0, 'flt.cut')] = 2400;
    p[pad(0, 'flt.res')] = 0.3;
    p[pad(0, 'flt.drive')] = 0.2;
    // exercise both smoothers: the POS smoother and the cutoff smoother
    p[pad(0, 'mod1.src')] = 1; p[pad(0, 'mod1.dst')] = 1; p[pad(0, 'mod1.amt')] = 1;
    p[pad(0, 'mod2.src')] = 1; p[pad(0, 'mod2.dst')] = 4; p[pad(0, 'mod2.amt')] = 0.8;
    p[pad(0, 'modenv.dec')] = 0.15;
    return p;
  };

  const spectrum = (sr: number): number[] => {
    const h = boot(patch(), sr);
    h.send({ t: 'trig', pad: 0, v: 1 });
    const n = Math.round(0.3 * sr);
    const x = h.render(Math.ceil(n / 128) + 1).L;
    return FREQS.map((f) => bandDb(x, n, f, sr));
  };

  it('renders the same spectrum at 44.1, 48 and 96 kHz', () => {
    const ref = spectrum(48000);
    const floor = Math.max(...ref) - 55; // ignore bins sitting on the FP floor
    for (const sr of [44100, 96000]) {
      const other = spectrum(sr);
      for (let i = 0; i < FREQS.length; i++) {
        if (ref[i] < floor) continue;
        expect(Math.abs(other[i] - ref[i]), `${FREQS[i]} Hz at ${sr}`).toBeLessThan(1);
      }
    }
  });
});

describe('drum determinism', () => {
  const noisy = (): ParamValues => {
    const p = defaultDrumParams();
    p[pad(0, 'oscA.level')] = 0.2;
    p[pad(0, 'noise.level')] = 1;
    p[pad(0, 'aenv.dec')] = 0.5;
    // RAND is drawn from the same generator, so this route is covered too
    p[pad(0, 'mod1.src')] = 3; p[pad(0, 'mod1.dst')] = 3; p[pad(0, 'mod1.amt')] = 0.5;
    return p;
  };
  const run = (seed?: number): Float32Array => {
    const h = boot(noisy());
    if (seed !== undefined) h.send({ t: 'seed', v: seed });
    h.send({ t: 'trig', pad: 0, v: 1 });
    return h.render(30).L;
  };

  it('two runs of the same seed are bit-identical', () => {
    expect(Array.from(run())).toEqual(Array.from(run()));
    expect(Array.from(run(1234))).toEqual(Array.from(run(1234)));
    // and the noise is really there
    expect(run().some((v) => v !== 0)).toBe(true);
  });

  it('a different seed gives a different render', () => {
    const a = run(1), b = run(2);
    let diff = 0;
    for (let i = 0; i < a.length; i++) diff += Math.abs(a[i] - b[i]);
    expect(diff / a.length).toBeGreaterThan(1e-5);
  });
});

describe('drum chain clamp', () => {
  it('clamps an out-of-range chain entry instead of playing a silent bar', () => {
    const h = boot(defaultDrumParams());
    const pats = makeEmptyPatterns();
    pats[patIdx(3, 0, 0)] = 2; // last pattern slot
    h.send({ t: 'pats', data: pats.buffer });
    h.send({ t: 'chain', list: [9] }); // out of range → clamped to 3
    h.send({ t: 'play' });
    const dur = stepDurSamples(126, 48000);
    const { L } = h.render(Math.ceil(dur / 128) + 1);
    expect(L.reduce((m, v) => Math.max(m, Math.abs(v)), 0)).toBeGreaterThan(0.01);
  });
});
