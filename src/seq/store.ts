// The SQ-4 conductor (docs/sq4-clips.md §9). Owns all musical decisions:
// the owner/queue launcher state, quantize boundary scheduling against the
// shared context-frame timebase, and the track gain (fader × mute × solo)
// math. Devices execute stamped commands and report back; owner flips on
// their clipstart/clipstop acks so the grid shows what is audible.

import { create } from 'zustand';
import { isTrackOpen, type OwnerMap, type Quant, QUANTS, type QueueMap, STOP } from './model';
import {
  b64ToBytes, boundaryFrame, bytesToB64, emptyClipBytes, loadSession,
  type PatchDoc, saveSession, type SessionDoc, songPosition,
} from './protocol';
import { factorySession } from './factory';
import { copySession, FACTORY_SESSION_PRESETS } from './sessionPresets';
import { embedSessionPatches } from './sessionExport';
import type { SeqRig } from './rig';
import type { RuntimeClipLibraryEntry } from './clipLibrary';
import { loadLibraryClipIntoSession } from './clipLibraryActions';
import {
  applyWrites, type CellWrite, copyRect, type GridClipboard, type GridPos, type GridSel,
  inRect, moveWrites, pasteWrites, selRect,
} from './gridEdit';
import { isTourSeen, markTourSeen, nextTourStep } from './onboarding';

export interface TrackPos {
  step: number;
  bar: number;
}

export interface SeqStore {
  session: SessionDoc;
  powered: boolean;
  playing: boolean; // logical transport state; the audio context stays running
  beat: number;
  bar: number;
  owner: OwnerMap;
  queue: QueueMap;
  pos: (TrackPos | null)[]; // per-track playhead from device pos messages
  sceneMute: Record<number, boolean>;
  trackMute: Record<number, boolean>;
  solo: Record<number, boolean>;
  quant: Quant;
  trackVol: number[];
  masterVol: number;
  swing: number;
  rig: SeqRig | null;
  anchor: number; // songStartFrame — beat zero of the shared timebase
  focus: { track: number; scene: number } | null;
  clipLoadRevision: number;
  tour: number | null; // active onboarding step index, null = tour hidden
  gridSel: GridSel | null; // session-grid cell selection (anchor/head rectangle)
  gridClipboard: GridClipboard | null; // in-memory, per-app (not persisted)
  gridDrag: { from: GridPos; to: GridPos; copy: boolean } | null; // live pointer drag

  powerOn: (rig?: SeqRig) => Promise<void>;
  toggleTransport: () => void;
  launch: (t: number, s: number) => void;
  stopTrack: (t: number) => void;
  launchScene: (s: number) => void;
  stopScene: (s: number) => void;
  togglePassThrough: (s: number, t: number) => void;
  updateClipBytes: (s: number, t: number, bytes: Uint8Array, bars: number) => void;
  createClip: (s: number, t: number) => void;
  deleteClip: (s: number, t: number) => void;
  loadLibraryClip: (s: number, t: number, entry: RuntimeClipLibraryEntry, semitones?: number) => boolean;
  setTrackPatch: (t: number, patch: PatchDoc) => void;
  loadTrackFactoryPatch: (t: number, index: number) => void;
  stopAll: () => void;
  toggleSceneMute: (s: number) => void;
  toggleTrackMute: (t: number) => void;
  toggleSolo: (t: number) => void;
  cycleQuant: (d: number) => void;
  setTrackVol: (t: number, v: number) => void;
  setMasterVol: (v: number) => void;
  setSwing: (v: number) => void;
  loadSessionPreset: (index: number) => void;
  tick: () => void; // UI clock — called from a rAF loop while powered
  enterFocus: (t: number, s?: number) => void;
  exitFocus: () => void;
  focusScene: (s: number) => void;
  startTour: () => void;
  advanceTour: (d: number) => void;
  endTour: () => void;
  setGridSelection: (anchor: GridPos, head?: GridPos) => void;
  clearGridSelection: () => void;
  copySelection: () => void;
  cutSelection: () => void;
  pasteAt: (s: number, t: number) => void;
  duplicateSelection: () => void;
  deleteSelection: () => void;
  moveClips: (from: GridPos, to: GridPos, opts?: { copy?: boolean }) => void;
  setGridDrag: (drag: { from: GridPos; to: GridPos; copy: boolean } | null) => void;
  undo: () => void;
  redo: () => void;
}

const initialSession = loadSession(factorySession());

