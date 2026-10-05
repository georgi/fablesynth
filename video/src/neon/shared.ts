// Shared timing for the NEON CHASE parts (132 BPM). Every part file starts one
// bar before its first song bar and ends one bar after its last, as handles.
import { TAKES, type TakeName } from '../demo/Take';

export const NEON_FPS = 30;
export const NEON_BAR = (4 * 60000) / 132;

/** Take-time helpers for one part: b(n) = take ms of audible take bar n. */
export function partClock(name: TakeName, firstBar: number, lastBar: number) {
  const grid = TAKES[name].marks.grid0; // measured by capture/grid.py
  const b = (n: number) => grid + n * NEON_BAR;
  const start = Math.round((b(firstBar - 1) / 1000) * NEON_FPS) * (1000 / NEON_FPS); // frame-exact
  const frames = Math.round(((b(lastBar + 1) - start) / 1000) * NEON_FPS);
  const frameOf = (n: number) => Math.round(((b(n) - start) / 1000) * NEON_FPS);
  return { b, start, frames, frameOf };
}
