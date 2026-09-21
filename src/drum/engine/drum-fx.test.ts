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

describe('DR-1 OTT insert', () => {
  it('changes only the enabled pad', () => {
    const p = bare();
    const render = (params: ParamValues, padIndex: number) => {
      const h = boot(params);
      h.send({ t: 'trig', pad: padIndex, v: 0.4 });
      return h.render(100).L;
    };
    const wet = { ...p, [pad(0, 'fx.ott.on')]: 1, [pad(0, 'fx.ott.depth')]: 1 };
    expect(render(wet, 0)).not.toEqual(render(p, 0));
    expect(render(wet, 1)).toEqual(render(p, 1));
    expect(render(wet, 0).every(Number.isFinite)).toBe(true);
  });
});

describe('DR-1 opt-in FX telemetry', () => {
  it('does not change audio and stops sending packets after the editor unsubscribes', () => {
    const p = bare();
    for (const effect of ['ott', 'comp', 'delay', 'reverb']) p[pad(0, `fx.${effect}.on`)] = 1;
    const plain = boot(p), metered = boot(p);
    for (const t of ['dynamics', 'echo', 'reverb']) metered.send({ t, on: true });
    plain.send({ t: 'trig', pad: 0, v: 1 }); metered.send({ t: 'trig', pad: 0, v: 1 });
    const reference = plain.render(200), actual = metered.render(200);
    expect(actual.L).toEqual(reference.L); expect(actual.R).toEqual(reference.R);
    const count = metered.sent.filter(m => ['dynamics', 'echo', 'reverb'].includes(m.t)).length;
    for (const t of ['dynamics', 'echo', 'reverb']) metered.send({ t, on: false });
    metered.render(40);
    expect(metered.sent.filter(m => ['dynamics', 'echo', 'reverb'].includes(m.t))).toHaveLength(count);
  });
  it('tags selected-pad packets and reports the shared output bus', () => {
    const p = bare();
    p[pad(3, 'fx.ott.on')] = 1; p[pad(3, 'fx.ott.depth')] = 1;
    p[pad(3, 'fx.comp.on')] = 1; p[pad(3, 'fx.delay.on')] = 1; p[pad(3, 'fx.delay.mix')] = 1;
    p[pad(3, 'fx.reverb.on')] = 1; p[pad(3, 'fx.reverb.mix')] = 1;
    const h = boot(p);
    h.send({ t: 'meterPad', pad: 3 }); h.send({ t: 'dynamics', on: true });
    h.send({ t: 'echo', on: true }); h.send({ t: 'reverb', on: true });
    h.send({ t: 'trig', pad: 3, v: 1 }); h.render(20);
    const dynamics = h.sent.find(m => m.t === 'dynamics');
    const echo = h.sent.find(m => m.t === 'echo');
    const reverb = h.sent.find(m => m.t === 'reverb');
    expect(dynamics?.pad).toBe(3); expect(echo?.pad).toBe(3);
    expect(reverb?.pad).toBe(3); expect(reverb?.shared).toBe(true); expect(typeof reverb?.bus).toBe('number');
  });

  it('reports silence for a zero reverb send while the dry pad plays', () => {
    const p = bare();
    p[pad(0, 'fx.reverb.on')] = 1;
    p[pad(0, 'fx.reverb.mix')] = 0;
    const h = boot(p);
    h.send({ t: 'meterPad', pad: 0 }); h.send({ t: 'reverb', on: true });
    h.send({ t: 'trig', pad: 0, v: 1 }); h.render(20);
    const packet = h.sent.filter(m => m.t === 'reverb').slice(-1)[0];
    expect(packet?.bus).toBe(0);
    expect(packet?.left).toBe(-90); expect(packet?.right).toBe(-90);
  });

  it('keeps a silent selected AUX pad on its bus and measures that bus tail', () => {
    const p = bare();
    p[pad(3, 'out')] = 1; // selected pad: AUX, silent
    p[pad(4, 'out')] = 1; // unrelated sounding pad: same AUX bus
    p[pad(4, 'fx.reverb.on')] = 1; p[pad(4, 'fx.reverb.mix')] = 1;
    const h = boot(p);
    h.send({ t: 'meterPad', pad: 3 }); h.send({ t: 'reverb', on: true });
    h.send({ t: 'trig', pad: 0, v: 1 }); // unrelated MAIN dry input
    h.send({ t: 'trig', pad: 4, v: 1 });
    h.render(30);
    const packet = h.sent.filter(m => m.t === 'reverb').slice(-1)[0];
    expect(packet?.pad).toBe(3); expect(packet?.bus).toBe(1);
    expect(packet?.left).toBeGreaterThan(-90); expect(packet?.right).toBeGreaterThan(-90);
  });
});

