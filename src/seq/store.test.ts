// Conductor tests: a FakeRig with recordable devices and a controllable
// frame clock stands in for the WebAudio rig. Acks are fired manually to
// assert the owner-flips-on-ack contract.

import { beforeEach, describe, expect, it } from 'vitest';
import type { SeqDevice } from './devices';
import { STOP } from './model';
import { barFrames } from './protocol';
import type { SeqRig } from './rig';
import { clipPattern, resetSeqStore, useSeqStore } from './store';
import { decodeClipLibrary } from './clipLibrary';
import { FACTORY_CLIP_LIBRARY } from './clipLibrary.gen';

const library = decodeClipLibrary({ v: 1, clips: FACTORY_CLIP_LIBRARY }).clips;

class FakeDevice implements SeqDevice {
  clips: Array<{ bars: number; atFrame: number; bytes: number }> = [];
  stops: number[] = [];
  tempos: Array<{ bpm: number; swing: number; anchor: number }> = [];
  updates: Array<{ bars: number; bytes: number }> = [];
  patches: unknown[] = [];
  onClipStart: ((frame: number) => void) | null = null;
  onClipStop: ((frame: number) => void) | null = null;
  onPos: ((step: number, bar: number) => void) | null = null;

  async init(): Promise<void> {}
  applyPatch(patch: unknown): void { this.patches.push(patch); }
  setTempo(bpm: number, swing: number, anchor: number): void {
    this.tempos.push({ bpm, swing, anchor });
  }
  scheduleClip(pattern: Uint8Array, bars: number, atFrame: number): void {
    this.clips.push({ bars, atFrame, bytes: pattern.length });
  }
  scheduleStop(atFrame: number): void {
    this.stops.push(atFrame);
  }
  updateClip(pattern: Uint8Array, bars: number): void {
    this.updates.push({ bars, bytes: pattern.length });
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

  now(): number { return this.frame; }
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
  it('unlocks audio with the transport stopped and sends tempo to every device', () => {
    expect(st().powered).toBe(true);
    expect(st().playing).toBe(false);
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
    st().launch(1, 2); // start the transport on bass, leaving drums idle
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

  it('launching while stopped starts a fresh transport without suspending audio', () => {
    const temposBefore = rig.dev(0).tempos.length;
    st().launch(0, 2);
    expect(st().playing).toBe(true);
    expect(rig.dev(0).tempos).toHaveLength(temposBefore + 1);
    expect(rig.dev(0).clips[0].atFrame).toBe(st().anchor);
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
  it('launchScene starts the stopped transport and launches on its new downbeat', () => {
    expect(st().playing).toBe(false);
    st().launchScene(2);
    expect(st().playing).toBe(true);
    expect(rig.dev(0).clips[0].atFrame).toBe(st().anchor);
  });

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

describe('clip editing', () => {
  it('updateClipBytes rewrites the doc, cache and persists', () => {
    const before = st().session.scenes[0].clips[0]!;
    const bytes = new Uint8Array(2 * 256).fill(0);
    bytes[0] = 1;
    st().updateClipBytes(0, 0, bytes, 2);
    const clip = st().session.scenes[0].clips[0]!;
    expect(clip.bars).toBe(2);
    expect(clip.pattern).not.toBe(before.pattern);
    expect(clipPattern(st().session, 0, 0)![0]).toBe(1);
  });

  it('hot-swaps the device when the clip is live on its track', () => {
    st().launch(0, 0);
    rig.dev(0).onClipStart!(0); // ack → owner[0] = 0
    st().updateClipBytes(0, 0, new Uint8Array(256), 1);
    expect(rig.dev(0).updates).toEqual([{ bars: 1, bytes: 256 }]);
  });

  it('hot-swaps when the clip is queued, not yet live', () => {
    st().launch(0, 0); // queued, no ack yet
    st().updateClipBytes(0, 0, new Uint8Array(256), 1);
    expect(rig.dev(0).updates).toHaveLength(1);
  });

  it('routes edits to the queued clip, not the outgoing live clip', () => {
    st().launch(0, 0);
    rig.dev(0).onClipStart!(0); // scene 0 live
    st().launch(0, 1); // scene 1 queued — the worklet's write target
    st().updateClipBytes(0, 0, new Uint8Array(256), 1); // edit live scene 0
    expect(rig.dev(0).updates).toHaveLength(0); // would clobber scene 1's pend
    st().updateClipBytes(1, 0, new Uint8Array(2 * 256), 2); // edit queued scene 1
    expect(rig.dev(0).updates).toEqual([{ bars: 2, bytes: 512 }]);
  });

  it('sends nothing when the clip is idle or another scene owns the track', () => {
    st().launch(0, 1);
    rig.dev(0).onClipStart!(0); // scene 1 owns track 0
    st().updateClipBytes(0, 0, new Uint8Array(256), 1); // editing scene 0's clip
    expect(rig.dev(0).updates).toHaveLength(0);
  });

  it('createClip writes a silent 1-bar clip into an empty cell only', () => {
    const empty = st().session.scenes.findIndex((sc) => !sc.clips[1]);
    expect(empty).toBeGreaterThanOrEqual(0);
    st().createClip(empty, 1);
    const clip = st().session.scenes[empty].clips[1]!;
    expect(clip.bars).toBe(1);
    expect(clipPattern(st().session, empty, 1)!.length).toBe(48); // BL1: 16*3
    const again = st().session.scenes[empty].clips[1];
    st().createClip(empty, 1); // no overwrite
    expect(st().session.scenes[empty].clips[1]).toBe(again);
  });

  it('deleteClip clears the cell, drops the cache and only touches a filled slot', () => {
    expect(st().session.scenes[0].clips[0]).not.toBeNull();
    st().deleteClip(0, 0);
    expect(st().session.scenes[0].clips[0]).toBeNull();
    expect(clipPattern(st().session, 0, 0)).toBeNull();
    const session = st().session;
    st().deleteClip(0, 0); // already empty → no-op
    expect(st().session).toBe(session);
  });

  it('deleteClip stops a live clip before removing it', () => {
    st().launch(0, 0);
    rig.dev(0).onClipStart!(0); // owner[0] = 0
    st().deleteClip(0, 0);
    expect(rig.dev(0).stops).toHaveLength(1);
    expect(st().queue[0]).toBe(STOP);
    expect(st().session.scenes[0].clips[0]).toBeNull();
  });

  it('deleteClip stops a queued-but-not-live clip too', () => {
    st().launch(0, 0); // queued, no ack
    st().deleteClip(0, 0);
    expect(rig.dev(0).stops).toHaveLength(1);
  });

  it('setTrackPatch swaps the patch doc in place', () => {
    st().setTrackPatch(0, { kind: 'inline', data: { params: { x: 1 } } });
    expect(st().session.tracks[0].patch).toEqual({ kind: 'inline', data: { params: { x: 1 } } });
    expect(st().session.tracks[1].patch.kind).toBe('factory');
  });

  it('recalls a factory device patch by the shared bank index', () => {
    st().loadTrackFactoryPatch(1, 4);
    expect(st().session.tracks[1].patch).toEqual({ kind: 'factory', index: 4 });
    expect(rig.dev(1).patches).toEqual([{ kind: 'factory', index: 4 }]);
  });

  it('loads a compatible library clip into only the target cell and preserves the patch', () => {
    const entry = library.find((clip) => clip.machine === 'DR1')!;
    const patch = st().session.tracks[0].patch;
    const other = st().session.scenes[1].clips[0];
    expect(st().loadLibraryClip(0, 0, entry)).toBe(true);
    expect(st().session.scenes[0].clips[0]?.name).toBe(entry.name);
    expect(clipPattern(st().session, 0, 0)).toEqual(entry.pattern);
    expect(st().session.scenes[1].clips[0]).toBe(other);
    expect(st().session.tracks[0].patch).toBe(patch);
    expect(st().clipLoadRevision).toBe(1);
  });

  it('rejects a library clip for another machine without changing the session', () => {
    const entry = library.find((clip) => clip.machine === 'BL1')!;
    const session = st().session;
    expect(st().loadLibraryClip(0, 0, entry)).toBe(false);
    expect(st().session).toBe(session);
  });

  it('hot-swaps a library load when the target is live or pending', () => {
    const entry = library.find((clip) => clip.machine === 'DR1')!;
    st().launch(0, 0);
    expect(st().loadLibraryClip(0, 0, entry)).toBe(true);
    expect(rig.dev(0).updates).toEqual([{ bars: entry.bars, bytes: entry.pattern.length }]);
  });
});

describe('transport', () => {
  it('uses one toggle for play and immediate stop without suspending audio', () => {
    st().toggleTransport();
    expect(st().playing).toBe(true);
    st().launch(0, 2);
    rig.dev(0).onClipStart!(1);

    st().toggleTransport();
    expect(st().playing).toBe(false);
    expect(st().beat).toBe(0);
    expect(st().bar).toBe(1);
    expect(st().queue[0]).toBe(STOP);
    expect(rig.dev(0).stops[rig.dev(0).stops.length - 1]).toBe(0);
  });

  it('play from a standstill with nothing active launches the first scene', () => {
    st().toggleTransport();
    expect(st().playing).toBe(true);
    // Scene 0 fires on the downbeat instead of running an empty transport.
    expect(rig.dev(0).clips[0].atFrame).toBe(st().anchor);
    expect(st().queue[0]).toBe(0);
  });

  it('ignores a clip-start ack that arrives after transport stop', () => {
    st().launch(0, 2);
    st().toggleTransport();
    rig.dev(0).onClipStart!(1);
    expect(st().owner[0]).toBeUndefined();
    expect(st().queue[0]).toBe(STOP);
  });
});

describe('grid selection', () => {
  it('setGridSelection defaults head to anchor and clears again', () => {
    st().setGridSelection({ s: 1, t: 2 });
    expect(st().gridSel).toEqual({ anchor: { s: 1, t: 2 }, head: { s: 1, t: 2 } });
    st().setGridSelection({ s: 1, t: 2 }, { s: 3, t: 0 });
    expect(st().gridSel!.head).toEqual({ s: 3, t: 0 });
    st().clearGridSelection();
    expect(st().gridSel).toBeNull();
  });

  it('clamps anchor and head to the grid bounds', () => {
    st().setGridSelection({ s: -3, t: 99 }, { s: 42, t: -1 });
    expect(st().gridSel).toEqual({
      anchor: { s: 0, t: 3 },
      head: { s: 5, t: 0 },
    });
  });

  it('setGridDrag stores and clears the live drag', () => {
    const drag = { from: { s: 0, t: 0 }, to: { s: 1, t: 0 }, copy: false };
    st().setGridDrag(drag);
    expect(st().gridDrag).toEqual(drag);
    st().setGridDrag(null);
    expect(st().gridDrag).toBeNull();
  });
});

describe('grid clipboard verbs', () => {
  it('copySelection captures the rectangle with per-column machines', () => {
    st().setGridSelection({ s: 2, t: 2 }, { s: 3, t: 3 });
    st().copySelection();
    const cb = st().gridClipboard!;
    expect(cb.machines).toEqual(['WT1', 'WT1']);
    expect(cb.cells.map((row) => row.map((c) => c?.name ?? null))).toEqual([
      ['GLASS HOOK', 'FOG STABS'],
      ['GLASS HOOK II', 'FOG STABS'],
    ]);
    expect(st().session.scenes[2].clips[2]).not.toBeNull(); // copy leaves sources
  });

  it('copySelection without a selection is a no-op', () => {
    st().copySelection();
    expect(st().gridClipboard).toBeNull();
  });

  it('cutSelection captures then clears the source cells and cache', () => {
    st().setGridSelection({ s: 0, t: 0 });
    st().cutSelection();
    expect(st().gridClipboard!.cells[0][0]?.name).toBeDefined();
    expect(st().session.scenes[0].clips[0]).toBeNull();
    expect(clipPattern(st().session, 0, 0)).toBeNull();
  });

  it('cutSelection boundary-stops a cut cell that owns its track', () => {
    st().launch(0, 0);
    rig.dev(0).onClipStart!(1);
    st().setGridSelection({ s: 0, t: 0 });
    st().cutSelection();
    expect(rig.dev(0).stops).toHaveLength(1);
    expect(st().queue[0]).toBe(STOP);
  });

  it('pasteAt writes the payload, replacing filled cells', () => {
    st().setGridSelection({ s: 2, t: 2 }); // GLASS HOOK
    st().copySelection();
    st().pasteAt(0, 2); // empty WT1 cell
    expect(st().session.scenes[0].clips[2]?.name).toBe('GLASS HOOK');
    expect(clipPattern(st().session, 0, 2)).toEqual(clipPattern(st().session, 2, 2));
    st().pasteAt(3, 2); // over GLASS HOOK II
    expect(st().session.scenes[3].clips[2]?.name).toBe('GLASS HOOK');
  });

  it('pasteAt skips machine-mismatched cells without touching the session', () => {
    st().setGridSelection({ s: 0, t: 0 }); // DR1 clip
    st().copySelection();
    const session = st().session;
    st().pasteAt(0, 1); // BL1 column
    expect(st().session).toBe(session);
  });

  it('pasteAt hot-swaps the device when the target cell is live', () => {
    st().launch(2, 2);
    rig.dev(2).onClipStart!(1); // GLASS HOOK live on track 2
    st().setGridSelection({ s: 3, t: 2 }); // GLASS HOOK II
    st().copySelection();
    st().pasteAt(2, 2);
    expect(rig.dev(2).updates).toHaveLength(1);
    expect(rig.dev(2).updates[0].bars).toBe(4);
  });

  it('duplicateSelection pastes the rectangle one scene down (nulls included)', () => {
    st().setGridSelection({ s: 0, t: 0 }, { s: 0, t: 3 }); // whole INTRO row
    st().duplicateSelection();
    const intro = st().session.scenes[0].clips;
    const build = st().session.scenes[1].clips;
    expect(build[0]?.name).toBe(intro[0]?.name);
    expect(build[1]).toBeNull(); // payload null replaces ACID CRAWL
    expect(build[3]?.name).toBe(intro[3]?.name);
  });

  it('duplicateSelection on the last scene is clamped away (no scene creation)', () => {
    const last = st().session.scenes.length - 1;
    st().setGridSelection({ s: last, t: 0 }, { s: last, t: 3 });
    const session = st().session;
    st().duplicateSelection();
    expect(st().session).toBe(session);
    expect(st().session.scenes).toHaveLength(last + 1);
  });

  it('deleteSelection clears every filled cell in the rectangle', () => {
    st().setGridSelection({ s: 0, t: 0 }, { s: 1, t: 1 });
    st().deleteSelection();
    for (const s of [0, 1]) {
      for (const t of [0, 1]) {
        expect(st().session.scenes[s].clips[t]).toBeNull();
        expect(clipPattern(st().session, s, t)).toBeNull();
      }
    }
  });
});

describe('grid drag / moveClips', () => {
  it('moves a single cell: source cleared, target written', () => {
    const name = st().session.scenes[0].clips[0]!.name;
    st().moveClips({ s: 0, t: 0 }, { s: 4, t: 0 }); // BREAK drums is empty
    expect(st().session.scenes[0].clips[0]).toBeNull();
    expect(st().session.scenes[4].clips[0]?.name).toBe(name);
    expect(clipPattern(st().session, 0, 0)).toBeNull();
    expect(clipPattern(st().session, 4, 0)).not.toBeNull();
  });

  it('Alt-copy keeps the source cell', () => {
    st().moveClips({ s: 0, t: 0 }, { s: 4, t: 0 }, { copy: true });
    expect(st().session.scenes[0].clips[0]).not.toBeNull();
    expect(st().session.scenes[4].clips[0]).not.toBeNull();
  });

  it('boundary-stops a moved cell that owns its track', () => {
    st().launch(0, 0);
    rig.dev(0).onClipStart!(1);
    st().moveClips({ s: 0, t: 0 }, { s: 4, t: 0 });
    expect(rig.dev(0).stops).toHaveLength(1);
    expect(st().queue[0]).toBe(STOP);
  });

  it('a machine-mismatched drop is a no-op', () => {
    const session = st().session;
    st().moveClips({ s: 0, t: 0 }, { s: 0, t: 1 }); // DR1 → BL1
    expect(st().session).toBe(session);
  });

  it('moves the selected block when grabbed inside it, selection follows', () => {
    st().setGridSelection({ s: 2, t: 2 }, { s: 2, t: 3 }); // GLASS HOOK + FOG STABS
    st().moveClips({ s: 2, t: 2 }, { s: 4, t: 2 });
    expect(st().session.scenes[2].clips[2]).toBeNull();
    expect(st().session.scenes[2].clips[3]).toBeNull();
    expect(st().session.scenes[4].clips[2]?.name).toBe('GLASS HOOK');
    expect(st().session.scenes[4].clips[3]?.name).toBe('FOG STABS');
    expect(st().gridSel).toEqual({ anchor: { s: 4, t: 2 }, head: { s: 4, t: 3 } });
  });

  it('drags only the grabbed cell when the grab point is outside the selection', () => {
    st().setGridSelection({ s: 0, t: 0 });
    st().moveClips({ s: 2, t: 2 }, { s: 0, t: 2 });
    expect(st().session.scenes[0].clips[2]?.name).toBe('GLASS HOOK');
    expect(st().session.scenes[0].clips[0]).not.toBeNull(); // selection untouched
  });
});

describe('grid undo/redo', () => {
  it('undo restores a deleted clip including its pattern cache', () => {
    const before = clipPattern(st().session, 0, 0)!;
    st().setGridSelection({ s: 0, t: 0 });
    st().deleteSelection();
    expect(st().session.scenes[0].clips[0]).toBeNull();
    st().undo();
    expect(st().session.scenes[0].clips[0]).not.toBeNull();
    expect(clipPattern(st().session, 0, 0)).toEqual(before);
  });

  it('redo reapplies and a fresh verb clears the redo stack', () => {
    st().setGridSelection({ s: 0, t: 0 });
    st().deleteSelection();
    st().undo();
    st().redo();
    expect(st().session.scenes[0].clips[0]).toBeNull();
    st().undo(); // filled again
    st().setGridSelection({ s: 0, t: 3 });
    st().deleteSelection(); // new verb → redo history gone
    st().redo();
    expect(st().session.scenes[0].clips[3]).toBeNull(); // redo was a no-op
    expect(st().session.scenes[0].clips[0]).not.toBeNull();
  });

  it('undo hot-swaps a restored clip that is still the live target', () => {
    st().launch(0, 0);
    rig.dev(0).onClipStart!(1);
    st().setGridSelection({ s: 0, t: 0 });
    st().deleteSelection(); // schedules the stop, owner unchanged until ack
    st().undo();
    expect(rig.dev(0).updates).toHaveLength(1);
  });

  it('undo with an empty history is a no-op', () => {
    const session = st().session;
    st().undo();
    expect(st().session).toBe(session);
    st().redo();
    expect(st().session).toBe(session);
  });

  it('undo merges clips only: a patch change made after the snapshot survives', () => {
    st().setGridSelection({ s: 0, t: 0 });
    st().deleteSelection(); // snapshot taken before this verb
    st().setTrackPatch(0, { kind: 'inline', data: { params: { x: 9 } } });
    st().undo();
    expect(st().session.scenes[0].clips[0]).not.toBeNull(); // clip edit reverted
    expect(st().session.tracks[0].patch).toMatchObject({ kind: 'inline' }); // patch kept
  });

  it('loadSessionPreset resets the undo history', () => {
    st().setGridSelection({ s: 0, t: 0 });
    st().deleteSelection(); // one undoable verb
    st().loadSessionPreset(0);
    const after = st().session;
    st().undo(); // must not resurrect the pre-preset document
    expect(st().session).toBe(after);
  });

  it('a move whose source clear stops the queued clip still hot-swaps the target from fresh state', () => {
    // Fill scene 1 / track 0 with a same-machine clip so it can be launched.
    st().setGridSelection({ s: 0, t: 0 });
    st().copySelection();
    st().pasteAt(1, 0);
    st().launch(0, 1);
    rig.dev(0).onClipStart!(1); // owner: scene 1
    st().launch(0, 0); // queue: scene 0
    st().moveClips({ s: 0, t: 0 }, { s: 1, t: 0 }); // drag the queued clip onto the owner cell
    // A stale state snapshot in syncWrites read queue as still s0 and skipped
    // this updateClip; fresh state resolves the target to the owner scene.
    expect(rig.dev(0).updates).toHaveLength(1);
    expect(st().queue[0]).toBe(STOP);
  });

  it('history is bounded, dropping the oldest snapshots past 50', () => {
    for (let i = 0; i < 60; i++) {
      st().setGridSelection(i % 2 === 0 ? { s: 2, t: 2 } : { s: 3, t: 2 });
      st().copySelection();
      st().pasteAt(0, 2);
    }
    for (let i = 0; i < 70; i++) st().undo();
    // Only the last 50 verbs unwind: the cell keeps verb #10's payload
    // (GLASS HOOK II) instead of returning to its empty initial state.
    expect(st().session.scenes[0].clips[2]?.name).toBe('GLASS HOOK II');
  });
});

describe('focus', () => {
  it('starts in session mode and enters on a track head', () => {
    expect(st().focus).toBeNull();
    st().enterFocus(2);
    expect(st().focus).toEqual({ track: 2, scene: 0 });
  });

  it('prefers the scene owning the track', () => {
    st().launch(1, 3);
    rig.dev(1).onClipStart!(0);
    st().enterFocus(1);
    expect(st().focus).toEqual({ track: 1, scene: 3 });
  });

  it('explicit scene wins (the ✎ path) and clamps to valid scenes', () => {
    st().enterFocus(0, 2);
    expect(st().focus).toEqual({ track: 0, scene: 2 });
    st().focusScene(99);
    expect(st().focus!.scene).toBe(st().session.scenes.length - 1);
    st().focusScene(-5);
    expect(st().focus!.scene).toBe(0);
  });

  it('remembers the last focused scene across exit/enter', () => {
    st().enterFocus(0, 2);
    st().exitFocus();
    expect(st().focus).toBeNull();
    st().enterFocus(3); // no owner on track 3, no explicit scene
    expect(st().focus).toEqual({ track: 3, scene: 2 });
  });

  it('switching heads keeps the scene', () => {
    st().enterFocus(0, 4);
    st().enterFocus(1);
    expect(st().focus).toEqual({ track: 1, scene: 4 });
  });
});
