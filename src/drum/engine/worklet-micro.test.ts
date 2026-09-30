import { describe, expect, it } from 'vitest';
import { makeDrumProcessor } from './workletHarness';
import { defaultDrumLane, type DrumRhythm } from '../rhythm';

function setup(hosted = false, bpm = 120, swing = 0) {
  (globalThis as unknown as { currentFrame: number }).currentFrame = 0;
  const h = makeDrumProcessor();
  const data = new Uint8Array(1024);
  const rhythm: DrumRhythm = { v: 1, lanes: Array(16).fill(null) };
  const lane = { ...defaultDrumLane(), delayMs: 10, stepDelayMs: Array(64).fill(0) as number[] };
  rhythm.lanes[0] = lane;
  const start = () => {
    if (hosted) {
      h.send({ t: 'host', on: true });
      h.send({ t: 'tempo', bpm, swing, anchor: 0 });
      h.send({ t: 'clip', data: data.buffer, bars: 2, rhythm, atFrame: 0 });
    } else {
      h.send({ t: 'p', k: 'seq.bpm', v: bpm });
      h.send({ t: 'p', k: 'master.swing', v: swing });
      h.send({ t: 'seq', data: data.buffer, chain: [0, 1], rhythm });
      h.send({ t: 'play' });
    }
  };
  const hits = () => h.sent.filter(m => m.t === 'poly' && m.pad === 0 && m.hit).map(m => [m.ordinal, m.frame]);
  return { h, data, rhythm, lane, start, hits };
}

describe('DR-1 micro timing', () => {
  it.each([false, true])('adds lane and per-bar step offsets and anticipates bar boundaries (hosted=%s)', hosted => {
    const f = setup(hosted);
    f.data[1] = 1; f.data[256] = 1;
    f.lane.stepDelayMs[1] = 15; f.lane.stepDelayMs[16] = -50;
    f.start(); f.h.render(780, 127);
    expect(f.hits()).toEqual([[1, 7200], [16, 94080]]);
  });
  it('clamps the first early hit to transport start and does not lose later early returns', () => {
    const f = setup(); f.lane.delayMs = -20; f.data[0] = 1;
    f.start(); f.h.render(1520, 127);
    expect(f.hits()).toEqual([[0, 0], [32, 191040]]);
  });
  it('orders adjacent hits by their shifted time, including across blocks', () => {
    const f = setup(false, 200, 1);
    f.data[1] = f.data[2] = 1; f.lane.delayMs = 0;
    f.lane.stepDelayMs[1] = 50; f.lane.stepDelayMs[2] = -50;
    f.start(); f.h.render(80, 127);
    expect(f.hits()).toEqual([[2, 4800], [1, 8402]]);
  });
  it('uses rotated FIT source steps and keeps milliseconds fixed at another tempo', () => {
    const f = setup(false, 150);
    Object.assign(f.lane, { enabled: true, steps: 3, rotation: 1, timing: { mode: 'fit', cycleBeats: 4 } });
    f.lane.stepDelayMs[0] = -30; f.data[0] = 1;
    f.start(); f.h.render(205, 127);
    expect(f.hits()).toEqual([[1, 24640]]);
  });
  it('editing offsets live does not replay hits already consumed', () => {
    const f = setup(); f.data[0] = f.data[1] = 1;
    f.start(); f.h.render(52, 128);
    f.lane.delayMs = 50;
    f.h.send({ t: 'seq', data: f.data.buffer, chain: [0, 1], rhythm: f.rhythm });
    f.h.render(40, 128);
    expect(f.hits()).toEqual([[0, 480], [1, 6480]]);
  });
});
