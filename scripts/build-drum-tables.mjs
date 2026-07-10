#!/usr/bin/env node
// Offline converter: real CC0 TR-808 WAV recordings -> DR-1 wavetable frame data.
//
// For each source sample we pick FRAMES analysis centers across the sound's
// active region (denser near the attack), take a Hann-windowed 2048-sample FFT
// at each center, keep the per-harmonic MAGNITUDE, and resynthesize one 2048-
// sample single cycle per frame using a FIXED deterministic phase per harmonic
// (shared across every frame and every table). Fixed phases keep neighbouring
// frames phase-coherent, so a `pos` sweep interpolates the sample's spectral
// evolution without comb-filtering.
//
// The SAME in-memory Float32 frame data is serialized to both engines:
//   src/drum/engine/sampledtables.gen.ts   (base64 Float32, decoded -> buildUserTable)
//   juce/source/drum/dsp/SampledTables.gen.{h,cpp}  (C float array -> buildUserTable)
// so the two are byte-identical by construction.
//
// Pure Node, no npm deps. Run: `node scripts/build-drum-tables.mjs`.

import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

export const SIZE = 2048;
export const FRAMES = 16;
const MAX_HARM = SIZE / 2; // 1024

const __dirname = dirname(fileURLToPath(import.meta.url));
const REPO = join(__dirname, '..');

// The 5 tables, in the exact order they append after GLITCH, with their WAVs.
export const SOURCES = [
  { name: '808SD', file: 'assets/drum-samples/808/808SD_SD5050.WAV' },
  { name: '808CP', file: 'assets/drum-samples/808/808CP_CP.WAV' },
  { name: '808CH', file: 'assets/drum-samples/808/808CH_CH.WAV' },
  { name: '808OH', file: 'assets/drum-samples/808/808OH_OH50.WAV' },
  { name: '808CY', file: 'assets/drum-samples/808/808CY_CY5050.WAV' },
];

// ---------- FFT (iterative radix-2, complex, in-place) ----------
// Verbatim port of the fft in src/engine/wavetables.ts.
export function fft(re, im, inverse) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const tr = re[i]; re[i] = re[j]; re[j] = tr;
      const ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = ((inverse ? 2 : -2) * Math.PI) / len;
    const wr = Math.cos(ang), wi = Math.sin(ang);
    const half = len >> 1;
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < half; k++) {
        const ur = re[i + k], ui = im[i + k];
        const xr = re[i + k + half], xi = im[i + k + half];
        const vr = xr * cr - xi * ci;
        const vi = xr * ci + xi * cr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + half] = ur - vr; im[i + k + half] = ui - vi;
        const ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr; cr = ncr;
      }
    }
  }
  if (inverse) {
    for (let i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
  }
}

// Add harmonic k with amplitude a, phase ph (cosine convention) to a spectrum,
// with the conjugate-symmetric partner so the IFFT stays real.
function setHarm(re, im, k, a, ph) {
  re[k] += a * Math.cos(ph);
  im[k] += a * Math.sin(ph);
  re[SIZE - k] += a * Math.cos(ph);
  im[SIZE - k] -= a * Math.sin(ph);
}

// ---------- deterministic per-harmonic phases ----------
// mulberry32 seeded PRNG -> a fixed phase for harmonic k, identical across every
// frame and every table so inter-frame interpolation stays coherent.
function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function makePhases(seed = 0x808) {
  const rng = mulberry32(seed);
  const ph = new Float64Array(MAX_HARM + 1);
  for (let k = 1; k <= MAX_HARM; k++) ph[k] = rng() * 2 * Math.PI;
  return ph;
}

