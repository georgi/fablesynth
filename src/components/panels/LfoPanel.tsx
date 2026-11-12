import { Knob } from '../Knob';
import { Stepper } from '../Stepper';
import { LFOView } from '../displays/LFOView';
import { ModSourceChip } from '../ModSourceChip';
import { ACCENTS } from '../../constants';
import type * as React from 'react';
import { useMemo, useState } from 'react';
import { LFO_CUSTOM, LFO_DIV_F, SYNC_BPM, lfoSource } from '../../params';
import { readLfoShape, seedLfoShape } from '../../lfoShape';
import { useStore } from '../../store';

function Toggle({ id, label, title }: { id: string; label: string; title?: string }) {
  const v = useStore((s) => s.params[id]);
  const setParam = useStore((s) => s.setParam);
  return (
    <button className={`lfo-tg${v ? ' on' : ''}`} aria-pressed={!!v} title={title} onClick={() => setParam(id, v ? 0 : 1)}>
      {label}
    </button>
  );
}

// The drawn shape of one LFO. The key changes only when one of its point
// params changes, so the points array is stable between unrelated edits.
function useLfoShape(lfo: number) {
  const key = useStore((s) => {
    const n = s.params[`lfo${lfo}.pn`] | 0;
    let k = String(n);
    for (let i = 1; i <= n; i++) k += `|${s.params[`lfo${lfo}.p${i}.x`]},${s.params[`lfo${lfo}.p${i}.y`]},${s.params[`lfo${lfo}.p${i}.c`]}`;
    return k;
  });
  return useMemo(() => readLfoShape(useStore.getState().params, lfo), [key, lfo]);
}

function LfoBlock({ id, lfo, src, accentKey, picker }: { id: string; lfo: number; src: number; accentKey: 'a' | 'b'; picker: React.ReactNode }) {
  const shape = useStore((s) => s.params[`${id}.shape`]);
  const oneshot = useStore((s) => s.params[`${id}.oneshot`]);
  const setLfoShape = useStore((s) => s.setLfoShape);
  const points = useLfoShape(lfo);
  const [paint, setPaint] = useState(false);
  const custom = (shape | 0) === LFO_CUSTOM;
  const rate = useStore((s) => s.params[`${id}.rate`]);
  const sync = useStore((s) => s.params[`${id}.sync`]);
  const syncrate = useStore((s) => s.params[`${id}.syncrate`]);
  // The display animates at the LFO's actual speed: the synced division when
  // SYNC is on, otherwise the free RATE in Hz.
  const dispRate = sync
    ? (SYNC_BPM / 60) * LFO_DIV_F[Math.min(LFO_DIV_F.length - 1, Math.max(0, syncrate | 0))]
    : rate;
  return (
    <div className="lfo-block" data-accent={accentKey}>
      <div className="panel-head">
        <ModSourceChip src={src} compact />
        <h2>LFO</h2>
        {picker}
        <div className="lfo-toggles">
          <Toggle id={`${id}.sync`} label="SYNC" />
          <Toggle id={`${id}.retrig`} label="TRIG" />
        </div>
        {/* A first visit to CUSTOM starts from the shape the LFO played before. */}
        <span className="ph-stepper"><Stepper paramId={`${id}.shape`}
          onStep={(next) => { if (next === LFO_CUSTOM && !points.length) setLfoShape(lfo, seedLfoShape(shape | 0)); }} /></span>
      </div>
      <LFOView className="lfo-curve" shape={shape} rate={dispRate} accent={ACCENTS[accentKey]}
        points={points} onPoints={(next) => setLfoShape(lfo, next)} paint={paint} oneshot={!!oneshot} />
      <div className="knob-row lfo-knobs">
        {sync
          ? <span className="lfo-div"><Stepper paramId={`${id}.syncrate`} accent={accentKey} /></span>
          : <Knob paramId={`${id}.rate`} size="sm" accent={accentKey} />}
        <Knob paramId={`${id}.rise`} size="sm" accent={accentKey} />
        <Knob paramId={`${id}.phase`} size="sm" accent={accentKey} />
        <div className="lfo-modes">
          <Toggle id={`${id}.oneshot`} label="1SHOT" title="One-shot: run one cycle from the note start, then hold the end of the shape" />
          <button className={`lfo-tg${paint ? ' on' : ''}`} aria-pressed={paint} disabled={!custom}
            title={custom ? 'STEP: drag in the shape to paint sixteen held steps' : 'STEP painting applies to the CUSTOM shape'}
            onClick={() => setPaint((v) => !v)}>STEP</button>
        </div>
      </div>
    </div>
  );
}

// Four LFOs in two slots: each slot shows one of two LFOs (left 1|3, right
// 2|4), picked by the number buttons beside its title.
export function LfoPanel() {
  const [choice, setChoice] = useState([0, 0]);
  return (
    <section className="panel panel-lfos" style={{ gridArea: 'lfos' }}>
      {[0, 1].map((slot) => {
        const n = slot + 1 + choice[slot]! * 2;
        const picker = (
          <span className="lfo-pick" role="tablist" aria-label={`LFO slot ${slot + 1}`}>
            {[0, 1].map((c) => (
              <button key={c} type="button" role="tab" className="lfo-pick-btn" aria-selected={choice[slot] === c}
                title={`show LFO ${slot + 1 + c * 2}`}
                onClick={() => setChoice((cur) => cur.map((v, i) => (i === slot ? c : v)))}>{slot + 1 + c * 2}</button>
            ))}
          </span>
        );
        return <LfoBlock key={n} id={`lfo${n}`} lfo={n} src={lfoSource(n)} accentKey={slot ? 'b' : 'a'} picker={picker} />;
      })}
    </section>
  );
}
