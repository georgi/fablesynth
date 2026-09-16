import { bassEngine, useBassStore } from '../store';
import { BassKnob } from './BassKnob';
import { EqPanel } from '../../components/panels/EqPanel';
import { DynamicsPanel } from '../../components/panels/DynamicsPanel';
import { TapeEchoPanel } from '../../components/panels/TapeEchoPanel';
import { ReverbPanel } from '../../components/panels/ReverbPanel';
import type { FxPanelAdapter } from '../../components/panels/fxAdapter';

function BassPower({ paramId }: { paramId: string }) {
  const on = useBassStore(s => s.params[paramId] > 0.5);
  const setParam = useBassStore(s => s.setParam);
  return <button className={`power-btn fx-power${on ? ' on' : ''}`} type="button" aria-label={`${paramId} power`} aria-pressed={on} onClick={() => setParam(paramId, on ? 0 : 1)} />;
}

interface FxGroupProps {
  effect: 'drive' | 'comp' | 'chorus' | 'delay' | 'reverb';
  title: string;
  note?: string;
  knobs: string[];
}

function FxGroup({ effect, title, note, knobs }: FxGroupProps) {
  const onId = `fx.${effect}.on`;
  const on = useBassStore((s) => s.params[onId]);
  const setParam = useBassStore((s) => s.setParam);

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
        {note ? <span className="bl-fx-note">{note}</span> : null}
      </div>
      <div className="fx-knobs">
        {knobs.map((paramId) => <BassKnob paramId={paramId} size="sm" accent="n" key={paramId} />)}
        {effect === 'comp' ? <span className="fx-auto-gain">AUTO GAIN</span> : null}
      </div>
    </section>
  );
}

export function BassFxRack() {
  const params = useBassStore(s => s.params);
  const setParam = useBassStore(s => s.setParam);
  const adapter: FxPanelAdapter = {
    engine: bassEngine,
    params,
    setParam,
    renderKnob: (id, key) => <BassKnob paramId={id} label={id.includes('fx.eq.') ? key?.toUpperCase() : undefined} size="sm" accent="n" />,
    renderPower: (id) => <BassPower paramId={id} />,
  };
  return (
    <section className="panel bl-fx-panel">
      <div className="fx-rack bl-fx-rack">
        <EqPanel adapter={adapter} />
        <DynamicsPanel kind="ott" adapter={adapter} />
        <DynamicsPanel kind="comp" adapter={adapter} />
        <FxGroup effect="drive" title="DRIVE" note="POST-ACCENT" knobs={['fx.drive.amt', 'fx.drive.mix']} />
        <FxGroup effect="chorus" title="CHORUS" knobs={['fx.chorus.rate', 'fx.chorus.depth', 'fx.chorus.mix']} />
        <TapeEchoPanel adapter={{ ...adapter, title: 'DELAY', context: 'PING-PONG' }} />
        <ReverbPanel adapter={adapter} />
      </div>
    </section>
  );
}
