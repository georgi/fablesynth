import { useBassStore } from '../store';
import { BassKnob } from './BassKnob';

interface FxGroupProps {
  effect: 'drive' | 'chorus' | 'delay' | 'reverb';
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
      </div>
    </section>
  );
}

export function BassFxRack() {
  return (
    <section className="panel bl-fx-panel">
      <div className="fx-rack bl-fx-rack">
        <FxGroup effect="drive" title="DRIVE" note="POST-ACCENT" knobs={['fx.drive.amt', 'fx.drive.mix']} />
        <FxGroup effect="chorus" title="CHORUS" knobs={['fx.chorus.rate', 'fx.chorus.depth', 'fx.chorus.mix']} />
        <FxGroup effect="delay" title="DELAY" note="PING-PONG · SYNC" knobs={['fx.delay.time', 'fx.delay.fb', 'fx.delay.mix']} />
        <FxGroup effect="reverb" title="REVERB" note="NO COMP · ACCENTS LIVE" knobs={['fx.reverb.size', 'fx.reverb.mix']} />
      </div>
    </section>
  );
}
