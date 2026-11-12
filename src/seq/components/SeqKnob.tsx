// Minimal interactive knob for SQ-4 — same look as the WT-1/BL-1 knobs
// (shared .knob styles from index.css) but driven by a plain 0..1 value
// instead of a synth parameter table.

import { A0, KNOB_TRACK, arcPath, degOf, useKnobDrag } from '../../components/knobKit';

interface SeqKnobProps {
  value: number; // 0..1
  onChange: (v: number) => void;
  label: string;
  size?: 'md' | 'sm' | 'xs';
  defaultValue?: number;
}

export function SeqKnob({ value, onChange, label, size = 'md', defaultValue = 0.5 }: SeqKnobProps) {
  const { elRef, handlers } = useKnobDrag(value, onChange);

  const deg = degOf(value);
  const pct = Math.round(value * 100);

  return (
    <div
      ref={elRef}
      className={`knob knob-${size}`}
      data-accent="n"
      tabIndex={0}
      role="slider"
      aria-label={label}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={pct}
      {...handlers}
      onDoubleClick={() => onChange(defaultValue)}
    >
      <svg viewBox="0 0 80 80">
        <circle className="k-body" cx="40" cy="40" r="26" />
        <path className="k-track" d={KNOB_TRACK} />
        <path className="k-arc" d={arcPath(40, 40, 33, A0, deg)} />
        <line className="k-ptr" x1="40" y1="40" x2="40" y2="17" transform={`rotate(${deg} 40 40)`} />
      </svg>
      <div className="k-label">{label}</div>
      <div className="k-value">{pct}</div>
    </div>
  );
}
