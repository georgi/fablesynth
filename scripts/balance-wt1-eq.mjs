#!/usr/bin/env node
// Measure the raw-voice spectrum of every WT-1 factory preset and fit the 3-band
// tone EQ (fx.eq.*) so each patch sits closer to a pink reference (equal energy
// per octave — the standard "balanced" mix target).
//
// The EQ is first in the FX chain, so correcting the raw voice (what this harness
// renders — the WebAudio FX nodes can't run headless) is exactly the right stage.
// Corrections are mean-removed (reshape only; loudness stays with the glue comp)
// and clamped to +/-6 dB so patches keep their character.
//
// Writes fx.eq.* into src/presets.ts AND juce/source/dsp/Presets.cpp (idempotent:
// any prior fx.eq.* is stripped first, so re-running re-fits cleanly).
//
//   node scripts/balance-wt1-eq.mjs           # measure + write both files
//   node scripts/balance-wt1-eq.mjs --dry-run # measure + print table only

import { readFile, writeFile } from 'node:fs/promises';
import vm from 'node:vm';
import { createServer } from 'vite';

const DRY = process.argv.includes('--dry-run');
// Reference the balance is fitted toward: 'avg' (the library's own average
// spectral shape — nudges outliers, preserves the natural rolloff) or 'pink'
// (equal energy per octave — an idealized target filtered voices never match,
// so it rails every patch to max brightness; kept for comparison only).
const TARGET = (process.argv.find((a) => a.startsWith('--target=')) || '--target=avg').split('=')[1];
const SR = 48000, BLOCK = 128;
const NOTES = [36, 48, 60];          // C2/C3/C4 — average across registers
const TOTAL = Math.floor(SR * 2.2), OFF = Math.floor(SR * 2.0);
const WIN_A = Math.floor(SR * 0.3), WIN_B = Math.floor(SR * 1.8); // sustain window
const NFFT = 4096, HOP = 2048;
const CAP = 6;                        // +/- dB clamp
const ON_THRESH = 0.5;               // enable EQ only if a band moves > this
// EQ regions (Hz) aligned to the low-shelf / mid-bell / high-shelf action.
const BANDS = [
  { key: 'low',  lo: 20,   hi: 250 },
  { key: 'mid',  lo: 250,  hi: 4000 },
  { key: 'high', lo: 4000, hi: 16000 },
];

// ---- iterative radix-2 FFT (in place) ----
function fft(re, im) {
  const N = re.length;
  for (let i = 1, j = 0; i < N; i++) {
    let bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= N; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < N; i += len) {
      let cwr = 1, cwi = 0;
      for (let k = 0; k < len / 2; k++) {
        const ur = re[i + k], ui = im[i + k];
        const a = re[i + k + len / 2], b = im[i + k + len / 2];
        const vr = a * cwr - b * cwi, vi = a * cwi + b * cwr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
        const n = cwr * wr - cwi * wi; cwi = cwr * wi + cwi * wr; cwr = n;
      }
    }
  }
}
const HANN = new Float64Array(NFFT);
for (let i = 0; i < NFFT; i++) HANN[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (NFFT - 1));

// Accumulate power per FFT bin over the sustained window of one mono buffer.
function accumulatePower(mono, power) {
  for (let start = WIN_A; start + NFFT <= WIN_B; start += HOP) {
    const re = new Float64Array(NFFT), im = new Float64Array(NFFT);
    for (let i = 0; i < NFFT; i++) re[i] = mono[start + i] * HANN[i];
    fft(re, im);
    for (let k = 1; k < NFFT / 2; k++) power[k] += re[k] * re[k] + im[k] * im[k];
  }
}

function bandStats(power) {
  const binHz = SR / NFFT;
  const out = {};
  let midNum = 0, midDen = 0;
  for (const b of BANDS) {
    let e = 0;
    for (let k = 1; k < NFFT / 2; k++) {
      const f = k * binHz;
      if (f >= b.lo && f < b.hi) {
        e += power[k];
        if (b.key === 'mid') { midNum += f * power[k]; midDen += power[k]; }
      }
    }
    out[b.key] = { db: 10 * Math.log10(e + 1e-30), octaves: Math.log2(b.hi / b.lo) };
  }
  out.midCentroid = midDen > 0 ? midNum / midDen : 900;
  return out;
}

