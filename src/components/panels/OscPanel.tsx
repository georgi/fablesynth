import { Knob } from '../Knob';
import { Stepper } from '../Stepper';
import { PowerButton } from '../PowerButton';
import { VSlider } from '../VSlider';
import { WavetableView } from '../displays/WavetableView';
import { ACCENTS } from '../../constants';
import { WARP_CROSS, warpNeutral } from '../../params';
import { engine, useStore } from '../../store';

interface OscPanelProps {
  prefix: 'oscA' | 'oscB';
  accentKey: 'a' | 'b';
  title: string;
  gridArea: string;
}

export function OscPanel({ prefix, accentKey, title, gridArea }: OscPanelProps) {
  const on = useStore((s) => s.params[`${prefix}.on`]);
  const tableIndex = useStore((s) => s.params[`${prefix}.table`]);
  const pos = useStore((s) => s.params[`${prefix}.pos`]);
  const powered = useStore((s) => s.powered);
  const modPos = useStore((s) => (prefix === 'oscA' ? s.modPosA : s.modPosB));
  const userTables = useStore((s) => s.userTables); // re-render when the pool changes
  const openEditor = useStore((s) => s.openEditor);
  const hosted = useStore((s) => s.hosted);
  const setParam = useStore((s) => s.setParam);
  void userTables;

  const table = powered && engine.tables ? engine.tables[tableIndex | 0] : null;

  // Warp preview. FM, AM and RM read the other oscillator, so the view needs
  // its table, frame and tuning ratio as well.
  const other = prefix === 'oscA' ? 'oscB' : 'oscA';
  const warpMode = useStore((s) => s.params[`${prefix}.warp`]) | 0;
  const warpAmt = useStore((s) => s.params[`${prefix}.warpamt`]);
  const cross = warpMode >= WARP_CROSS;
  const modOn = useStore((s) => (cross ? s.params[`${other}.on`] : 0));
  const modTable = useStore((s) => (cross ? s.params[`${other}.table`] : 0));
  const modPosB = useStore((s) => (cross ? s.params[`${other}.pos`] : 0));
  const modSemis = useStore((s) => (cross
    ? (s.params[`${other}.oct`] - s.params[`${prefix}.oct`]) * 12 + s.params[`${other}.semi`] - s.params[`${prefix}.semi`]
      + (s.params[`${other}.fine`] - s.params[`${prefix}.fine`]) / 100
    : 0));
  const modTableViz = cross && modOn && powered && engine.tables ? engine.tables[modTable | 0] ?? null : null;
  const warp = warpMode === 0 || (cross && !modTableViz) ? null
    : { mode: warpMode, amount: warpAmt, mod: modTableViz, modPos: modPosB, ratio: Math.pow(2, modSemis / 12) };

  return (
    <section className={`panel panel-osc${on ? '' : ' off'}`} id={`panel-${prefix}`} data-accent={accentKey} style={{ gridArea }}>
      <div className="panel-head">
        <span className="ph-power"><PowerButton paramId={`${prefix}.on`} /></span>
        <h2>{title}</h2>
        {/* A new warp mode starts at its neutral amount. */}
        <span className="osc-warp" title={cross ? `modulator: OSC ${other === 'oscA' ? 'A' : 'B'} (must be on; its level can be 0)` : 'oscillator warp mode'}>
          <Stepper paramId={`${prefix}.warp`} label="WARP" accent={accentKey}
            onStep={(mode) => setParam(`${prefix}.warpamt`, warpNeutral(mode))} />
        </span>
        <span className="ph-stepper">
          {!hosted && (
            <button className="wt-edit" aria-label="edit wavetable" title="import / draw wavetable" onClick={() => openEditor(prefix)}>✎</button>
          )}
          <Stepper paramId={`${prefix}.table`} />
        </span>
      </div>
      <div className="osc-body">
        <WavetableView className="wt3d" table={table} pos={pos} modPos={modPos} accent={ACCENTS[accentKey]} warp={warp} />
        <div className="pos-holder">
          <VSlider paramId={`${prefix}.pos`} accent={accentKey} ghost={modPos} />
        </div>
        <div className="osc-knobs">
          <Knob paramId={`${prefix}.oct`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.semi`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.fine`} size="sm" accent={accentKey} />
          <Stepper paramId={`${prefix}.unison`} label="UNI" accent={accentKey} />
          <Knob paramId={`${prefix}.detune`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.spread`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.blend`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.warpamt`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.level`} size="md" accent={accentKey} />
          <Knob paramId={`${prefix}.pan`} size="sm" accent={accentKey} />
        </div>
      </div>
    </section>
  );
}
