import { Knob } from '../Knob';
import { Stepper } from '../Stepper';
import { LFOView } from '../displays/LFOView';
import { ModSourceChip } from '../ModSourceChip';
import { ACCENTS } from '../../constants';
import { useStore } from '../../store';

function LfoBlock({ id, src, title, accentKey }: { id: 'lfo1' | 'lfo2'; src: number; title: string; accentKey: 'a' | 'b' }) {
  const shape = useStore((s) => s.params[`${id}.shape`]);
  const rate = useStore((s) => s.params[`${id}.rate`]);
  return (
    <div className="lfo-block">
      <div className="panel-head">
        <ModSourceChip src={src} compact />
        <h2>{title}</h2>
        <span className="ph-stepper"><Stepper paramId={`${id}.shape`} /></span>
      </div>
      <LFOView className="lfo-curve" shape={shape} rate={rate} accent={ACCENTS[accentKey]} />
      <div className="knob-row">
        <Knob paramId={`${id}.rate`} size="md" accent={accentKey} />
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
