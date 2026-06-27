import { Knob } from '../Knob';
import { EnvView } from '../displays/EnvView';
import { ModSourceChip } from '../ModSourceChip';
import { useStore } from '../../store';

interface EnvPanelProps {
  id: 'env1' | 'env2';
  title: string;
  gridArea: string;
  viewAccent: string;
  knobAccent: 'n' | 'f';
  modSource?: number; // MOD_SOURCES index, if this envelope can be dragged as a source
}

export function EnvPanel({ id, title, gridArea, viewAccent, knobAccent, modSource }: EnvPanelProps) {
  const a = useStore((s) => s.params[`${id}.a`]);
  const d = useStore((s) => s.params[`${id}.d`]);
  const sus = useStore((s) => s.params[`${id}.s`]);
  const r = useStore((s) => s.params[`${id}.r`]);

  return (
    <section className="panel panel-env" style={{ gridArea }}>
      <div className="panel-head">{modSource && <ModSourceChip src={modSource} compact />}<h2>{title}</h2></div>
      <EnvView className="env-curve" a={a} d={d} s={sus} r={r} accent={viewAccent} />
      <div className="knob-row">
        <Knob paramId={`${id}.a`} size="sm" accent={knobAccent} />
        <Knob paramId={`${id}.d`} size="sm" accent={knobAccent} />
        <Knob paramId={`${id}.s`} size="sm" accent={knobAccent} />
        <Knob paramId={`${id}.r`} size="sm" accent={knobAccent} />
      </div>
    </section>
  );
}
