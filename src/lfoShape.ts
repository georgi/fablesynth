// Drawn LFO shapes. A shape is a set of points on one LFO cycle, stored in the
// flat `lfoN.p*` params (params.ts) so it travels with every patch. The
// geometry is the clip automation lane's: the shape is a lane of LFO_LANE
// steps, so the editor reuses the lane point, bend and paint operations and
// the engines mirror evalLane (lfoShapeValue in ott-worklet.js / LfoShape.h).

import { LFO_HOLD, LFO_MAX_POINTS, type ParamValues } from './params';
import {
  addPoint, bendSegment, evalLane, movePoint, paintCell, removePoint, tidyPoints, type AutoPoint,
} from './seq/clipAutomation';

/** Lane length that holds one LFO cycle. AUTO_TICK then gives 256 positions. */
export const LFO_LANE = 64;

export const lfoPointId = (lfo: number, k: number, field: 'x' | 'y' | 'c') => `lfo${lfo}.p${k}.${field}`;

/** The LFO's drawn shape as lane points (t in 0..LFO_LANE). */
export function readLfoShape(params: ParamValues, lfo: number): AutoPoint[] {
  const n = Math.min(LFO_MAX_POINTS, Math.max(0, Math.round(params[`lfo${lfo}.pn`] ?? 0)));
  const points: AutoPoint[] = [];
  for (let k = 1; k <= n; k++) {
    const c = params[lfoPointId(lfo, k, 'c')] ?? 0;
    const point: AutoPoint = { t: (params[lfoPointId(lfo, k, 'x')] ?? 0) * LFO_LANE, v: params[lfoPointId(lfo, k, 'y')] ?? 0.5 };
    if (c > 1.5) point.hold = true; else if (c) point.c = c;
    points.push(point);
  }
  return points;
}

/** The param values that store `points` (sorted, at most LFO_MAX_POINTS). */
export function lfoShapeParams(lfo: number, points: AutoPoint[]): ParamValues {
  const out: ParamValues = { [`lfo${lfo}.pn`]: Math.min(LFO_MAX_POINTS, points.length) };
  for (let k = 1; k <= LFO_MAX_POINTS; k++) {
    const p = points[k - 1];
    out[lfoPointId(lfo, k, 'x')] = p ? p.t / LFO_LANE : 0;
    out[lfoPointId(lfo, k, 'y')] = p ? p.v : 0.5;
    out[lfoPointId(lfo, k, 'c')] = p ? (p.hold ? LFO_HOLD : p.c ?? 0) : 0;
  }
  return out;
}

/** Shape value in -1..+1 at a phase in cycles. An empty shape rests at 0. */
export function evalLfoShape(points: AutoPoint[], phase: number): number {
  const v = evalLane(points, LFO_LANE, (phase - Math.floor(phase)) * LFO_LANE);
  return v === null ? 0 : v * 2 - 1;
}

// ---- edits: each returns the new points. A shape holds at most
// LFO_MAX_POINTS points ----

export function addLfoPoint(points: AutoPoint[], x: number, y: number): { points: AutoPoint[]; index: number } {
  const added = addPoint(points, x * LFO_LANE, y);
  return added.points.length > LFO_MAX_POINTS ? { points, index: -1 } : added;
}
export const moveLfoPoint = (points: AutoPoint[], i: number, x: number, y: number) =>
  movePoint(points, LFO_LANE, i, x * LFO_LANE, y);
export const bendLfoSegment = (points: AutoPoint[], i: number, c: number) => bendSegment(points, i, c);
export const removeLfoPoint = (points: AutoPoint[], i: number) => removePoint(points, i);
/**
 * One held step of width 1/steps at the cell that holds x. The rest of the
 * shape stays as drawn while the points last; after that the whole shape
 * becomes `steps` held cells, so a paint stroke always lands.
 */
export function paintLfoStep(points: AutoPoint[], x: number, y: number, steps: number): AutoPoint[] {
  const grid = LFO_LANE / steps;
  const next = paintCell(points, LFO_LANE, x * LFO_LANE, y, grid);
  if (next.length <= LFO_MAX_POINTS) return next;
  if (steps > LFO_MAX_POINTS) return points;
  const cell = Math.min(steps - 1, Math.floor(Math.min(0.999999, Math.max(0, x)) * steps));
  return Array.from({ length: steps }, (_, s): AutoPoint => ({
    t: s * grid, v: s === cell ? Math.min(1, Math.max(0, y)) : evalLane(points, LFO_LANE, (s + 0.5) * grid) ?? 0.5, hold: true,
  }));
}
/** Hold (step) on or off for the segment that leaves point i. */
export const toggleLfoHold = (points: AutoPoint[], i: number) =>
  tidyPoints(points.map((p, j) => (j === i ? { ...p, hold: !p.hold, c: undefined } : p)));

/** Index of the segment under phase x: the last point at or before it (wraps). */
export function lfoSegmentAt(points: AutoPoint[], x: number): number {
  let i = -1;
  for (let j = 0; j < points.length; j++) if (points[j]!.t <= x * LFO_LANE) i = j;
  return i < 0 ? points.length - 1 : i;
}

// A drawn starting point for each standard shape (LFO_SHAPES order), so
// CUSTOM begins as the shape the LFO played before.
const QUARTER = 0.294; // bend that puts sin(45 deg) at the middle of a segment
const SEEDS: AutoPoint[][] = [
  [{ t: 0, v: 0.5, c: -QUARTER }, { t: 16, v: 1, c: QUARTER }, { t: 32, v: 0.5, c: -QUARTER }, { t: 48, v: 0, c: QUARTER }], // SINE
  [{ t: 0, v: 0 }, { t: 32, v: 1 }],                              // TRI
  [{ t: 0, v: 1 }, { t: LFO_LANE - 0.25, v: 0 }],                 // SAW
  [{ t: 0, v: 1, hold: true }, { t: 32, v: 0, hold: true }],      // SQR
  [0.62, 0.2, 0.85, 0.4, 0.05, 0.7, 0.3, 0.95].map((v, i) => ({ t: i * 8, v, hold: true })), // S&H
];
export function seedLfoShape(shape: number): AutoPoint[] {
  return (SEEDS[shape] ?? SEEDS[1]!).map((p) => ({ ...p }));
}
