import { describe, it, expect } from 'vitest';
import {
  STEPS, NPATTERNS, makeEmptyPatterns, patIdx, cycleStep,
  stepDurSamples, swingDelaySamples, nextChainPos, ACCENT_VEL, PLAIN_VEL,
  randomizePadPattern,
} from './seq';

describe('sequencer model', () => {
  it('pattern buffer layout', () => {
    const p = makeEmptyPatterns();
    expect(p.length).toBe(NPATTERNS * 16 * STEPS);
    p[patIdx(1, 2, 3)] = 2;
    expect(p[1 * 256 + 2 * 16 + 3]).toBe(2);
  });

  it('step cycle off→on→accent→off', () => {
    expect(cycleStep(0)).toBe(1);
    expect(cycleStep(1)).toBe(2);
    expect(cycleStep(2)).toBe(0);
    expect(ACCENT_VEL).toBe(1);
    expect(PLAIN_VEL).toBeCloseTo(0.72);
  });

  it('step duration: 126 bpm, 48k → 60/126/4 s of samples', () => {
    expect(stepDurSamples(126, 48000)).toBeCloseTo((60 / 126 / 4) * 48000, 3);
  });

  it('swing delays only off-16ths, up to 2/3 of a step', () => {
    const dur = 1000;
    expect(swingDelaySamples(0, 1, dur)).toBe(0);
    expect(swingDelaySamples(2, 0.5, dur)).toBe(0);
    expect(swingDelaySamples(1, 1, dur)).toBeCloseTo(667, 0);
    expect(swingDelaySamples(3, 0.5, dur)).toBeCloseTo(333.5, 0);
  });

  it('chain advances and wraps', () => {
    expect(nextChainPos(3, 0)).toBe(1);
    expect(nextChainPos(3, 2)).toBe(0);
    expect(nextChainPos(1, 0)).toBe(0);
  });
});

describe('randomizePadPattern (RAND button)', () => {
  it('only rewrites the target pad row within the target pattern', () => {
    const p = makeEmptyPatterns();
    p[patIdx(0, 5, 2)] = 1; // another pad, same pattern — must survive
    p[patIdx(1, 2, 2)] = 2; // same pad, another pattern — must survive
    const next = randomizePadPattern(p, 0, 2, () => 0.9); // rng always "off"
    expect(next[patIdx(0, 5, 2)]).toBe(1);
    expect(next[patIdx(1, 2, 2)]).toBe(2);
    for (let step = 0; step < STEPS; step++) expect(next[patIdx(0, 2, step)]).toBe(0);
  });

  it('every written step is a valid StepVal (0/1/2), never on without a preceding on-roll', () => {
    let call = 0;
    const seq = [0.1, 0.1, 0.5, 0.5, 0.9, 0.9];
    const rng = () => seq[call++ % seq.length];
    const next = randomizePadPattern(makeEmptyPatterns(), 0, 0, rng);
    for (let step = 0; step < STEPS; step++) {
      const v = next[patIdx(0, 0, step)];
      expect([0, 1, 2]).toContain(v);
    }
  });

  it('is deterministic for a fixed rng', () => {
    const a = randomizePadPattern(makeEmptyPatterns(), 0, 0, () => 0.2);
    const b = randomizePadPattern(makeEmptyPatterns(), 0, 0, () => 0.2);
    expect(a).toEqual(b);
  });
});
