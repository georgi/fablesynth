// @ts-nocheck -- this focused Node-side generator test is run by Vitest; the app does not ship Node typings.
// Writes juce/test/fixtures/automix-vectors.json, the parity vectors for the
// native auto-mix port (juce/source/dsp/automix, checked by automix_test).
// Every input is deterministic and stored next to the web outputs, so the
// native test runs the same functions on the same data.
// Run: AUTOMIX_VECTORS=write npx vitest run src/engine/automixVectors.generate.test.ts
import { writeFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import {
  BAND_HZ, EQ_REFERENCES, SpectrumCapture, addBandPower, alignToReference, eqCurveDb, fitEq,
  referenceDb, removeEq,
} from './autoEq';
import { eqCoefficients, eqResponseDb } from './eqResponse';
import { COMP_STYLES, detectorDb, estimateBpm, fitComp } from './autoComp';
import { MixListener, analyse, buildPlan, detectRole, gatedLoudness, kWeightPower } from '../seq/mixAssistant';
import { masterFxParams } from '../seq/masterFx';
import { KitCapture, fitKitMix } from '../drum/kitMix';

const MODE = process.env.AUTOMIX_VECTORS;
const OUT = new URL('../../juce/test/fixtures/automix-vectors.json', import.meta.url).pathname;

function rng(seed: number) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// Typed arrays become plain arrays; non-finite numbers become strings.
const plain = (v: unknown): unknown => {
  if (v === undefined) return null;
  if (typeof v === 'number') return Number.isFinite(v) ? v : String(v);
  // float32 values round-trip exactly through 9 significant digits; this keeps
  // the fixture small without changing a single input sample.
  if (v instanceof Float32Array) return Array.from(v, (x) => Number.isFinite(x) ? Number(x.toPrecision(9)) : String(x));
  if (ArrayBuffer.isView(v)) return Array.from(v as Float64Array, plain);
  if (Array.isArray(v)) return v.map(plain);
  if (v && typeof v === 'object') return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, plain(x)]));
  return v;
};

function eqVectors() {
  const r = rng(1);
  const spectra = [
    BAND_HZ.map(f => -3.8 * Math.log2(Math.max(f, 100) / 1000) - 14),
    BAND_HZ.map((f, i) => -2 * Math.log2(f / 1000) - 12 + 6 * Math.exp(-((Math.log2(f / 300)) ** 2) * 3) + (i % 3) - 1),
    BAND_HZ.map(f => -6 * Math.log2(f / 1000) - 18 + (f > 5000 ? -20 : 0)),
    BAND_HZ.map(f => (f < 60 ? -80 : -5 * Math.log2(f / 1000) - 15) + 4 * (r() - 0.5)),
  ];
  const fits = [];
  for (const [s, levels] of spectra.entries())
    for (const ref of EQ_REFERENCES)
      for (const amount of [0.5, 1])
        for (const sampleRate of s === 0 ? [44100, 48000] : [48000])
          fits.push({ spectrum: s, ref: ref.id, amount, sampleRate, fit: fitEq(levels, ref, amount, sampleRate), align: alignToReference(levels, ref) });
  const bands = Array.from({ length: 12 }, (_, i) => ({
    freq: 30 * 2 ** (r() * 9.5), gain: (r() - 0.5) * 24, q: 0.3 + r() * 4, type: i % 3, on: i % 5 !== 4,
  }));
  const freqs = [20, 31.5, 100, 440, 1000, 3000, 9000, 15000, 21000];
  const coeffs = bands.flatMap(band => [44100, 96000].map(sampleRate => {
    const c = eqCoefficients(band, sampleRate);
    return { band, sampleRate, c, response: freqs.map(f => eqResponseDb(c, f, sampleRate)) };
  }));
  const curve = { bands: bands.slice(0, 4), sampleRate: 48000, db: BAND_HZ.map(f => eqCurveDb(bands.slice(0, 4), f, 48000)), removed: removeEq(spectra[1], bands.slice(0, 4), 48000) };
  const reference = EQ_REFERENCES.map(ref => ({ id: ref.id, db: [...BAND_HZ, 20, 22050].map(f => referenceDb(ref, f)) }));

  // SpectrumCapture over analyser-style dB frames, two of them silent.
  const sr = 16000, fftSize = 512;
  const capture = new SpectrumCapture(sr, fftSize);
  const frames = Array.from({ length: 6 }, (_, n) => {
    const spectrum = new Float32Array(fftSize / 2).map((_, k) => -30 - 8 * Math.log2(Math.max(1, k)) + 10 * r());
    const amp = n === 2 || n === 4 ? 1e-5 : 0.3 * (1 + n);
    const time = new Float32Array(fftSize).map((_, i) => amp * Math.sin(i * 0.21 + n) * (0.5 + r()));
    return { spectrum, time, audible: capture.add(spectrum, time) };
  });
  return { spectra, fits, coeffs, curve, reference, capture: { sampleRate: sr, fftSize, frames, bandPower: capture.bandPower, levels: capture.levels(), seconds: capture.seconds } };
}

