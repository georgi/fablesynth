import { describe, it, expect } from 'vitest';
// The offline converter is a pure Node ESM module with no side effects on import.
import {
  SIZE,
  FRAMES,
  fft,
  encodeWav,
  parseWav,
  analyzeSampleToFrames,
  makePhases,
} from '../../../scripts/build-drum-tables.mjs';
import { SAMPLED_TABLES } from './sampledtables.gen';
// Read the generated C++ as raw text (Vite ?raw, typed via vite/client) so the
// test needs no node:fs / @types/node.
import CPP_SRC from '../../../juce/source/drum/dsp/SampledTables.gen.cpp?raw';

// base64 -> Float32Array without Buffer (atob is typed via the DOM lib).
function base64ToFloat32(b64: string): Float32Array {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  const out = new Float32Array(bytes.byteLength / 4);
  new Uint8Array(out.buffer).set(bytes);
  return out;
}

// Magnitude spectrum (bins 0..SIZE/2) of a single SIZE-sample frame.
function frameSpectrum(frame: Float32Array): Float64Array {
  const re = new Float64Array(SIZE);
  const im = new Float64Array(SIZE);
  for (let i = 0; i < SIZE; i++) re[i] = frame[i];
  fft(re, im, false);
  const mag = new Float64Array(SIZE / 2 + 1);
  for (let k = 0; k <= SIZE / 2; k++) mag[k] = Math.hypot(re[k], im[k]);
  return mag;
}

describe('sampled drum table converter', () => {
  it('round-trips a single-partial WAV to a near-pure single-harmonic frame', () => {
    // Pure sine with exactly HARM cycles per SIZE-sample window -> FFT bin HARM.
    const HARM = 8;
    const sr = 44100;
    const n = 8192;
    const sine = new Float32Array(n);
    for (let i = 0; i < n; i++) sine[i] = Math.sin((2 * Math.PI * HARM * i) / SIZE);

    const wav = encodeWav(sine, sr);
    const { samples, sampleRate, channels } = parseWav(wav);
    expect(sampleRate).toBe(sr);
    expect(channels).toBe(1);

    const frames = analyzeSampleToFrames(samples, makePhases());
    expect(frames.length).toBe(FRAMES);

    // Inspect a mid frame; its spectrum must be dominated by harmonic HARM.
    const mag = frameSpectrum(frames[8]);
    let maxBin = 0;
    for (let k = 1; k <= SIZE / 2; k++) if (mag[k] > mag[maxBin]) maxBin = k;
    expect(maxBin).toBe(HARM);

    let total = 0;
    for (let k = 1; k <= SIZE / 2; k++) total += mag[k] * mag[k];
    // Allow a little Hann leakage into the immediate neighbours.
    let dom = 0;
    for (let k = HARM - 1; k <= HARM + 1; k++) dom += mag[k] * mag[k];
    expect(dom / total).toBeGreaterThan(0.95);
  });

  it('emits byte-identical frame floats into the TS and C++ generated files', () => {
    // Decode the first table's frames from the TS .gen (base64 Float32).
    const raw = SAMPLED_TABLES[0];
    const tsFloats = base64ToFloat32(raw.wave);
    expect(tsFloats.length).toBe(FRAMES * SIZE);

    // Parse the first table's floats out of the C++ kData literal.
    const cpp = CPP_SRC;
    const start = cpp.indexOf('kData[kNumTables * FRAMES * SIZE] = {');
    expect(start).toBeGreaterThan(0);
    const open = cpp.indexOf('{', start);
    const close = cpp.indexOf('\n};', open);
    const body = cpp.slice(open + 1, close);
    const tokens = body.match(/-?\d[\d.eE+-]*f/g) ?? [];
    expect(tokens.length).toBeGreaterThanOrEqual(FRAMES * SIZE);

    // Compare the first table's SIZE*FRAMES floats bit-for-bit (float32).
    const scratch = new Float32Array(1);
    for (let i = 0; i < FRAMES * SIZE; i++) {
      scratch[0] = parseFloat(tokens[i]); // parse C++ literal, round to float32
      expect(scratch[0]).toBe(tsFloats[i]);
    }
  });
});
