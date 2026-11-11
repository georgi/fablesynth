import { describe, it } from 'vitest';
import { makeBassProcessor, type BassHarness } from './bassHarness';
import { makeBassWasmProcessor } from './bassWasmHarness';
import { generateTables } from '../../engine/wavetables';
import { defaultBassParams } from '../params';
import { FACTORY_PATCHES, patchToState } from '../patches';
import type { ParamValues } from '../../params';

// Realtime factor of the JavaScript worklet against the compiled C++ engine on
// the same render (docs/wasm-bl1.md). Skipped by default. Run ONE case per
// process, as worklet-bass.bench.test.ts explains:
//   BENCH=1 npx vitest run src/bass/engine/worklet-bass-wasm.bench.test.ts -t 'js heavy'

const tables = generateTables();
const tableMsg = () => ({
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
});

// worklet-bass.bench.test.ts HEAVY + FX_ON.
const HEAVY = (): ParamValues => {
  const p = defaultBassParams();
  p['osc.unison'] = 7; p['osc.detune'] = 0.3; p['osc.spread'] = 0.6; p['osc.level'] = 0.8;
  p['sub.level'] = 0.5; p['sub.shape'] = 1;
  p['flt.type'] = 1; p['flt.res'] = 0.7; p['flt.drive'] = 0.6; p['flt.env'] = 0.8;
  p['lfo.depth'] = 0.4; p['aenv.sus'] = 1;
  p['fx.drive.on'] = 1; p['fx.drive.amt'] = 0.5; p['fx.drive.mix'] = 0.5;
  p['fx.chorus.on'] = 1; p['fx.chorus.mix'] = 0.4;
  p['fx.delay.on'] = 1; p['fx.delay.mix'] = 0.35;
  p['fx.reverb.on'] = 1; p['fx.reverb.mix'] = 0.3;
  return p;
};

type Case = 'heavy' | 'acid';
function boot(engine: 'js' | 'wasm', which: Case): BassHarness {
  const acid = patchToState(FACTORY_PATCHES.find((p) => p.name === 'ACID LINE')!);
  const params = which === 'heavy' ? HEAVY() : acid.params;
  let h: BassHarness;
  if (engine === 'js') { h = makeBassProcessor(48000); h.send({ t: 'init', params }); }
  else { const w = makeBassWasmProcessor(48000); w.sendInit(params); h = w; }
  h.send(tableMsg());
  if (which === 'heavy') h.send({ t: 'noteon', semi: 0, vel: 1 });
  else { h.send({ t: 'pats', data: acid.patterns.slice().buffer }); h.send({ t: 'chain', list: [0] }); h.send({ t: 'play' }); }
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

describe.skipIf(!proc?.env?.BENCH)('BL-1 js vs wasm render cost', () => {
  for (const engine of ['js', 'wasm'] as const)
    for (const which of ['heavy', 'acid'] as const)
      it(`${engine} ${which}`, () => { report(`${engine} ${which}`, measure(engine, which)); }, 120_000);
});
