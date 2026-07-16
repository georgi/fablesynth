import { Knob } from '../Knob';
import { PowerButton } from '../PowerButton';

function FxModule({ fx, title, knobs }: { fx: string; title: string; knobs: string[] }) {
  return (
    <div className="fx-module" data-fx={fx}>
      <div className="panel-head">
        <span className="ph-power"><PowerButton paramId={`fx.${fx}.on`} /></span>
        <h2>{title}</h2>
      </div>
      <div className="knob-row">
        {knobs.map((k) => (
          <Knob key={k} paramId={`fx.${fx}.${k}`} size="sm" accent="n" />
        ))}
      </div>
    </div>
  );
}

export function FxPanel() {
  return (
    <section className="panel panel-fx" style={{ gridArea: 'fx' }}>
      <FxModule fx="eq" title="EQ" knobs={['low', 'mid', 'mfreq', 'high']} />
      <FxModule fx="drive" title="DRIVE" knobs={['amt', 'mix']} />
      <FxModule fx="chorus" title="CHORUS" knobs={['rate', 'depth', 'mix']} />
      <FxModule fx="delay" title="DELAY" knobs={['time', 'fb', 'mix']} />
      <FxModule fx="reverb" title="REVERB" knobs={['size', 'mix']} />
      <FxModule fx="comp" title="COMP" knobs={['thr', 'gain']} />
    </section>
  );
}
