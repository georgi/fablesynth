// Vertical slider used for wavetable position, with a "ghost" marker that
// tracks the live modulated position coming back from the DSP thread. It is also
// a Serum-style mod target: drop a source chip to assign it, and the colored
// depth band it grows can be dragged (right-click to remove).

import type * as React from 'react';
import { useEffect, useRef } from 'react';
import { PARAMS, DEST_OF_PARAM, SOURCE_COLORS, normToValue, valueToNorm } from '../params';
import { useStore } from '../store';

const clamp01 = (n: number) => Math.min(1, Math.max(0, n));
const clampAmt = (n: number) => Math.min(1, Math.max(-1, n));

interface VSliderProps {
  paramId: string;
  accent?: string;
  ghost: number; // live modulated position 0..1, -1 = hidden
}

// A draggable modulation-depth band rising from the current value.
function PosModBand({ index, amt, src, baseNorm }: { index: number; amt: number; src: number; baseNorm: number }) {
  const updateMod = useStore((s) => s.updateMod);
  const removeMod = useStore((s) => s.removeMod);
  const elRef = useRef<HTMLDivElement>(null);
  const drag = useRef<{ y: number; amt: number } | null>(null);

  const top = clamp01(baseNorm + amt);
  const lo = Math.min(baseNorm, top);
  const hi = Math.max(baseNorm, top);

  return (
    <div
      ref={elRef}
      className="vs-mod"
      style={{ bottom: `${lo * 100}%`, height: `${(hi - lo) * 100}%`, ['--src' as string]: SOURCE_COLORS[src] }}
      onPointerDown={(e) => {
        e.stopPropagation();
        drag.current = { y: e.clientY, amt };
        elRef.current?.setPointerCapture(e.pointerId);
      }}
      onPointerMove={(e) => {
        if (!drag.current) return;
        e.stopPropagation();
        const dy = drag.current.y - e.clientY;
        updateMod(index, { amt: clampAmt(drag.current.amt + dy * (e.shiftKey ? 0.0008 : 0.004)) });
      }}
      onPointerUp={(e) => {
        drag.current = null;
        try { elRef.current?.releasePointerCapture(e.pointerId); } catch { /* ignore */ }
      }}
      onContextMenu={(e) => { e.preventDefault(); e.stopPropagation(); removeMod(index); }}
    />
  );
}

export function VSlider({ paramId, accent, ghost }: VSliderProps) {
  const def = PARAMS[paramId];
  const value = useStore((s) => s.params[paramId]);
  const setParam = useStore((s) => s.setParam);
  const norm = clamp01(valueToNorm(def, value));

  const dest = DEST_OF_PARAM[paramId];
  const mods = useStore((s) => s.mods);
  const addMod = useStore((s) => s.addMod);
  const modDrag = useStore((s) => s.modDrag);
  const myMods = dest ? mods.map((m, i) => ({ m, i })).filter(({ m }) => m.dst === dest) : [];
  const dropActive = dest && modDrag > 0;

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
      className={`vslider${dest ? ' vslider-mod' : ''}${dropActive ? ' mod-target' : ''}`}
      data-accent={accent}
      tabIndex={0}
      role="slider"
      aria-label="wavetable position"
      aria-valuemin={def.min}
      aria-valuemax={def.max}
      aria-valuenow={Number(value.toFixed(3))}
      aria-valuetext={def.fmt ? def.fmt(value) : value.toFixed(2)}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerEnd}
      onPointerCancel={onPointerEnd}
      onKeyDown={onKeyDown}
      onDragOver={dest ? (e) => { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; } : undefined}
      onDrop={dest ? (e) => {
        e.preventDefault();
        const src = parseInt(e.dataTransfer.getData('mod-src') || '0', 10);
        if (src) addMod(src, dest);
      } : undefined}
    >
      <div className="vs-track" ref={trackRef}>
        <div className="vs-fill" style={{ height: `${pct}%` }} />
        {myMods.map(({ m, i }) => (
          <PosModBand key={i} index={i} amt={m.amt} src={m.src} baseNorm={norm} />
        ))}
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
