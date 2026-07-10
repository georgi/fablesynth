import { useDrumStore } from '../store';
import { DrumKnob } from './DrumKnob';
import { OutPanel } from './OutPanel';

interface FxGroupProps {
  effect: 'drive' | 'comp' | 'chorus' | 'delay' | 'reverb';
  title: string;
  knobs: string[];
}

function FxGroup({ effect, title, knobs }: FxGroupProps) {
  const onId = `fx.${effect}.on`;
  const on = useDrumStore((s) => s.params[onId]);
  const setParam = useDrumStore((s) => s.setParam);

  return (
    <section className={`fx-group${on === 1 ? ' on' : ''}`} data-accent="n">
      <div className="fx-group-head">
        <button
          className={`power-btn fx-power${on === 1 ? ' on' : ''}`}
          type="button"
          aria-label={`${title} power`}
          aria-pressed={on === 1}
          onClick={() => setParam(onId, on === 1 ? 0 : 1)}
        />
        <h2>{title}</h2>
      </div>
      <div className="fx-knobs">
        {knobs.map((paramId) => <DrumKnob paramId={paramId} size="sm" accent="n" key={paramId} />)}
      </div>
    </section>
  );
}

export function FxRack() {
  return (
    <section className="panel dr-fx-panel">
      <div className="fx-rack">
        <FxGroup effect="drive" title="DRIVE" knobs={['fx.drive.amt', 'fx.drive.mix']} />
        <FxGroup effect="comp" title="COMP" knobs={['fx.comp.thr', 'fx.comp.gain']} />
        <FxGroup effect="chorus" title="CHORUS" knobs={['fx.chorus.rate', 'fx.chorus.depth', 'fx.chorus.mix']} />
        <FxGroup effect="delay" title="DELAY" knobs={['fx.delay.time', 'fx.delay.fb', 'fx.delay.mix']} />
        <FxGroup effect="reverb" title="REVERB" knobs={['fx.reverb.size', 'fx.reverb.mix']} />
        <OutPanel />
      </div>
    </section>
  );
}