const mean = (a) => a.reduce((s, x) => s + x, 0) / a.length;
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

// A preset's per-band level relative to its own 3-band mean — normalizes out
// loudness so only spectral *shape* remains (comparable across patches).
const relShape = (stats) => {
  const db = BANDS.map((b) => stats[b.key].db);
  const m = mean(db);
  return db.map((d) => d - m);
};

// Fit fx.eq.* by nudging a preset's shape toward the reference shape. Corrections
// are the negated deviation, clamped to +/-CAP; already-balanced patches return on:false.
function fitEq(shape, ref, midCentroid) {
  const dev = shape.map((s, i) => s - ref[i]);           // off-reference per band
  const gains = dev.map((d) => clamp(-d, -CAP, CAP));
  const [low, mid, high] = gains;
  const imbBefore = Math.max(...dev) - Math.min(...dev);
  const resid = dev.map((d, i) => d + gains[i]);
  const imbAfter = Math.max(...resid) - Math.min(...resid);
  const base = { imbBefore, imbAfter, mfreq: clamp(Math.round(midCentroid), 200, 5000) };
  if (Math.max(Math.abs(low), Math.abs(mid), Math.abs(high)) <= ON_THRESH)
    return { on: false, low: 0, mid: 0, high: 0, ...base };
  return { on: true, low: +low.toFixed(1), mid: +mid.toFixed(1), high: +high.toFixed(1), ...base };
}

// ---- source-file injection ----
const esc = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

function injectTs(content, name, eq) {
  const re = new RegExp(`(name: '${esc(name)}',\\s*params: \\{)([^}]*)(\\})`);
  if (!re.test(content)) throw new Error(`preset not found in presets.ts: ${name}`);
  return content.replace(re, (_m, pre, body, post) => {
    body = body.replace(/\s*'fx\.eq\.[a-z]+':\s*-?[\d.]+,?/g, '');
    if (!eq.on) return pre + body + post; // stripped any stale eq, add nothing
    const trimmed = body.replace(/\s+$/, '');
    const comma = trimmed && !trimmed.endsWith(',') ? ',' : '';
    const line = `\n      'fx.eq.on': 1, 'fx.eq.low': ${eq.low}, 'fx.eq.mid': ${eq.mid}, ` +
                 `'fx.eq.mfreq': ${eq.mfreq}, 'fx.eq.high': ${eq.high},\n    `;
    return pre + body + comma + line + post;
  });
}

function injectCpp(content, name, eq) {
  const marker = `{"${name}", {`;
  const mi = content.indexOf(marker);
  if (mi < 0) throw new Error(`preset not found in Presets.cpp: ${name}`);
  const listOpen = mi + marker.length - 1; // index of the entries-list '{'
  let depth = 0, close = -1;
  for (let i = listOpen; i < content.length; i++) {
    const ch = content[i];
    if (ch === '{') depth++;
    else if (ch === '}') { if (--depth === 0) { close = i; break; } }
  }
  if (close < 0) throw new Error(`unbalanced braces for ${name} in Presets.cpp`);
  let entries = content.slice(listOpen + 1, close);
  entries = entries.replace(/\s*\{"fx\.eq\.[a-z]+",\s*-?[\d.]+f?\},?/g, '');
  if (!eq.on) return content.slice(0, listOpen + 1) + entries + content.slice(close);
  const trimmed = entries.replace(/\s+$/, '');
  const comma = trimmed && !trimmed.endsWith(',') ? ',' : '';
  const f = (v) => (Number.isInteger(v) ? `${v}.0f` : `${v}f`);
  const line = `\n            {"fx.eq.on", 1}, {"fx.eq.low", ${f(eq.low)}}, {"fx.eq.mid", ${f(eq.mid)}}, ` +
               `{"fx.eq.mfreq", ${eq.mfreq}}, {"fx.eq.high", ${f(eq.high)}},\n        `;
  return content.slice(0, listOpen + 1) + entries + comma + line + content.slice(close);
}

// ---- main ----
const vite = await createServer({ server: { middlewareMode: true, hmr: false }, appType: 'custom' });
const { FACTORY_PRESETS } = await vite.ssrLoadModule('/src/presets.ts');
const { defaultParams } = await vite.ssrLoadModule('/src/params.ts');
const { generateTables } = await vite.ssrLoadModule('/src/engine/wavetables.ts');

