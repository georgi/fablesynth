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
});
