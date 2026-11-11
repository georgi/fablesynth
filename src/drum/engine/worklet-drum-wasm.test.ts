// The compiled C++ DR-1 (worklet-drum-wasm.js) against the JavaScript worklet:
// every factory kit plays its own patterns and chain for four bars through
// both. docs/wasm-dr1.md records the measured numbers. Set DR_WASM_REPORT to
// a file path to write them as JSON lines.
import { describe, it, expect } from 'vitest';
import { makeDrumProcessor } from './workletHarness';
import { makeDrumWasmProcessor, samplesMsg } from './drumWasmHarness';
import { generateDrumTables } from './drumtables';
import { generateSampledDrumTables } from './sampledtables.gen';
import { generateTables } from '../../engine/wavetables';
import { FACTORY_KITS, kitToState } from '../kits';

const fsName = 'node:' + 'fs';
const fs = (await import(/* @vite-ignore */ fsName)) as { appendFileSync(path: string, data: string): void };
const env = (globalThis as { process?: { env?: Record<string, string | undefined> } }).process?.env ?? {};

const SR = 48000;
const db = (x: number) => 20 * Math.log10(Math.max(1e-12, x));
const tables = [...generateDrumTables(), ...generateTables(), ...generateSampledDrumTables()];
const tableMsg = () => ({
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
});

type Harness = { send(m: unknown): void; render(blocks: number): { L: Float32Array; R: Float32Array }; sent: { t: string }[] };

function play(h: Harness, kitName: string, init: (params: unknown) => void) {
  const state = kitToState(FACTORY_KITS.find((k) => k.name === kitName)!);
  init(state.params);
  h.send(tableMsg());
  h.send(samplesMsg());
  h.send({ t: 'seq', data: state.patterns.slice().buffer, chain: state.chain, rhythm: state.drumRhythm });
  h.send({ t: 'play' });
  const bars = 4, bpm = state.params['seq.bpm'] || 126;
  return h.render(Math.ceil((bars * 4 * 60 / bpm) * SR / 128));
}

export function compare(kitName: string) {
  const js = makeDrumProcessor(SR);
  // The C++ Rng starts at 0x9e3779b9; the JS worklet at 0x1d3c5b7f.
  const a = play(js, kitName, (params) => { js.send({ t: 'init', params }); js.send({ t: 'seed', v: 0x9e3779b9 | 0 }); });
  const wasm = makeDrumWasmProcessor(SR);
  const b = play(wasm, kitName, wasm.sendInit);
  let peakA = 0, peakD = 0, sumA = 0, sumB = 0, sumD = 0;
  for (const [x, y] of [[a.L, b.L], [a.R, b.R]]) {
    for (let i = 0; i < x.length; i++) {
      const d = x[i] - y[i];
      peakA = Math.max(peakA, Math.abs(x[i])); peakD = Math.max(peakD, Math.abs(d));
      sumA += x[i] * x[i]; sumB += y[i] * y[i]; sumD += d * d;
    }
  }
  const n = a.L.length * 2;
  const steps = (h: Harness) => h.sent.filter((m) => m.t === 'step').length;
  return {
    name: kitName, peakJs: db(peakA), rmsJs: db(Math.sqrt(sumA / n)), rmsWasm: db(Math.sqrt(sumB / n)),
    peakDiff: db(peakD), rmsDiff: db(Math.sqrt(sumD / n)),
    finite: b.L.every(Number.isFinite) && b.R.every(Number.isFinite), wasmSteps: steps(wasm), jsSteps: steps(js),
  };
}

// Highest waveform RMS difference (dB re full scale) each kit may show. The
// values sit about 3 dB above the measured ones (docs/wasm-dr1.md).
const MAX_RMS_DIFF: Record<string, number> = {
  'TR-VOID': -69, 'ROOM ONE': -61, BITCRUSH: -63, '808 CLASSIC': -65, 'DEEP DUB': -70,
  'DUST HOUSE': -63, WAREHOUSE: -60, 'METAL WORK': -67, 'TAPE KIT': -75, MINIMAL: -66,
  'BROKEN TOYS': -90, 'LIVE ROOM': -72, UZU: -68, '808+UZU HYBRID': -72, 'NEON GRID': -67,
  'ACID CAVE': -67, 'BOOM BAP': -68, 'PIRATE RADIO': -66, 'CC0 IMPACT': -64,
  'CC0 BOUNCE': -63, 'CC0 WAREHOUSE': -65, 'CC0 DEEP HOUSE': -69, 'CC0 BASS RUSH': -66,
};

describe('DR-1 wasm engine', () => {
  it('reports the chain latency of the JavaScript worklet', () => {
    const h = makeDrumWasmProcessor(SR);
    h.sendInit({});
    expect(h.sent.find((m) => m.t === 'latency')).toEqual({ t: 'latency', n: makeDrumProcessor(SR).latency });
  });

  it('posts KIT MIX pad audio in 4096-frame blocks', () => {
    const h = makeDrumWasmProcessor(SR);
    h.sendInit(kitToState(FACTORY_KITS[0]).params);
    h.send(tableMsg());
    h.send({ t: 'padCapture', on: true });
    h.send({ t: 'trig', pad: 0, v: 1 });
    h.render(40);
    const blocks = h.sent.filter((m) => m.t === 'padAudio' && m.pad === 0) as unknown as { frames: number; data: Float32Array }[];
    expect(blocks.map((b) => b.frames)).toEqual([4096]);
    expect(blocks[0].data.some((v) => Math.abs(v) > 1e-3)).toBe(true);
  });

  for (const kit of FACTORY_KITS) {
    it(`renders ${kit.name} close to the JavaScript worklet`, () => {
      const r = compare(kit.name);
      if (env.DR_WASM_REPORT) fs.appendFileSync(env.DR_WASM_REPORT, JSON.stringify(r) + '\n');
      expect(r.finite).toBe(true);
      expect(r.rmsWasm).toBeGreaterThan(-40);
      expect(r.wasmSteps).toBe(r.jsSteps);
      expect(Math.abs(r.rmsWasm - r.rmsJs)).toBeLessThan(0.1);
      expect(r.rmsDiff).toBeLessThan(MAX_RMS_DIFF[kit.name] ?? -60);
    }, 30000); // two four-bar renders; slow under a parallel full run
  }
});
