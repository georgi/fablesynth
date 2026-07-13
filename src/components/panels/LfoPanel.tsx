import { Knob } from '../Knob';
import { Stepper } from '../Stepper';
import { LFOView } from '../displays/LFOView';
import { ModSourceChip } from '../ModSourceChip';
import { ACCENTS } from '../../constants';
import { LFO_DIV_F, SYNC_BPM } from '../../params';
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

function LfoBlock({ id, src, title, accentKey }: { id: 'lfo1' | 'lfo2'; src: number; title: string; accentKey: 'a' | 'b' }) {
  const shape = useStore((s) => s.params[`${id}.shape`]);
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
        <h2>{title}</h2>
        <div className="lfo-toggles">
          <Toggle id={`${id}.sync`} label="SYNC" />
          <Toggle id={`${id}.retrig`} label="TRIG" />
        </div>
        <span className="ph-stepper"><Stepper paramId={`${id}.shape`} /></span>
      </div>
      <LFOView className="lfo-curve" shape={shape} rate={dispRate} accent={ACCENTS[accentKey]} />
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
      <LfoBlock id="lfo1" src={1} title="LFO 1" accentKey="a" />
      <LfoBlock id="lfo2" src={2} title="LFO 2" accentKey="b" />
    </section>
  );
}