interface PeakGuard {
  gain: number;
  reset(): void;
  gainFor(l: number, r: number): number;
  process(l: Float32Array, r: Float32Array, n: number): void;
}

describe('DR-1 module headroom', () => {
  const ceiling = 0.8912509381337456;
  const internals = (h: DrumHarness) => h.proc as unknown as {
    padFx: { headroom: Record<string, PeakGuard>; delayFeedbackGuard: PeakGuard }[];
    verbInputGuards: PeakGuard[];
    busOut: { inputGuard: PeakGuard }[];
  };

  it('passes normal levels unchanged, catches the first overload, and preserves stereo balance', () => {
    const guard = internals(boot(bare())).padFx[0].headroom.input;
    const l = new Float32Array([0, 0.1, -0.7, 0.8]);
    const r = new Float32Array([0.02, -0.3, 0.5, -0.8]);
    const dryL = l.slice(), dryR = r.slice();
    guard.process(l, r, l.length);
    expect(l).toEqual(dryL); expect(r).toEqual(dryR);
    const hotL = new Float32Array([20, -12, 0.2]);
    const hotR = new Float32Array([5, -3, 0.05]);
    guard.process(hotL, hotR, hotL.length);
    expect(peak(hotL)).toBeLessThanOrEqual(ceiling + 1e-7);
    for (let i = 0; i < hotL.length; i++) expect(hotL[i] / hotR[i]).toBeCloseTo(4, 6);
    // Release recovers gradually rather than snapping to unity between peaks.
    expect(hotL[2]).toBeLessThan(0.02);
    const quiet = new Float32Array(96000);
    guard.process(quiet, quiet.slice(), quiet.length);
    expect(guard.gain).toBe(1);
    guard.reset();
    expect(guard.gain).toBe(1);
  });

  it('contains invalid samples before they enter a stateful module', () => {
    const guard = internals(boot(bare())).padFx[0].headroom.input;
    const l = new Float32Array([NaN, Infinity, 0.1]);
    const r = new Float32Array([0.1, -Infinity, 0.2]);
    guard.process(l, r, l.length);
    expect([...l]).toEqual([0, 0, Math.fround(0.1)]);
    expect([...r]).toEqual([0, 0, Math.fround(0.2)]);
  });

  it.each([44100, 48000, 96000])('bounds every FX boundary and feedback write under overload at %i Hz', (sr) => {
    const p = defaultDrumParams();
    for (let i = 0; i < PAD_COUNT; i++) {
      for (const effect of ['drive', 'comp', 'ott', 'chorus', 'delay', 'reverb']) p[pad(i, `fx.${effect}.on`)] = 1;
      p[pad(i, 'fx.drive.amt')] = 1; p[pad(i, 'fx.drive.mix')] = 0.5;
      p[pad(i, 'fx.comp.thr')] = -40;
      p[pad(i, 'fx.ott.depth')] = 1; p[pad(i, 'fx.ott.up')] = 2;
      p[pad(i, 'fx.ott.down')] = 2; p[pad(i, 'fx.ott.time')] = 0.01;
      p[pad(i, 'fx.chorus.mix')] = 1; p[pad(i, 'fx.chorus.depth')] = 1;
      p[pad(i, 'fx.delay.mix')] = 1; p[pad(i, 'fx.delay.fb')] = 0.92;
      p[pad(i, 'fx.delay.time')] = 0.02;
      p[pad(i, 'fx.reverb.mix')] = 0.8; p[pad(i, 'fx.reverb.size')] = 1;
      p[pad(i, 'lvl')] = 1; p[pad(i, 'out')] = i % BUS_COUNT;
    }
    p['master.volume'] = 1;
    const h = boot(p, sr), state = internals(h);
    const boundaries = [...state.padFx.flatMap((fx) => Object.values(fx.headroom)),
      ...state.verbInputGuards, ...state.busOut.map((b) => b.inputGuard)];
    const seen = new Set<PeakGuard>();
    let worst = 0, feedbackWorst = 0, invalid = false, reductions = 0;
    for (const guard of boundaries) {
      const process = guard.process.bind(guard);
      guard.process = (l, r, n) => {
        if (peak(l, 0, n) > ceiling || peak(r, 0, n) > ceiling) reductions++;
        process(l, r, n); seen.add(guard);
        worst = Math.max(worst, peak(l, 0, n), peak(r, 0, n));
        invalid ||= !l.every(Number.isFinite) || !r.every(Number.isFinite);
      };
    }
    for (const fx of state.padFx) {
      const guard = fx.delayFeedbackGuard, gainFor = guard.gainFor.bind(guard);
      guard.gainFor = (l, r) => {
        const g = gainFor(l, r);
        feedbackWorst = Math.max(feedbackWorst, Math.abs(l * g), Math.abs(r * g));
        return g;
      };
    }
    let outputPeak = 0;
    for (let hit = 0; hit < 6; hit++) {
      for (let i = 0; i < PAD_COUNT; i++) h.send({ t: 'trig', pad: i, v: hit < 2 ? 0.15 : 1 });
      const out = h.renderBus(64, 0);
      outputPeak = Math.max(outputPeak, peak(out.L), peak(out.R));
    }
    const tail = h.renderBus(128, 0);
    expect(tail.L.every(Number.isFinite)).toBe(true);
    expect(seen.size).toBe(boundaries.length);
    expect(reductions).toBeGreaterThan(0);
    expect(invalid).toBe(false);
    expect(worst).toBeLessThanOrEqual(ceiling + 1e-7);
    expect(feedbackWorst).toBeLessThanOrEqual(ceiling + 1e-7);
    expect(outputPeak).toBeGreaterThan(0.01);
    expect(outputPeak).toBeLessThanOrEqual(ceiling + 1e-7);
  });
});

