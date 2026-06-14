import { Knob } from '../Knob';
import { Stepper } from '../Stepper';
import { PowerButton } from '../PowerButton';
import { FilterView } from '../displays/FilterView';
import { ACCENTS } from '../../constants';
import { useStore } from '../../store';

export function FilterPanel() {
  const type = useStore((s) => s.params['filter.type']);
  const cutoff = useStore((s) => s.params['filter.cutoff']);
  const res = useStore((s) => s.params['filter.res']);
  const on = useStore((s) => s.params['filter.on']);

  return (
    <section className="panel panel-filter" data-accent="f" style={{ gridArea: 'filter' }}>
      <div className="panel-head">
        <span className="ph-power"><PowerButton paramId="filter.on" /></span>
        <h2>FILTER</h2>
        <span className="ph-stepper"><Stepper paramId="filter.type" /></span>
      </div>
      <FilterView className="filter-curve" type={type} cutoff={cutoff} res={res} on={!!on} accent={ACCENTS.f} />
      <div className="knob-row" id="filter-knobs">
        <Knob paramId="filter.cutoff" size="lg" accent="f" />
        <Knob paramId="filter.res" size="md" accent="f" />
        <Knob paramId="filter.drive" size="sm" accent="f" />
        <Knob paramId="filter.env" size="sm" accent="f" />
        <Knob paramId="filter.key" size="sm" accent="f" />
      </div>
    </section>
  );
}
