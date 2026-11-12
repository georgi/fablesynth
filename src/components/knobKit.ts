// Shared geometry and gesture handling for the SVG rotary knobs (WT-1 Knob,
// ParamKnob for DR-1/BL-1, SeqKnob). Drag vertically (shift = fine), scroll
// wheel, arrow keys when focused. The knobs differ only in what a value is and
// what they draw on top of the common .knob markup.

import type * as React from 'react';
import { useEffect, useRef } from 'react';

export const A0 = -135, A1 = 135;

export function polar(cx: number, cy: number, r: number, deg: number): [number, number] {
  const a = ((deg - 90) * Math.PI) / 180;
  return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
}

export function arcPath(cx: number, cy: number, r: number, from: number, to: number): string {
  if (Math.abs(to - from) < 0.01) to = from + 0.01;
  const [x0, y0] = polar(cx, cy, r, from);
  const [x1, y1] = polar(cx, cy, r, to);
  const large = Math.abs(to - from) > 180 ? 1 : 0;
  const sweep = to > from ? 1 : 0;
  return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${r} ${r} 0 ${large} ${sweep} ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}

export const clamp01 = (n: number) => Math.min(1, Math.max(0, n));
export const degOf = (n: number) => A0 + (A1 - A0) * clamp01(n);

/** The full 270° background track every knob draws. */
export const KNOB_TRACK = arcPath(40, 40, 33, A0, A1);

export interface KnobDrag {
  elRef: React.RefObject<HTMLDivElement>;
  /** Spread onto the knob's root element. */
  handlers: {
    onPointerDown(e: React.PointerEvent): void;
    onPointerMove(e: React.PointerEvent): void;
    onPointerUp(e: React.PointerEvent): void;
    onPointerCancel(e: React.PointerEvent): void;
    onKeyDown(e: React.KeyboardEvent): void;
  };
}

/**
 * Pointer, wheel and keyboard gestures over a normalized 0..1 value. `norm` is
 * the current value; `onNorm` receives each new one. While a drag runs, the
 * working value is not reset from `norm`, so a store round-trip cannot fight
 * the pointer.
 */
export function useKnobDrag(norm: number, onNorm: (n: number) => void): KnobDrag {
  const elRef = useRef<HTMLDivElement>(null);
  const normRef = useRef(norm);
  const draggingRef = useRef(false);
  const lastYRef = useRef(0);
  const onNormRef = useRef(onNorm);
  onNormRef.current = onNorm;

  // Keep the working value in sync with external changes (presets, etc.).
  useEffect(() => {
    if (!draggingRef.current) normRef.current = norm;
  }, [norm]);

  const nudge = (delta: number) => {
    normRef.current = clamp01(normRef.current + delta);
    onNormRef.current(normRef.current);
  };

  // Non-passive wheel so preventDefault works: React's synthetic onWheel is
  // passive, which would let the page scroll under the pointer instead.
  useEffect(() => {
    const el = elRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      nudge((e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.005 : 0.03));
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
    // nudge reads the latest value and callback through refs.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const onPointerEnd = (e: React.PointerEvent) => {
    if (!draggingRef.current) return;
    draggingRef.current = false;
    elRef.current?.classList.remove('dragging');
    try { elRef.current?.releasePointerCapture(e.pointerId); } catch { /* ignore */ }
  };

  return {
    elRef,
    handlers: {
      onPointerDown(e) {
        if (e.button !== 0) return;
        draggingRef.current = true;
        lastYRef.current = e.clientY;
        elRef.current?.setPointerCapture(e.pointerId);
        elRef.current?.classList.add('dragging');
        e.preventDefault();
      },
      onPointerMove(e) {
        if (!draggingRef.current) return;
        const dy = lastYRef.current - e.clientY;
        lastYRef.current = e.clientY;
        nudge(dy * (e.shiftKey ? 0.0008 : 0.005));
      },
      onPointerUp: onPointerEnd,
      onPointerCancel: onPointerEnd,
      onKeyDown(e) {
        const step = e.shiftKey ? 0.005 : 0.02;
        if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { nudge(step); e.preventDefault(); }
        else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { nudge(-step); e.preventDefault(); }
      },
    },
  };
}
