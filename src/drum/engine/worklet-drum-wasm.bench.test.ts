import { describe, it } from 'vitest';
import { makeDrumProcessor } from './workletHarness';
import { makeDrumWasmProcessor, samplesMsg } from './drumWasmHarness';
import { generateDrumTables } from './drumtables';
import { generateSampledDrumTables } from './sampledtables.gen';
import { generateTables } from '../../engine/wavetables';
import { FACTORY_KITS, kitToState } from '../kits';

// Realtime factor of the JavaScript worklet against the compiled C++ engine on
// the same render (docs/wasm-dr1.md). Skipped by default. Run ONE case per
// process, so one engine's JIT state does not bias the other:
//   BENCH=1 npx vitest run src/drum/engine/worklet-drum-wasm.bench.test.ts -t 'js WAREHOUSE'
// The result goes to stderr, and to the file in BENCH_OUT when it is set.

const tables = [...generateDrumTables(), ...generateTables(), ...generateSampledDrumTables()];
const tableMsg = () => ({
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
});

type Proc = { process(inputs: unknown[], outputs: Float32Array[][]): boolean };
function boot(engine: 'js' | 'wasm', kitName: string): Proc {
  const state = kitToState(FACTORY_KITS.find((k) => k.name === kitName)!);
  const h = engine === 'js' ? makeDrumProcessor(48000) : makeDrumWasmProcessor(48000);
  if (engine === 'js') h.send({ t: 'init', params: state.params });
  else (h as ReturnType<typeof makeDrumWasmProcessor>).sendInit(state.params);
  h.send(tableMsg());
  h.send(samplesMsg());
  h.send({ t: 'seq', data: state.patterns.slice().buffer, chain: state.chain, rhythm: state.drumRhythm });
  h.send({ t: 'play' });
  return h.proc;
}

// Realtime factor over `seconds` of audio, best of `reps` runs.
function measure(engine: 'js' | 'wasm', kitName: string, seconds = 10, reps = 3): number {
  const blocks = Math.round((seconds * 48000) / 128);
  const outputs = Array.from({ length: 5 }, () => [new Float32Array(128), new Float32Array(128)]);
  let best = 0;
  for (let k = 0; k < reps; k++) {
    const p = boot(engine, kitName);
    for (let b = 0; b < 400; b++) p.process([], outputs); // warm the JIT
    const t0 = performance.now();
    for (let b = 0; b < blocks; b++) p.process([], outputs);
    best = Math.max(best, seconds / ((performance.now() - t0) / 1000));
  }
  return best;
}

const proc = (globalThis as { process?: { env?: Record<string, string | undefined>; stderr?: { write(s: string): void } } }).process;
const fsName = 'node:' + 'fs';
async function report(label: string, rtf: number) {
  const line = `${label}: ${rtf.toFixed(1)}x realtime (${(2666.7 / rtf).toFixed(1)} µs/block)\n`;
  proc?.stderr?.write(line);
  if (proc?.env?.BENCH_OUT) {
    const fs = (await import(/* @vite-ignore */ fsName)) as { appendFileSync(p: string, s: string): void };
    fs.appendFileSync(proc.env.BENCH_OUT, line);
  }
}

describe.skipIf(!proc?.env?.BENCH)('DR-1 js vs wasm render cost', () => {
  for (const engine of ['js', 'wasm'] as const)
    for (const kit of ['WAREHOUSE', 'CC0 WAREHOUSE'])
      it(`${engine} ${kit}`, async () => { await report(`${engine} ${kit}`, measure(engine, kit)); }, 120_000);
});
