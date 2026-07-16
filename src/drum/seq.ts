// Pure sequencer data model + timing math. The worklet re-implements the same
// math internally (self-contained); Task 4's parity test asserts the constants.

import { PAD_COUNT } from './params';

export const STEPS = 16;
export const NPATTERNS = 4;
export const PATTERN_NAMES = ['1', '2', '3', '4'];
export type StepVal = 0 | 1 | 2; // off / on / accent
export const ACCENT_VEL = 1.0;
export const PLAIN_VEL = 0.72;
// Swing: odd 16ths are delayed by swing * SWING_MAX of a step (1.0 → triplet feel).
export const SWING_MAX = 0.667;

export type Patterns = Uint8Array;

export const patIdx = (pat: number, padI: number, step: number): number =>
  pat * PAD_COUNT * STEPS + padI * STEPS + step;

export const makeEmptyPatterns = (): Patterns => new Uint8Array(NPATTERNS * PAD_COUNT * STEPS);

export const cycleStep = (v: number): StepVal => ((v + 1) % 3) as StepVal;

export const stepDurSamples = (bpm: number, sampleRate: number): number =>
  (60 / bpm / 4) * sampleRate;

export const swingDelaySamples = (step: number, swing: number, stepDur: number): number =>
  step % 2 === 1 ? swing * SWING_MAX * stepDur : 0;

export const nextChainPos = (chainLen: number, pos: number): number =>
  chainLen > 0 ? (pos + 1) % chainLen : 0;
