import { describe, expect, it } from 'vitest';
import { makeDrumProcessor } from './workletHarness';
import { patIdx, makeEmptyPatterns } from '../seq';

const run = (h: ReturnType<typeof makeDrumProcessor>, blocks: number, size: number) => h.render(blocks, size);
const clipIdx = (bar: number, pad: number, step: number) => (bar * 16 + pad) * 16 + step;

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

  it('advances FIT lanes independently and does not fire grid lanes on FIT events', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const patterns = makeEmptyPatterns();
    patterns[patIdx(0, 0, 0)] = 1;
    patterns[patIdx(0, 0, 1)] = 1;
    patterns[patIdx(0, 0, 2)] = 1;
    for (let s = 0; s < 5; s++) patterns[patIdx(0, 1, s)] = 1;
    patterns[patIdx(0, 1, 1)] = 1;
    const lanes: Array<Record<string, unknown> | null> = Array.from({ length: 16 }, () => null);
    lanes[0] = { enabled: true, sourceBar: 0, steps: 3, rotation: 0, timing: { mode: 'fit', cycleBeats: 4 } };
    lanes[1] = { enabled: true, sourceBar: 0, steps: 5, rotation: 0, timing: { mode: 'fit', cycleBeats: 4 } };
    h.send({ t: 'p', k: 'seq.bpm', v: 120 });
    h.send({ t: 'seq', data: patterns.buffer, chain: [0], rhythm: { v: 1, lanes } });
    h.send({ t: 'play' });
    run(h, 751, 128);
    const p0 = h.sent.filter((m) => m.t === 'poly' && m.pad === 0 && m.hit).map((m) => m.frame as number);
    const p1 = h.sent.filter((m) => m.t === 'poly' && m.pad === 1 && m.hit).map((m) => m.frame as number);
    expect(p0.slice(0, 3)).toEqual([0, 32000, 64000]);
    expect(p1.slice(0, 5)).toEqual([0, 19200, 38400, 57600, 76800]);
    const gridMessages = h.sent.filter((m) => m.t === 'step');
    expect(gridMessages.every((m) => (m.hits as number[]).length === 0)).toBe(true);
  });

  it('uses the standalone chain for ordinary lanes when another lane is POLY', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const patterns = makeEmptyPatterns();
    patterns[patIdx(0, 2, 0)] = 1;
    const lanes: Array<Record<string, unknown> | null> = Array.from({ length: 16 }, () => null);
    lanes[0] = { enabled: true, sourceBar: 0, steps: 3, rotation: 0, timing: { mode: 'fit', cycleBeats: 4 } };
    h.send({ t: 'p', k: 'seq.bpm', v: 120 });
    h.send({ t: 'seq', data: patterns.buffer, chain: [0], rhythm: { v: 1, lanes } });
    h.send({ t: 'play' });
    run(h, 751, 128);
    const ordinary = h.sent.filter((m) => m.t === 'step' && Array.isArray(m.hits) && (m.hits as number[]).includes(2));
    expect(ordinary.map((m) => m.frame as number).slice(0, 2)).toEqual([0, 96000]);
  });

  it('transitions legacy and POLY hosted clips without crashing or replaying history', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const legacy = new Uint8Array(256);
    const poly = new Uint8Array(256);
    poly[clipIdx(0, 0, 0)] = 1;
    const lanes: Array<Record<string, unknown> | null> = Array.from({ length: 16 }, () => null);
    lanes[0] = { enabled: true, sourceBar: 0, steps: 16, rotation: 0, timing: { mode: 'grid' } };
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: legacy.buffer, bars: 1, atFrame: 0, rhythm: null });
    run(h, 1, 128);
    h.send({ t: 'clip', data: poly.buffer, bars: 1, atFrame: 193, rhythm: { v: 1, lanes } });
    expect(() => run(h, 2, 128)).not.toThrow();
    expect(h.sent.filter((m) => m.t === 'clipstart').map((m) => m.frame)).toEqual([0, 193]);
    const starts = h.sent.filter((m) => m.t === 'clipstart');
    expect(starts[1].frame).toBe(193);
    h.send({ t: 'clip', data: legacy.buffer, bars: 1, atFrame: 449, rhythm: null });
    expect(() => run(h, 2, 128)).not.toThrow();
    expect(h.sent.filter((m) => m.t === 'clipstart').map((m) => m.frame)).toEqual([0, 193, 449]);
  });

  it('executes hosted start and stop at their stamped frames', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const data = new Uint8Array(256);
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: data.buffer, bars: 1, atFrame: 65, rhythm: null });
    run(h, 1, 128);
    h.send({ t: 'clipstop', atFrame: 193 });
    run(h, 2, 128);
    expect(h.sent.filter((m) => m.t === 'clipstart').map((m) => m.frame)).toEqual([65]);
    expect(h.sent.filter((m) => m.t === 'clipstop').map((m) => m.frame)).toEqual([193]);
  });

  it('clears POLY on an explicit ordinary clip replacement', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const poly = new Uint8Array(256);
    const ordinary = new Uint8Array(256);
    const lanes: Array<Record<string, unknown> | null> = Array.from({ length: 16 }, () => null);
    lanes[0] = { enabled: true, sourceBar: 0, steps: 3, rotation: 0, timing: { mode: 'fit', cycleBeats: 4 } };
    h.send({ t: 'host', on: 1 });
    h.send({ t: 'tempo', bpm: 120, swing: 0, anchor: 0 });
    h.send({ t: 'clip', data: poly.buffer, bars: 1, atFrame: 0, rhythm: { v: 1, lanes } });
    run(h, 1, 128);
    h.send({ t: 'clipupdate', data: ordinary.buffer, bars: 1, rhythm: null });
    const proc = h.proc as unknown as { clip: { rhythm: unknown } | null };
    expect(proc.clip?.rhythm).toBeNull();
  });

  it('keeps the shared standalone clock when rotation changes live', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const patterns = makeEmptyPatterns();
    patterns[patIdx(0, 0, 0)] = 1;
    patterns[patIdx(0, 2, 0)] = 1;
    const lanes = Array.from({ length: 16 }, () => null) as Array<Record<string, unknown> | null>;
    lanes[0] = { enabled: true, sourceBar: 0, steps: 3, rotation: 0, timing: { mode: 'fit', cycleBeats: 4 } };
    h.send({ t: 'p', k: 'seq.bpm', v: 120 });
    h.send({ t: 'seq', data: patterns.buffer, chain: [0], rhythm: { v: 1, lanes } });
    h.send({ t: 'play' });
    run(h, 79, 128); // currentFrame = 10112, just after the live edit point
    lanes[0] = { ...lanes[0], rotation: 1 };
    h.send({ t: 'seq', data: patterns.buffer, chain: [0], rhythm: { v: 1, lanes } });
    run(h, 700, 128);
    const kickFrames = h.sent.filter((m) => m.t === 'step' && (m.hits as number[]).includes(2)).map((m) => m.frame as number);
    const fitFrames = h.sent.filter((m) => m.t === 'poly' && m.pad === 0).map((m) => m.frame as number);
    expect(kickFrames).toEqual([0, 96000]);
    expect(fitFrames.slice(0, 3)).toEqual([0, 32000, 64000]);
  });

  it('hands standalone playback back to the legacy clock without an immediate retrigger', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const patterns = makeEmptyPatterns();
    patterns[patIdx(0, 0, 0)] = 1;
    const lanes = Array.from({ length: 16 }, () => null) as Array<Record<string, unknown> | null>;
    lanes[0] = { enabled: true, sourceBar: 0, steps: 3, rotation: 0, timing: { mode: 'fit', cycleBeats: 4 } };
    h.send({ t: 'p', k: 'seq.bpm', v: 120 });
    h.send({ t: 'seq', data: patterns.buffer, chain: [0], rhythm: { v: 1, lanes } });
    h.send({ t: 'play' });
    run(h, 79, 128);
    h.send({ t: 'seq', data: patterns.buffer, chain: [0], rhythm: null });
    run(h, 20, 128);
    let frames = h.sent.filter((m) => m.t === 'step' && (m.hits as number[]).includes(0));
    expect(frames).toHaveLength(0);
    run(h, 660, 128);
    frames = h.sent.filter((m) => m.t === 'step' && (m.hits as number[]).includes(0));
    expect(frames).toHaveLength(1);
  });

  it('re-times future POLY events at a live BPM change without catch-up', () => {
    (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
    const h = makeDrumProcessor();
    const patterns = makeEmptyPatterns();
    for (let s = 0; s < 16; s++) patterns[patIdx(0, 0, s)] = 1;
    const lanes = Array.from({ length: 16 }, () => null) as Array<Record<string, unknown> | null>;
    lanes[0] = { enabled: true, sourceBar: 0, steps: 16, rotation: 0, timing: { mode: 'grid' } };
    h.send({ t: 'p', k: 'seq.bpm', v: 120 });
    h.send({ t: 'seq', data: patterns.buffer, chain: [0], rhythm: { v: 1, lanes } });
    h.send({ t: 'play' });
    run(h, 750, 128); // exactly 96000 samples
    h.send({ t: 'p', k: 'seq.bpm', v: 180 });
    run(h, 100, 128);
    const frames = h.sent.filter((m) => m.t === 'step').map((m) => m.frame as number);
    const after = frames.filter((frame) => frame >= 96000);
    expect(after.slice(0, 3)).toEqual([96000, 100000, 104000]);
  });
});