function compVectors() {
  const r = rng(2);
  const rate = 30, sampleRate = 3000, per = sampleRate / rate;
  const series = (bpm: number, n: number, silent: number[]) => {
    const beat = rate * 60 / bpm;
    return Array.from({ length: n }, (_, i) => silent.includes(i) ? (i % 2 ? -Infinity : -80)
      : -24 + 14 * Math.exp(-((i % beat) / 2.5)) + 2 * (r() - 0.5));
  };
  const blocks = series(125, 180, [5, 6, 90]);
  const traces = blocks.map((v, i) => {
    if (!Number.isFinite(v) || v < -60) return null;
    const amp = 10 ** (v / 20) * Math.SQRT2;
    return new Float32Array(per).map((_, k) => Math.abs(amp * Math.sin((i * per + k) * 0.31)) * (0.8 + 0.4 * r()));
  });
  const holeIndex = blocks.indexOf(Math.max(...blocks.filter(Number.isFinite)));
  const holey = traces.map((t, i) => i === holeIndex ? null : t);
  const fits = [];
  for (const style of COMP_STYLES)
    for (const depth of [0.5, 1, 1.5]) {
      fits.push({ style: style.id, depth, bpm: null, traces: 'none', fit: fitComp(blocks, style, depth) });
      fits.push({ style: style.id, depth, bpm: 128, traces: 'full', fit: fitComp(blocks, style, depth, { bpm: 128, traces, sampleRate }) });
      fits.push({ style: style.id, depth, bpm: null, traces: 'holey', fit: fitComp(blocks, style, depth, { traces: holey, sampleRate }) });
    }
  const short = blocks.slice(0, 40).map((v, i) => i < 15 ? v : -90);
  fits.push({ style: 'glue', depth: 1, bpm: null, traces: 'none', blocks: short, fit: fitComp(short, COMP_STYLES[0], 1) });
  const bpmSeries = [blocks, series(96, 240, []), series(140, 200, [3]), blocks.slice(0, 60), new Array(120).fill(-20)];
  const bpm = bpmSeries.map(b => ({ blocks: b, bpm: estimateBpm(b) }));
  const detTraces = traces.slice(0, 8).map((t, i) => i === 3 ? null : t);
  const detector = { count: 8, nullIndex: 3, attack: 0.005, release: 0.12, sampleRate, out: detectorDb(detTraces, 0.005, 0.12, sampleRate) };
  return { sampleRate, blocks, traces, holeIndex, fits, bpm, detector };
}

