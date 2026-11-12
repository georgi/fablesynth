// LFO shape view. Always animates a free-running phase dot. With the CUSTOM
// shape it is also the editor of the drawn shape: the gestures are the clip
// automation lane's (click to place a point, drag to move, Alt-drag to bend a
// segment, double-click or right-click to remove; STEP mode paints held cells).

import { useEffect, useRef, type PointerEvent as ReactPointerEvent } from 'react';
import { setupCanvas } from './canvas';
import { LfoClock } from '../../engine/lfoClock';
import { LFO_CUSTOM } from '../../params';
import {
  LFO_LANE, addLfoPoint, bendLfoSegment, evalLfoShape, lfoSegmentAt, moveLfoPoint, paintLfoStep, removeLfoPoint,
} from '../../lfoShape';
import type { AutoPoint } from '../../seq/clipAutomation';

type LfoFn = (p: number) => number;
const LFO_FNS: (LfoFn | null)[] = [
  (p) => Math.sin(2 * Math.PI * p),
  (p) => 1 - 4 * Math.abs(p - 0.5),
  (p) => 1 - 2 * p,
  (p) => (p < 0.5 ? 1 : -1),
  null, // s&h drawn as steps
];

const PAD = 5;
const HIT = 9;          // point grab radius, px
const STEPS = 16;       // STEP mode cells per cycle
const SNAP = 32;        // point x grid; Shift places freely

interface LFOViewProps {
  shape: number;
  rate: number;
  accent: string;
  className?: string;
  /** Drawn shape, shown and edited while `shape` is CUSTOM. */
  points?: AutoPoint[];
  onPoints?: (points: AutoPoint[]) => void;
  /** STEP mode: a drag paints held cells. */
  paint?: boolean;
  /** The dot stops at the end of the cycle, as the one-shot LFO does. */
  oneshot?: boolean;
}

type Drag =
  | { kind: 'move'; index: number; base: AutoPoint[] }
  | { kind: 'bend'; index: number; y0: number; c0: number; dir: number; base: AutoPoint[] }
  | { kind: 'paint'; x: number; y: number };

