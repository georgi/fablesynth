// SQ-4 hosted clips through the JavaScript and WebAssembly DR-1 worklets:
// tempo, a clip with its lane rhythm launched at a future frame with
// automation, a resizing clipupdate, a tempo change and a clipstop.
// docs/wasm-sq4.md records the numbers.
import { describe, expect, it } from 'vitest';
import { makeDrumProcessor } from './workletHarness';
import { makeDrumWasmProcessor, samplesMsg } from './drumWasmHarness';
import { generateDrumTables } from './drumtables';
import { generateSampledDrumTables } from './sampledtables.gen';
import { generateTables } from '../../engine/wavetables';
import { FACTORY_KITS, kitToState } from '../kits';
import { compareEvents, parity, rampLane, report, runHosted } from '../../seq/hostedParityHarness';

const SR = 48000;
const BUSES = 5;
const tables = [...generateDrumTables(), ...generateTables(), ...generateSampledDrumTables()];
const tableMsg = () => ({
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
});

function script(init: unknown[], kit: string, bpm: number, swing: number, anchor: number, bpm2?: number) {
  const state = kitToState(FACTORY_KITS.find((k) => k.name === kit)!);
  const dur = 60 / bpm / 4 * SR;
  // Clip bytes are bar-major [(bar * 16 + pad) * 16 + step]: pattern p is bar p.
  const one = state.patterns.slice(0, 256), two = state.patterns.slice(0, 512);
  return {
    0: [...init, tableMsg(), samplesMsg(), { t: 'host', on: 1 }, { t: 'tempo', bpm, swing, anchor }],
    10: [{ t: 'clip', data: one, bars: 1, atFrame: anchor + 16 * dur, rhythm: state.drumRhythm }, { t: 'auto', lanes: [rampLane('pad0.lvl', 16, 0.4, 0.9)] }],
    1100: [{ t: 'clipupdate', data: two, bars: 2, rhythm: state.drumRhythm }, { t: 'auto', lanes: [rampLane('pad1.lvl', 8, 0.3, 0.8)] }],
    1600: bpm2 ? [{ t: 'tempo', bpm: bpm2, swing, anchor }] : [],
    2100: [{ t: 'clipstop', atFrame: 2100 * 128 + 300 }],
  };
}

export function compareDrum(kit: string, bpm: number, swing: number, anchor: number, bpm2?: number) {
  const params = kitToState(FACTORY_KITS.find((k) => k.name === kit)!).params;
  const js = makeDrumProcessor(SR);
  // The C++ Rng starts at 0x9e3779b9; the JS worklet at 0x1d3c5b7f.
  const a = runHosted(js, script([{ t: 'init', params }, { t: 'seed', v: 0x9e3779b9 | 0 }], kit, bpm, swing, anchor, bpm2), 2300, BUSES + 1, BUSES);
  const wasm = makeDrumWasmProcessor(SR);
  wasm.sendInit(params);
  const b = runHosted(wasm, script([], kit, bpm, swing, anchor, bpm2), 2300, BUSES + 1, BUSES);
  const e = compareEvents(a.events, b.events), r = parity(a, b);
  report(`DR-1 hosted ${kit} ${bpm}${bpm2 ? '->' + bpm2 : ''} bpm ` + JSON.stringify({ ...r, pos: e.posCount, moved: e.moved, maxSkew: e.maxSkew }));
  return { e, r };
}

describe('DR-1 wasm hosted clips', () => {
  // 125 BPM: a sixteenth is 5760 samples, 45 render blocks.
  for (const kit of ['ROOM ONE', '808 CLASSIC']) {
    it(`matches the JavaScript worklet clip timing, ${kit}`, () => {
      const { e, r } = compareDrum(kit, 125, 0, 1024);
      expect(e.edgesWasm).toEqual(e.edgesJs);
      expect(e.edgesJs.map((x) => x.t)).toEqual(['clipstart', 'clipstop']);
      expect(e.posWasm).toEqual(e.posJs);
      expect(e.maxSkew).toBeLessThanOrEqual(1);
      expect(r.finite).toBe(true);
      expect(r.rmsJs).toBeGreaterThan(-40);
      // Standalone DR-1 parity is -22 ... -36 dB RMS (docs/wasm-dr1.md).
      expect(r.rmsDiff).toBeLessThan(-20);
      expect(Math.abs(r.rmsWasm - r.rmsJs)).toBeLessThan(0.5);
    });
  }

  it('keeps clip timing and level across a tempo change with swing', () => {
    const { e, r } = compareDrum('ROOM ONE', 124, 0.3, 1000, 130);
    expect(e.edgesWasm).toEqual(e.edgesJs);
    expect(e.posWasm).toEqual(e.posJs);
    expect(e.maxSkew).toBeLessThanOrEqual(1);
    expect(Math.abs(r.rmsWasm - r.rmsJs)).toBeLessThan(0.5);
  });
});
