import { beforeEach, describe, expect, it } from 'vitest';
import {
  applyQueue, isTrackAudible, queueApplies, SCENES, seededRand, STOP, stepsFor, TRACKS,
} from './model';
import { resetSeqStore, useSeqStore } from './store';

const st = () => useSeqStore.getState();

/** Advance the clock n quarter-note beats. */
const tick = (n = 1) => {
  for (let i = 0; i < n; i++) st().onBeat();
};

beforeEach(() => resetSeqStore());

describe('launcher primitives', () => {
  it('applyQueue overwrites owners and STOP clears them', () => {
    const owner = { 0: 2, 1: 2 };
    const next = applyQueue(owner, { 0: 4, 1: STOP, 3: 1 });
    expect(next).toEqual({ 0: 4, 3: 1 });
    // input untouched
    expect(owner).toEqual({ 0: 2, 1: 2 });
  });

  it('queueApplies fires on every beat at 1/4 and only on the downbeat at 1 BAR', () => {
    expect(queueApplies('1/4', 1)).toBe(true);
    expect(queueApplies('1/4', 0)).toBe(true);
    expect(queueApplies('1 BAR', 0)).toBe(true);
    expect(queueApplies('1 BAR', 1)).toBe(false);
    expect(queueApplies('OFF', 0)).toBe(false);
  });
});

describe('quantized launching', () => {
  it('opens with DROP A owning all four tracks, playing at 1 BAR quantize', () => {
    expect(st().playing).toBe(true);
    expect(st().quant).toBe('1 BAR');
    expect(st().owner).toEqual({ 0: 2, 1: 2, 2: 2, 3: 2 });
  });

  it('a launch at 1 BAR waits in the queue until the next downbeat', () => {
    st().setClip(0, 3);
    expect(st().owner[0]).toBe(2);
    expect(st().queue[0]).toBe(3);
    tick(3); // beats 1,2,3 — still inside the bar
    expect(st().owner[0]).toBe(2);
    tick(); // wraps to beat 0
    expect(st().owner[0]).toBe(3);
    expect(st().queue).toEqual({});
  });

  it('a launch at 1/4 applies on the very next beat', () => {
    st().cycleQuant(1); // 1 BAR -> 1/4
    expect(st().quant).toBe('1/4');
    st().setClip(2, 4);
    expect(st().owner[2]).toBe(2);
    tick();
    expect(st().owner[2]).toBe(4);
  });

  it('a launch with quantize OFF applies immediately', () => {
    st().cycleQuant(-1); // 1 BAR -> OFF
    expect(st().quant).toBe('OFF');
    st().setClip(1, 4);
    expect(st().owner[1]).toBe(4);
    expect(st().queue).toEqual({});
  });

  it('re-launching before the boundary replaces the queued target', () => {
    st().setClip(0, 3);
    st().setClip(0, STOP);
    tick(4);
    expect(st().owner[0]).toBeUndefined();
  });

  it('launching while paused resumes the clock', () => {
    st().togglePlay();
    expect(st().playing).toBe(false);
    st().setClip(0, 0);
    expect(st().playing).toBe(true);
  });

  it('the clock is inert while paused', () => {
    st().setClip(0, 3);
    st().togglePlay();
    const { beat, bar } = st();
    tick(8);
    expect(st().beat).toBe(beat);
    expect(st().bar).toBe(bar);
    expect(st().owner[0]).toBe(2); // queue untouched too
  });
});

describe('scene operations', () => {
  it('launchScene queues only the tracks that have clips', () => {
    st().launchScene(1); // BUILD has no LEAD clip
    expect(st().queue).toEqual({ 0: 1, 1: 1, 3: 1 });
    tick(4);
    expect(st().owner).toEqual({ 0: 1, 1: 1, 2: 2, 3: 1 });
  });

  it('stopScene stops only the tracks that scene owns', () => {
    st().launchScene(4); // BREAK: bass/lead/pads
    tick(4);
    expect(st().owner).toEqual({ 0: 2, 1: 4, 2: 4, 3: 4 });
    st().stopScene(4);
    tick(4);
    expect(st().owner).toEqual({ 0: 2 });
  });

  it('stopAll clears owners and pending queue immediately', () => {
    st().setClip(0, 5);
    st().stopAll();
    expect(st().owner).toEqual({});
    expect(st().queue).toEqual({});
  });

  it('bar counter advances once per four beats', () => {
    expect(st().bar).toBe(1);
    tick(4);
    expect(st().bar).toBe(2);
    expect(st().beat).toBe(0);
  });

  it('cycleQuant wraps in both directions', () => {
    st().cycleQuant(1);
    st().cycleQuant(1);
    expect(st().quant).toBe('OFF');
    st().cycleQuant(1);
    expect(st().quant).toBe('1 BAR');
    st().cycleQuant(-1);
    expect(st().quant).toBe('OFF');
  });
});

describe('audibility (mute / solo / scene mute)', () => {
  it('a track with no owner is silent', () => {
    expect(isTrackAudible(0, {}, {}, {}, {})).toBe(false);
  });

  it('track mute silences only that track', () => {
    st().toggleTrackMute(1);
    const s = st();
    expect(isTrackAudible(0, s.owner, s.trackMute, s.sceneMute, s.solo)).toBe(true);
    expect(isTrackAudible(1, s.owner, s.trackMute, s.sceneMute, s.solo)).toBe(false);
  });

  it('solo silences every other track', () => {
    st().toggleSolo(2);
    const s = st();
    expect(isTrackAudible(2, s.owner, s.trackMute, s.sceneMute, s.solo)).toBe(true);
    expect(isTrackAudible(0, s.owner, s.trackMute, s.sceneMute, s.solo)).toBe(false);
  });

  it('scene mute silences tracks owned by that scene', () => {
    st().toggleSceneMute(2); // DROP A owns everything
    const s = st();
    for (let t = 0; t < TRACKS.length; t++) {
      expect(isTrackAudible(t, s.owner, s.trackMute, s.sceneMute, s.solo)).toBe(false);
    }
  });
});

describe('clip step previews', () => {
  it('are deterministic and stable across calls', () => {
    expect(stepsFor(0, 2)).toBe(stepsFor(0, 2)); // cached
    const a = seededRand(42), b = seededRand(42);
    expect(a()).toBe(b());
  });

  it('exist as 16 steps for every clip slot', () => {
    SCENES.forEach((sc, s) => {
      sc.clips.forEach((c, t) => {
        if (!c) return;
        const steps = stepsFor(t, s);
        expect(steps).toHaveLength(16);
        steps.forEach((sp) => expect(sp.h).toBeGreaterThanOrEqual(3));
      });
    });
  });

  it('drum patterns keep the four-on-the-floor anchors', () => {
    SCENES.forEach((sc, s) => {
      if (!sc.clips[0]) return;
      const steps = stepsFor(0, s);
      for (const i of [0, 4, 8, 12]) expect(steps[i].on).toBe(true);
    });
  });
});
