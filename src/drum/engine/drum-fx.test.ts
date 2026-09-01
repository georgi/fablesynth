// DR-1 FX rack, now that it runs inside the worklet (review W6): the 4x
// oversampled drive, the shared per-bus reverb, the per-bus limiter, the idle
// gate and the reported chain latency. None of this was testable while the FX
// were a graph of native WebAudio nodes.
import { describe, it, expect } from 'vitest';
import { makeDrumProcessor, BUS_COUNT, type DrumHarness } from './workletHarness';
import { generateDrumTables } from './drumtables';
import { defaultDrumParams, pad, PAD_COUNT } from '../params';
import type { ParamValues } from '../../params';
import { fft } from '../../engine/wavetables';

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

// Every FX stage off, so a test can switch on exactly the one it measures.
function bare(): ParamValues {
  const p = defaultDrumParams();
  for (let i = 0; i < PAD_COUNT; i++) {
    p[pad(i, 'fx.comp.on')] = 0;
    p[pad(i, 'fx.reverb.on')] = 0;
    p[pad(i, 'fx.drive.on')] = 0;
    p[pad(i, 'fx.chorus.on')] = 0;
    p[pad(i, 'fx.delay.on')] = 0;
  }
  return p;
}

const peak = (x: Float32Array, a = 0, b = x.length): number => {
  let m = 0;
  for (let i = a; i < Math.min(b, x.length); i++) m = Math.max(m, Math.abs(x[i]));
  return m;
};
const maxDelta = (x: Float32Array, a: number, b: number): number => {
  let m = 0;
  for (let i = Math.max(1, a); i < Math.min(x.length, b); i++) m = Math.max(m, Math.abs(x[i] - x[i - 1]));
  return m;
};

describe('DR-1 chain latency', () => {
  // Same figure the plugin reports (Fx.h kDriveLatency + the limiter lookahead):
  // the dry path is delayed by the shaper's FIR group delay whether the drive
  // is on, off or gated, so the number never moves at run time.
  it('reports drive FIR + limiter lookahead, 99 samples at 48 kHz', () => {
    expect(boot(bare()).latency).toBe(99);
  });

  it('scales the lookahead with the sample rate', () => {
    expect(boot(bare(), 44100).latency).toBe(27 + Math.round(0.0015 * 44100));
    expect(boot(bare(), 96000).latency).toBe(27 + Math.round(0.0015 * 96000));
  });
});

// ---- alias metric, same shape as src/engine/worklet.quality.test.ts ----
function blackmanHarris(n: number): Float64Array {
  const w = new Float64Array(n);
  const a = [0.35875, 0.48829, 0.14128, 0.01168];
  for (let i = 0; i < n; i++) {
    const t = (2 * Math.PI * i) / (n - 1);
    w[i] = a[0] - a[1] * Math.cos(t) + a[2] * Math.cos(2 * t) - a[3] * Math.cos(3 * t);
  }
  return w;
}

// Worst non-harmonic spectral peak in dB relative to the fundamental; bins
// within ±12 of a harmonic (and of DC) are masked so the window's own skirts
// are not what gets measured.
function aliasFloorDb(x: Float32Array, sampleRate: number, f0: number, n: number): number {
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
    if (h === 1) for (let k = Math.max(0, Math.ceil(c) - 12); k <= Math.min(half - 1, Math.floor(c) + 12); k++) ref = Math.max(ref, mag[k]);
  }
  let worst = 0;
  for (let k = 0; k < half; k++) if (!masked[k]) worst = Math.max(worst, mag[k]);
  return 20 * Math.log10(worst / Math.max(1e-30, ref));
}

