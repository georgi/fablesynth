import { describe, it, expect } from 'vitest';
import {
  cycleOct, getStep, makeEmptyPatterns, NPATTERNS, randomPattern, setStep,
  STEP_STRIDE, stepDurSamples, STEPS, stepSemi, swingDelaySamples,
  SWING_MAX, writePattern,
} from './seq';

describe('bass seq model', () => {
  it('empty patterns read back as neutral rests', () => {
    const p = makeEmptyPatterns();
    expect(p.length).toBe(NPATTERNS * STEPS * STEP_STRIDE);
    const s = getStep(p, 2, 7);
    expect(s).toEqual({ on: false, note: 0, oct: 0, acc: false, slide: false, duration: 1 });
  });

  it('setStep/getStep round-trips all fields', () => {
    let p = makeEmptyPatterns();
    p = setStep(p, 1, 3, { on: true, note: 10, oct: -1, acc: true, slide: true, duration: 3 });
    expect(getStep(p, 1, 3)).toEqual({ on: true, note: 10, oct: -1, acc: true, slide: true, duration: 3 });
    // neighbours untouched
    expect(getStep(p, 1, 2).on).toBe(false);
    expect(getStep(p, 1, 4).on).toBe(false);
    // partial update preserves the rest
    p = setStep(p, 1, 3, { acc: false });
    expect(getStep(p, 1, 3)).toEqual({ on: true, note: 10, oct: -1, acc: false, slide: true, duration: 3 });
  });

  it('packs slide independently from duration', () => {
    const p = setStep(makeEmptyPatterns(), 0, 2, { on: true, note: 7, slide: true, duration: 12 });
    expect(getStep(p, 0, 2)).toMatchObject({ note: 7, slide: true, duration: 12 });
    expect(p[2 * STEP_STRIDE + 1] & 0x80).toBe(0x80);
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
    expect(stepSemi({ on: true, note: 3, oct: 1, acc: false, slide: false })).toBe(15);
    expect(stepSemi({ on: true, note: 10, oct: -1, acc: false, slide: false })).toBe(-2);
  });

  it('cycleOct walks 0 → +1 → -1 → 0', () => {
    expect(cycleOct(0)).toBe(1);
    expect(cycleOct(1)).toBe(-1);
    expect(cycleOct(-1)).toBe(0);
  });

  it('timing math matches the worklet constants', () => {
    expect(stepDurSamples(138, 48000)).toBeCloseTo((60 / 138 / 4) * 48000, 6);
    const dur = stepDurSamples(120, 48000);
    expect(swingDelaySamples(0, 1, dur)).toBe(0);
    expect(swingDelaySamples(1, 1, dur)).toBeCloseTo(SWING_MAX * dur, 6);
    expect(swingDelaySamples(1, 0.5, dur)).toBeCloseTo(0.5 * SWING_MAX * dur, 6);
  });

  it('randomPattern yields 16 valid steps and durations', () => {
    for (let trial = 0; trial < 20; trial++) {
      const steps = randomPattern();
      expect(steps.length).toBe(STEPS);
      const p = writePattern(makeEmptyPatterns(), 0, steps);
      for (let i = 0; i < STEPS; i++) {
        const s = getStep(p, 0, i);
        expect(s.note).toBeGreaterThanOrEqual(0);
        expect(s.note).toBeLessThan(12);
        expect([-1, 0, 1]).toContain(s.oct);
        expect(s.duration).toBeGreaterThanOrEqual(1);
        expect(s.duration).toBeLessThanOrEqual(4);
        if (!s.on) continue;
      }
    }
  });
});
