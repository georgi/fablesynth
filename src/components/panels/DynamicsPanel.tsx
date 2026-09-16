import { Knob } from '../Knob';
import { PowerButton } from '../PowerButton';
import { DynamicsView } from '../displays/DynamicsView';
import { engine as wtEngine, useStore } from '../../store';
import type { FxPanelAdapter } from './fxAdapter';
import { fxId } from './fxAdapter';
import './dynamics.css';

export function DynamicsPanel({ kind, adapter }: { kind: 'ott' | 'comp'; adapter?: FxPanelAdapter }) {
  const wtParams = useStore(s => adapter ? null : s.params);
  const params = adapter?.params ?? wtParams!;
  const prefix = adapter?.prefix ?? '', on = params[fxId(prefix, `fx.${kind}.on`)] > 0.5;
  const power = adapter?.renderPower ?? ((id: string) => <PowerButton paramId={id} />);
  const knob = adapter?.renderKnob ?? ((id: string) => <Knob paramId={id} size="sm" accent="n" />);
  return <section className={`panel panel-dynamics${on ? '' : ' dynamics-bypassed'}`} style={{ gridArea: kind }} aria-label={kind === 'ott' ? 'OTT multiband dynamics' : 'Compressor'}>
    <div className="panel-head">
      {power(fxId(prefix, `fx.${kind}.on`))}
      <h2>{kind === 'ott' ? 'OTT' : 'COMP'}</h2>
      <span className="dynamics-caption">{on ? kind === 'ott' ? '3 BAND' : `${(params[fxId(prefix, 'fx.comp.ratio')] ?? 4).toFixed(1)}:1 · SOFT KNEE` : 'BYPASS'}</span>
      <span className="dynamics-legend">{kind === 'ott' ? 'LEVEL / ± GAIN' : <><i /> IN <i className="legend-out" /> OUT</>}</span>
    </div>
    <DynamicsView kind={kind} telemetryEngine={adapter?.engine ?? wtEngine} telemetryParams={params} prefix={prefix} />
    <div className="dynamics-controls">
      {(kind === 'ott' ? ['depth', 'time', 'up', 'down'] : ['thr', 'att', 'rel', 'ratio']).map(k => <span key={k}>{knob(fxId(prefix, `fx.${kind}.${k}`), k)}</span>)}
    </div>
    {kind === 'comp' && <div className="dynamics-footnote">AUTO GAIN · GR BEFORE AUTO · ~3 s HISTORY</div>}
    {kind === 'ott' && <div className="dynamics-footnote">120 Hz / 2.5 kHz · WET GAIN BEFORE AUTO · ±24 dB VIEW</div>}
  </section>;
}