describe('DR-1 drive aliasing', () => {
  // A held tone at 1055 Hz: 48000/1055 = 45.5, so every folded partial lands
  // half way between two harmonics and cannot hide under the mask.
  const F0 = 440 * Math.pow(2, (60 + 24 + 0.14 - 69) / 12);
  const N = 8192;

  const tone = (driveOn: number, amt: number): Float32Array => {
    const p = bare();
    p[pad(0, 'oscA.tune')] = 24;
    p[pad(0, 'oscA.fine')] = 14;
    p[pad(0, 'oscA.level')] = 0.9;
    p[pad(0, 'oscB.level')] = 0;
    p[pad(0, 'penv.amt')] = 0;
    p[pad(0, 'aenv.att')] = 0.0005;
    p[pad(0, 'aenv.hold')] = 0.25; // flat top, long enough for the window
    p[pad(0, 'v2l')] = 0;
    p[pad(0, 'fx.drive.on')] = driveOn;
    p[pad(0, 'fx.drive.amt')] = amt;
    p[pad(0, 'fx.drive.mix')] = 1;
    const h = boot(p);
    h.send({ t: 'trig', pad: 0, v: 1 });
    const { L } = h.render(Math.ceil((2048 + N) / 128) + 1);
    return L.subarray(2048, 2048 + N) as Float32Array;
  };

  it('keeps the 4x oversampled shaper below -70 dBc', () => {
    // Measured here: -50.6 dB with the drive off (the engine's own floor at
    // this note), -76.6 dB of extra images with the drive at 0.6 through the
    // 4x path. A 1x shaper folds every generated harmonic straight back and
    // reads about -30 dB.
    const off = aliasFloorDb(tone(0, 0.6), 48000, F0, N);
    const on = aliasFloorDb(tone(1, 0.6), 48000, F0, N);
    expect(on).toBeLessThan(-70);
    expect(on).toBeLessThan(off + 6); // the drive adds no visible image floor
  });

  it('does not click when AMT moves under a sounding pad', () => {
    // The shaper runs a block at a time under one set of gains, which is exact
    // only while those gains are block-rate. If a coefficient ever starts
    // ramping inside a block, the block form diverges by ~0.3 full scale and
    // this is where it shows up.
    const p = bare();
    p[pad(0, 'oscA.tune')] = 24;
    p[pad(0, 'penv.amt')] = 0;
    p[pad(0, 'aenv.att')] = 0.001;
    p[pad(0, 'aenv.hold')] = 0.25;
    p[pad(0, 'fx.drive.on')] = 1;
    p[pad(0, 'fx.drive.amt')] = 0.1;
    p[pad(0, 'fx.drive.mix')] = 1;
    const h = boot(p);
    h.send({ t: 'trig', pad: 0, v: 1 });
    h.render(20);
    for (const amt of [0.3, 0.55, 0.8, 0.95]) {
      h.send({ t: 'p', k: pad(0, 'fx.drive.amt'), v: amt });
      const x = h.render(4).L;
      // A harder drive is legitimately a steeper waveform, so the boundary is
      // measured against the new setting's own slew, not the old one's.
      const boundary = maxDelta(x, 0, 64);
      const steady = maxDelta(x, 128, 512);
      expect(boundary, `amt ${amt}`).toBeLessThan(steady * 2);
      expect(peak(x)).toBeLessThan(1);
    }
  });

  it('stays clean at full drive', () => {
    expect(aliasFloorDb(tone(1, 1), 48000, F0, N)).toBeLessThan(-65);
  });
});

describe('DR-1 bus limiter', () => {
  const CEILING = 0.8912509381337456; // -1 dBFS

  it('never exceeds -1 dBFS with all sixteen pads hitting at once', () => {
    const p = defaultDrumParams();
    for (let i = 0; i < PAD_COUNT; i++) {
      p[pad(i, 'lvl')] = 1;
      p[pad(i, 'oscA.level')] = 1;
      p[pad(i, 'aenv.dec')] = 1;
      p[pad(i, 'fx.comp.gain')] = 12; // worst case: full makeup on every pad
    }
    p['master.volume'] = 1;
    const h = boot(p);
    for (let i = 0; i < PAD_COUNT; i++) h.send({ t: 'trig', pad: i, v: 1 });
    const { L, R } = h.renderBus(120, 0);
    // sixteen pads summing on one bus really do overshoot without a ceiling
    expect(peak(L)).toBeGreaterThan(0.5);
    expect(peak(L)).toBeLessThanOrEqual(CEILING + 1e-6);
    expect(peak(R)).toBeLessThanOrEqual(CEILING + 1e-6);
  });

  it('limits every bus independently', () => {
    const p = defaultDrumParams();
    for (let i = 0; i < PAD_COUNT; i++) {
      p[pad(i, 'lvl')] = 1;
      p[pad(i, 'oscA.level')] = 1;
      p[pad(i, 'aenv.dec')] = 1;
      p[pad(i, 'out')] = i % BUS_COUNT;
    }
    p['master.volume'] = 1;
    const h = boot(p);
    for (let i = 0; i < PAD_COUNT; i++) h.send({ t: 'trig', pad: i, v: 1 });
    const outs = Array.from({ length: BUS_COUNT }, () => [new Float32Array(128), new Float32Array(128)]);
    let worst = 0;
    for (let b = 0; b < 120; b++) {
      h.proc.process([], outs);
      for (const [l, r] of outs) worst = Math.max(worst, peak(l), peak(r));
    }
    expect(worst).toBeLessThanOrEqual(CEILING + 1e-6);
  });
});