// ---------- WAV parsing (16-bit PCM RIFF, mono or stereo -> mono) ----------
export function parseWav(buf) {
  const u8 = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  const tag = (o) => String.fromCharCode(u8[o], u8[o + 1], u8[o + 2], u8[o + 3]);
  if (tag(0) !== 'RIFF' || tag(8) !== 'WAVE') throw new Error('not a RIFF/WAVE file');

  let fmt = null, dataOff = -1, dataLen = 0;
  let p = 12;
  while (p + 8 <= u8.length) {
    const id = tag(p);
    const sz = dv.getUint32(p + 4, true);
    const body = p + 8;
    if (id === 'fmt ') {
      fmt = {
        format: dv.getUint16(body, true),
        channels: dv.getUint16(body + 2, true),
        sampleRate: dv.getUint32(body + 4, true),
        bits: dv.getUint16(body + 14, true),
      };
    } else if (id === 'data') {
      dataOff = body; dataLen = sz;
    }
    p = body + sz + (sz & 1); // chunks are word-aligned
  }
  if (!fmt) throw new Error('missing fmt chunk');
  if (dataOff < 0) throw new Error('missing data chunk');
  if (fmt.format !== 1 || fmt.bits !== 16) {
    throw new Error(`unsupported WAV: format=${fmt.format} bits=${fmt.bits} (need 16-bit PCM)`);
  }

  const ch = fmt.channels;
  const frameCount = Math.floor(dataLen / (2 * ch));
  const mono = new Float32Array(frameCount);
  for (let i = 0; i < frameCount; i++) {
    let acc = 0;
    for (let c = 0; c < ch; c++) acc += dv.getInt16(dataOff + (i * ch + c) * 2, true);
    mono[i] = acc / (ch * 32768);
  }
  return { sampleRate: fmt.sampleRate, channels: ch, length: frameCount, samples: mono };
}

// ---------- WAV encoding (16-bit PCM mono) — used by the round-trip test ----------
export function encodeWav(samples, sampleRate = 44100) {
  const n = samples.length;
  const bytes = new Uint8Array(44 + n * 2);
  const dv = new DataView(bytes.buffer);
  const str = (o, s) => { for (let i = 0; i < s.length; i++) bytes[o + i] = s.charCodeAt(i); };
  str(0, 'RIFF'); dv.setUint32(4, 36 + n * 2, true); str(8, 'WAVE');
  str(12, 'fmt '); dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true); dv.setUint16(22, 1, true);
  dv.setUint32(24, sampleRate, true); dv.setUint32(28, sampleRate * 2, true);
  dv.setUint16(32, 2, true); dv.setUint16(34, 16, true);
  str(36, 'data'); dv.setUint32(40, n * 2, true);
  for (let i = 0; i < n; i++) {
    const s = Math.max(-1, Math.min(1, samples[i]));
    dv.setInt16(44 + i * 2, Math.round(s * 32767), true);
  }
  return bytes;
}