// Seed the worklet's Math.random (noise seeds, unison phase spread) so renders —
// and therefore the fitted EQ values written to source — are reproducible.
function mulberry32(seed) {
  return () => {
    seed |= 0; seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const seededMath = Object.create(Math);
seededMath.random = mulberry32(0x5eed);

const ctx = vm.createContext({
  Float32Array, Float64Array, Uint8Array, Math: seededMath, sampleRate: SR, currentFrame: 0,
  AudioWorkletProcessor: class { constructor() { this.port = { onmessage: null, postMessage() {} }; } },
  registerProcessor: (_n, Ctor) => (ctx.Ctor = Ctor),
});
new vm.Script(await readFile(new URL('../src/engine/worklet.js', import.meta.url), 'utf8')).runInContext(ctx);
const tables = generateTables().map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, mask: t.size - 1, data: t.data }));

function renderMono(preset, note) {
  seededMath.random = mulberry32(0x5eed); // independent, reproducible per render
  const p = new ctx.Ctor();
  p.p = { ...defaultParams(), ...preset.params };
  p.tables = tables;
  p.noteOn(note, 1);
  const mono = new Float32Array(TOTAL);
  for (let at = 0; at < TOTAL; at += BLOCK) {
    const o = [[new Float32Array(BLOCK), new Float32Array(BLOCK)]];
    if (at >= OFF && at - BLOCK < OFF) p.noteOff(note);
    ctx.currentFrame = at;
    p.process([], o);
    for (let i = 0; i < BLOCK && at + i < TOTAL; i++) mono[at + i] = 0.5 * (o[0][0][i] + o[0][1][i]);
  }
  return mono;
}

// Pass 1: measure every preset's spectral shape.
const measured = [];
for (const preset of FACTORY_PRESETS) {
  if (preset.name === 'INIT') continue;
  const power = new Float64Array(NFFT / 2);
  for (const note of NOTES) accumulatePower(renderMono(preset, note), power);
  const stats = bandStats(power);
  measured.push({ name: preset.name, shape: relShape(stats), midCentroid: stats.midCentroid });
}
await vite.close();

// Reference shape the whole library is nudged toward.
const ref = TARGET === 'pink'
  ? (() => { const p = BANDS.map((b) => 10 * Math.log10(Math.log2(b.hi / b.lo))); const m = mean(p); return p.map((x) => x - m); })()
  : BANDS.map((_, i) => mean(measured.map((x) => x.shape[i]))); // 'avg': library mean shape

const rows = [];
const fits = new Map();
for (const { name, shape, midCentroid } of measured) {
  const eq = fitEq(shape, ref, midCentroid);
  fits.set(name, eq);
  rows.push({
    preset: name,
    low: eq.on ? eq.low : '—', mid: eq.on ? eq.mid : '—', high: eq.on ? eq.high : '—',
    mfreq: eq.on ? eq.mfreq : '—',
    imbalBefore: +eq.imbBefore.toFixed(1), imbalAfter: +eq.imbAfter.toFixed(1),
  });
}

console.table(rows);
const before = mean(rows.map((r) => r.imbalBefore)), after = mean(rows.map((r) => r.imbalAfter));
console.log(`Mean band imbalance: ${before.toFixed(2)} dB -> ${after.toFixed(2)} dB across ${rows.length} presets ` +
            `(${rows.filter((r) => r.low !== '—').length} EQ'd, cap +/-${CAP} dB, target='${TARGET}')`);

if (DRY) { console.log('\n--dry-run: no files written.'); process.exit(0); }

const tsPath = new URL('../src/presets.ts', import.meta.url);
const cppPath = new URL('../juce/source/dsp/Presets.cpp', import.meta.url);
let ts = await readFile(tsPath, 'utf8');
let cpp = await readFile(cppPath, 'utf8');
for (const [name, eq] of fits) { ts = injectTs(ts, name, eq); cpp = injectCpp(cpp, name, eq); }
await writeFile(tsPath, ts);
await writeFile(cppPath, cpp);
console.log(`\nWrote fx.eq.* into src/presets.ts and juce/source/dsp/Presets.cpp.`);
