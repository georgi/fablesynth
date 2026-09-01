import { describe, it } from 'vitest';

// `process` is not in the DOM lib this project compiles against; the bench is
// the only place that needs it.
declare const process: { env: Record<string, string | undefined>; stderr: { write(s: string): void } };
import { bootWt, type WtHarness } from './workletHarness';
import type { ParamValues } from '../params';

// Render-cost benchmark for the WT-1 worklet. Skipped by default (it is a
// measurement, not an assertion); run it with:
//   BENCH=1 npx vitest run src/engine/worklet.bench.test.ts
// Patches and conditions mirror docs/audio-engine-review.md §1 so the numbers
// are comparable with the JUCE engine's.

const HEAVY: Partial<ParamValues> = {
  'oscA.on': 1, 'oscA.unison': 7, 'oscA.detune': 0.3, 'oscA.spread': 0.6, 'oscA.level': 0.7,
  'oscB.on': 1, 'oscB.unison': 7, 'oscB.detune': 0.25, 'oscB.spread': 0.5, 'oscB.level': 0.6,
  'sub.on': 1, 'sub.level': 0.5,
  'noise.on': 1, 'noise.type': 1, 'noise.level': 0.3,
  'filter.on': 1, 'filter.type': 1, 'filter.cutoff': 4000, 'filter.res': 0.4, 'filter.drive': 0.5,
  'filter2.on': 1, 'filter2.type': 0, 'filter2.cutoff': 6000, 'filter2.res': 0.3,
  'mat1.src': 1, 'mat1.dst': 3, 'mat1.amt': 0.4,
  'mat2.src': 2, 'mat2.dst': 1, 'mat2.amt': 0.3,
};

// FX conditions (finding W6: the chain now runs inside the worklet). ALL_OFF is
// the closest thing to the old native-node worklet — every stage gated, only the
// drive dry delay, DC blocker and limiter still running.
const FX_ALL_OFF: Partial<ParamValues> = {
  'fx.eq.on': 0, 'fx.drive.on': 0, 'fx.chorus.on': 0,
  'fx.delay.on': 0, 'fx.reverb.on': 0, 'fx.comp.on': 0,
};
const FX_ALL_ON: Partial<ParamValues> = {
  'fx.eq.on': 1, 'fx.eq.low': 3, 'fx.eq.mid': -2, 'fx.eq.high': 2,
  'fx.drive.on': 1, 'fx.drive.amt': 0.6, 'fx.drive.mix': 0.8,
  'fx.chorus.on': 1, 'fx.chorus.mix': 0.5,
  'fx.delay.on': 1, 'fx.delay.mix': 0.4, 'fx.delay.fb': 0.4,
  'fx.reverb.on': 1, 'fx.reverb.mix': 0.4,
  'fx.comp.on': 1,
};

const LIGHT: Partial<ParamValues> = {
  'oscA.on': 1, 'oscA.unison': 1, 'oscA.level': 0.75,
  'oscB.on': 0,
  'filter.on': 1, 'filter.type': 0, 'filter.cutoff': 6000, 'filter.res': 0.2, 'filter.drive': 0,
  'filter2.on': 0, 'sub.on': 0, 'noise.on': 0,
};

function hold(h: WtHarness): void {
  for (let i = 0; i < 8; i++) h.send({ t: 'on', n: 48 + i * 3, v: 0.9 });
}

// µs per 128-sample block over `seconds` of audio, best of `reps` runs.
function measure(params: Partial<ParamValues>, seconds = 10, reps = 3): number {
  const blocks = Math.round((seconds * 48000) / 128);
  let best = Infinity;
  for (let r = 0; r < reps; r++) {
    const h = bootWt(params);
    hold(h);
    h.render(200); // warm up JIT + fill envelopes
    const t0 = performance.now();
    h.render(blocks);
    const dt = performance.now() - t0;
    best = Math.min(best, (dt * 1000) / blocks);
  }
  return best;
}

describe.skipIf(!process.env.BENCH)('WT-1 render cost', () => {
  it('heavy patch', () => {
    const us = measure(HEAVY);
    process.stderr.write(`heavy: ${us.toFixed(1)} µs/block (${((us / 2666.7) * 100).toFixed(1)} % of a core)\n`);
  }, 120_000);

  it('light patch', () => {
    const us = measure(LIGHT);
    process.stderr.write(`light: ${us.toFixed(1)} µs/block (${((us / 2666.7) * 100).toFixed(1)} % of a core)\n`);
  }, 120_000);

  it('heavy patch, FX chain all off', () => {
    const us = measure({ ...HEAVY, ...FX_ALL_OFF });
    process.stderr.write(`heavy + FX off: ${us.toFixed(1)} µs/block (${((us / 2666.7) * 100).toFixed(1)} % of a core)\n`);
  }, 120_000);

  it('heavy patch, full FX chain', () => {
    const us = measure({ ...HEAVY, ...FX_ALL_ON });
    process.stderr.write(`heavy + full FX: ${us.toFixed(1)} µs/block (${((us / 2666.7) * 100).toFixed(1)} % of a core)\n`);
  }, 120_000);

  it('FX chain alone (silent input, full chain)', () => {
    const us = measure({ 'oscA.on': 0, 'oscB.on': 0, 'sub.on': 0, 'noise.on': 0, ...FX_ALL_ON });
    process.stderr.write(`FX chain alone: ${us.toFixed(1)} µs/block (${((us / 2666.7) * 100).toFixed(1)} % of a core)\n`);
  }, 120_000);
});
