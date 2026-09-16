import { useLayoutEffect } from 'react';
import { drumEngine, useDrumStore } from '../store';
import { OUT_NAMES, pad } from '../params';
import { DrumKnob } from './DrumKnob';
import { OutPanel } from './OutPanel';
import { EqPanel } from '../../components/panels/EqPanel';
import { DynamicsPanel } from '../../components/panels/DynamicsPanel';
import { TapeEchoPanel } from '../../components/panels/TapeEchoPanel';
import { ReverbPanel } from '../../components/panels/ReverbPanel';
import type { FxPanelAdapter } from '../../components/panels/fxAdapter';

function DrumPower({ paramId }: { paramId: string }) {
  const on = useDrumStore(s => s.params[paramId] > 0.5);
  const setParam = useDrumStore(s => s.setParam);
  return <button className={`power-btn fx-power${on ? ' on' : ''}`} type="button" aria-label={`${paramId} power`} aria-pressed={on} onClick={() => setParam(paramId, on ? 0 : 1)} />;
}

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
        {effect === 'comp' && <span className="fx-auto-gain">AUTO GAIN</span>}
      </div>
    </section>
  );
}

export function FxRack() {
  const selectedPad = useDrumStore((s) => s.sel);
  const padName = useDrumStore((s) => s.padNames[selectedPad]);
  const padNumber = String(selectedPad + 1).padStart(2, '0');
  const params = useDrumStore(s => s.params);
  const setParam = useDrumStore(s => s.setParam);
  const activeEngine = drumEngine;
  const bus = Math.max(0, Math.min(OUT_NAMES.length - 1, params[pad(selectedPad, 'out')] | 0));
  useLayoutEffect(() => { activeEngine.setMeterPad(selectedPad); }, [activeEngine, selectedPad, bus]);
  const adapter: FxPanelAdapter = {
    engine: activeEngine,
    params,
    setParam,
    prefix: pad(selectedPad, ''),
    renderKnob: (id, key) => <DrumKnob paramId={id} label={id.includes('fx.eq.') ? key?.toUpperCase() : undefined} size="sm" accent="n" />,
    renderPower: (id) => <DrumPower paramId={id} />,
  };

  return (
    <section className="panel dr-fx-panel" aria-label={`Pad ${padNumber} ${padName} FX chain`}>
      <div className="dr-fx-head">
        <span className="dr-led dr-led-a" aria-hidden="true" />
        <h2>PAD {padNumber} FX CHAIN</h2>
        <span className="dr-fx-padname">{padName}</span>
        <span className="dr-fx-flow" aria-hidden="true">EQ › OTT › COMP › DRIVE › CHORUS › DELAY › REVERB</span>
      </div>
      <div className="fx-rack">
        <EqPanel key={selectedPad} adapter={adapter} />
        <DynamicsPanel kind="ott" adapter={adapter} />
        <DynamicsPanel kind="comp" adapter={adapter} />
        <FxGroup padIndex={selectedPad} effect="drive" title="DRIVE" knobs={['fx.drive.amt', 'fx.drive.mix']} />
        <FxGroup padIndex={selectedPad} effect="chorus" title="CHORUS" knobs={['fx.chorus.rate', 'fx.chorus.depth', 'fx.chorus.mix']} />
        <TapeEchoPanel adapter={{ ...adapter, title: 'DELAY', context: 'PING-PONG' }} />
        <ReverbPanel adapter={{ ...adapter, context: OUT_NAMES[bus] }} />
        <OutPanel />
      </div>
    </section>
  );
}
