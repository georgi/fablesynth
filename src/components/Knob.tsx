import { useControlMenu } from './ControlMenu';
// WT-1 rotary knob. Gestures and geometry come from knobKit; double-click
// resets. Bipolar params sweep from 12 o'clock.
//
// Knobs that map to a modulation destination double as Serum-style mod targets:
// drop a source chip on one to assign it, then drag the colored ring it grows to
// set the depth (right-click the ring to remove it).

import { useEffect, useRef, useState } from 'react';
import { PARAMS, DEST_OF_PARAM, SOURCE_COLORS, normToValue, valueToNorm, routeBipolar, routeLabel } from '../params';
import { useStore } from '../store';
import { useModsByDest } from '../hooks/useModsByDest';
import { modLive, modNormOffset, subscribeModLive } from '../engine/modLive';
import { useAutoLive } from '../seq/autoLive';
import { A0, KNOB_TRACK, arcPath, clamp01, degOf, polar, useKnobDrag } from './knobKit';
import type { ParamKnobSize } from './ParamKnob';

const clampAmt = (n: number) => Math.min(1, Math.max(-1, n));

/** Radius step between stacked mod rings, in the knob's 80-unit space. */
const RING_STEP = 3.4;
/** The knob's SVG view for a ring count: 80 units, plus room for the outer rings. */
export function ringViewBox(rings: number): string {
  const pad = Math.max(0, rings - 1) * RING_STEP;
  return `${-pad} ${-pad} ${80 + 2 * pad} ${80 + 2 * pad}`;
}

export type KnobSize = ParamKnobSize;

// One modulation ring: an arc from the current value to where this route would
// push it (both ways when bipolar), in its source color. Vertical drag sets the
// depth; right-click removes. `slotNum` is the absolute slot (1..16) the ring
// edits in the fixed mod pool. `onActive` drives the knob's readout.
function ModRing({ slotNum, amt, src, bi, baseNorm, r, focus, onActive }: {
  slotNum: number; amt: number; src: number; bi: boolean; baseNorm: number; r: number;
  focus: number; onActive: (slot: number | null) => void;
}) {
  const updateSlot = useStore((s) => s.updateSlot);
  const clearSlot = useStore((s) => s.clearSlot);
  const ref = useRef<SVGPathElement>(null);
  const drag = useRef<{ y: number; amt: number } | null>(null);

  const d = arcPath(40, 40, r, degOf(bi ? baseNorm - amt : baseNorm), degOf(baseNorm + amt));
  const state = focus === 0 ? '' : focus === src ? ' k-mod-lit' : ' k-mod-dim';

  return (
    <path
      ref={ref}
      className={`k-mod${state}`}
      d={d}
      style={{ color: SOURCE_COLORS[src] }}
      onPointerEnter={() => onActive(slotNum)}
      onPointerLeave={() => { if (!drag.current) onActive(null); }}
      onPointerDown={(e) => {
        if (e.button !== 0) return;
        e.stopPropagation();
        drag.current = { y: e.clientY, amt };
        onActive(slotNum);
        ref.current?.setPointerCapture(e.pointerId);
      }}
      onPointerMove={(e) => {
        if (!drag.current) return;
        e.stopPropagation();
        const dy = drag.current.y - e.clientY;
        updateSlot(slotNum, { amt: clampAmt(drag.current.amt + dy * (e.shiftKey ? 0.001 : 0.005)) });
      }}
      onPointerUp={(e) => {
        drag.current = null;
        try { ref.current?.releasePointerCapture(e.pointerId); } catch { /* ignore */ }
      }}
      onContextMenu={(e) => { e.preventDefault(); e.stopPropagation(); clearSlot(slotNum); }}
    />
  );
}

interface KnobProps {
  paramId: string;
  size?: KnobSize;
  accent?: string;
  label?: string;
}