function mixVectors() {
  const r = rng(3);
  const sr = 16000, fftSize = 256, bins = fftSize / 2, tracks = 4;
  const listener = new MixListener(sr, fftSize, tracks);
  const spec = (tilt: number, lift: number) => new Float32Array(bins).map((_, k) => -40 + lift - tilt * Math.log2(Math.max(1, k)) + 6 * r());
  const sig = (amp: number, w: number) => new Float32Array(fftSize).map((_, i) => amp * Math.sin(i * w) * (0.6 + 0.8 * r()));
  const frames = Array.from({ length: 12 }, (_, n) => {
    const quiet = n === 7;
    const frame = {
      spectrum: spec(5, 0), input: sig(quiet ? 1e-5 : 0.2 + 0.05 * (n % 4), 0.13),
      output: n % 6 === 5 ? null : sig(0.3, 0.13),
      tracks: [
        { time: sig(n % 4 === 0 ? 0.5 : 0.15, 0.02), spectrum: spec(9, 4) },
        { time: sig(0.12, 0.05), spectrum: spec(7, 0) },
        n % 3 ? { time: sig(0.08, 0.4), spectrum: null } : null,
        { time: sig(n < 4 ? 0.1 : 0, 0.3), spectrum: spec(3, -6) },
      ],
    };
    frame.audible = listener.add(frame.spectrum, frame.input, frame.output ?? undefined, frame.tracks.map(t => t && { time: t.time, spectrum: t.spectrum ?? undefined }));
    return frame;
  });
  const heardAnalysis = analyse(listener);

  const level = (f: number) => -4 * Math.log2(Math.max(f, 90) / 1000) - 14 + (f > 300 && f < 700 ? 3 : 0) + (f > 9000 ? -6 : 0);
  const authored = {
    spectrum: BAND_HZ.map(level), lufsIn: -16.2, lufsOut: -13.1, peakDb: -2.4, crestDb: 15.3, loudDb: -18.7, kOffsetDb: 0.4, seconds: 12,
    tracks: [
      { lufs: -17, power: 0.004, activity: 0.9, lowShare: 0.5, rangeDb: 12 },
      { lufs: -23, power: 0.002, activity: 0.95, lowShare: 0.6, rangeDb: 5 },
      null,
      { lufs: -30, power: 0.0005, activity: 0.5, lowShare: 0.05, rangeDb: 14 },
      { lufs: -21, power: 0.001, activity: 0.9, lowShare: 0.1, rangeDb: 4 },
    ],
  };
  const infos = [
    { machine: 'DR1', name: 'GROOVE' }, { machine: 'BL1', name: 'Low end' }, { machine: 'WT1', name: 'SILENT' },
    { machine: 'WT1', name: 'Hook line' }, { machine: 'WT1', name: 'WASH' },
  ];
  const fx = masterFxParams();
  const limited = { ...fx, 'master.fx.limiter.ceiling': -3, 'master.fx.comp.ratio': 2.5, 'master.fx.eq.low': 1.5 };
  const plans = [];
  const plan = (input) => plans.push({ input, plan: buildPlan(input) });
  plan({ analysis: authored, reference: 'mix', loudness: -14, amount: 1, bpm: 124, sampleRate: 48000, masterFx: fx, masterVol: 0.8, trackVol: [0.7, 0.8, 0.5, 1, 0.6], tracks: infos });
  plan({ analysis: authored, reference: 'dub', loudness: -9, amount: 0.6, bpm: 118, sampleRate: 44100, masterFx: limited, masterVol: 0.9, trackVol: [0.95, 0.9, 0.5, 0.98, 0.97], tracks: infos, roles: [null, 'bass', null, 'pad', 'lead'] });
  plan({ analysis: { ...authored, lufsOut: -Infinity, crestDb: 20 }, reference: 'warm', loudness: -11, amount: 0.3, bpm: 90, sampleRate: 48000, masterFx: fx, masterVol: 0.5, trackVol: [0.5, 0.5, 0.5, 0.5, 0.5], tracks: infos });
  plan({ analysis: { ...authored, lufsOut: -20 }, reference: 'bright', loudness: -9, amount: 1, bpm: 174, sampleRate: 48000, masterFx: fx, masterVol: 0.6, trackVol: [1, 1, 1, 1, 1], tracks: infos });
  plan({ analysis: { ...authored, crestDb: 10, tracks: [authored.tracks[0], null, null, null, null] }, reference: 'pink', loudness: -14, amount: 1, bpm: 120, sampleRate: 48000, masterFx: limited, masterVol: 1, trackVol: [0.8, 0.8, 0.8, 0.8, 0.8], tracks: infos });
  plan({ analysis: heardAnalysis, reference: 'mix', loudness: -14, amount: 0.8, bpm: 126, sampleRate: sr, masterFx: fx, masterVol: 0.85, trackVol: [0.8, 0.7, 0.6, 0.9], tracks: [
    { machine: 'DR1', name: 'KIT' }, { machine: 'BL1', name: 'acid' }, { machine: 'WT1', name: 'Pad chords' }, { machine: 'WT1', name: 'Arp' }] });

  const names = ['KICK', 'kick2', 'Big Drum', 'HATS', 'hat_loop', 'SUB', 'Lead-1', 'STAB', 'CHORDS', 'strings', 'Mystery', ''];
  const stats = [{ lufs: -20, power: 0.01, activity: 0.9, lowShare: 0.1, rangeDb: 5 }, { lufs: -20, power: 0.01, activity: 0.5, lowShare: 0.35, rangeDb: 12 }, { lufs: -20, power: 0.01, activity: 0.8, lowShare: 0.5, rangeDb: 6 }];
  const roles = names.flatMap(name => ['DR1', 'BL1', 'WT1'].flatMap(machine => stats.map((t, s) => ({ machine, name, stats: s, role: detectRole(machine, t, name) }))));
  const loud = [[-20, -18, -22, -60, -80], [-90], [], [-14, -14.5, -30, -13]].flatMap(blocks => [0, 1.2].map(k => ({ blocks, k, lufs: gatedLoudness(blocks, k) })));
  const kweight = [20, 38, 100, 1000, 1500, 8000].map(f => ({ f, p: kWeightPower(f) }));
  return { sampleRate: sr, fftSize, tracks, frames, analysis: heardAnalysis, listener: { inBlocks: listener.inBlocks, outBlocks: listener.outBlocks, trackPower: listener.trackPower, peak: listener.peak, frames: listener.frames }, plans, roles, roleStats: stats, loud, kweight };
}

