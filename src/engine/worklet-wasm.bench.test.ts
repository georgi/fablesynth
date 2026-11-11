import { describe, it } from 'vitest';
import { bootWt, type WtHarness } from './workletHarness';
import { bootWtWasm } from './wtWasmHarness';
import type { ParamValues } from '../params';

// Render cost of the JavaScript worklet against the compiled C++ engine on
// the same patch (docs/wasm-wt1.md). Skipped by default. Run ONE case per
// process, so one engine's JIT state does not affect the other:
//   BENCH=1 npx vitest run src/engine/worklet-wasm.bench.test.ts -t 'js heavy'

// worklet.bench.test.ts HEAVY + FX_ALL_ON, 8 held voices.
const HEAVY: Partial<ParamValues> = {
  'oscA.on': 1, 'oscA.unison': 7, 'oscA.detune': 0.3, 'oscA.spread': 0.6, 'oscA.level': 0.7,
  'oscB.on': 1, 'oscB.unison': 7, 'oscB.detune': 0.25, 'oscB.spread': 0.5, 'oscB.level': 0.6,
  'sub.on': 1, 'sub.level': 0.5,
  'noise.on': 1, 'noise.type': 1, 'noise.level': 0.3,
  'filter.on': 1, 'filter.type': 1, 'filter.cutoff': 4000, 'filter.res': 0.4, 'filter.drive': 0.5,
  'filter2.on': 1, 'filter2.type': 0, 'filter2.cutoff': 6000, 'filter2.res': 0.3,
  'mat1.src': 1, 'mat1.dst': 3, 'mat1.amt': 0.4,
  'mat2.src': 2, 'mat2.dst': 1, 'mat2.amt': 0.3,
  'fx.eq.on': 1, 'fx.eq.low': 3, 'fx.eq.mid': -2, 'fx.eq.high': 2,
  'fx.drive.on': 1, 'fx.drive.amt': 0.6, 'fx.drive.mix': 0.8,
  'fx.chorus.on': 1, 'fx.chorus.mix': 0.5,
  'fx.delay.on': 1, 'fx.delay.mix': 0.4, 'fx.delay.fb': 0.4,
  'fx.reverb.on': 1, 'fx.reverb.mix': 0.4,
  'fx.comp.on': 1,
};
// worklet.bench.test.ts LIGHT, one held voice.
const LIGHT: Partial<ParamValues> = {
  'oscA.on': 1, 'oscA.unison': 1, 'oscA.level': 0.75,
  'oscB.on': 0,
  'filter.on': 1, 'filter.type': 0, 'filter.cutoff': 6000, 'filter.res': 0.2, 'filter.drive': 0,
  'filter2.on': 0, 'sub.on': 0, 'noise.on': 0,
};

type Case = 'heavy' | 'light';
function boot(engine: 'js' | 'wasm', which: Case): WtHarness {
  const h = engine === 'js' ? bootWt(which === 'heavy' ? HEAVY : LIGHT) : bootWtWasm(which === 'heavy' ? HEAVY : LIGHT);
  const voices = which === 'heavy' ? 8 : 1;
  for (let i = 0; i < voices; i++) h.send({ t: 'on', n: 48 + i * 3, v: 0.9 });
  return h;
}

// Realtime factor over `seconds` of audio, best of `reps` runs.
function measure(engine: 'js' | 'wasm', which: Case, seconds = 10, reps = 3): number {
  const blocks = Math.round((seconds * 48000) / 128);
  const l = new Float32Array(128), r = new Float32Array(128);
  let best = 0;
  for (let k = 0; k < reps; k++) {
    const h = boot(engine, which);
    for (let b = 0; b < 400; b++) h.proc.process([], [[l, r]]); // warm the JIT
    const t0 = performance.now();
    for (let b = 0; b < blocks; b++) h.proc.process([], [[l, r]]);
    best = Math.max(best, seconds / ((performance.now() - t0) / 1000));
  }
  return best;
}

const proc = (globalThis as { process?: { env?: Record<string, string | undefined>; stderr?: { write(s: string): void } } }).process;
const report = (label: string, rtf: number) =>
  proc?.stderr?.write(`${label}: ${rtf.toFixed(1)}x realtime (${(2666.7 / rtf).toFixed(1)} µs/block)\n`);

describe.skipIf(!proc?.env?.BENCH)('WT-1 js vs wasm render cost', () => {
  for (const engine of ['js', 'wasm'] as const)
    for (const which of ['heavy', 'light'] as const)
      it(`${engine} ${which}`, () => { report(`${engine} ${which}`, measure(engine, which)); }, 180_000);
});