// ---------- analysis -> FRAMES x SIZE frames ----------
// Normalize peak to 1, find the active region (above a silence threshold), pick
// FRAMES centers spaced denser near the attack, and resynthesize each frame from
// the windowed magnitude spectrum at its center using the fixed phase table.
export function analyzeSampleToFrames(mono, phases = makePhases()) {
  const n = mono.length;
  const x = new Float32Array(n);
  let peak = 1e-9;
  for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(mono[i]));
  const g = 1 / peak;
  for (let i = 0; i < n; i++) x[i] = mono[i] * g;

  // Active region: first / last sample above 2% of full scale.
  const thresh = 0.02;
  let start = 0, end = n - 1;
  while (start < n && Math.abs(x[start]) < thresh) start++;
  while (end > start && Math.abs(x[end]) < thresh) end--;
  if (start >= end) { start = 0; end = Math.max(1, n - 1); }
  const span = end - start;

  const win = new Float64Array(SIZE); // Hann window
  for (let i = 0; i < SIZE; i++) win[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (SIZE - 1));

  const re = new Float64Array(SIZE);
  const im = new Float64Array(SIZE);
  const sre = new Float64Array(SIZE);
  const sim = new Float64Array(SIZE);

  const frames = [];
  for (let f = 0; f < FRAMES; f++) {
    // Denser near the attack: quadratic spacing from start -> end.
    const frac = FRAMES === 1 ? 0 : f / (FRAMES - 1);
    const center = Math.round(start + span * frac * frac);

    // Windowed 2048-chunk centered on `center`, zero-padded at the edges.
    re.fill(0); im.fill(0);
    const base = center - SIZE / 2;
    for (let i = 0; i < SIZE; i++) {
      const s = base + i;
      re[i] = (s >= 0 && s < n ? x[s] : 0) * win[i];
    }
    fft(re, im, false);

    // Resynthesize a single cycle from per-harmonic magnitude + fixed phase.
    sre.fill(0); sim.fill(0);
    for (let k = 1; k < MAX_HARM; k++) {
      const mag = Math.hypot(re[k], im[k]);
      if (mag > 0) setHarm(sre, sim, k, mag, phases[k]);
    }
    fft(sre, sim, true); // -> time domain (real part)

    const frame = new Float32Array(SIZE);
    let fp = 1e-9;
    for (let i = 0; i < SIZE; i++) fp = Math.max(fp, Math.abs(sre[i]));
    const fg = 1 / fp;
    for (let i = 0; i < SIZE; i++) frame[i] = sre[i] * fg;
    frames.push(frame);
  }
  return frames;
}

// ---------- serialization helpers ----------
// base64 of the little-endian bytes of a Float32Array — same scheme as
// src/engine/usertables.ts, laid out frame-major (frame 0's SIZE samples first).
export function framesToBase64(frames) {
  const flat = new Float32Array(frames.length * SIZE);
  for (let f = 0; f < frames.length; f++) flat.set(frames[f], f * SIZE);
  return Buffer.from(flat.buffer, flat.byteOffset, flat.byteLength).toString('base64');
}

const GEN_HEADER_TS = `// DO NOT EDIT — generated by scripts/build-drum-tables.mjs`;

function emitTs(tables) {
  const entries = tables.map((t) =>
    `  { name: ${JSON.stringify(t.name)}, frames: ${FRAMES}, wave: ${JSON.stringify(t.base64)} },`,
  ).join('\n');
  return `${GEN_HEADER_TS}
//
// Sample-derived DR-1 drum wavetables (real CC0 TR-808 recordings resynthesized
// from their windowed magnitude spectra). Each \`wave\` is base64 of a Float32Array
// of length frames*SIZE, laid out frame-major — the same encoding as
// src/engine/usertables.ts. Decoded frames feed the shared buildUserTable, so
// these tables anti-alias identically to every other table and are byte-identical
// to juce/source/drum/dsp/SampledTables.gen.cpp (same generator run).

import { SIZE, buildUserTable, type GeneratedTable } from '../../engine/wavetables';

interface RawSampledTable {
  name: string;
  frames: number;
  wave: string; // base64 Float32, frame-major
}

export const SAMPLED_TABLES: RawSampledTable[] = [
${entries}
];

// base64 -> Float32Array, same decode as src/engine/usertables.ts (atob is
// available in the browser engine and in the Node test runtime).
function base64ToFloat32(b64: string): Float32Array {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  // Copy into a fresh, aligned buffer (atob output may be unaligned).
  const out = new Float32Array(bytes.byteLength / 4);
  new Uint8Array(out.buffer).set(bytes);
  return out;
}

// Decode each embedded table to its single-cycle frames and band-limit via the
// shared pipeline. Order: 808SD, 808CP, 808CH, 808OH, 808CY.
export function generateSampledDrumTables(): GeneratedTable[] {
  return SAMPLED_TABLES.map((t) => {
    const flat = base64ToFloat32(t.wave);
    const frames: Float32Array[] = [];
    for (let f = 0; f < t.frames; f++) frames.push(flat.subarray(f * SIZE, (f + 1) * SIZE));
    return buildUserTable(t.name, frames);
  });
}
`;
}

