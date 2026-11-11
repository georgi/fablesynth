// SQ-4 hosted clips through the JavaScript and WebAssembly WT-1 worklets:
// tempo, a chord clip launched at a future frame with automation, a resizing
// clipupdate with FX automation, a tempo change and a clipstop.
// docs/wasm-sq4.md records the numbers.
import { describe, expect, it } from 'vitest';
import { bootWt } from './workletHarness';
import { bootWtWasm } from './wtWasmHarness';
import { FACTORY_PRESETS } from '../presets';
import type { ParamValues } from '../params';
import { compareEvents, parity, rampLane, report, runHosted } from '../seq/hostedParityHarness';

const SR = 48000;
const LANES = 8, STRIDE = 3;

// Per step, 8 note lanes of 3 bytes: flags (bit0 on, bit1 accent, bits 2.. duration), semi, octave.
function clip(bars: number, notes: [number, number, number, number][]): Uint8Array {
  const d = new Uint8Array(bars * 16 * LANES * STRIDE);
  for (const [s, lane, semi, dur] of notes) {
    const o = (s * LANES + lane) * STRIDE;
    d[o] = 1 | (dur << 2) | (s % 4 === 0 ? 2 : 0); d[o + 1] = semi % 12; d[o + 2] = 1 + Math.floor(semi / 12);
  }
  return d;
}

function script(bpm: number, swing: number, anchor: number, bpm2?: number) {
  const dur = 60 / bpm / 4 * SR;
  const a = clip(1, [[0, 0, 0, 3], [0, 1, 7, 3], [4, 0, 3, 2], [8, 0, 5, 4], [8, 1, 12, 4], [13, 0, 10, 2]]);
  const b = clip(2, [[0, 0, 0, 6], [0, 1, 3, 6], [0, 2, 7, 6], [10, 0, 5, 2], [16, 0, 8, 4], [16, 1, 15, 4], [26, 0, 10, 3]]);
  return {
    0: [{ t: 'host', on: 1 }, { t: 'tempo', bpm, swing, anchor }],
    10: [{ t: 'clip', data: a, bars: 1, atFrame: anchor + 16 * dur }, { t: 'auto', lanes: [rampLane('filter.cutoff', 16, 300, 4000)] }],
    1100: [{ t: 'clipupdate', data: b, bars: 2 }, { t: 'auto', lanes: [rampLane('fx.delay.mix', 8, 0.1, 0.5)] }],
    1600: bpm2 ? [{ t: 'tempo', bpm: bpm2, swing, anchor }] : [],
    2100: [{ t: 'clipstop', atFrame: 2100 * 128 + 300 }],
  };
}

export function compareWt(name: string, bpm: number, swing: number, anchor: number, bpm2?: number) {
  const params = FACTORY_PRESETS.find((p) => p.name === name)!.params as Partial<ParamValues>;
  const a = runHosted(bootWt(params), script(bpm, swing, anchor, bpm2), 2300);
  const b = runHosted(bootWtWasm(params), script(bpm, swing, anchor, bpm2), 2300);
  const e = compareEvents(a.events, b.events), r = parity(a, b);
  report(`WT-1 hosted ${name} ${bpm}${bpm2 ? '->' + bpm2 : ''} bpm ` + JSON.stringify({ ...r, pos: e.posCount, moved: e.moved, maxSkew: e.maxSkew }));
  return { e, r };
}

describe('WT-1 wasm hosted clips', () => {
  // 125 BPM: a sixteenth is 5760 samples, 45 render blocks.
  for (const name of ['INIT', 'VELVET PAD', 'ACID LINE']) {
    it(`matches the JavaScript worklet clip timing and audio, ${name}`, () => {
      const { e, r } = compareWt(name, 125, 0, 1024);
      expect(e.edgesWasm).toEqual(e.edgesJs);
      expect(e.edgesJs.map((x) => x.t)).toEqual(['clipstart', 'clipstop']);
      expect(e.posWasm).toEqual(e.posJs);
      expect(e.maxSkew).toBeLessThanOrEqual(1);
      expect(r.finite).toBe(true);
      expect(r.rmsJs).toBeGreaterThan(-45);
      expect(r.rmsDiff).toBeLessThan(-60);
    });
  }

  it('keeps clip timing and level across a tempo change with swing', () => {
    const { e, r } = compareWt('VELVET PAD', 124, 0.3, 1000, 130);
    expect(e.edgesWasm).toEqual(e.edgesJs);
    expect(e.posWasm).toEqual(e.posJs);
    // Swung odd steps on fractional frames: the native engine reports a few
    // pos messages up to two blocks (5 ms) early; the audio still matches.
    expect(e.maxSkew).toBeLessThanOrEqual(2);
    expect(Math.abs(r.rmsWasm - r.rmsJs)).toBeLessThan(0.5);
    expect(r.rmsDiff).toBeLessThan(-60);
  });
});
