import { describe, it } from 'vitest';
import { makeBassProcessor, type BassHarness } from './bassHarness';
import { generateTables } from '../../engine/wavetables';
import { defaultBassParams } from '../params';
import type { ParamValues } from '../../params';

// Render-cost benchmark for the BL-1 worklet. Skipped by default (it is a
// measurement, not an assertion); run it with:
//   BENCH=1 npx vitest run src/bass/engine/worklet-bass.bench.test.ts
// Conditions mirror docs/audio-engine-review.md §1 so the numbers compare with
// the WT-1 and JUCE figures.

const tables = generateTables();
const tableMsg = {
  t: 'tables',
  list: tables.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
};

const HEAVY = (): ParamValues => {
  const p = defaultBassParams();
  p['osc.unison'] = 7; p['osc.detune'] = 0.3; p['osc.spread'] = 0.6; p['osc.level'] = 0.8;
  p['sub.level'] = 0.5; p['sub.shape'] = 1;
  p['flt.type'] = 1; p['flt.res'] = 0.7; p['flt.drive'] = 0.6; p['flt.env'] = 0.8;
  p['lfo.depth'] = 0.4; p['aenv.sus'] = 1;
  return p;
};

// One voice, no spread, no drive — the mono filter path.
const LIGHT = (): ParamValues => {
  const p = defaultBassParams();
  p['osc.unison'] = 1; p['osc.spread'] = 0; p['osc.level'] = 0.8;
  p['sub.level'] = 0; p['flt.type'] = 0; p['flt.res'] = 0.3; p['flt.drive'] = 0;
  p['aenv.sus'] = 1;
  return p;
};

// µs per 128-sample block over `seconds` of audio, best of `reps` runs.
function measure(params: ParamValues, seconds = 10, reps = 3): number {
  const blocks = Math.round((seconds * 48000) / 128);
  const l = new Float32Array(128), r = new Float32Array(128);
  let best = Infinity;
  for (let k = 0; k < reps; k++) {
    const h: BassHarness = makeBassProcessor(48000);
    h.send({ t: 'init', params });
    h.send(tableMsg);
    h.send({ t: 'noteon', semi: 0, vel: 1 });
    for (let b = 0; b < 400; b++) h.proc.process([], [[l, r]]); // warm the JIT
    const t0 = performance.now();
    for (let b = 0; b < blocks; b++) h.proc.process([], [[l, r]]);
    best = Math.min(best, ((performance.now() - t0) * 1000) / blocks);
  }
  return best;
}

// @types/node is not installed; reach the env through globalThis.
const proc = (globalThis as {
  process?: { env?: Record<string, string | undefined>; stderr?: { write(s: string): void } };
}).process;
const env = proc?.env;
const report = (label: string, us: number): void => {
  proc?.stderr?.write(`${label}: ${us.toFixed(1)} µs/block (${((us / 2666.7) * 100).toFixed(2)} % of a core)\n`);
};

describe.skipIf(!env?.BENCH)('BL-1 render cost', () => {
  it('heavy patch', () => { report('heavy', measure(HEAVY())); }, 120_000);
  it('light patch', () => { report('light', measure(LIGHT())); }, 120_000);
});