describe('DR-1 chain latency', () => {
  // Both the pad insert and the post-mix group strip retain their FIR-aligned
  // drive latency, followed by the output limiter lookahead.
  it('reports two drive FIR stages + limiter lookahead, 126 samples at 48 kHz', () => {
    expect(boot(bare()).latency).toBe(126);
  });

  it('scales the lookahead with the sample rate', () => {
    expect(boot(bare(), 44100).latency).toBe(54 + Math.round(0.0015 * 44100));
    expect(boot(bare(), 96000).latency).toBe(54 + Math.round(0.0015 * 96000));
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
    // This test isolates insert gating. A live group reverb deliberately owns
    // a longer post-mix tail and therefore keeps the output stage awake.
    p['fx.reverb.on'] = 0;
    const h = boot(p);
    h.send({ t: 'trig', pad: 0, v: 1 });
    h.render(300); // 0.8 s: enough for the serial pad + group insert gates
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
    const { L } = h.render(80);
    // After compressor attack/threshold smoothing, before auto-gain settles.
    let s2 = 0;
    for (let i = 1024; i < 3072; i++) s2 += L[i] * L[i];
    return Math.sqrt(s2 / 2048);
  };

  it('compresses an abrupt loud passage before auto-gain catches up', () => {
    const p = bare(); p[pad(0, 'fx.comp.on')] = 1; p[pad(0, 'fx.comp.thr')] = -40;
    const h = boot(p); h.render(1);
    const fx = (h.proc as unknown as { padFx: { process(l: Float32Array, r: Float32Array, n: number, live: boolean): void }[] }).padFx[0];
    const l = new Float32Array(128), r = new Float32Array(128);
    let inputEnergy = 0, outputEnergy = 0;
    for (let block = 0; block < 770; block++) {
      const amp = block < 750 ? 0.03 : 0.3;
      for (let i = 0; i < 128; i++) l[i] = r[i] = amp * Math.sin(2 * Math.PI * 600 * (block * 128 + i) / 48000);
      if (block >= 760) for (const v of l) inputEnergy += v * v;
      fx.process(l, r, 128, true);
      if (block >= 760) for (const v of l) outputEnergy += v * v;
    }
    expect(Math.sqrt(outputEnergy / inputEnergy)).toBeLessThan(0.8);
  });

  it('uses automatic gain instead of the serialized legacy MAKEUP value', () => {
    expect(held(1, 1, 12)).toBe(held(1, 1, 0));
  });

  it('matches sustained input level after settling at different thresholds', () => {
    for (const threshold of [-16, -32, -40]) {
      const p = bare(); p[pad(0, 'fx.comp.on')] = 1; p[pad(0, 'fx.comp.thr')] = threshold;
      const h = boot(p);
      // Feed the actual worklet pad FX directly, before the bus limiter.
      const fx = (h.proc as unknown as { padFx: { process(l: Float32Array, r: Float32Array, n: number, live: boolean): void }[] }).padFx[0];
      h.render(1); // distribute the parameter snapshot to the pad chain
      let inputEnergy = 0, outputEnergy = 0;
      const l = new Float32Array(128), r = new Float32Array(128);
      for (let block = 0; block < 1500; block++) {
        for (let i = 0; i < 128; i++) l[i] = r[i] = 0.15 * Math.sin(2 * Math.PI * 600 * (block * 128 + i) / 48000);
        if (block > 1125) for (const v of l) inputEnergy += v * v;
        fx.process(l, r, 128, true);
        if (block > 1125) for (const v of l) outputEnergy += v * v;
      }
      expect(Math.abs(10 * Math.log10(outputEnergy / inputEnergy))).toBeLessThan(0.5);
    }
  });
});
