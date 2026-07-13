import { useDrumStore } from '../store';
import { pad } from '../params';
import { DrumKnob } from './DrumKnob';
import { OutPanel } from './OutPanel';

interface FxGroupProps {
  padIndex: number;
  effect: 'drive' | 'comp' | 'chorus' | 'delay' | 'reverb';
  title: string;
  knobs: string[];
}

function FxGroup({ padIndex, effect, title, knobs }: FxGroupProps) {
  const onId = pad(padIndex, `fx.${effect}.on`);
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
        {knobs.map((field) => {
          const paramId = pad(padIndex, field);
          return <DrumKnob paramId={paramId} size="sm" accent="n" key={paramId} />;
        })}
      </div>
    </section>
  );
}

export function FxRack() {
  const selectedPad = useDrumStore((s) => s.sel);
  const padName = useDrumStore((s) => s.padNames[selectedPad]);
  const padNumber = String(selectedPad + 1).padStart(2, '0');

  return (
    <section className="panel dr-fx-panel" aria-label={`Pad ${padNumber} ${padName} FX chain`}>
      <div className="dr-fx-head">
        <span className="dr-led dr-led-a" aria-hidden="true" />
        <h2>PAD {padNumber} FX CHAIN</h2>
        <span className="dr-fx-padname">{padName}</span>
        <span className="dr-fx-flow" aria-hidden="true">DRIVE › COMP › CHORUS › DELAY › REVERB</span>
      </div>
      <div className="fx-rack">
        <FxGroup padIndex={selectedPad} effect="drive" title="DRIVE" knobs={['fx.drive.amt', 'fx.drive.mix']} />
        <FxGroup padIndex={selectedPad} effect="comp" title="COMP" knobs={['fx.comp.thr', 'fx.comp.gain']} />
        <FxGroup padIndex={selectedPad} effect="chorus" title="CHORUS" knobs={['fx.chorus.rate', 'fx.chorus.depth', 'fx.chorus.mix']} />
        <FxGroup padIndex={selectedPad} effect="delay" title="DELAY" knobs={['fx.delay.time', 'fx.delay.fb', 'fx.delay.mix']} />
        <FxGroup padIndex={selectedPad} effect="reverb" title="REVERB" knobs={['fx.reverb.size', 'fx.reverb.mix']} />
        <OutPanel />
      </div>
    </section>
  );
}