export function LFOView({ shape, rate, accent, className, points, onPoints, paint, oneshot }: LFOViewProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const redraw = useRef<(() => void) | null>(null);
  const custom = (shape | 0) === LFO_CUSTOM;
  const propsRef = useRef({ shape, rate, accent, points: points ?? [], custom, oneshot: !!oneshot });
  propsRef.current = { shape: shape | 0, rate, accent, points: points ?? [], custom, oneshot: !!oneshot };
  const clock = useRef(new LfoClock());
  const t0Ref = useRef(performance.now());
  const drag = useRef<Drag | null>(null);
  // The newest shape. Pointer moves can arrive faster than React renders, so
  // the handlers read and write this in place of the `points` prop.
  const latest = useRef<AutoPoint[]>(points ?? []);
  latest.current = points ?? [];
  const commit = (next: AutoPoint[]) => { latest.current = next; onPoints!(next); };

  useEffect(() => {
    let raf = 0;
    const draw = (canvas: HTMLCanvasElement) => {
      const { shape, rate, accent, points, custom, oneshot } = propsRef.current;
      const { ctx, w, h } = setupCanvas(canvas);
      ctx.clearRect(0, 0, w, h);
      const W = w - PAD * 2, mid = h / 2, amp = h / 2 - PAD;
      const yOf = (v: number) => mid - v * amp * 0.9;
      if (custom) {
        // Beat grid: quarters bright, the STEP cells faint.
        ctx.lineWidth = 1;
        for (let s = 1; s < STEPS; s++) {
          ctx.globalAlpha = s % 4 ? 0.07 : 0.2;
          ctx.strokeStyle = '#8893a8';
          ctx.beginPath(); ctx.moveTo(PAD + (s / STEPS) * W, PAD); ctx.lineTo(PAD + (s / STEPS) * W, h - PAD); ctx.stroke();
        }
        ctx.globalAlpha = 0.14;
        ctx.beginPath(); ctx.moveTo(PAD, mid); ctx.lineTo(w - PAD, mid); ctx.stroke();
        ctx.globalAlpha = 1;
      }
      const value = (p: number) => {
        if (custom) return evalLfoShape(points, p);
        if (shape === 4) {
          const v = Math.sin(Math.floor(p * 8) * 78.233 + 12.9898) * 43758.5453;
          return (v - Math.floor(v) - 0.5) * 2;
        }
        return (LFO_FNS[shape] || LFO_FNS[0]!)(p);
      };
      ctx.beginPath();
      if (shape === 4) {
        const steps = 8;
        for (let s = 0; s < steps; s++) {
          const y = yOf(value((s + 0.5) / steps));
          const x0 = PAD + (s / steps) * W, x1 = PAD + ((s + 1) / steps) * W;
          if (s === 0) ctx.moveTo(x0, y); else ctx.lineTo(x0, y);
          ctx.lineTo(x1, y);
        }
      } else {
        // A drawn shape can step: sample finely so the steps stay vertical.
        const n = custom ? Math.max(96, Math.round(W)) : 96;
        for (let i = 0; i <= n; i++) {
          const p = Math.min(i / n, 0.999999);
          const x = PAD + (i / n) * W, y = yOf(value(p));
          if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        }
      }
      ctx.strokeStyle = accent;
      ctx.lineWidth = 1.5;
      ctx.globalAlpha = 0.9;
      ctx.stroke();
      ctx.globalAlpha = 1;

      if (custom) {
        // Point handles: round for a ramp, square for a held step.
        for (const p of points) {
          const x = PAD + (p.t / LFO_LANE) * W, y = yOf(p.v * 2 - 1);
          ctx.fillStyle = '#07090e'; ctx.strokeStyle = accent; ctx.lineWidth = 1.2;
          ctx.beginPath();
          if (p.hold) ctx.rect(x - 2.5, y - 2.5, 5, 5); else ctx.arc(x, y, 3, 0, Math.PI * 2);
          ctx.fill(); ctx.stroke();
        }
        if (!points.length) {
          ctx.fillStyle = '#8893a8'; ctx.globalAlpha = 0.8;
          ctx.font = '8px "IBM Plex Mono", monospace'; ctx.textAlign = 'center';
          ctx.fillText('CLICK TO PLACE POINTS · ALT-DRAG TO BEND', w / 2, mid + 3);
          ctx.globalAlpha = 1;
        }
      }

      // phase dot (cosmetic free-run; a one-shot stops at the end, then restarts)
      const seconds = (performance.now() - t0Ref.current) / 1000;
      let phase = clock.current.phase(seconds, rate);
      if (oneshot) {
        const cycles = seconds * rate, lap = cycles % 2; // one cycle of motion, one of rest
        phase = lap < 1 ? lap : 0.999999;
      }
      ctx.beginPath();
      ctx.arc(PAD + phase * W, yOf(value(phase)), 2.5, 0, Math.PI * 2);
      ctx.fillStyle = '#fff';
      ctx.shadowColor = accent;
      ctx.shadowBlur = 8;
      ctx.fill();
      ctx.shadowBlur = 0;
    };
    const frame = () => {
      const canvas = canvasRef.current;
      if (canvas) draw(canvas);
      raf = requestAnimationFrame(frame);
    };
    const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
    const stop = () => { if (raf) cancelAnimationFrame(raf); raf = 0; };
    const start = () => {
      stop();
      if (mq.matches) {
        // Static frame: freeze the phase clock so the dot doesn't drift, then
        // draw once instead of running the free-run loop.
        const canvas = canvasRef.current;
        if (canvas) draw(canvas);
      } else {
        raf = requestAnimationFrame(frame);
      }
    };
    redraw.current = () => { const canvas = canvasRef.current; if (canvas) draw(canvas); };
    start();
    mq.addEventListener('change', start);
    return () => { stop(); redraw.current = null; mq.removeEventListener('change', start); };
  }, []);

  useEffect(() => {
    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) redraw.current?.();
  }, [shape, rate, accent, points, oneshot]);

  // ---- editing (CUSTOM only) ----
  const editable = custom && !!onPoints;
  const locate = (e: { clientX: number; clientY: number }) => {
    const r = canvasRef.current!.getBoundingClientRect();
    const W = r.width - PAD * 2, amp = (r.height / 2 - PAD) * 0.9;
    const x = Math.min(1, Math.max(0, (e.clientX - r.left - PAD) / W));
    const y = Math.min(1, Math.max(0, ((r.height / 2 - (e.clientY - r.top)) / amp + 1) / 2));
    return { x, y, px: e.clientX - r.left, py: e.clientY - r.top, W, amp, mid: r.height / 2 };
  };
  const hitPoint = (at: ReturnType<typeof locate>) => {
    let best = -1, bestD = HIT;
    latest.current.forEach((p, i) => {
      const d = Math.hypot(PAD + (p.t / LFO_LANE) * at.W - at.px, at.mid - (p.v * 2 - 1) * at.amp - at.py);
      if (d < bestD) { best = i; bestD = d; }
    });
    return best;
  };
  const snap = (x: number, free: boolean) => (free ? x : Math.round(x * SNAP) / SNAP);

  const down = (e: ReactPointerEvent<HTMLCanvasElement>) => {
    if (!editable || e.button !== 0) return;
    try { e.currentTarget.setPointerCapture(e.pointerId); } catch { /* synthetic pointer */ }
    const pts = latest.current, at = locate(e);
    if (paint) {
      commit(paintLfoStep(pts, Math.min(at.x, 0.999), at.y, STEPS));
      drag.current = { kind: 'paint', x: at.x, y: at.y };
      return;
    }
    const hit = hitPoint(at);
    if (e.altKey && pts.length > 1) {
      const i = lfoSegmentAt(pts, at.x), next = pts[(i + 1) % pts.length]!;
      drag.current = { kind: 'bend', index: i, y0: e.clientY, c0: pts[i]!.c ?? 0, dir: Math.sign(next.v - pts[i]!.v) || 1, base: pts };
    } else if (hit >= 0) {
      drag.current = { kind: 'move', index: hit, base: pts };
    } else {
      const added = addLfoPoint(pts, snap(at.x, e.shiftKey), at.y);
      if (added.index < 0) return; // the shape is full
      commit(added.points);
      drag.current = { kind: 'move', index: added.index, base: added.points };
    }
  };
  const move = (e: ReactPointerEvent<HTMLCanvasElement>) => {
    const d = drag.current;
    if (!editable || !d) return;
    const at = locate(e);
    if (d.kind === 'paint') {
      // Fill every cell between the previous and the current pointer sample.
      let pts = latest.current;
      const cells = Math.max(1, Math.ceil(Math.abs(at.x - d.x) * STEPS));
      for (let k = 1; k <= cells; k++) {
        const f = k / cells;
        pts = paintLfoStep(pts, Math.min(0.999, d.x + (at.x - d.x) * f), d.y + (at.y - d.y) * f, STEPS);
      }
      commit(pts);
      drag.current = { kind: 'paint', x: at.x, y: at.y };
    } else if (d.kind === 'move') {
      commit(moveLfoPoint(d.base, d.index, snap(at.x, e.shiftKey), at.y));
    } else {
      commit(bendLfoSegment(d.base, d.index, d.c0 - ((d.y0 - e.clientY) / 40) * d.dir));
    }
  };
  const up = () => { drag.current = null; };
  const removeAt = (e: { clientX: number; clientY: number; preventDefault: () => void }) => {
    if (!editable) return;
    const hit = hitPoint(locate(e));
    if (hit < 0) return;
    e.preventDefault();
    commit(removeLfoPoint(latest.current, hit));
  };

  return (
    <canvas
      ref={canvasRef}
      className={`${className ?? ''}${editable ? ' lfo-edit' : ''}`}
      role={editable ? 'application' : undefined}
      aria-label={editable ? 'LFO shape editor: click to place points, drag to move, Alt-drag to bend, double-click to remove' : undefined}
      onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up}
      onDoubleClick={removeAt} onContextMenu={removeAt}
    />
  );
}