// Shortest round-tripping decimal for a float32 value, suffixed `f` so C++ reads
// it as a float literal. The value is already exactly a float32 (read back from a
// Float32Array), so its shortest decimal re-rounds to the same float32.
function cppFloat(v) {
  if (v === 0) return (1 / v === -Infinity ? '-0.f' : '0.f');
  let s = v.toString();
  if (!/[.eE]/.test(s)) s += '.';
  return s + 'f';
}

function emitCppHeader() {
  return `${GEN_HEADER_TS}
//
// Declaration for the sample-derived DR-1 drum tables. Definition (the frame
// data) lives in SampledTables.gen.cpp. Mirrors DrumTables.h.
#pragma once

#include "../../dsp/Wavetables.h"

#include <vector>

namespace fable {

// Exactly 808SD, 808CP, 808CH, 808OH, 808CY in that order.
std::vector<GeneratedTable> generateSampledDrumTables();

} // namespace fable
`;
}

function emitCpp(tables) {
  const names = tables.map((t) => `"${t.name}"`).join(', ');
  const lines = [];
  for (const t of tables) {
    for (let f = 0; f < FRAMES; f++) {
      const row = [];
      for (let i = 0; i < SIZE; i++) row.push(cppFloat(t.frames[f][i]));
      lines.push('    ' + row.join(', ') + ',');
    }
  }
  return `${GEN_HEADER_TS}
//
// Sample-derived DR-1 drum wavetables (real CC0 TR-808 recordings resynthesized
// from their windowed magnitude spectra). The frame floats below are byte-identical
// to src/drum/engine/sampledtables.gen.ts (same generator run). Decoded frames feed
// the shared buildUserTable, so these tables anti-alias like every other table.
#include "SampledTables.gen.h"

#include <string>
#include <vector>

namespace fable {
namespace {

constexpr int kNumTables = ${tables.length};

const char* const kNames[kNumTables] = { ${names} };

// [table][frame][sample] flattened: kNumTables * FRAMES * SIZE floats.
const float kData[kNumTables * FRAMES * SIZE] = {
${lines.join('\n')}
};

} // namespace

std::vector<GeneratedTable> generateSampledDrumTables() {
    std::vector<GeneratedTable> tables;
    tables.reserve(kNumTables);
    for (int ti = 0; ti < kNumTables; ti++) {
        std::vector<std::vector<float>> frames(FRAMES, std::vector<float>(SIZE));
        for (int f = 0; f < FRAMES; f++)
            for (int i = 0; i < SIZE; i++)
                frames[f][i] = kData[((ti * FRAMES) + f) * SIZE + i];
        tables.push_back(buildUserTable(kNames[ti], frames));
    }
    return tables;
}

} // namespace fable
`;
}

// ---------- main ----------
export function build() {
  const phases = makePhases();
  const tables = SOURCES.map(({ name, file }) => {
    const buf = readFileSync(join(REPO, file));
    const { samples } = parseWav(buf);
    const frames = analyzeSampleToFrames(samples, phases);
    return { name, frames, base64: framesToBase64(frames) };
  });

  writeFileSync(join(REPO, 'src/drum/engine/sampledtables.gen.ts'), emitTs(tables));
  writeFileSync(join(REPO, 'juce/source/drum/dsp/SampledTables.gen.h'), emitCppHeader());
  writeFileSync(join(REPO, 'juce/source/drum/dsp/SampledTables.gen.cpp'), emitCpp(tables));
  return tables;
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const tables = build();
  console.log(`Generated ${tables.length} sampled drum tables: ${tables.map((t) => t.name).join(', ')}`);
  console.log('Wrote src/drum/engine/sampledtables.gen.ts');
  console.log('Wrote juce/source/drum/dsp/SampledTables.gen.h');
  console.log('Wrote juce/source/drum/dsp/SampledTables.gen.cpp');
}
