import { describe, expect, it } from 'vitest';
import { renderToStaticMarkup } from 'react-dom/server';
import { ChorusPanel, chorusDelayMs } from './ChorusPanel';
import type { FxPanelAdapter } from './fxAdapter';

const render = (params: Record<string, number>, prefix = '', compact = false) => {
  const adapter: FxPanelAdapter = {
    params: Object.fromEntries(Object.entries(params).map(([key, value]) => [`${prefix}fx.chorus.${key}`, value])),
    prefix, setParam: () => {},
    engine: { subscribeDynamics: () => () => {}, subscribeEcho: () => () => {}, subscribeReverb: () => () => {} },
    renderKnob: id => <span>{id}</span>, renderPower: id => <button>{id}</button>,
  };
  return renderToStaticMarkup(<ChorusPanel adapter={adapter} compact={compact} />);
};

describe('chorus modulation preview', () => {
  it('matches the DSP stereo tap delays and opposite excursions', () => {
    expect(chorusDelayMs(0, 1, 1)).toEqual([12, 17]);
    const peak = chorusDelayMs(0.25, 1, 1);
    expect(peak[0]).toBeCloseTo(17.3);
    expect(peak[1]).toBeCloseTo(12.76);
    const trough = chorusDelayMs(0.75, 1, 1);
    expect(trough[0]).toBeCloseTo(6.7);
    expect(trough[1]).toBeCloseTo(21.24);
    // Zero depth still has the DSP's 0.8 ms base modulation.
    expect(chorusDelayMs(0.25, 1, 0)[0]).toBeCloseTo(12.8);
    expect(chorusDelayMs(0.125, 2, 1)).toEqual(peak);
  });

  it('responds to rate, depth and mix without an audio context', () => {
    const baseline = { on: 1, rate: 0.6, depth: 0.5, mix: 0.4 };
    const html = render(baseline);
    for (const update of [{ rate: 4 }, { depth: 1 }, { mix: 1 }]) {
      expect(render({ ...baseline, ...update })).not.toBe(html);
    }
    expect(html).toContain('not a live meter');
    expect(html).toContain('STEREO');
    expect(render({ ...baseline, mix: 0 })).toContain('DRY');
    expect(render({ ...baseline, on: 0 })).toContain('chorus-bypassed');
    expect(render({ ...baseline, on: 0 })).toContain('BYPASS');
  });

  it('supports compact WT and selected drum pad namespaces', () => {
    expect(render({}, '', true)).toContain('fx-module panel-chorus');
    const html = render({ on: 1, rate: 8, depth: 1 }, 'pad7.');
    for (const key of ['on', 'rate', 'depth', 'mix']) expect(html).toContain(`pad7.fx.chorus.${key}`);
    expect(html).not.toMatch(/NaN|Infinity/);
    expect(html).toContain('Rate 8.00 Hz');
  });
});
