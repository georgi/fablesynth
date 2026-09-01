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
//
// Run ONE case per process. Several measures in one vitest process inflate the
// later ones badly (V8 gives up optimizing the shared call sites), so compare
// cases only across separate runs:
//   BENCH=1 npx vitest run src/bass/engine/worklet-bass.bench.test.ts -t 'heavy patch, full FX'

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

// All four wet FX stages on. The rack lives in the worklet now, so this is
// the cost the native-node graph used to hide (audio-engine review B5/W6).
const FX_ON = (p: ParamValues): ParamValues => {
  p['fx.drive.on'] = 1; p['fx.drive.amt'] = 0.5; p['fx.drive.mix'] = 0.5;
  p['fx.chorus.on'] = 1; p['fx.chorus.mix'] = 0.4;
  p['fx.delay.on'] = 1; p['fx.delay.mix'] = 0.35;
  p['fx.reverb.on'] = 1; p['fx.reverb.mix'] = 0.3;
  return p;
};

// All four on except drive — the stage that costs 83 % of the rack. A gated
// stage must cost nothing, so this should land near the FX_OFF figure plus
// chorus + delay + reverb.
const FX_NO_DRIVE = (p: ParamValues): ParamValues => { FX_ON(p); p['fx.drive.on'] = 0; return p; };

// Every wet stage off: the rack still runs master gain, the DC block and the
// lookahead limiter, as the plugin does.
const FX_OFF = (p: ParamValues): ParamValues => {
  p['fx.drive.on'] = 0; p['fx.chorus.on'] = 0; p['fx.delay.on'] = 0; p['fx.reverb.on'] = 0;
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
function measure(params: ParamValues, seconds = 10, reps = 3, note = true): number {
  const blocks = Math.round((seconds * 48000) / 128);
  const l = new Float32Array(128), r = new Float32Array(128);
  let best = Infinity;
  for (let k = 0; k < reps; k++) {
    const h: BassHarness = makeBassProcessor(48000);
    h.send({ t: 'init', params });
    h.send(tableMsg);
    if (note) h.send({ t: 'noteon', semi: 0, vel: 1 });
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
  it('heavy patch, FX off', () => { report('heavy / fx off', measure(FX_OFF(HEAVY()))); }, 120_000);
  it('heavy patch, full FX', () => { report('heavy / fx on ', measure(FX_ON(HEAVY()))); }, 120_000);
  it('heavy patch, full FX minus drive', () => { report('heavy / no drive', measure(FX_NO_DRIVE(HEAVY()))); }, 120_000);
  it('light patch, FX off', () => { report('light / fx off', measure(FX_OFF(LIGHT()))); }, 120_000);
  it('light patch, full FX', () => { report('light / fx on ', measure(FX_ON(LIGHT()))); }, 120_000);
  it('silent, full FX', () => { report('idle  / fx on ', measure(FX_ON(LIGHT()), 10, 3, false)); }, 120_000);
});
