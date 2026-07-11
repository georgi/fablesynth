// Conductor tests: a FakeRig with recordable devices and a controllable
// frame clock stands in for the WebAudio rig. Acks are fired manually to
// assert the owner-flips-on-ack contract.

import { beforeEach, describe, expect, it } from 'vitest';
import type { SeqDevice } from './devices';
import { STOP } from './model';
import { barFrames } from './protocol';
import type { SeqRig } from './rig';
import { resetSeqStore, useSeqStore } from './store';

class FakeDevice implements SeqDevice {
  clips: Array<{ bars: number; atFrame: number; bytes: number }> = [];
  stops: number[] = [];
  tempos: Array<{ bpm: number; swing: number; anchor: number }> = [];
  onClipStart: ((frame: number) => void) | null = null;
  onClipStop: ((frame: number) => void) | null = null;
  onPos: ((step: number, bar: number) => void) | null = null;

  async init(): Promise<void> {}
  applyPatch(): void {}
  setTempo(bpm: number, swing: number, anchor: number): void {
    this.tempos.push({ bpm, swing, anchor });
  }
  scheduleClip(pattern: Uint8Array, bars: number, atFrame: number): void {
    this.clips.push({ bars, atFrame, bytes: pattern.length });
  }
  scheduleStop(atFrame: number): void {
    this.stops.push(atFrame);
  }
  panic(): void {}
}

class FakeRig implements SeqRig {
  sampleRate = 48000;
  frame = 0;
  devices: SeqDevice[] = [new FakeDevice(), new FakeDevice(), new FakeDevice(), new FakeDevice()];
  trackAnalysers = null;
  gains: Record<number, number> = {};
  master = 0;
  suspended = false;

  now(): number { return this.frame; }
  async suspend(): Promise<void> { this.suspended = true; }
  async resume(): Promise<void> { this.suspended = false; }
  setTrackGain(t: number, gain: number): void { this.gains[t] = gain; }
  setMasterGain(gain: number): void { this.master = gain; }
  sendTempo(bpm: number, swing: number, anchor: number): void {
    for (const d of this.devices) d.setTempo(bpm, swing, anchor);
  }
  panic(): void {}

  dev(t: number): FakeDevice { return this.devices[t] as FakeDevice; }
}

let rig: FakeRig;
const st = () => useSeqStore.getState();

beforeEach(async () => {
  resetSeqStore();
  rig = new FakeRig();
  rig.frame = 1000;
  await st().powerOn(rig);
});

describe('power-on', () => {
  it('starts the transport: anchor ahead of now, tempo to every device', () => {
    expect(st().powered).toBe(true);
    expect(st().playing).toBe(true);
    expect(st().anchor).toBeGreaterThan(1000);
    for (let t = 0; t < 4; t++) {
      expect(rig.dev(t).tempos).toHaveLength(1);
      expect(rig.dev(t).tempos[0].bpm).toBe(122);
    }
  });

  it('applies fader gains (silent tracks stay open until muted)', () => {
    expect(rig.gains[0]).toBeGreaterThan(0);
    expect(rig.master).toBeGreaterThan(0);
  });
});

describe('quantized launching', () => {
  it('launch schedules the clip at the next bar boundary and queues in the UI', () => {
    rig.frame = st().anchor + 10; // just past beat zero
    st().launch(0, 2); // DROP A drums
    const d = rig.dev(0);
    expect(d.clips).toHaveLength(1);
    const bar = barFrames(122, 48000);
    expect(d.clips[0].atFrame).toBeCloseTo(st().anchor + bar, 6);
    expect(d.clips[0].bars).toBe(2);
    expect(d.clips[0].bytes).toBe(2 * 256);
    expect(st().queue[0]).toBe(2);
    expect(st().owner[0]).toBeUndefined(); // not yet — waits for the ack
  });

  it('owner flips on the clipstart ack, not before', () => {
    st().launch(0, 2);
    rig.dev(0).onClipStart!(12345);
    expect(st().owner[0]).toBe(2);
    expect(st().queue[0]).toBeUndefined();
  });

  it('re-launching before the boundary re-targets (device pending replaces)', () => {
    st().launch(0, 2);
    st().launch(0, 3);
    expect(rig.dev(0).clips).toHaveLength(2);
    expect(st().queue[0]).toBe(3);
    rig.dev(0).onClipStart!(1);
    expect(st().owner[0]).toBe(3);
  });

  it('quant OFF stamps atFrame 0 (= now)', () => {
    st().cycleQuant(1);
    st().cycleQuant(1); // 1 BAR -> 1/4 -> OFF
    expect(st().quant).toBe('OFF');
    st().launch(1, 2);
    expect(rig.dev(1).clips[0].atFrame).toBe(0);
  });

  it('launching an empty slot is a no-op', () => {
    st().launch(0, 4); // BREAK has no drums
    expect(rig.dev(0).clips).toHaveLength(0);
  });

  it('launching while paused resumes the context', () => {
    st().togglePlay();
    expect(rig.suspended).toBe(true);
    st().launch(0, 2);
    expect(rig.suspended).toBe(false);
    expect(st().playing).toBe(true);
  });
});

