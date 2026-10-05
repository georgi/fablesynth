// Spotlight for the NEON CHASE parts: dims every component except the one in
// focus. Rects come from the take (capture/neon.mjs samples them every 80 ms),
// so the hole follows the real UI through scrolls and layout changes.
import React from 'react';
import { Easing } from 'remotion';

export type RectRow = { t: number; r: Record<string, number[]> };
/** From take time `t` on, spotlight the union of `keys` (empty = no dim). */
export type FocusKey = { t: number; keys: string[] };

const PAD = 10;
const MOVE = 320; // ms to glide between targets

function rectAt(rows: RectRow[], t: number, keys: string[]) {
  let lo = 0, hi = rows.length - 1;
  while (lo < hi) { const m = (lo + hi + 1) >> 1; if (rows[m].t <= t) lo = m; else hi = m - 1; }
  const r = rows[lo].r;
  const boxes = keys.map((k) => r[k]).filter(Boolean);
  if (!boxes.length) return null;
  const x0 = Math.min(...boxes.map((b) => b[0])), y0 = Math.min(...boxes.map((b) => b[1]));
  const x1 = Math.max(...boxes.map((b) => b[0] + b[2])), y1 = Math.max(...boxes.map((b) => b[1] + b[3]));
  return [x0 - PAD, y0 - PAD, x1 - x0 + 2 * PAD, y1 - y0 + 2 * PAD];
}

export const Focus: React.FC<{ rows: RectRow[]; keys: FocusKey[]; t: number; color: string; strength?: number }> = ({ rows, keys, t, color, strength = 0.85 }) => {
  let i = 0;
  while (i < keys.length - 1 && keys[i + 1].t <= t) i++;
  if (t < keys[0].t) return null;
  const cur = keys[i], prev = keys[i - 1];
  const u = prev ? Easing.inOut(Easing.cubic)(Math.min(1, (t - cur.t) / MOVE)) : 1;
  const a = rectAt(rows, t, cur.keys);
  const b = prev ? rectAt(rows, t, prev.keys) : null;
  // Fade the dim in or out when one side has no focus; glide between two rects.
  const dim = (a ? 1 : 0) * u + (b ? 1 : 0) * (1 - u) * (prev ? 1 : 0);
  const box = a && b ? a.map((v, k) => b[k] + (v - b[k]) * u) : a ?? b;
  if (!box || dim < 0.01) return null;
  const [x, y, w, h] = box;
  return (
    <div
      style={{
        position: 'absolute', left: x, top: y, width: w, height: h, borderRadius: 14, pointerEvents: 'none',
        boxShadow: `0 0 0 4000px rgba(6,7,11,${strength * dim}), 0 0 0 1.5px ${color}${Math.round(dim * 140).toString(16).padStart(2, '0')}, 0 0 40px ${color}${Math.round(dim * 50).toString(16).padStart(2, '0')}`,
      }}
    />
  );
};
