import { describe, expect, it } from 'vitest';
import { makeDrumProcessor } from './workletHarness';
import { patIdx, makeEmptyPatterns } from '../seq';

const run = (h: ReturnType<typeof makeDrumProcessor>, blocks: number, size: number) => h.render(blocks, size);

describe('DR-1 POLY scheduler', () => {
  it('keeps GRID events on absolute sixteenth frames across fragmented blocks', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const patterns = makeEmptyPatterns();
    patterns[patIdx(0, 0, 0)] = 1;
    patterns[patIdx(0, 0, 2)] = 1;
    const lanes: Array<Record<string, unknown> | null> = Array.from({ length: 16 }, () => null);
    lanes[0] = { enabled: true, sourceBar: 0, steps: 3, rotation: 0, timing: { mode: 'grid' } };
    h.send({ t: 'p', k: 'seq.bpm', v: 120 });
    h.send({ t: 'seq', data: patterns.buffer, chain: [0], rhythm: { v: 1, lanes } });
    h.send({ t: 'play' });
    run(h, 1, 96);
    run(h, 188, 127);
    const frames = h.sent.filter((m) => m.t === 'step').map((m) => m.frame as number);
    expect(frames.slice(0, 4)).toEqual([0, 6000, 12000, 18000]);
  });

  it('places FIT 3 events at 0, 32000, and 64000 samples', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const patterns = makeEmptyPatterns();
    for (let s = 0; s < 3; s++) patterns[patIdx(0, 0, s)] = 1;
    const lanes: Array<Record<string, unknown> | null> = Array.from({ length: 16 }, () => null);
    lanes[0] = { enabled: true, sourceBar: 0, steps: 3, rotation: 0, timing: { mode: 'fit', cycleBeats: 4 } };
    h.send({ t: 'p', k: 'seq.bpm', v: 120 });
    h.send({ t: 'seq', data: patterns.buffer, chain: [0], rhythm: { v: 1, lanes } });
    h.send({ t: 'play' });
    run(h, 501, 128);
    const frames = h.sent.filter((m) => m.t === 'poly' && m.pad === 0 && m.hit).map((m) => m.frame as number);
    expect(frames.slice(0, 3)).toEqual([0, 32000, 64000]);
  });
});