describe('stopping', () => {
  it('stopTrack schedules a stop and clears on the clipstop ack', () => {
    st().launch(0, 2);
    rig.dev(0).onClipStart!(1);
    st().stopTrack(0);
    expect(rig.dev(0).stops).toHaveLength(1);
    expect(st().queue[0]).toBe(STOP);
    rig.dev(0).onClipStop!(2);
    expect(st().owner[0]).toBeUndefined();
    expect(st().queue[0]).toBeUndefined();
  });

  it('stopTrack on an idle track is a no-op', () => {
    st().stopTrack(0);
    expect(rig.dev(0).stops).toHaveLength(0);
  });

  it('a stop ack clears a pending-only launch that was cancelled', () => {
    st().launch(0, 2);
    st().stopTrack(0); // cancels the pending launch in the device
    rig.dev(0).onClipStop!(1);
    expect(st().queue[0]).toBeUndefined();
    expect(st().owner[0]).toBeUndefined();
  });

  it('stopAll fans out to every occupied track', () => {
    st().launch(0, 2);
    st().launch(1, 2);
    rig.dev(0).onClipStart!(1);
    st().stopAll();
    expect(rig.dev(0).stops).toHaveLength(1);
    expect(rig.dev(1).stops).toHaveLength(1); // pending-only also stopped
    expect(rig.dev(2).stops).toHaveLength(0); // idle untouched
  });
});

describe('scene operations', () => {
  it('launchScene schedules only tracks with clips', () => {
    st().launchScene(1); // BUILD: no LEAD clip
    expect(rig.dev(0).clips).toHaveLength(1);
    expect(rig.dev(1).clips).toHaveLength(1);
    expect(rig.dev(2).clips).toHaveLength(0);
    expect(rig.dev(3).clips).toHaveLength(1);
  });

  it('launchScene stops tracks whose cell is empty (Ableton stop buttons)', () => {
    st().launchScene(2); // DROP A: all four tracks
    rig.devices.forEach((d) => (d as FakeDevice).onClipStart!(1));
    st().launchScene(4); // BREAK: no drums clip
    expect(rig.dev(0).stops).toHaveLength(1);
    expect(st().queue[0]).toBe(STOP);
  });

  it('launchScene leaves idle empty tracks alone (stop is a no-op)', () => {
    st().launchScene(4); // BREAK: no drums clip, drums never started
    expect(rig.dev(0).stops).toHaveLength(0);
    expect(st().queue[0]).toBeUndefined();
  });

  it('a pass-through empty cell lets the previous clip ride', () => {
    st().launchScene(2);
    rig.devices.forEach((d) => (d as FakeDevice).onClipStart!(1));
    st().togglePassThrough(4, 0); // BREAK drums: remove the stop button
    st().launchScene(4);
    expect(rig.dev(0).stops).toHaveLength(0);
    expect(st().owner[0]).toBe(2); // DROP A drums keep playing
  });

  it('togglePassThrough flips the session doc flag both ways', () => {
    st().togglePassThrough(4, 0);
    expect(st().session.scenes[4].pass).toContain(0);
    st().togglePassThrough(4, 0);
    expect(st().session.scenes[4].pass).not.toContain(0);
  });

  it('stopScene stops only tracks owned by (or queued for) that scene', () => {
    st().launchScene(2);
    rig.devices.forEach((d) => (d as FakeDevice).onClipStart!(1));
    st().launch(0, 3); // drums re-queued to DROP B
    rig.dev(0).onClipStart!(2);
    st().stopScene(2);
    expect(rig.dev(0).stops).toHaveLength(0); // now owned by scene 3
    expect(rig.dev(1).stops).toHaveLength(1);
    expect(rig.dev(2).stops).toHaveLength(1);
    expect(rig.dev(3).stops).toHaveLength(1);
  });
});

describe('mute / solo / gains', () => {
  it('track mute closes the gain, unmute restores the fader value', () => {
    const before = rig.gains[1];
    st().toggleTrackMute(1);
    expect(rig.gains[1]).toBe(0);
    st().toggleTrackMute(1);
    expect(rig.gains[1]).toBeCloseTo(before, 9);
  });

  it('solo closes every other track', () => {
    st().toggleSolo(2);
    expect(rig.gains[2]).toBeGreaterThan(0);
    expect(rig.gains[0]).toBe(0);
    expect(rig.gains[1]).toBe(0);
    expect(rig.gains[3]).toBe(0);
  });

  it('scene mute closes tracks owned by that scene', () => {
    st().launch(0, 2);
    rig.dev(0).onClipStart!(1);
    st().toggleSceneMute(2);
    expect(rig.gains[0]).toBe(0);
    expect(rig.gains[1]).toBeGreaterThan(0); // not owned by scene 2
  });

  it('swing changes go to devices live with the unchanged anchor', () => {
    const anchor = st().anchor;
    st().setSwing(0.4);
    const last = rig.dev(0).tempos[rig.dev(0).tempos.length - 1];
    expect(last.swing).toBe(0.4);
    expect(last.anchor).toBe(anchor);
  });
});

describe('pause', () => {
  it('togglePlay suspends and resumes the context', () => {
    st().togglePlay();
    expect(rig.suspended).toBe(true);
    expect(st().playing).toBe(false);
    st().togglePlay();
    expect(rig.suspended).toBe(false);
  });
});
