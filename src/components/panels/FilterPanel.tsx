import { Knob } from '../Knob';
import { Stepper } from '../Stepper';
import { PowerButton } from '../PowerButton';
import { FilterView } from '../displays/FilterView';
import { ACCENTS } from '../../constants';
import { useStore } from '../../store';

function FilterBlock({ prefix, label }: { prefix: string; label: string }) {
  const on = useStore((s) => s.params[`${prefix}.on`]);
  return (
    <div className={`filter-block${on ? '' : ' off'}`}>
      <div className="filter-subhead">
        <PowerButton paramId={`${prefix}.on`} />
        <span className="fb-label">{label}</span>
        <Stepper paramId={`${prefix}.type`} accent="f" />
      </div>
      <div className="knob-row">
        <Knob paramId={`${prefix}.cutoff`} size="md" accent="f" />
        <Knob paramId={`${prefix}.res`} size="sm" accent="f" />
        <Knob paramId={`${prefix}.drive`} size="sm" accent="f" />
        <Knob paramId={`${prefix}.env`} size="sm" accent="f" />
        <Knob paramId={`${prefix}.key`} size="sm" accent="f" />
      </div>
    </div>
  );
}

export function FilterPanel() {
  const t1 = useStore((s) => s.params['filter.type']);
  const c1 = useStore((s) => s.params['filter.cutoff']);
  const r1 = useStore((s) => s.params['filter.res']);
  const on1 = useStore((s) => s.params['filter.on']);
  const t2 = useStore((s) => s.params['filter2.type']);
  const c2 = useStore((s) => s.params['filter2.cutoff']);
  const r2 = useStore((s) => s.params['filter2.res']);
  const on2 = useStore((s) => s.params['filter2.on']);
  const route = useStore((s) => s.params['filter.route']);

  const filters = [
    { type: t1, cutoff: c1, res: r1, on: !!on1 },
    { type: t2, cutoff: c2, res: r2, on: !!on2 },
  ];

  return (
    <section className="panel panel-filter" data-accent="f" style={{ gridArea: 'filter' }}>
      <div className="panel-head">
        <h2>FILTERS</h2>
        <span className="ph-stepper"><Stepper paramId="filter.route" accent="f" /></span>
      </div>
      <FilterView className="filter-curve" filters={filters} route={route | 0} accent={ACCENTS.f} />
      <FilterBlock prefix="filter" label="F1" />
      <FilterBlock prefix="filter2" label="F2" />
    </section>
  );
}
