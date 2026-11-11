// SQ-4 hosted clips through the JavaScript and WebAssembly BL-1 worklets:
// tempo, a clip launched at a future frame with automation, a resizing
// clipupdate with new automation, a tempo change and a clipstop.
// docs/wasm-sq4.md records the numbers.
import { describe, expect, it } from 'vitest';
import { makeBassProcessor } from './bassHarness';
import { makeBassWasmProcessor } from './bassWasmHarness';
import { generateTables } from '../../engine/wavetables';
import { FACTORY_PATCHES, patchToState } from '../patches';
import { compareEvents, parity, rampLane, report, runHosted } from '../../seq/hostedParityHarness';

const SR = 48000;
const tables = generateTables();
const tableMsg = () => ({
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
});

// 3 bytes per step: flags (bit0 on, bit1 accent, bits 2.. duration), semi | slide, octave.
function clip(bars: number, notes: [number, number, number][]): Uint8Array {
  const d = new Uint8Array(bars * 16 * 3);
  for (const [s, semi, dur] of notes) { d[s * 3] = 1 | (dur << 2) | (s % 4 === 0 ? 2 : 0); d[s * 3 + 1] = semi; d[s * 3 + 2] = 1; }
  return d;
}

function script(init: unknown, bpm: number, swing: number, anchor: number, bpm2?: number) {
  const dur = 60 / bpm / 4 * SR;
  const a = clip(1, [[0, 0, 2], [3, 7, 1], [6, 3, 2], [8, 0, 1], [11, 10, 1], [14, 5, 2]]);
  const b = clip(2, [[0, 0, 4], [4, 3, 1], [7, 7, 2], [12, 0, 1], [18, 5, 2], [24, 10, 1], [29, 3, 3]]);
  return {
    0: [init, tableMsg(), { t: 'host', on: 1 }, { t: 'tempo', bpm, swing, anchor }],
    10: [{ t: 'clip', data: a, bars: 1, atFrame: anchor + 16 * dur }, { t: 'auto', lanes: [rampLane('flt.cut', 16, 200, 2400)] }],
    1100: [{ t: 'clipupdate', data: b, bars: 2 }, { t: 'auto', lanes: [rampLane('flt.res', 8, 0.2, 0.8)] }],
    1600: bpm2 ? [{ t: 'tempo', bpm: bpm2, swing, anchor }] : [],
    2100: [{ t: 'clipstop', atFrame: 2100 * 128 + 300 }],
  };
}

export function compareBass(bpm: number, swing: number, anchor: number, bpm2?: number) {
  const params = patchToState(FACTORY_PATCHES.find((p) => p.name === 'RUBBER SUB')!).params;
  const js = makeBassProcessor(SR);
  const a = runHosted(js, script({ t: 'init', params }, bpm, swing, anchor, bpm2), 2300);
  const wasm = makeBassWasmProcessor(SR);
  wasm.sendInit(params);
  const b = runHosted(wasm, script({ t: 'p', k: 'flt.cut', v: params['flt.cut'] }, bpm, swing, anchor, bpm2), 2300);
  const e = compareEvents(a.events, b.events), r = parity(a, b);
  report(`BL-1 hosted ${bpm}${bpm2 ? '->' + bpm2 : ''} bpm ` + JSON.stringify({ ...r, pos: e.posCount, moved: e.moved, maxSkew: e.maxSkew }));
  return { e, r };
}

describe('BL-1 wasm hosted clips', () => {
  // 125 BPM: a sixteenth is 5760 samples, 45 render blocks, so every step
  // and gate-off lands on a block start in both engines.
  it('matches the JavaScript worklet clip timing and audio on block-aligned steps', () => {
    const { e, r } = compareBass(125, 0, 1024);
    expect(e.edgesWasm).toEqual(e.edgesJs);
    expect(e.edgesJs.map((x) => x.t)).toEqual(['clipstart', 'clipstop']);
    expect(e.posWasm).toEqual(e.posJs);
    expect(e.maxSkew).toBeLessThanOrEqual(1);
    expect(r.finite).toBe(true);
    expect(r.rmsJs).toBeGreaterThan(-30);
    expect(r.rmsDiff).toBeLessThan(-80);
  });

  it('keeps clip timing and level across a tempo change on fractional-sample steps', () => {
    // Two native behaviors (the JUCE SQ-4 does the same): the engine fires a
    // due step at a gate-off split inside the block, where the JS worklet
    // waits for the next block start; and a tempo change re-derives the
    // bar-locked LFO phase from the anchor, where the JS LFO accumulates.
    // Only the timing and the level are compared.
    const { e, r } = compareBass(124, 0.3, 1000, 130);
    expect(e.edgesWasm).toEqual(e.edgesJs);
    expect(e.posWasm).toEqual(e.posJs);
    expect(e.maxSkew).toBeLessThanOrEqual(1);
    expect(Math.abs(r.rmsWasm - r.rmsJs)).toBeLessThan(0.5);
  });
});