describe('DR-1 shared reverb', () => {
  const verbPatch = (size: number): ParamValues => {
    const p = bare();
    p[pad(0, 'aenv.dec')] = 0.05;
    p[pad(0, 'penv.amt')] = 0;
    p[pad(0, 'fx.reverb.on')] = 1;
    p[pad(0, 'fx.reverb.mix')] = 1;
    p[pad(0, 'fx.reverb.size')] = size;
    return p;
  };

  // The old graph quantised SIZE into six buckets so pads could share a
  // convolver; the knob stepped in ~0.75 s jumps. A Freeverb takes SIZE as a
  // continuous parameter, so neighbouring values must give neighbouring tails.
  it('responds continuously to SIZE', () => {
    const tail = (size: number): Float32Array => {
      const h = boot(verbPatch(size));
      h.send({ t: 'trig', pad: 0, v: 1 });
      h.render(20);
      return h.render(60).L;
    };
    const a = tail(0.4), near = tail(0.41), far = tail(0.62);
    const rms = (x: Float32Array, y: Float32Array): number => {
      let s = 0;
      for (let i = 0; i < x.length; i++) s += (x[i] - y[i]) ** 2;
      return Math.sqrt(s / x.length);
    };
    expect(rms(a, near)).toBeGreaterThan(0); // not quantised away
    expect(rms(a, near)).toBeLessThan(rms(a, far) * 0.4); // and proportionate
  });

  it('does not cut the tail when SIZE changes', () => {
    const h = boot(verbPatch(0.3));
    h.send({ t: 'trig', pad: 0, v: 1 });
    const before = h.render(60).L;
    const level = peak(before, before.length - 1280);
    expect(level).toBeGreaterThan(1e-4); // there is a tail to protect
    h.send({ t: 'p', k: pad(0, 'fx.reverb.size'), v: 0.9 });
    const after = h.render(60).L;
    // the tail carries on at a comparable level, and does not step
    expect(peak(after, 0, 1280)).toBeGreaterThan(level * 0.3);
    expect(maxDelta(after, 0, 640)).toBeLessThan(maxDelta(before, before.length - 1280, before.length) * 4);
  });

  it('runs one reverb per bus, shared by the pads on it', () => {
    // Two pads on MAIN with reverb on share one network: the state object is
    // per bus, not per pad.
    const h = boot(verbPatch(0.5));
    const st = h.proc as unknown as { verbs: unknown[]; padFx: unknown[] };
    expect(st.verbs.length).toBe(BUS_COUNT);
    expect(st.padFx.length).toBe(PAD_COUNT);
  });
});

