import { describe, expect, it } from 'vitest';
import { bootWt } from './workletHarness';
import { makeBassProcessor } from '../bass/engine/bassHarness';
import { makeDrumProcessor } from '../drum/engine/workletHarness';
import { defaultBassParams } from '../bass/params';
import { defaultDrumParams } from '../drum/params';
import { SynthEngine } from './synth';
import { BassEngine } from '../bass/engine/bass-synth';
import { DrumEngine } from '../drum/engine/drum-synth';
import { useStore } from '../store';
import { useBassStore } from '../bass/store';
import { useDrumStore } from '../drum/store';

it('hosted preset loaders request sound resets through the main-thread engines', () => {
  const wt = new SynthEngine(), bass = new BassEngine(), drum = new DrumEngine();
  const cases = [
    { engine: wt, load: () => {
      useStore.getState().attachHosted(wt);
      useStore.getState().applyPreset({});
    } },
    { engine: bass, load: () => {
      useBassStore.getState().attachHosted(bass);
      useBassStore.getState().loadPatchByValue('f0');
    } },
    { engine: drum, load: () => {
      useDrumStore.getState().attachHosted(drum);
      useDrumStore.getState().loadKitByValue('f0');
      useDrumStore.getState().setParamsFromKit(defaultDrumParams());
    } },
  ];
  for (const { engine, load } of cases) {
    const messages: { t: string; preserveTransport?: boolean }[] = [];
    engine.ready = true;
    engine.node = { port: { postMessage: (m: typeof messages[number]) => messages.push(m) } } as unknown as AudioWorkletNode;
    load();
    const resets = messages.filter(m => m.t === 'panic');
    expect(resets.length).toBeGreaterThan(0);
    expect(resets.every(m => m.preserveTransport === true)).toBe(true);
  }
});

const factories = {
  WT1: () => bootWt(),
  BL1: () => {
    const h = makeBassProcessor();
    h.send({ t: 'init', params: defaultBassParams() });
    return h;
  },
  DR1: () => {
    const h = makeDrumProcessor();
    h.send({ t: 'init', params: defaultDrumParams() });
    return h;
  },
};

describe.each(Object.entries(factories))('%s hosted sound reset', (_name, boot) => {
  it('keeps playing and honors queued launches and stops; full panic cancels them', () => {
    const h = boot();
    let frame = 0;
    const outputs = Array.from({ length: 5 }, () => [new Float32Array(128), new Float32Array(128)]);
    const render = (blocks: number) => {
      for (let i = 0; i < blocks; i++) {
        Object.defineProperty(globalThis, 'currentFrame', { configurable: true, value: frame });
        h.proc.process([], outputs);
        frame += 128;
      }
    };
    // Empty steps exercise transport independently of each machine's note layout.
    const clip = { t: 'clip', data: new Uint8Array(256), bars: 1, atFrame: 0 };
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send(clip);
    render(1);
    h.send({ t: 'panic', preserveTransport: true });
    h.send({ t: 'init', params: {} });
    render(48);
    expect(h.sent.filter(m => m.t === 'pos').map(m => m.step)).toEqual([0, 1]);
    h.send({ ...clip, atFrame: frame + 256 });
    h.send({ t: 'panic', preserveTransport: true });
    render(3);
    expect(h.sent.filter(m => m.t === 'clipstart')).toHaveLength(2);
    h.send({ t: 'clipstop', atFrame: frame + 256 });
    h.send({ t: 'panic', preserveTransport: true });
    render(3);
    expect(h.sent.filter(m => m.t === 'clipstop')).toHaveLength(1);
    h.send({ ...clip, atFrame: frame + 256 });
    h.send({ t: 'panic' });
    render(50);
    expect(h.sent.filter(m => m.t === 'clipstart')).toHaveLength(2);
  });
});

it('WT1 keeps synced LFO phase and frequency when a hosted patch changes tempo', () => {
  const h = bootWt({ 'lfo1.sync': 1 });
  const p = h.proc as unknown as {
    bpm: number; gLfo1: { phase: number };
  };
  h.send({ t: 'host', on: 1 });
  h.send({ t: 'tempo', bpm: 140, swing: 0, anchor: 0 });
  h.setFrame(4096);
  h.render(1);
  const phase = p.gLfo1.phase;
  h.send({ t: 'init', params: { 'seq.bpm': 90 } });
  h.send({ t: 'p', k: 'seq.bpm', v: 80 });
  h.setFrame(4096);
  h.render(1);
  expect(p.bpm).toBe(140);
  expect(p.gLfo1.phase).toBe(phase);
});

it('BL1 keeps its bar-locked LFO on conductor tempo through a patch load', () => {
  const h = factories.BL1();
  const p = h.proc as unknown as { songPos: number; lfoValue(): number };
  h.send({ t: 'host', on: 1 });
  h.send({ t: 'tempo', bpm: 140, swing: 0, anchor: 0 });
  p.songPos = 4096;
  const value = p.lfoValue();
  h.send({ t: 'init', params: { 'seq.bpm': 90 } });
  h.send({ t: 'p', k: 'seq.bpm', v: 80 });
  expect(p.lfoValue()).toBe(value);
});

it('BL1 panic clears audible delay tails', () => {
  const h = makeBassProcessor();
  h.send({ t: 'init', params: { ...defaultBassParams(), 'osc.level': 0,
    'sub.level': 1, 'aenv.sus': 1, 'fx.delay.on': 1, 'fx.delay.mix': 0.8,
    'fx.delay.time': 0.2, 'fx.delay.fb': 0.8 } });
  h.send({ t: 'noteon', semi: 0, vel: 1 });
  const peak = (x: Float32Array) => x.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
  expect(peak(h.render(150).L)).toBeGreaterThan(0.01);
  h.send({ t: 'panic' });
  const out = h.render(150);
  expect(peak(out.L)).toBe(0);
  expect(peak(out.R)).toBe(0);
});
