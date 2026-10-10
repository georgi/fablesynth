import { describe, expect, it } from 'vitest';
import { BAND_HZ, EQ_REFERENCES, SpectrumCapture, eqCurveDb, fitEq, referenceById, referenceDb, removeEq } from './autoEq';

const SR = 48000, FFT = 16384;

function capture(shapeDb: (f: number) => number) {
  const c = new SpectrumCapture(SR, FFT);
  // Per-bin density: band power grows with bandwidth, so subtract 10·log10(f).
  const spec = new Float32Array(FFT / 2).map((_, k) => { const f = Math.max(1, k * SR / FFT); return shapeDb(f) - 10 * Math.log10(f); });
  const time = new Float32Array(FFT).fill(0.1);
  for (let i = 0; i < 20; i++) c.add(spec, time);
  return c.levels();
}

describe('auto-EQ', () => {
  it('leaves a source that matches each reference flat', () => {
    for (const ref of EQ_REFERENCES) {
      const fit = fitEq(capture(f => referenceDb(ref, f)), ref, 1, SR);
      expect(fit.bands.every(b => Math.abs(b.gain) <= 1), ref.id).toBe(true);
    }
  });

  it('fits the same source differently against different references', () => {
    const levels = capture(f => referenceDb(referenceById('mix'), f));
    const bass = fitEq(levels, referenceById('bass'), 1, SR), lead = fitEq(levels, referenceById('lead'), 1, SR);
    expect(bass.bands[3].gain).toBeLessThan(0);
    expect(lead.bands[0].gain).toBeLessThan(0);
  });

  it('does not boost a register the source never plays', () => {
    const levels = capture(f => f < 150 ? -90 : referenceDb(referenceById('mix'), f));
    expect(fitEq(levels, referenceById('mix'), 1, SR).bands[0].gain).toBe(0);
  });

  it('removes the current EQ from a post-EQ measurement', () => {
    const ref = referenceById('mix');
    const eq = [{ type: 1, freq: 2500, q: 1.1, gain: 6, on: true }];
    const post = capture(f => referenceDb(ref, f) + eqCurveDb(eq, f, SR));
    const fit = fitEq(removeEq(post, eq, SR), ref, 1, SR);
    expect(Math.max(...fit.deviation.filter((_, i) => BAND_HZ[i] >= 63).map(Math.abs))).toBeLessThan(1.5);
  });
});
