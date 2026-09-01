import { describe, expect, it } from 'vitest';
import { detectCycleLength, sliceToFrames } from './usertables';
import { fft, SIZE } from './wavetables';

// User-table import quality (finding J8). `sliceToFrames` used to stretch each
// detected cycle to 2048 samples with linear interpolation before the
// band-limiting FFT, so the interpolation images landed inside the band that
// was kept and became permanent "harmonics" of the imported table.

// A tone made of harmonics 1..H at equal amplitude, period `period` samples.
function harmonicTone(period: number, cycles: number, H: number): Float32Array {
  const n = Math.ceil(period * cycles);
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let k = 1; k <= H; k++) s += Math.sin((2 * Math.PI * k * i) / period + k * 0.7);
    x[i] = s / H;
  }
  return x;
}

// Magnitude of every harmonic of a 2048-sample single-cycle frame.
function frameHarmonics(frame: Float32Array): Float64Array {
  const re = new Float64Array(SIZE), im = new Float64Array(SIZE);
  for (let i = 0; i < SIZE; i++) re[i] = frame[i];
  fft(re, im, false);
  const out = new Float64Array(SIZE / 2);
  for (let k = 0; k < SIZE / 2; k++) out[k] = Math.hypot(re[k], im[k]);
  return out;
}

describe('detectCycleLength', () => {
  it('finds an integer period', () => {
    const x = harmonicTone(128, 60, 8);
    expect(detectCycleLength(x, 48000)).toBeCloseTo(128, 1);
  });

  it('refines a fractional period past the integer lag grid', () => {
    // Without the parabolic refinement this could only ever return 137 or 138,
    // and the 0.4-sample error would walk the frame start a full cycle across
    // a 64-frame import.
    const x = harmonicTone(137.4, 80, 6);
    const p = detectCycleLength(x, 48000);
    expect(p).toBeGreaterThan(137.1);
    expect(p).toBeLessThan(137.7);
    expect(Number.isInteger(p)).toBe(false);
  });
});

describe('sliceToFrames band-limited resampling', () => {
  it('adds no harmonics above the source cycle content', () => {
    // Source holds harmonics 1..8 and nothing else. An exact resampling to 2048
    // reproduces exactly those; linear interpolation adds sinc² images that
    // measured around -35 dB here.
    const x = harmonicTone(131, 12, 8);
    const frames = sliceToFrames(x, 131);
    const mag = frameHarmonics(frames[0]);
    let ref = 0;
    for (let k = 1; k <= 8; k++) ref = Math.max(ref, mag[k]);
    let spur = 0;
    for (let k = 9; k < SIZE / 2; k++) spur = Math.max(spur, mag[k]);
    expect(20 * Math.log10(spur / ref)).toBeLessThan(-80);
  });

  it('keeps the source harmonic amplitudes flat', () => {
    // The projection must also leave the genuine partials alone: an exact
    // resampling has no sinc² droop across the kept band at all.
    const x = harmonicTone(131, 12, 8);
    const mag = frameHarmonics(sliceToFrames(x, 131)[0]);
    for (let k = 2; k <= 8; k++) expect(mag[k] / mag[1]).toBeGreaterThan(0.98);
  });

  it('handles a fractional period without drifting across frames', () => {
    const period = 137.4;
    const x = harmonicTone(period, 40, 6);
    const frames = sliceToFrames(x, period);
    expect(frames.length).toBeGreaterThan(8);
    // Every frame is the same waveform, so late frames must still line up with
    // the first one. A rounded period would slip a whole cycle by frame 30.
    const a = frames[0], b = frames[frames.length - 1];
    let num = 0, da = 0, db = 0;
    for (let i = 0; i < SIZE; i++) { num += a[i] * b[i]; da += a[i] * a[i]; db += b[i] * b[i]; }
    expect(num / Math.sqrt(da * db)).toBeGreaterThan(0.99);
  });
});
