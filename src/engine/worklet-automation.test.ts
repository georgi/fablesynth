import { describe, expect, it } from 'vitest';
import { bootWt } from './workletHarness';
import { makeBassProcessor } from '../bass/engine/bassHarness';
import { makeDrumProcessor } from '../drum/engine/workletHarness';
import { defaultBassParams } from '../bass/params';
import { defaultDrumParams } from '../drum/params';

const machines = {
  WT1: { key: 'filter.cutoff', boot: () => bootWt() },
  BL1: { key: 'flt.cut', boot: () => { const h = makeBassProcessor(); h.send({ t: 'init', params: defaultBassParams() }); return h; } },
  DR1: { key: 'pad0.lvl', boot: () => { const h = makeDrumProcessor(); h.send({ t: 'init', params: defaultDrumParams() }); return h; } },
};

type Proc = { p?: ArrayLike<number> | Record<string, number>; pv?: ArrayLike<number>; autoHeld: Map<unknown, number> };

describe.each(Object.entries(machines))('%s clip automation', (_name, { key, boot }) => {
  it('plays the lane on the anchor clock, keeps knob edits, and restores on stop', () => {
    const h = boot();
    const proc = h.proc as unknown as Proc;
    const values = proc.pv ?? proc.p!;
    let frame = 0;
    const outputs = [[new Float32Array(128), new Float32Array(128)]];
    const renderAt = (f: number) => {
      frame = f;
      Object.defineProperty(globalThis, 'currentFrame', { configurable: true, value: frame });
      h.proc.process([], outputs);
    };
    // Four held steps with values 100, 200, 300, 400; the lane cycles every four sixteenths.
    const table = new Float32Array(64).map((_, i) => 100 * (1 + Math.floor(i / 16)));
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(4096), bars: 1, atFrame: 0 });
    h.send({ t: 'auto', lanes: [{ k: key, table, len: 4, fit: 0, rot: 0 }] });
    renderAt(0);
    const [[slot, stored]] = [...proc.autoHeld];
    const read = () => (values as Record<string | number, number>)[slot as string];
    expect(read()).toBeCloseTo(100);
    const step = (60 / 120 / 4) * 48000; // harness sample rate
    renderAt(Math.round(step * 6.5)); // six and a half steps: lane step 2.5
    expect(read()).toBeCloseTo(300);

    h.send({ t: 'p', k: key, v: 42 });
    renderAt(Math.round(step * 7.5));
    expect(read()).toBeCloseTo(400);
    expect(proc.autoHeld.get(slot)).toBe(42);
    expect(stored).not.toBe(42);

    h.send({ t: 'clipstop', atFrame: 0 });
    renderAt(Math.round(step * 8.5));
    expect(read()).toBe(42);
    expect(proc.autoHeld.size).toBe(0);
  });
});

describe.each(['WT1', 'DR1'] as const)('%s standalone sequence automation', machine => {
  it('plays on the standalone transport, reports its anchor, and restores on stop', () => {
    const h = machine === 'WT1' ? bootWt({ 'seq.bpm': 120 }) : makeDrumProcessor();
    if (machine === 'DR1') h.send({ t: 'init', params: { ...defaultDrumParams(), 'seq.bpm': 120 } });
    const proc = h.proc as unknown as Proc;
    const values = proc.pv ?? proc.p!;
    const outputs = [[new Float32Array(128), new Float32Array(128)]];
    const renderAt = (f: number) => {
      Object.defineProperty(globalThis, 'currentFrame', { configurable: true, value: f });
      h.proc.process([], outputs);
    };
    const table = new Float32Array(64).map((_, i) => 0.1 * (1 + Math.floor(i / 16)));
    h.send({ t: 'seqauto', lanes: [{ k: machine === 'WT1' ? 'fx.delay.fb' : 'pad0.lvl', table, len: 4, fit: 0, rot: 0 }] });
    renderAt(0);
    expect(proc.autoHeld.size).toBe(0); // stopped: the lane waits for the transport

    Object.defineProperty(globalThis, 'currentFrame', { configurable: true, value: 0 });
    h.send({ t: 'play' });
    expect(h.sent).toContainEqual({ t: 'anchor', frame: 0 });
    renderAt(0);
    const [[slot, stored]] = [...proc.autoHeld];
    expect((values as Record<string | number, number>)[slot as string]).toBeCloseTo(0.1);
    const step = (60 / 120 / 4) * 48000;
    renderAt(Math.round(step * 6.5));
    expect((values as Record<string | number, number>)[slot as string]).toBeCloseTo(0.3);

    h.send({ t: 'stop' });
    renderAt(Math.round(step * 7.5));
    expect((values as Record<string | number, number>)[slot as string]).toBe(stored);
    expect(proc.autoHeld.size).toBe(0);
  });
});

