import { useLayoutEffect } from 'react';
import { drumEngine, useDrumStore } from '../store';
import { useSeqStore } from '../../seq/store';
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
  const scope = useSeqStore((s) => s.drumFxScope);
  const openDrumFx = useSeqStore((s) => s.openDrumFx);
  const params = useDrumStore(s => s.params);
  const setParam = useDrumStore(s => s.setParam);
  const activeEngine = drumEngine;
  const bus = Math.max(0, Math.min(OUT_NAMES.length - 1, params[pad(selectedPad, 'out')] | 0));
  useLayoutEffect(() => { activeEngine.setMeterPad(selectedPad); }, [activeEngine, selectedPad, bus]);
  const adapter: FxPanelAdapter = {
    engine: activeEngine,
    params,
    setParam,
    prefix: scope === 'pad' ? pad(selectedPad, '') : '',
    renderKnob: (id, key) => <DrumKnob paramId={id} label={id.includes('fx.eq.') ? key?.toUpperCase() : undefined} size="sm" accent="n" />,
    renderPower: (id) => <DrumPower paramId={id} />,
  };

  return (
    <section className="panel dr-fx-panel" aria-label={scope === 'pad' ? 'DR-1 selected pad effects' : 'DR-1 drum group channel strip'}>
      <div className="dr-fx-head">
        <span className="dr-led dr-led-a" aria-hidden="true" />
        <h2>{scope === 'pad' ? 'PAD FX INSERT' : 'DRUM GROUP CHANNEL STRIP'}</h2>
        <div className="dr-fx-scope" role="group" aria-label="FX scope">
          <button type="button" className={scope === 'pad' ? 'active' : ''} aria-pressed={scope === 'pad'} onClick={() => openDrumFx('pad')}>PAD FX</button>
          <button type="button" className={scope === 'group' ? 'active' : ''} aria-pressed={scope === 'group'} onClick={() => openDrumFx('group')}>GROUP FX</button>
        </div>
        <span className="dr-fx-padname">{scope === 'pad' ? `PAD ${String(selectedPad + 1).padStart(2, '0')}` : 'ALL 16 PADS'}</span>
        <span className="dr-fx-flow" aria-hidden="true">EQ › OTT › COMP › DRIVE › CHORUS › DELAY › REVERB</span>
      </div>
      <div className="fx-rack">
        <EqPanel key={`${scope}-${selectedPad}`} adapter={adapter} />
        <DynamicsPanel kind="ott" adapter={adapter} />
        <DynamicsPanel kind="comp" adapter={adapter} />
        <DrivePanel adapter={adapter} />
        <ChorusPanel adapter={adapter} />
        <TapeEchoPanel adapter={{ ...adapter, title: 'DELAY', context: 'PING-PONG' }} />
        <ReverbPanel adapter={{ ...adapter, context: scope === 'pad' ? OUT_NAMES[bus] : 'POST MIX' }} />
        {scope === 'pad' && <OutPanel />}
      </div>
    </section>
  );
}
