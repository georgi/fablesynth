import { useLayoutEffect } from 'react';
import { drumEngine, useDrumStore } from '../store';
import { OUT_NAMES, pad } from '../params';
import { DrumKnob } from './DrumKnob';
import { OutPanel } from './OutPanel';
import { EqPanel } from '../../components/panels/EqPanel';
import { DrivePanel } from '../../components/panels/DrivePanel';
import { ChorusPanel } from '../../components/panels/ChorusPanel';
import { DynamicsPanel } from '../../components/panels/DynamicsPanel';
import { TapeEchoPanel } from '../../components/panels/TapeEchoPanel';
import { ReverbPanel } from '../../components/panels/ReverbPanel';
import type { FxPanelAdapter } from '../../components/panels/fxAdapter';

function DrumPower({ paramId }: { paramId: string }) {
  const on = useDrumStore(s => s.params[paramId] > 0.5);
  const setParam = useDrumStore(s => s.setParam);
  return <button className={`power-btn fx-power${on ? ' on' : ''}`} type="button" aria-label={`${paramId} power`} aria-pressed={on} onClick={() => setParam(paramId, on ? 0 : 1)} />;
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
        <DrivePanel adapter={adapter} />
        <ChorusPanel adapter={adapter} />
        <TapeEchoPanel adapter={{ ...adapter, title: 'DELAY', context: 'PING-PONG' }} />
        <ReverbPanel adapter={{ ...adapter, context: OUT_NAMES[bus] }} />
        <OutPanel />
      </div>
    </section>
  );
}
