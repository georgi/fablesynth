import { Knob } from '../Knob';
import { Stepper } from '../Stepper';
import { LFOView } from '../displays/LFOView';
import { ACCENTS } from '../../constants';
import { useStore } from '../../store';

function Toggle({ id, label }: { id: string; label: string }) {
  const v = useStore((s) => s.params[id]);
  const setParam = useStore((s) => s.setParam);
  return (
    <button className={`lfo-tg${v ? ' on' : ''}`} aria-pressed={!!v} onClick={() => setParam(id, v ? 0 : 1)}>
      {label}
    </button>
  );
}

function LfoBlock({ id, title, accentKey }: { id: 'lfo1' | 'lfo2'; title: string; accentKey: 'a' | 'b' }) {
  const shape = useStore((s) => s.params[`${id}.shape`]);
  const rate = useStore((s) => s.params[`${id}.rate`]);
  const sync = useStore((s) => s.params[`${id}.sync`]);
  return (
    <div className="lfo-block" data-accent={accentKey}>
      <div className="panel-head">
        <h2>{title}</h2>
        <span className="ph-stepper"><Stepper paramId={`${id}.shape`} /></span>
      </div>
      <LFOView className="lfo-curve" shape={shape} rate={rate} accent={ACCENTS[accentKey]} />
      <div className="lfo-toggles">
        <Toggle id={`${id}.sync`} label="SYNC" />
        <Toggle id={`${id}.retrig`} label="TRIG" />
      </div>
      <div className="knob-row lfo-knobs">
        {sync
          ? <span className="lfo-div"><Stepper paramId={`${id}.syncrate`} accent={accentKey} /></span>
          : <Knob paramId={`${id}.rate`} size="sm" accent={accentKey} />}
        <Knob paramId={`${id}.rise`} size="sm" accent={accentKey} />
        <Knob paramId={`${id}.phase`} size="sm" accent={accentKey} />
      </div>
    </div>
  );
}

export function LfoPanel() {
  return (
    <section className="panel panel-lfos" style={{ gridArea: 'lfos' }}>
      <LfoBlock id="lfo1" title="LFO 1" accentKey="a" />
      <LfoBlock id="lfo2" title="LFO 2" accentKey="b" />
    </section>
  );
}
