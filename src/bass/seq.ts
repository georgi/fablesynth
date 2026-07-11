// Pure BL-1 pitch-sequencer data model + timing math. A step is
// { on, note (0..11 within the lane octave), oct (-1/0/+1), acc, slide };
// patterns are packed 3 bytes per step so they cross the worklet port as one
// Uint8Array. The worklet re-implements the same unpack + timing internally
// (self-contained); the parity test asserts the constants.

export const STEPS = 16;
export const NPATTERNS = 4;
export const PATTERN_NAMES = ['A', 'B', 'C', 'D'];
export const NOTE_LANES = 12; // one octave of lanes, C at the bottom
export const OCT_MIN = -1;
export const OCT_MAX = 1;

// byte 0: bit0 on, bit1 acc, bit2 slide · byte 1: note 0..11 · byte 2: oct+1
export const STEP_STRIDE = 3;

export const ACCENT_VEL = 1.0;
export const PLAIN_VEL = 0.72;
// Non-tied gates close at this fraction of the step (classic 303 staccato).
export const GATE_FRAC = 0.55;
// Swing: odd 16ths are delayed by swing * SWING_MAX of a step (1.0 → triplet feel).
export const SWING_MAX = 0.667;

export interface Step {
  on: boolean;
  note: number; // 0..11
  oct: number; // -1 | 0 | 1
  acc: boolean;
  slide: boolean;
}

export type Patterns = Uint8Array;

export const stepOff = (pat: number, step: number): number =>
  (pat * STEPS + step) * STEP_STRIDE;

export const makeEmptyPatterns = (): Patterns => {
  const p = new Uint8Array(NPATTERNS * STEPS * STEP_STRIDE);
  // oct byte defaults to 1 (= oct 0) so untouched rests read back neutral
  for (let i = 2; i < p.length; i += STEP_STRIDE) p[i] = 1;
  return p;
};

export function getStep(p: Patterns, pat: number, step: number): Step {
  const o = stepOff(pat, step);
  const flags = p[o];
  return {
    on: (flags & 1) !== 0,
    note: Math.min(NOTE_LANES - 1, p[o + 1]),
    oct: Math.min(OCT_MAX, Math.max(OCT_MIN, p[o + 2] - 1)),
    acc: (flags & 2) !== 0,
    slide: (flags & 4) !== 0,
  };
}

export function setStep(p: Patterns, pat: number, step: number, s: Partial<Step>): Patterns {
  const next = p.slice();
  const cur = getStep(p, pat, step);
  const merged = { ...cur, ...s };
  const o = stepOff(pat, step);
  next[o] = (merged.on ? 1 : 0) | (merged.acc ? 2 : 0) | (merged.slide ? 4 : 0);
  next[o + 1] = Math.min(NOTE_LANES - 1, Math.max(0, merged.note | 0));
  next[o + 2] = Math.min(OCT_MAX, Math.max(OCT_MIN, merged.oct | 0)) + 1;
  return next;
}

// Semitone offset from the root for a step (lane note + octave switch).
export const stepSemi = (s: Step): number => s.note + 12 * s.oct;

export const cycleOct = (oct: number): number => (oct >= OCT_MAX ? OCT_MIN : oct + 1);

export const stepDurSamples = (bpm: number, sampleRate: number): number =>
  (60 / bpm / 4) * sampleRate;

export const swingDelaySamples = (step: number, swing: number, stepDur: number): number =>
  step % 2 === 1 ? swing * SWING_MAX * stepDur : 0;

export const nextChainPos = (chainLen: number, pos: number): number =>
  chainLen > 0 ? (pos + 1) % chainLen : 0;

// A slide line is drawn INTO a step from its predecessor when both are on
// and the step is flagged slide (matches the worklet's tie rule).
export const slidesInto = (prev: Step, cur: Step): boolean =>
  cur.on && cur.slide && prev.on;

export function writePattern(p: Patterns, pat: number, steps: Array<Partial<Step>>): Patterns {
  let next = p;
  steps.forEach((s, i) => {
    if (i < STEPS) next = setStep(next, pat, i, { on: false, note: 0, oct: 0, acc: false, slide: false, ...s });
  });
  return next;
}

// The RAND button: sparse minor-pentatonic line with occasional octave
// throws, accents and slides — same flavor as the design mock's generator.
export function randomPattern(rng: () => number = Math.random): Array<Partial<Step>> {
  const pool = [0, 0, 0, 3, 5, 7, 10];
  const steps: Array<Partial<Step>> = [];
  for (let i = 0; i < STEPS; i++) {
    const on = rng() < 0.6;
    steps.push({
      on,
      note: pool[(rng() * pool.length) | 0],
      oct: rng() < 0.2 ? (rng() < 0.5 ? -1 : 1) : 0,
      acc: on && rng() < 0.25,
      slide: on && i > 0 && rng() < 0.22,
    });
  }
  return steps;
}