// Decoded clip pattern bytes, keyed `${scene}:${track}`. Session docs are
// edited in place (clip editing actions below); the cache is refreshed on
// every write so it never serves stale bytes.
const clipBytes = new Map<string, Uint8Array>();
function bytesFor(session: SessionDoc, s: number, t: number): Uint8Array | null {
  const clip = session.scenes[s]?.clips[t];
  if (!clip) return null;
  const key = `${s}:${t}`;
  let b = clipBytes.get(key);
  if (!b) {
    b = b64ToBytes(clip.pattern);
    clipBytes.set(key, b);
  }
  return b;
}

/** Preview bytes for the UI (null for empty cells). */
export function clipPattern(session: SessionDoc, s: number, t: number): Uint8Array | null {
  return bytesFor(session, s, t);
}

// Bounded undo/redo for grid editing verbs only (not knob/param changes).
// Sessions are immutable, so a snapshot is just the document reference.
const HISTORY_LIMIT = 50;
const undoStack: SessionDoc[] = [];
const redoStack: SessionDoc[] = [];

const gainCurve = (v: number) => v * v * 1.4;

export const useSeqStore = create<SeqStore>((set, get) => {
  // The scene most recently scheduled per track — the clipstart ack promotes
  // it to owner (acks don't carry a clip identity; devices hold one pending
  // slot, so the last schedule is by construction the one that started).
  const lastScheduled: Record<number, number> = {};

  // Scene to reopen on the next head-click focus (survives exit, not reload).
  let lastFocusScene = 0;

  const clampScene = (s: number): number =>
    Math.max(0, Math.min(get().session.scenes.length - 1, s));

  const applyGains = () => {
    const st = get();
    if (!st.rig) return;
    for (let t = 0; t < st.session.tracks.length; t++) {
      const open = isTrackOpen(t, st.owner, st.trackMute, st.sceneMute, st.solo);
      st.rig.setTrackGain(t, open ? gainCurve(st.trackVol[t]) : 0);
    }
  };

  const boundary = (): number => {
    const st = get();
    if (!st.rig) return 0;
    return boundaryFrame(st.quant, st.rig.now(), st.anchor, st.session.bpm, st.rig.sampleRate);
  };

  const startTransport = () => {
    const st = get();
    if (!st.rig || st.playing) return;
    const anchor = st.rig.now() + 256;
    st.rig.sendTempo(st.session.bpm, st.swing, anchor);
    set({ playing: true, anchor, beat: 0, bar: 1 });
  };

  const stopTransport = () => {
    const st = get();
    if (!st.rig || !st.playing) return;
    const queue = { ...st.queue };
    st.session.tracks.forEach((_, t) => {
      if (st.owner[t] == null && st.queue[t] == null) return;
      st.rig!.devices[t].scheduleStop(0);
      queue[t] = STOP;
    });
    set({ playing: false, beat: 0, bar: 1, queue });
  };

  const persist = () => {
    const st = get();
    saveSession(embedSessionPatches({ ...st.session, quant: st.quant, swing: st.swing }));
  };

  const clampPos = (p: GridPos): GridPos => ({
    s: Math.max(0, Math.min(get().session.scenes.length - 1, p.s)),
    t: Math.max(0, Math.min(get().session.tracks.length - 1, p.t)),
  });

  const pushHistory = () => {
    undoStack.push(get().session);
    if (undoStack.length > HISTORY_LIMIT) undoStack.shift();
    redoStack.length = 0;
  };

  // Per changed cell: keep the clipBytes cache in sync and mirror the
  // updateClipBytes / deleteClip runtime semantics — hot-swap the device when
  // the cell is the track's live/queued target, boundary-stop a cleared cell
  // that is playing or queued. Runs against the already-updated session.
  const syncWrites = (writes: CellWrite[]) => {
    for (const w of writes) {
      // Re-read per write: an earlier stopTrack in this loop mutates queue,
      // and the hot-swap target below must see that fresh state (a stale
      // snapshot would skip the updateClip for a moved clip's new home).
      const st = get();
      const key = `${w.s}:${w.t}`;
      if (w.clip == null) {
        clipBytes.delete(key);
        if (st.owner[w.t] === w.s || st.queue[w.t] === w.s) st.stopTrack(w.t);
      } else {
        const bytes = b64ToBytes(w.clip.pattern);
        clipBytes.set(key, bytes);
        const q = st.queue[w.t];
        const target = q != null && q !== STOP ? q : st.owner[w.t];
        if (st.rig && target === w.s) st.rig.devices[w.t].updateClip(bytes, w.clip.bars);
      }
    }
  };

  // One chokepoint for every grid verb: history push, immutable session
  // rebuild, cache/runtime sync, persist. Skipped cells (machine mismatch)
  // are surfaced as a log line — never partially written.
  const applyGridWrites = (writes: CellWrite[], skipped: number) => {
    if (skipped) console.info(`SQ-4: skipped ${skipped} machine-mismatched cell(s)`);
    if (!writes.length) return;
    pushHistory();
    set({ session: applyWrites(get().session, writes) });
    syncWrites(writes);
    persist();
  };

  // Undo/redo covers grid editing verbs ONLY, so restoring a snapshot merges
  // its clips into the CURRENT session — bpm, quant, swing, track patches and
  // scene pass flags mutated after the snapshot keep their live values (the
  // rig was never told to revert them).
  const restoreSession = (snapshot: SessionDoc) => {
    const cur = get().session;
    const writes: CellWrite[] = [];
    for (let s = 0; s < snapshot.scenes.length; s++) {
      for (let t = 0; t < snapshot.tracks.length; t++) {
        const a = cur.scenes[s]?.clips[t] ?? null;
        const b = snapshot.scenes[s].clips[t];
        if (a === b) continue;
        if (a && b && a.pattern === b.pattern && a.bars === b.bars && a.name === b.name) continue;
        writes.push({ s, t, clip: b });
      }
    }
    const next: SessionDoc = {
      ...cur,
      scenes: cur.scenes.map((sc, s) => (snapshot.scenes[s] ? { ...sc, clips: snapshot.scenes[s].clips } : sc)),
    };
    set({ session: next });
    syncWrites(writes);
    persist();
  };

  return {
    session: initialSession,
    powered: false,
    playing: false,
    beat: 0,
    bar: 1,
    owner: {},
    queue: {},
    pos: initialSession.tracks.map(() => null),
    sceneMute: {},
    trackMute: {},
    solo: {},
    quant: initialSession.quant,
    trackVol: initialSession.tracks.map((t) => t.gain),
    masterVol: 0.75,
    swing: initialSession.swing,
    rig: null,
    anchor: 0,
    focus: null,
    clipLoadRevision: 0,
    tour: null,
    gridSel: null,
    gridClipboard: null,
    gridDrag: null,

    powerOn: async (rigIn?: SeqRig) => {
      const session = get().session;
      let rig = rigIn;
      if (!rig) {
        const { WebAudioRig } = await import('./rig');
        const wr = new WebAudioRig();
        await wr.init(session);
        rig = wr;
      }
      // Power unlocks the audio context, but the transport remains stopped
      // until Play, a clip, or a scene is launched.
      const anchor = rig.now() + 256;
      rig.sendTempo(session.bpm, get().swing, anchor);
      rig.setMasterGain(gainCurve(get().masterVol));

      // Wire acks — owner flips exactly when the audio changed.
      rig.devices.forEach((d, t) => {
        d.onClipStart = () => {
          set((st) => {
            // An immediate transport stop can overtake a clip-start message.
            // Ignore that stale ack instead of resurrecting a stopped track.
            if (!st.playing) return {};
            const owner = { ...st.owner, [t]: lastScheduled[t] };
            const queue = { ...st.queue };
            if (queue[t] === lastScheduled[t]) delete queue[t];
            return { owner, queue };
          });
          applyGains();
        };
        d.onClipStop = () => {
          set((st) => {
            const owner = { ...st.owner };
            delete owner[t];
            const queue = { ...st.queue };
            if (queue[t] === STOP) delete queue[t];
            const pos = st.pos.slice();
            pos[t] = null;
            return { owner, queue, pos };
          });
        };
        d.onPos = (step, bar) => {
          set((st) => {
            const pos = st.pos.slice();
            pos[t] = { step, bar };
            return { pos };
          });
        };
      });

      // First visit: open the guided tour right after power-on, once the
      // grid is actually interactive (the HELP button replays it anytime).
      set({ rig, anchor, powered: true, playing: false, beat: 0, bar: 1, tour: isTourSeen() ? null : 0 });
      applyGains();
    },

    toggleTransport: () => {
      const st = get();
      if (st.playing) return stopTransport();
      // Starting from a standstill with nothing on the grid launches the first
      // scene so Play makes sound instead of running an empty transport.
      const hasActive = st.session.tracks.some(
        (_, t) => st.owner[t] != null || (st.queue[t] != null && st.queue[t] !== STOP),
      );
      if (!hasActive && st.session.scenes.length > 0) return st.launchScene(0);
      return startTransport();
    },

    launch: (t, s) => {
      let st = get();
      const bytes = bytesFor(st.session, s, t);
      const clip = st.session.scenes[s]?.clips[t];
      if (!st.rig || !bytes || !clip) return;
      const rig = st.rig;
      if (!st.playing) {
        startTransport();
        st = get();
      }
      lastScheduled[t] = s;
      rig.devices[t].scheduleClip(bytes, clip.bars, boundary());
      set((cur) => ({ queue: { ...cur.queue, [t]: s } }));
    },

    stopTrack: (t) => {
      const st = get();
      if (!st.rig) return;
      if (st.owner[t] == null && st.queue[t] == null) return;
      st.rig.devices[t].scheduleStop(boundary());
      set((cur) => ({ queue: { ...cur.queue, [t]: STOP } }));
    },

    // Empty cells are stop buttons (Ableton semantics): launching a scene
    // stops uncovered tracks unless the cell is marked pass-through.
    launchScene: (s) => {
      if (!get().playing) startTransport();
      const st = get();
      const sc = st.session.scenes[s];
      sc?.clips.forEach((c, t) => {
        if (c) st.launch(t, s);
        else if (!sc.pass?.includes(t)) st.stopTrack(t);
      });
    },

    stopScene: (s) => {
      const st = get();
      st.session.tracks.forEach((_, t) => {
        if (st.owner[t] === s || st.queue[t] === s) st.stopTrack(t);
      });
    },

    stopAll: () => {
      const st = get();
      st.session.tracks.forEach((_, t) => st.stopTrack(t));
    },

    togglePassThrough: (s, t) => {
      set((st) => {
        const scenes = st.session.scenes.map((sc, i) => {
          if (i !== s) return sc;
          const pass = sc.pass?.includes(t)
            ? (sc.pass ?? []).filter((x) => x !== t)
            : [...(sc.pass ?? []), t];
          return { ...sc, pass };
        });
        return { session: { ...st.session, scenes } };
      });
      persist();
    },

    updateClipBytes: (s, t, bytes, bars) => {
      const st = get();
      const clip = st.session.scenes[s]?.clips[t];
      if (!clip) return;
      const scenes = st.session.scenes.map((sc, i) => {
        if (i !== s) return sc;
        const clips = sc.clips.slice();
        clips[t] = { ...clip, bars, pattern: bytesToB64(bytes) };
        return { ...sc, clips };
      });
      set({ session: { ...st.session, scenes } });
      clipBytes.set(`${s}:${t}`, bytes);
      persist();
      // The worklet applies clipupdate to its pending clip when one exists,
      // else the live clip — send only when the edited scene is that target.
      const q = st.queue[t];
      const target = q != null && q !== STOP ? q : st.owner[t];
      if (st.rig && target === s) {
        st.rig.devices[t].updateClip(bytes, bars);
      }
    },

    createClip: (s, t) => {
      const st = get();
      if (!st.session.scenes[s] || st.session.scenes[s].clips[t]) return;
      const bytes = emptyClipBytes(st.session.tracks[t].machine, 1);
      const scenes = st.session.scenes.map((sc, i) => {
        if (i !== s) return sc;
        const clips = sc.clips.slice();
        clips[t] = { name: 'NEW', bars: 1, pattern: bytesToB64(bytes) };
        return { ...sc, clips };
      });
      set({ session: { ...st.session, scenes } });
      clipBytes.set(`${s}:${t}`, bytes);
      persist();
    },

    deleteClip: (s, t) => {
      const st = get();
      if (!st.session.scenes[s]?.clips[t]) return;
      // Stop cleanly first so no orphaned playback rides on after the clip is
      // gone — the boundary stop schedules through the normal launcher path.
      if (st.owner[t] === s || st.queue[t] === s) st.stopTrack(t);
      const scenes = st.session.scenes.map((sc, i) => {
        if (i !== s) return sc;
        const clips = sc.clips.slice();
        clips[t] = null;
        return { ...sc, clips };
      });
      set({ session: { ...st.session, scenes } });
      clipBytes.delete(`${s}:${t}`);
      persist();
    },

    loadLibraryClip: (s, t, entry, semitones = 0) => {
      const st = get();
      let loaded;
      try { loaded = loadLibraryClipIntoSession(st.session, s, t, entry, { semitones }); }
      catch { return false; }
      const bytes = loaded.bytes;
      set({
        session: loaded.session,
        clipLoadRevision: st.clipLoadRevision + 1,
      });
      clipBytes.set(`${s}:${t}`, bytes);
      persist();
      const q = st.queue[t];
      const target = q != null && q !== STOP ? q : st.owner[t];
      if (st.rig && target === s) st.rig.devices[t].updateClip(bytes, loaded.bars);
      return true;
    },

    setTrackPatch: (t, patch) => {
      set((st) => ({
        session: {
          ...st.session,
          tracks: st.session.tracks.map((tr, i) => (i === t ? { ...tr, patch } : tr)),
        },
      }));
      persist();
    },

    loadTrackFactoryPatch: (t, index) => {
      const st = get();
      if (!st.session.tracks[t] || index < 0) return;
      const patch: PatchDoc = { kind: 'factory', index };
      st.rig?.devices[t].applyPatch(patch);
      set((current) => ({
        session: {
          ...current.session,
          tracks: current.session.tracks.map((track, i) => i === t ? { ...track, patch } : track),
        },
      }));
      persist();
    },

    toggleSceneMute: (s) => {
      set((st) => ({ sceneMute: { ...st.sceneMute, [s]: !st.sceneMute[s] } }));
      applyGains();
    },
    toggleTrackMute: (t) => {
      set((st) => ({ trackMute: { ...st.trackMute, [t]: !st.trackMute[t] } }));
      applyGains();
    },
    toggleSolo: (t) => {
      set((st) => ({ solo: { ...st.solo, [t]: !st.solo[t] } }));
      applyGains();
    },

    cycleQuant: (d) => {
      set((st) => {
        const ix = (QUANTS.indexOf(st.quant) + d + QUANTS.length) % QUANTS.length;
        return { quant: QUANTS[ix] };
      });
      persist();
    },

    setTrackVol: (t, v) => {
      set((st) => {
        const trackVol = st.trackVol.slice();
        trackVol[t] = v;
        return { trackVol };
      });
      applyGains();
    },

    setMasterVol: (v) => {
      set({ masterVol: v });
      get().rig?.setMasterGain(gainCurve(v));
    },

    // Swing is safe to change live: it only shifts intra-step offsets, never
    // the anchor math (docs §3/§6).
    setSwing: (v) => {
      set({ swing: v });
      const st = get();
      st.rig?.sendTempo(st.session.bpm, v, st.anchor);
      persist();
    },

    loadSessionPreset: (index) => {
      const preset = FACTORY_SESSION_PRESETS[index];
      if (!preset) return;
      const session = copySession(preset.session);
      const st = get();
      st.rig?.devices.forEach((device, t) => {
        device.panic();
        device.applyPatch(session.tracks[t].patch);
      });
      const anchor = st.rig ? st.rig.now() + 256 : 0;
      st.rig?.sendTempo(session.bpm, session.swing, anchor);
      clipBytes.clear();
      // A preset load replaces the whole document AND reprograms the rig
      // (patches, tempo); pre-load snapshots would undo the doc but not the
      // rig, desyncing audible vs persisted state — so history resets here.
      undoStack.length = 0;
      redoStack.length = 0;
      set({
        session, quant: session.quant, swing: session.swing,
        trackVol: session.tracks.map((track) => track.gain),
        playing: false, beat: 0, bar: 1, owner: {}, queue: {},
        pos: session.tracks.map(() => null), anchor, focus: null,
        clipLoadRevision: st.clipLoadRevision + 1,
        gridSel: null, gridDrag: null,
      });
      persist();
    },

    tick: () => {
      const st = get();
      if (!st.rig || !st.playing) return;
      const { beat, bar } = songPosition(st.rig.now(), st.anchor, st.session.bpm, st.rig.sampleRate);
      if (beat !== st.beat || bar !== st.bar) set({ beat, bar });
    },

    enterFocus: (t, s) => {
      const st = get();
      const scene = s ?? st.owner[t] ?? st.focus?.scene ?? lastFocusScene;
      set({ focus: { track: t, scene: clampScene(scene) } });
    },

    exitFocus: () => {
      const f = get().focus;
      if (f) lastFocusScene = f.scene;
      set({ focus: null });
    },

    focusScene: (s) => {
      const f = get().focus;
      if (f) set({ focus: { ...f, scene: clampScene(s) } });
    },

    // The tour anchors to session-grid elements, so it always exits focus
    // mode first; stepping past the last card ends it and marks it seen.
    startTour: () => {
      get().exitFocus();
      set({ tour: 0 });
    },
    advanceTour: (d) => {
      const cur = get().tour;
      if (cur == null) return;
      const next = nextTourStep(cur, d);
      if (next == null) return get().endTour();
      set({ tour: next });
    },
    endTour: () => {
      markTourSeen();
      set({ tour: null });
    },

    setGridSelection: (anchor, head) => {
      const a = clampPos(anchor);
      set({ gridSel: { anchor: a, head: head ? clampPos(head) : a } });
    },

    clearGridSelection: () => set({ gridSel: null }),

    copySelection: () => {
      const st = get();
      if (!st.gridSel) return;
      set({ gridClipboard: copyRect(st.session, selRect(st.gridSel)) });
    },

    cutSelection: () => {
      const st = get();
      if (!st.gridSel) return;
      const rect = selRect(st.gridSel);
      set({ gridClipboard: copyRect(st.session, rect) });
      const writes: CellWrite[] = [];
      for (let s = rect.s0; s <= rect.s1; s++) {
        for (let t = rect.t0; t <= rect.t1; t++) {
          if (st.session.scenes[s]?.clips[t]) writes.push({ s, t, clip: null });
        }
      }
      applyGridWrites(writes, 0);
    },

    pasteAt: (s, t) => {
      const st = get();
      if (!st.gridClipboard) return;
      const plan = pasteWrites(st.session, st.gridClipboard, { s, t });
      applyGridWrites(plan.writes, plan.skipped);
    },

    // Ableton-style duplicate: the selection pasted one scene below its
    // bottom edge on the same tracks; rows past the last scene are clamped
    // away (no scene creation in v1).
    duplicateSelection: () => {
      const st = get();
      if (!st.gridSel) return;
      const rect = selRect(st.gridSel);
      const plan = pasteWrites(st.session, copyRect(st.session, rect), { s: rect.s1 + 1, t: rect.t0 });
      applyGridWrites(plan.writes, plan.skipped);
    },

    deleteSelection: () => {
      const st = get();
      if (!st.gridSel) return;
      const rect = selRect(st.gridSel);
      const writes: CellWrite[] = [];
      for (let s = rect.s0; s <= rect.s1; s++) {
        for (let t = rect.t0; t <= rect.t1; t++) {
          if (st.session.scenes[s]?.clips[t]) writes.push({ s, t, clip: null });
        }
      }
      applyGridWrites(writes, 0);
    },

    // Drag-and-drop commit: moves the selected block (when the grab point is
    // inside the selection) or the single grabbed cell by the grab→drop
    // vector; `copy` keeps the sources (Alt-drag).
    moveClips: (from, to, opts = {}) => {
      const st = get();
      const rect = st.gridSel && inRect(selRect(st.gridSel), from.s, from.t)
        ? selRect(st.gridSel)
        : { s0: from.s, s1: from.s, t0: from.t, t1: from.t };
      const plan = moveWrites(st.session, rect, from, to, !!opts.copy);
      applyGridWrites(plan.writes, plan.skipped);
      if (plan.writes.length) {
        const ds = to.s - from.s;
        const dt = to.t - from.t;
        get().setGridSelection(
          { s: rect.s0 + ds, t: rect.t0 + dt },
          { s: rect.s1 + ds, t: rect.t1 + dt },
        );
      }
    },

    setGridDrag: (drag) => set({ gridDrag: drag }),

    undo: () => {
      const target = undoStack.pop();
      if (!target) return;
      redoStack.push(get().session);
      restoreSession(target);
    },

    redo: () => {
      const target = redoStack.pop();
      if (!target) return;
      undoStack.push(get().session);
      restoreSession(target);
    },
  };
});

/** Reset launcher state (used by tests). */
export function resetSeqStore(): void {
  clipBytes.clear();
  undoStack.length = 0;
  redoStack.length = 0;
  useSeqStore.setState({
    session: factorySession(),
    powered: false,
    playing: false,
    beat: 0,
    bar: 1,
    owner: {},
    queue: {},
    pos: initialSession.tracks.map(() => null),
    sceneMute: {},
    trackMute: {},
    solo: {},
    quant: '1 BAR',
    trackVol: initialSession.tracks.map((t) => t.gain),
    masterVol: 0.75,
    swing: 0,
    rig: null,
    anchor: 0,
    focus: null,
    clipLoadRevision: 0,
    tour: null,
    gridSel: null,
    gridClipboard: null,
    gridDrag: null,
  });
}
