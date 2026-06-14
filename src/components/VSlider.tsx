// Vertical slider used for wavetable position, with a "ghost" marker that
// tracks the live modulated position coming back from the DSP thread.

import { useEffect, useRef } from 'react';
import { PARAMS, normToValue, valueToNorm } from '../params';
import { useStore } from '../store';

const clamp01 = (n: number) => Math.min(1, Math.max(0, n));

interface VSliderProps {
  paramId: string;
  accent?: string;
  ghost: number; // live modulated position 0..1, -1 = hidden
}

export function VSlider({ paramId, accent, ghost }: VSliderProps) {
  const def = PARAMS[paramId];
  const value = useStore((s) => s.params[paramId]);
  const setParam = useStore((s) => s.setParam);
  const norm = clamp01(valueToNorm(def, value));

  const elRef = useRef<HTMLDivElement>(null);
  const trackRef = useRef<HTMLDivElement>(null);
  const draggingRef = useRef(false);
  const normRef = useRef(norm);
  useEffect(() => { if (!draggingRef.current) normRef.current = norm; }, [norm]);

  const setNorm = (n: number) => {
    normRef.current = clamp01(n);
    setParam(paramId, normToValue(def, normRef.current));
  };

  const moveTo = (clientY: number) => {
    const r = trackRef.current!.getBoundingClientRect();
    setNorm(1 - (clientY - r.top) / r.height);
  };

  useEffect(() => {
    const el = elRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      setNorm(normRef.current + (e.deltaY < 0 ? 0.03 : -0.03));
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const onPointerDown = (e: React.PointerEvent) => {
    draggingRef.current = true;
    elRef.current?.setPointerCapture(e.pointerId);
    moveTo(e.clientY);
  };
  const onPointerMove = (e: React.PointerEvent) => { if (draggingRef.current) moveTo(e.clientY); };
  const onPointerEnd = () => { draggingRef.current = false; };
  const onKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'ArrowUp') { setNorm(normRef.current + 0.02); e.preventDefault(); }
    else if (e.key === 'ArrowDown') { setNorm(normRef.current - 0.02); e.preventDefault(); }
  };

  const pct = norm * 100;

  return (
    <div
      ref={elRef}
      className="vslider"
      data-accent={accent}
      tabIndex={0}
      role="slider"
      aria-label="wavetable position"
      aria-valuenow={Number(value.toFixed(3))}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerEnd}
      onPointerCancel={onPointerEnd}
      onKeyDown={onKeyDown}
    >
      <div className="vs-track" ref={trackRef}>
        <div className="vs-fill" style={{ height: `${pct}%` }} />
        <div
          className="vs-ghost"
          style={ghost < 0 ? { opacity: 0 } : { opacity: 1, bottom: `${ghost * 100}%` }}
        />
        <div className="vs-handle" style={{ bottom: `${pct}%` }} />
      </div>
      <div className="vs-label">POS</div>
    </div>
  );
}