export function Knob({ paramId, size = 'md', accent, label }: KnobProps) {
  const controlMenu = useControlMenu('WT1', paramId);
  const def = PARAMS[paramId];
  const value = useStore((s) => s.params[paramId]);
  const setParam = useStore((s) => s.setParam);

  const dest = DEST_OF_PARAM[paramId];
  const addRoute = useStore((s) => s.addRoute);
  const modDrag = useStore((s) => s.modDrag);
  const myMods = useModsByDest(dest);
  const dropActive = dest && modDrag > 0;
  const modFocus = useStore((s) => s.modFocus);
  const [activeRing, setActiveRing] = useState<number | null>(null);
  const ring = myMods.find((m) => m.slot === activeRing);

  const bipolar = (def.min as number) < 0;
  const norm = clamp01(valueToNorm(def, value));

  const autoLive = useAutoLive('WT1', paramId, norm);
  const { elRef, handlers } = useKnobDrag(norm, (n) => setParam(paramId, normToValue(def, n)));

  // Live modulation dot: rides the shared modLive rAF pump and writes SVG
  // attributes directly — telemetry never touches React state (16 modulated
  // knobs at ~23 Hz would otherwise re-render the whole panel tree).
  const liveRef = useRef<SVGCircleElement>(null);
  const liveNormRef = useRef(norm);
  liveNormRef.current = norm;
  const hasMods = myMods.length > 0;
  useEffect(() => {
    if (!hasMods || !dest) return;
    return subscribeModLive(() => {
      const el = liveRef.current;
      if (!el) return;
      if (!modLive.active) { el.style.opacity = '0'; return; }
      const mn = clamp01(liveNormRef.current + modNormOffset(def, modLive.sums[dest]));
      const [x, y] = polar(40, 40, 33, degOf(mn));
      el.setAttribute('cx', x.toFixed(2));
      el.setAttribute('cy', y.toFixed(2));
      el.style.opacity = '1';
    });
  }, [hasMods, dest, def]);

  const deg = degOf(norm);
  const arcD = arcPath(40, 40, 33, bipolar ? degOf(valueToNorm(def, 0)) : A0, deg);
  const valueText = def.fmt ? def.fmt(value) : value.toFixed(2);
  const ringBi = ring ? routeBipolar(ring.src, ring.pol) : false;
  const text = ring ? routeLabel(ring.src, ring.amt, ringBi) : valueText;
  const labelText = label ?? def.label ?? '';

  return (
    <div
      ref={elRef}
      onContextMenu={controlMenu.onContextMenu}
      className={`knob knob-${size}${dest ? ' knob-mod' : ''}${dropActive ? ' mod-target' : ''}${myMods.length ? ' has-mod' : ''}${ring ? ' ring-active' : ''}`}
      data-accent={accent}
      tabIndex={0}
      role="slider"
      aria-label={label || def.label || def.id}
      aria-valuemin={def.min}
      aria-valuemax={def.max}
      aria-valuenow={value}
      aria-valuetext={valueText}
      {...handlers}
      onDoubleClick={() => setParam(paramId, def.def)}
      onDragOver={dest ? (e) => { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; } : undefined}
      onDrop={dest ? (e) => {
        e.preventDefault();
        const src = parseInt(e.dataTransfer.getData('mod-src') || '0', 10);
        if (src) addRoute(src, dest);
      } : undefined}
    >
      {/* Each ring after the first lies outside the 80-unit box, so the view
          grows by one ring width per extra ring: the knob shrinks a little and
          every ring stays inside the knob's own bounds. */}
      <svg viewBox={ringViewBox(myMods.length)}>
        <circle className="k-body" cx="40" cy="40" r="26" />
        <path className="k-track" d={KNOB_TRACK} />
        <path className="k-arc" d={arcD} />
        {myMods.map((m, k) => (
          <ModRing key={m.slot} slotNum={m.slot} amt={m.amt} src={m.src} bi={routeBipolar(m.src, m.pol)}
            baseNorm={norm} r={38 + k * RING_STEP} focus={modFocus} onActive={setActiveRing} />
        ))}
        <path ref={autoLive.ring} className="k-auto" d="" style={{ opacity: 0 }} />
        <circle ref={autoLive.dot} className="k-auto-dot" r="3.2" style={{ opacity: 0 }} />
        <line className="k-ptr" x1="40" y1="40" x2="40" y2="17" transform={`rotate(${deg} 40 40)`} />
        {myMods.length > 0 && (
          <circle ref={liveRef} className="k-live" r="3.4" style={{ color: SOURCE_COLORS[myMods[0].src] }} />
        )}
      </svg>
      <div className="k-label">{labelText}</div>
      <div className="k-value" style={ring ? { color: SOURCE_COLORS[ring.src] } : undefined}>{text}</div>
      {controlMenu.menu}
    </div>
  );
}
