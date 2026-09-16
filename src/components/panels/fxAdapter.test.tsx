import { describe, expect, it } from 'vitest';
import { renderToStaticMarkup } from 'react-dom/server';
import { DrivePanel } from './DrivePanel';
import { DynamicsPanel } from './DynamicsPanel';
import { TapeEchoPanel } from './TapeEchoPanel';
import { ReverbPanel } from './ReverbPanel';
import type { FxPanelAdapter } from './fxAdapter';

describe('shared instrument FX panels', () => {
  it.each(['', 'pad3.'])('binds supported controls to the instrument namespace %s', prefix => {
    const controls: string[] = [];
    const adapter: FxPanelAdapter = {
      prefix, params: { [`${prefix}fx.delay.time`]: 0.36 }, setParam: () => {},
      engine: { subscribeDynamics: () => () => {}, subscribeEcho: () => () => {}, subscribeReverb: () => () => {} },
      renderKnob: id => { controls.push(id); return <span>{id}</span>; },
      renderPower: id => <button>{id}</button>,
    };
    const html = renderToStaticMarkup(<>
      <DynamicsPanel kind="ott" adapter={adapter} /><DynamicsPanel kind="comp" adapter={adapter} />
      <DrivePanel adapter={adapter} />
      <TapeEchoPanel adapter={{ ...adapter, title: 'DELAY' }} />
      <ReverbPanel adapter={{ ...adapter, context: prefix ? 'AUX 2' : undefined }} />
    </>);
    expect(controls).toEqual(['ott.depth', 'ott.time', 'ott.up', 'ott.down', 'comp.thr', 'comp.att', 'comp.rel', 'comp.ratio', 'drive.amt', 'drive.tone', 'drive.mix',
      'delay.time', 'delay.fb', 'delay.mix', 'reverb.size', 'reverb.mix'].map(id => `${prefix}fx.${id}`));
    expect(html).toContain('Saturation type');
    expect(html).toContain('TRANSFER / MIX');
    expect(html).not.toContain('TAPE ECHO');
    expect(html).not.toContain('DRIFT');
    if (prefix) expect(html).toContain('SHARED BUS RETURN');
  });
});
