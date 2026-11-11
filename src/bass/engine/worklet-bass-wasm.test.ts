// The compiled C++ BL-1 (worklet-bass-wasm.js) against the JavaScript worklet:
// the three factory patches play pattern A for four bars through both.
// docs/wasm-bl1.md records the measured numbers.
import { describe, it, expect } from 'vitest';
import { makeBassProcessor, type BassHarness } from './bassHarness';
import { makeBassWasmProcessor } from './bassWasmHarness';
import { generateTables } from '../../engine/wavetables';
import { FACTORY_PATCHES, patchToState } from '../patches';

const tables = generateTables();
const tableMsg = () => ({
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
});
const SR = 48000;
const db = (x: number) => 20 * Math.log10(Math.max(1e-12, x));

function play(h: BassHarness, name: string, init: (params: unknown) => void) {
  const state = patchToState(FACTORY_PATCHES.find((p) => p.name === name)!);
  init(state.params);
  h.send(tableMsg());
  h.send({ t: 'pats', data: state.patterns.slice().buffer });
  h.send({ t: 'chain', list: [0] });
  h.send({ t: 'play' });
  const bars = 4, bpm = state.params['seq.bpm'];
  return h.render(Math.ceil((bars * 4 * 60 / bpm) * SR / 128));
}

export function compare(name: string) {
  const js = makeBassProcessor(SR);
  const a = play(js, name, (params) => js.send({ t: 'init', params }));
  const wasm = makeBassWasmProcessor(SR);
  const b = play(wasm, name, wasm.sendInit);
  let peakA = 0, peakD = 0, sumA = 0, sumB = 0, sumD = 0;
  for (const [x, y] of [[a.L, b.L], [a.R, b.R]]) {
    for (let i = 0; i < x.length; i++) {
      const d = x[i] - y[i];
      peakA = Math.max(peakA, Math.abs(x[i])); peakD = Math.max(peakD, Math.abs(d));
      sumA += x[i] * x[i]; sumB += y[i] * y[i]; sumD += d * d;
    }
  }
  const n = a.L.length * 2;
  return {
    name, peakJs: db(peakA), rmsJs: db(Math.sqrt(sumA / n)), rmsWasm: db(Math.sqrt(sumB / n)),
    peakDiff: db(peakD), rmsDiff: db(Math.sqrt(sumD / n)),
    finite: [...b.L, ...b.R].every(Number.isFinite), wasmSteps: wasm.sent.filter((m) => m.t === 'step').length,
    jsSteps: js.sent.filter((m) => m.t === 'step').length,
  };
}

describe('BL-1 wasm engine', () => {
  it('reports the FX latency and the BassParams id table', () => {
    const h = makeBassWasmProcessor(SR);
    h.sendInit({});
    expect(h.sent.find((m) => m.t === 'latency')).toEqual({ t: 'latency', n: 107 });
  });

  for (const name of ['ACID LINE', 'RUBBER SUB', 'NEON SQUELCH']) {
    it(`renders ${name} pattern A close to the JavaScript worklet`, () => {
      const r = compare(name);
      const env = (globalThis as { process?: { env?: Record<string, string | undefined>; stderr?: { write(s: string): void } } }).process;
      if (env?.env?.WASM_REPORT) env.stderr?.write(JSON.stringify(r) + '\n');
      expect(r.finite).toBe(true);
      expect(r.rmsWasm).toBeGreaterThan(-40);
      expect(r.wasmSteps).toBe(r.jsSteps);
      expect(Math.abs(r.rmsWasm - r.rmsJs)).toBeLessThan(0.5);
      // NEON SQUELCH measures -58 dB: the JS/C++ port difference on its
      // resonant edges, which the native plugin shares (docs/wasm-bl1.md).
      expect(r.rmsDiff).toBeLessThan(-50);
    });
  }
});