describe('DR-1 FX idle gate', () => {
  // The gate is what makes sixteen chains affordable, and the one thing it
  // must never do is cut a tail that is still audible.
  const longTail = (): ParamValues => {
    const p = bare();
    p[pad(0, 'aenv.dec')] = 0.1;
    p[pad(0, 'penv.amt')] = 0;
    p[pad(0, 'fx.reverb.on')] = 1;
    p[pad(0, 'fx.reverb.mix')] = 1;
    p[pad(0, 'fx.reverb.size')] = 0.9;
    return p;
  };

  it('lets a multi-second reverb tail decay instead of truncating it', () => {
    const h = boot(longTail());
    h.send({ t: 'trig', pad: 0, v: 1 });
    const x = h.render(2600).L; // 6.9 s
    // Walk the envelope in 128-sample frames: it must never fall by more than
    // 40 dB in one frame while still above the gate threshold.
    let prev = 0;
    for (let f = 0; f + 128 <= x.length; f += 128) {
      const p2 = peak(x, f, f + 128);
      if (prev > 1e-4) expect(p2, `frame ${f}`).toBeGreaterThan(prev * 0.01);
      prev = p2;
    }
    expect(peak(x, x.length - 1280)).toBeLessThan(1e-4); // it did finish
    expect(peak(x, 0, 12800)).toBeGreaterThan(0.02); // and it was a real tail
  });

  it('gates an idle pad and re-engages without a click', () => {
    const h = boot(longTail());
    h.send({ t: 'trig', pad: 0, v: 1 });
    h.render(2600); // long past the tail: the chain is gated by now
    const gated = h.proc as unknown as { padFx: { gated: boolean }[] };
    expect(gated.padFx[0].gated).toBe(true);
    expect(peak(h.render(4).L)).toBeLessThan(1e-4);
    h.send({ t: 'trig', pad: 0, v: 1 });
    const x = h.render(20).L;
    expect(peak(x)).toBeGreaterThan(0.01);
    // the first samples out of the gate ramp up from silence like any hit
    expect(peak(x, 0, 32)).toBeLessThan(0.05);
  });

  it('gates a pad whose drive is on — the oversampler is not always-on work', () => {
    // Every factory kit that switches DRIVE on switches it on for all sixteen
    // pads, so an always-on drive stage would defeat the gate on a whole kit.
    const p = bare();
    p[pad(0, 'aenv.dec')] = 0.05;
    p[pad(0, 'penv.amt')] = 0;
    p[pad(0, 'fx.drive.on')] = 1;
    p[pad(0, 'fx.drive.amt')] = 0.6;
    p[pad(0, 'fx.drive.mix')] = 0.2;
    const h = boot(p);
    h.send({ t: 'trig', pad: 0, v: 1 });
    h.render(200); // 0.53 s: hit gone, past the 0.25 s hold
    const st = h.proc as unknown as { padFx: { gated: boolean }[]; busOut: { gated: boolean }[] };
    expect(st.padFx[0].gated).toBe(true);
    expect(st.busOut[0].gated).toBe(true);
    // and it comes back cleanly
    h.send({ t: 'trig', pad: 0, v: 1 });
    const x = h.render(20).L;
    expect(peak(x)).toBeGreaterThan(0.01);
    expect(peak(x, 0, 32)).toBeLessThan(0.05);
  });

  it('holds the gate open for a full delay round trip', () => {
    const p = bare();
    p[pad(0, 'aenv.dec')] = 0.05;
    p[pad(0, 'penv.amt')] = 0;
    p[pad(0, 'fx.delay.on')] = 1;
    p[pad(0, 'fx.delay.time')] = 0.9;
    p[pad(0, 'fx.delay.fb')] = 0.8;
    p[pad(0, 'fx.delay.mix')] = 1;
    const h = boot(p);
    h.send({ t: 'trig', pad: 0, v: 1 });
    h.render(150); // 0.4 s: dry gone, first echo not back yet
    const echo = h.render(300).L; // the 0.9 s echo lands in here
    expect(peak(echo)).toBeGreaterThan(1e-3);
  });
});

describe('DR-1 pad compressor', () => {
  const held = (on: number, lvl: number, makeup = 0): number => {
    const p = bare();
    p[pad(0, 'fx.comp.on')] = on;
    p[pad(0, 'fx.comp.thr')] = -24;
    p[pad(0, 'fx.comp.gain')] = makeup;
    p[pad(0, 'lvl')] = lvl;
    p[pad(0, 'oscA.level')] = 1;
    p[pad(0, 'penv.amt')] = 0;
    p[pad(0, 'aenv.att')] = 0.001;
    p[pad(0, 'aenv.hold')] = 0.25;
    p[pad(0, 'v2l')] = 0;
    const h = boot(p);
    h.send({ t: 'trig', pad: 0, v: 1 });
    const { L } = h.render(80); // inside the flat top, past the 3 ms attack
    let s2 = 0;
    for (let i = 4096; i < L.length; i++) s2 += L[i] * L[i];
    return Math.sqrt(s2 / (L.length - 4096));
  };

  it('narrows the range between a loud and a quiet hit', () => {
    // WebAudio's node applies a spec-defined makeup even at MAKEUP 0, so the
    // thing to measure is the ratio, not the absolute level.
    const dry = held(0, 1) / held(0, 0.25);
    const wet = held(1, 1) / held(1, 0.25);
    expect(wet).toBeLessThan(dry * 0.8);
  });

  it('adds MAKEUP on top of the curve', () => {
    expect(held(1, 1, 12)).toBeGreaterThan(held(1, 1, 0) * 1.5);
  });
});
