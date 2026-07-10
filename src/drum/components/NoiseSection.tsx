import { pad } from '../params';
import { useDrumStore } from '../store';
import { DrumKnob } from './DrumKnob';
import { NoiseView } from './NoiseView';

export function NoiseSection() {
  const sel = useDrumStore((s) => s.sel);
  const colorId = pad(sel, 'noise.color');
  const color = useDrumStore((s) => s.params[colorId]);

  return (
    <section className="panel dr-noise-section" data-accent="b">
      <div className="panel-head">
        <span className="dr-led dr-led-b" aria-hidden="true" />
        <h2>NOISE</h2>
        <span className="st-value dr-noise-type">WHITE</span>
      </div>
      <NoiseView color={color} />
      <div className="knob-row dr-noise-knobs">
        <DrumKnob paramId={colorId} size="sm" accent="b" />
        <DrumKnob paramId={pad(sel, 'noise.level')} size="md" accent="b" />
      </div>
    </section>
  );
}
