import { OUT_NAMES, pad } from '../params';
import { useDrumStore } from '../store';

export function OutPanel() {
  const padNames = useDrumStore((s) => s.padNames);
  const out0 = useDrumStore((s) => s.params[pad(0, 'out')]);
  const out1 = useDrumStore((s) => s.params[pad(1, 'out')]);
  const out2 = useDrumStore((s) => s.params[pad(2, 'out')]);
  const out3 = useDrumStore((s) => s.params[pad(3, 'out')]);
  const out4 = useDrumStore((s) => s.params[pad(4, 'out')]);
  const out5 = useDrumStore((s) => s.params[pad(5, 'out')]);
  const out6 = useDrumStore((s) => s.params[pad(6, 'out')]);
  const out7 = useDrumStore((s) => s.params[pad(7, 'out')]);
  const out8 = useDrumStore((s) => s.params[pad(8, 'out')]);
  const out9 = useDrumStore((s) => s.params[pad(9, 'out')]);
  const out10 = useDrumStore((s) => s.params[pad(10, 'out')]);
  const out11 = useDrumStore((s) => s.params[pad(11, 'out')]);
  const out12 = useDrumStore((s) => s.params[pad(12, 'out')]);
  const out13 = useDrumStore((s) => s.params[pad(13, 'out')]);
  const out14 = useDrumStore((s) => s.params[pad(14, 'out')]);
  const out15 = useDrumStore((s) => s.params[pad(15, 'out')]);
  const outputs = [out0, out1, out2, out3, out4, out5, out6, out7, out8, out9, out10, out11, out12, out13, out14, out15];

  const namesFor = (output: number) => padNames.filter((_, i) => outputs[i] === output);
  const mainCount = namesFor(0).length;

  return (
    <section className="fx-group out-panel">
      <div className="fx-group-head out-head">
        <h2>OUT</h2>
        <span>FX → MAIN ONLY</span>
      </div>
      <div className="out-routes">
        <div className="out-route main">
          <span className="out-dot" aria-hidden="true" />
          <strong>MAIN</strong>
          <span>{mainCount} {mainCount === 1 ? 'PAD' : 'PADS'}</span>
        </div>
        {OUT_NAMES.slice(1).map((name, output) => {
          const assigned = namesFor(output + 1);
          return (
            <div className={`out-route${assigned.length ? ' assigned' : ''}`} key={name}>
              <span className="out-dot" aria-hidden="true" />
              <strong>{name}</strong>
              <span title={assigned.join(', ')}>{assigned.length ? assigned.join(', ') : '—'}</span>
            </div>
          );
        })}
      </div>
    </section>
  );
}
