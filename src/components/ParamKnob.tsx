// Rotary knob bound to one entry of a machine's parameter table. DR-1 and BL-1
// wrap it with their store (DrumKnob, BassKnob). Double-click resets to the
// default; bipolar params sweep from 12 o'clock.

import { useControlMenu } from './ControlMenu';
import { normToValue, valueToNorm, type ParamDef } from '../params';
import { useAutoLive } from '../seq/autoLive';
import type { MachineId } from '../seq/protocol';
import { A0, KNOB_TRACK, arcPath, clamp01, degOf, useKnobDrag } from './knobKit';

export type ParamKnobSize = 'lg' | 'md' | 'sm' | 'xs';

export interface ParamKnobProps {
  machine: MachineId;
  paramId: string;
  def: ParamDef;
  value: number;
  setParam: (id: string, value: number) => void;
  size?: ParamKnobSize;
  accent?: string;
  label?: string;
}

export function ParamKnob({ machine, paramId, def, value, setParam, size = 'md', accent, label }: ParamKnobProps) {
  const controlMenu = useControlMenu(machine, paramId);
  const norm = clamp01(valueToNorm(def, value));
  const autoLive = useAutoLive(machine, paramId, norm);
  const { elRef, handlers } = useKnobDrag(norm, (n) => setParam(paramId, normToValue(def, n)));

  const deg = degOf(norm);
  const from = (def.min as number) < 0 ? degOf(valueToNorm(def, 0)) : A0;
  const text = def.fmt ? def.fmt(value) : value.toFixed(2);
  const labelText = label ?? def.label ?? '';

  return (
    <div
      ref={elRef}
      onContextMenu={controlMenu.onContextMenu}
      className={`knob knob-${size}`}
      data-accent={accent}
      tabIndex={0}
      role="slider"
      aria-label={label || def.label || def.id}
      aria-valuemin={def.min}
      aria-valuemax={def.max}
      aria-valuenow={value}
      aria-valuetext={text}
      {...handlers}
      onDoubleClick={() => setParam(paramId, def.def)}
    >
      <svg viewBox="0 0 80 80">
        <circle className="k-body" cx="40" cy="40" r="26" />
        <path className="k-track" d={KNOB_TRACK} />
        <path className="k-arc" d={arcPath(40, 40, 33, from, deg)} />
        <path ref={autoLive.ring} className="k-auto" d="" style={{ opacity: 0 }} />
        <circle ref={autoLive.dot} className="k-auto-dot" r="3.2" style={{ opacity: 0 }} />
        <line className="k-ptr" x1="40" y1="40" x2="40" y2="17" transform={`rotate(${deg} 40 40)`} />
      </svg>
      <div className="k-label">{labelText}</div>
      <div className="k-value">{text}</div>
      {controlMenu.menu}
    </div>
  );
}
