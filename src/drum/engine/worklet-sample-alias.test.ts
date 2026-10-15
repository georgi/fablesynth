// Pitched-up one-shots must not fold energy back below 16 kHz. The worklet's
// sample layer is compared with an ideal band-limited resampler (Kaiser sinc,
// 32 zero crossings, cutoff 0.4375 of the output rate) computed here; the
// native twin is in juce/test/drum_engine_test.cpp.
import { describe, it, expect } from 'vitest';
import { makeDrumProcessor } from './workletHarness';
import { defaultDrumParams, pad } from '../params';

const N = 1024;
const BANDS = [250, 1000, 2000, 4000, 8000, 12000, 16000];

function fft(re: Float64Array, im: Float64Array) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len;
    for (let i = 0; i < n; i += len) {
      for (let k = 0; k < len / 2; k++) {
        const wr = Math.cos(ang * k), wi = Math.sin(ang * k);
        const ur = re[i + k], ui = im[i + k];
        const vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
        const vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
      }
    }
  }
}

function bessI0(x: number) {
  let sum = 1, term = 1;
  for (let k = 1; k < 80; k++) { term *= (x / (2 * k)) ** 2; sum += term; if (term < 1e-18 * sum) break; }
  return sum;
}

// Ideal resample of `src` read at `ast` source samples per output sample.
function idealRef(src: Float32Array, ast: number, count: number) {
  const fc = Math.min(0.5, 0.4375 / ast), beta = 9, zc = 32, half = zc / (2 * fc);
  const norm = bessI0(beta), last = src.length - 1;
  const out = new Float64Array(count);
  for (let m = 0; m < count; m++) {
    const pos = m * ast;
    let acc = 0, ws = 0;
    for (let k = Math.ceil(pos - half); k <= Math.floor(pos + half); k++) {
      const x = (k - pos) * 2 * fc, r = x / zc;
      if (Math.abs(r) >= 1) continue;
      const g = (x === 0 ? 1 : Math.sin(Math.PI * x) / (Math.PI * x)) * bessI0(beta * Math.sqrt(1 - r * r)) / norm;
      acc += g * src[Math.min(last, Math.max(0, k))]; ws += g;
    }
    out[m] = acc / ws;
  }
  return out;
}

// Worst band (250 Hz..16 kHz) of |x - ref| energy relative to the total ref
// energy, in dB. `lo`/`hi` bound the analysed region, away from the edge fade.
function aliasDb(x: Float32Array, ref: Float64Array, sr: number, lo: number, hi: number) {
  const band = new Float64Array(BANDS.length - 1);
  let total = 1e-30;
  for (let s = lo; s + N <= hi; s += N / 2) {
    const a = new Float64Array(N), ai = new Float64Array(N), b = new Float64Array(N), bi = new Float64Array(N);
    for (let i = 0; i < N; i++) {
      const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1));
      a[i] = (x[s + i] - ref[s + i]) * w; b[i] = ref[s + i] * w;
    }
    fft(a, ai); fft(b, bi);
    for (let k = 1; k < N / 2; k++) {
      const f = k * sr / N;
      total += b[k] * b[k] + bi[k] * bi[k];
      for (let q = 0; q < band.length; q++) if (f >= BANDS[q] && f < BANDS[q + 1]) band[q] += a[k] * a[k] + ai[k] * ai[k];
    }
  }
  const db = Array.from(band, (e) => 10 * Math.log10(Math.max(e, 1e-30) / total));
  return { worst: Math.max(...db), low: db[0] };
}

describe('drum one-shot pitched playback alias rejection', () => {
  // Deterministic broadband noise: the worst case for fold-back.
  const makeSource = (len: number) => {
    let seed = 12345;
    return Float32Array.from({ length: len }, () => {
      seed = (seed * 1664525 + 1013904223) >>> 0;
      return (seed / 4294967296 - 0.5) * 0.8;
    });
  };

  const run = (sr: number, srcRate: number, semis: number) => {
    const src = makeSource(32768);
    const h = makeDrumProcessor(sr);
    const p = defaultDrumParams();
    p[pad(0, 'oscB.level')] = 1; p[pad(0, 'oscB.table')] = 0; p[pad(0, 'oscB.detune')] = 1;
    h.send({ t: 'init', params: p });
    h.send({ t: 'samples', list: [{ sampleRate: srcRate, buf: src.slice().buffer }] });
    const proc = h.proc as unknown as {
      padIds: Int32Array[];
      renderSample(state: Record<string, number | boolean>, fields: Int32Array, base: number,
        pitch0: number, pitch1: number, start: number, left: Float32Array, right: Float32Array,
        offset: number, count: number): boolean;
    };
    const ast = (srcRate / sr) * 2 ** (semis / 12);
    const count = Math.floor((src.length - 1) / ast) - 1;
    const left = new Float32Array(count), right = new Float32Array(count);
    proc.renderSample({ pos: -1, index: -1, done: false, havePrev: false, pStep: 0 },
      proc.padIds[0], 8, semis, semis, 0, left, right, 0, count);
    const ref = idealRef(src, ast, count).map((v) => v * 0.75);
    return { ast, ...aliasDb(left, ref, sr, 600, count - 1200) };
  };

  it.each([[48000, 7], [48000, 12], [44100, 7], [48000, 24], [96000, 24]])(
    'at %i Hz and +%i semitones the fold-back stays below -65 dB', (sr, semis) => {
      const r = run(sr, 44100, semis);
      expect(r.ast).toBeGreaterThan(1.05);
      expect(r.worst).toBeLessThan(-65);
      expect(r.low).toBeLessThan(-65);
    });
});
