import { describe, expect, it } from 'vitest';
import {
  cycleOct, getStep, makeEmptyPatterns, NPATTERNS, randomPattern, setStep,
  STEP_STRIDE, stepDurSamples, STEPS, stepSemi, SWING_MAX, swingDelaySamples,
  writePattern,
} from './noteseq';

describe('wt-1 note seq model', () => {
  it('empty patterns read back as neutral rests', () => {
    const p = makeEmptyPatterns();
    expect(p.length).toBe(NPATTERNS * STEPS * STEP_STRIDE);
    const s = getStep(p, 2, 7);
    expect(s).toEqual({ on: false, note: 0, oct: 0, acc: false, duration: 1 });
  });

  it('setStep/getStep round-trips all fields', () => {
    let p = makeEmptyPatterns();
    p = setStep(p, 1, 3, { on: true, note: 10, oct: -1, acc: true, duration: 3 });
    expect(getStep(p, 1, 3)).toEqual({ on: true, note: 10, oct: -1, acc: true, duration: 3 });
    // neighbours untouched
    expect(getStep(p, 1, 2).on).toBe(false);
    expect(getStep(p, 1, 4).on).toBe(false);
    // partial update preserves the rest
    p = setStep(p, 1, 3, { acc: false });
    expect(getStep(p, 1, 3)).toEqual({ on: true, note: 10, oct: -1, acc: false, duration: 3 });
  });

  it('setStep clamps note and oct', () => {
    let p = makeEmptyPatterns();
    p = setStep(p, 0, 0, { on: true, note: 99, oct: 5 });
    expect(getStep(p, 0, 0).note).toBe(11);
    expect(getStep(p, 0, 0).oct).toBe(1);
    p = setStep(p, 0, 0, { duration: 99 });
    expect(getStep(p, 0, 0).duration).toBe(63);
  });

  it('stepSemi combines lane + octave', () => {
    expect(stepSemi({ on: true, note: 3, oct: 1, acc: false, tie: false })).toBe(15);
    expect(stepSemi({ on: true, note: 10, oct: -1, acc: false, tie: false })).toBe(-2);
  });

  it('cycleOct walks 0 → +1 → -1 → 0', () => {
    expect(cycleOct(0)).toBe(1);
    expect(cycleOct(1)).toBe(-1);
    expect(cycleOct(-1)).toBe(0);
  });

  it('timing math matches the family convention', () => {
    expect(stepDurSamples(120, 48000)).toBeCloseTo(6000);
    const dur = stepDurSamples(120, 48000);
    expect(swingDelaySamples(0, 1, dur)).toBe(0); // even steps never swing
    expect(swingDelaySamples(1, 1, dur)).toBeCloseTo(SWING_MAX * dur);
    expect(swingDelaySamples(1, 0.5, dur)).toBeCloseTo(0.5 * SWING_MAX * dur);
  });

  it('writePattern + randomPattern produce a full valid pattern', () => {
    const rng = (() => { let x = 1; return () => ((x = (x * 16807) % 2147483647) / 2147483647); })();
    const p = writePattern(makeEmptyPatterns(), 2, randomPattern(rng));
    let onCount = 0;
    for (let i = 0; i < STEPS; i++) {
      const s = getStep(p, 2, i);
      expect(s.note).toBeGreaterThanOrEqual(0);
      expect(s.note).toBeLessThan(12);
      expect(Math.abs(s.oct)).toBeLessThanOrEqual(1);
      if (s.on) onCount++;
      expect(s.duration).toBeGreaterThanOrEqual(1);
      expect(s.duration).toBeLessThanOrEqual(4);
    }
    expect(onCount).toBeGreaterThan(0);
    // other patterns untouched
    expect(getStep(p, 0, 0).on).toBe(false);
  });
});