function kitVectors() {
  const r = rng(4);
  const sr = 48000, n = 1024, capture = new KitCapture(sr, 5);
  const blocks = [];
  let frames = 0;
  for (let blk = 0; blk < 3; blk++) {
    frames += n;
    for (let pad = 0; pad < 5; pad++) {
      if (pad === 4) continue;
      const data = new Float32Array(n).map((_, i) => {
        const t = (blk * n + i) / sr;
        if (pad === 0) return 0.8 * Math.sin(2 * Math.PI * 55 * t) * Math.exp(-i / 400);
        if (pad === 1) return 0.2 * (r() * 2 - 1);
        if (pad === 2) return 0.4 * Math.sin(2 * Math.PI * 190 * t) * Math.exp(-i / 300) + 0.15 * (r() * 2 - 1) * Math.exp(-i / 200);
        return 0.05 * Math.sin(2 * Math.PI * 2400 * t);
      });
      capture.add(pad, data, frames);
      blocks.push({ pad, data, frames });
    }
  }
  const mean = capture.meanBands();
  const fits = [];
  const fit = (pads, lvl, ref, amount) => fits.push({ pads, lvl, ref, amount, fit: fitKitMix(pads, lvl, EQ_REFERENCES.find(x => x.id === ref), amount) });
  for (const ref of ['drums', 'mix', 'pink']) for (const amount of [0.5, 1]) fit(mean, [0.8, 0.6, 0.95, 0.7, 0.5], ref, amount);
  const made = Array.from({ length: 6 }, (_, p) => new Float64Array(BAND_HZ.length).map((_, b) => 1e-3 * 10 ** ((-Math.abs(b - 4 * p - 2) * 3 + 10 * r()) / 10)));
  fit(made, [1, 0.5, 0.7, 0, 0.9, 0.6], 'drums', 1);
  fit(made, [0.99, 0.99, 0.99, 0.99, 0.99, 0.99], 'bass', 0.7);
  fit(made, [0.4, 0.4, 0.4, 0.4, 0.4, 0.4], 'lead', 1);
  fit([made[0], new Float64Array(BAND_HZ.length)], [0.8, 0.8], 'drums', 1);
  return { sampleRate: sr, pads: 5, blocks, bands: capture.bands, mean, frames: capture.frames, fits };
}

describe.skipIf(MODE !== 'write')('automix parity vectors', () => {
  it('writes juce/test/fixtures/automix-vectors.json', () => {
    const toFixed = [0, 0.05, 0.25, -0.25, 1.45, -1.45, 2.5, -0.04, 9.95, 99.95, 0.35 * 100, 1234.5678, -6].map(v => ({ v, d0: v.toFixed(0), d1: v.toFixed(1) }));
    const round = [0.5, -0.5, 1.5, -1.5, 2.4999, -2.5000001, 0.49999999999999994].map(v => ({ v, r: Math.round(v) }));
    const vectors = { eq: eqVectors(), comp: compVectors(), mix: mixVectors(), kit: kitVectors(), toFixed, round };
    writeFileSync(OUT, JSON.stringify(plain(vectors)) + '\n');
    expect(vectors.comp.fits.length).toBeGreaterThan(0);
  });
});
