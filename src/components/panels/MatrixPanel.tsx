import { Knob } from '../Knob';
import { MOD_SOURCES, MOD_DESTS } from '../../params';
import { useStore } from '../../store';

function MatrixSelect({ paramId, options }: { paramId: string; options: string[] }) {
  const value = useStore((s) => s.params[paramId]);
  const setParam = useStore((s) => s.setParam);
  return (
    <select
      className="mx-select"
      value={value | 0}
      onChange={(e) => setParam(paramId, parseInt(e.target.value, 10))}
    >
      {options.map((o, i) => (
        <option key={i} value={i}>{o}</option>
      ))}
    </select>
  );
}

export function MatrixPanel() {
  return (
    <section className="panel panel-matrix" style={{ gridArea: 'matrix' }}>
      <div className="panel-head"><h2>MOD MATRIX</h2></div>
      <div className="matrix-rows">
        {[1, 2, 3, 4].map((s) => (
          <div className="mx-row" key={s}>
            <MatrixSelect paramId={`mat${s}.src`} options={MOD_SOURCES} />
            <span className="mx-arrow">▸</span>
            <MatrixSelect paramId={`mat${s}.dst`} options={MOD_DESTS} />
            <Knob paramId={`mat${s}.amt`} size="xs" label="" />
          </div>
        ))}
      </div>
    </section>
  );
}