// Check DSP state, rather than just the parameter store: cached FX settings
// must update while a lane runs and restore when it is removed.
const fxMachines = [
  { name: 'WT1', boot: machines.WT1.boot, key: 'fx.delay.fb', strip: (p: FxProc) => p.fx! },
  { name: 'BL1', boot: machines.BL1.boot, key: 'fx.delay.fb', strip: (p: FxProc) => p.fx! },
  { name: 'DR1 pad', boot: machines.DR1.boot, key: 'pad0.fx.delay.fb', strip: (p: FxProc) => p.padFx![0] },
  { name: 'DR1 group', boot: machines.DR1.boot, key: 'fx.delay.fb', strip: (p: FxProc) => p.groupFx![0] },
];
type FxStrip = { dlFb: { target: number }; setParams: (...args: unknown[]) => void };
type FxProc = { fx?: FxStrip; padFx?: FxStrip[]; groupFx?: FxStrip[] };

describe.each(fxMachines)('$name FX automation', ({ boot, key, strip }) => {
  it('updates the DSP, preserves knob edits, and restores when the lane is removed', () => {
    const h = boot();
    const fx = strip(h.proc as unknown as FxProc);
    const output = [[new Float32Array(128), new Float32Array(128)]];
    const renderAt = (frame: number) => {
      Object.defineProperty(globalThis, 'currentFrame', { configurable: true, value: frame });
      h.proc.process([], output);
      expect(output[0].every(channel => channel.every(Number.isFinite))).toBe(true);
    };
    h.send({ t: 'p', k: key, v: 0.25 });
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: new Uint8Array(4096), bars: 1, atFrame: 0 });
    h.send({ t: 'auto', lanes: [{ k: key, table: Float32Array.from({ length: 64 }, (_, i) => i < 32 ? 0.2 : 0.6), len: 4, fit: 0, rot: 0 }] });
    renderAt(0);
    expect(fx.dlFb.target).toBeCloseTo(0.2);
    h.send({ t: 'p', k: key, v: 0.35 });
    renderAt(15000);
    expect(fx.dlFb.target).toBeCloseTo(0.6);
    h.send({ t: 'auto', lanes: [] });
    renderAt(16000);
    expect(fx.dlFb.target).toBeCloseTo(0.35);
  });
});

it('WT1 applies FX automation per chunk within a large host block', () => {
  const h = bootWt();
  const fx = (h.proc as unknown as FxProc).fx!;
  const targets: number[] = [];
  const setParams = fx.setParams.bind(fx);
  fx.setParams = (...args) => { setParams(...args); targets.push(fx.dlFb.target); };
  h.send({ t: 'host', on: 1 });
  h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
  h.send({ t: 'clip', data: new Uint8Array(4096), bars: 1, atFrame: 0 });
  h.send({ t: 'auto', lanes: [{ k: 'fx.delay.fb', table: Float32Array.from({ length: 16 }, (_, i) => i / 20), len: 1, fit: 0, rot: 0 }] });
  h.setFrame(0);
  h.proc.process([], [[new Float32Array(512), new Float32Array(512)]]);
  expect(targets).toHaveLength(4);
  targets.forEach((value, i) => expect(value).toBeCloseTo(i * 128 / 6000 * 16 / 20));
});
