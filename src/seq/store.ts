// The SQ-4 conductor (docs/sq4-clips.md §9). Owns all musical decisions:
// the owner/queue launcher state, quantize boundary scheduling against the
// shared context-frame timebase, and the track gain (fader × mute × solo)
// math. Devices execute stamped commands and report back; owner flips on
// their clipstart/clipstop acks so the grid shows what is audible.

import { create } from 'zustand';
import { isTrackOpen, type OwnerMap, type Quant, QUANTS, type QueueMap, STOP } from './model';
import {
  b64ToBytes, boundaryFrame, loadSession, saveSession, type SessionDoc, songPosition,
} from './protocol';
import { factorySession } from './factory';
import type { SeqRig } from './rig';

export interface TrackPos {
  step: number;
  bar: number;
}

export interface SeqStore {
  session: SessionDoc;
  powered: boolean;
  playing: boolean; // context running (pause = ctx.suspend)
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

  powerOn: (rig?: SeqRig) => Promise<void>;
  togglePlay: () => void;
  launch: (t: number, s: number) => void;
  stopTrack: (t: number) => void;
  launchScene: (s: number) => void;
  stopScene: (s: number) => void;
  stopAll: () => void;
  toggleSceneMute: (s: number) => void;
  toggleTrackMute: (t: number) => void;
  toggleSolo: (t: number) => void;
  cycleQuant: (d: number) => void;
  setTrackVol: (t: number, v: number) => void;
  setMasterVol: (v: number) => void;
  setSwing: (v: number) => void;
  tick: () => void; // UI clock — called from a rAF loop while powered
}

const initialSession = loadSession(factorySession());

// Decoded clip pattern bytes, keyed `${scene}:${track}` (session docs are
// immutable in v1, so decode once).
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

const gainCurve = (v: number) => v * v * 1.4;

export const useSeqStore = create<SeqStore>((set, get) => {
  // The scene most recently scheduled per track — the clipstart ack promotes
  // it to owner (acks don't carry a clip identity; devices hold one pending
  // slot, so the last schedule is by construction the one that started).
  const lastScheduled: Record<number, number> = {};

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

  const resumeIfPaused = () => {
    const st = get();
    if (st.rig && !st.playing) {
      void st.rig.resume();
      set({ playing: true });
    }
  };

  const persist = () => {
    const st = get();
    saveSession({ ...st.session, quant: st.quant, swing: st.swing });
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

    powerOn: async (rigIn?: SeqRig) => {
      const session = get().session;
      let rig = rigIn;
      if (!rig) {
        const { WebAudioRig } = await import('./rig');
        const wr = new WebAudioRig();
        await wr.init(session);
        rig = wr;
      }
      // Transport starts at power-on: anchor one block ahead, tempo to all
      // devices. Silence until the first clip launches, but the grid is live.
      const anchor = rig.now() + 256;
      rig.sendTempo(session.bpm, get().swing, anchor);
      rig.setMasterGain(gainCurve(get().masterVol));

      // Wire acks — owner flips exactly when the audio changed.
      rig.devices.forEach((d, t) => {
        d.onClipStart = () => {
          set((st) => {
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

      set({ rig, anchor, powered: true, playing: true });
      applyGains();
    },

    togglePlay: () => {
      const st = get();
      if (!st.rig) return;
      if (st.playing) void st.rig.suspend();
      else void st.rig.resume();
      set({ playing: !st.playing });
    },

    launch: (t, s) => {
      const st = get();
      const bytes = bytesFor(st.session, s, t);
      const clip = st.session.scenes[s]?.clips[t];
      if (!st.rig || !bytes || !clip) return;
      resumeIfPaused();
      lastScheduled[t] = s;
      st.rig.devices[t].scheduleClip(bytes, clip.bars, boundary());
      set((cur) => ({ queue: { ...cur.queue, [t]: s } }));
    },

    stopTrack: (t) => {
      const st = get();
      if (!st.rig) return;
      if (st.owner[t] == null && st.queue[t] == null) return;
      st.rig.devices[t].scheduleStop(boundary());
      set((cur) => ({ queue: { ...cur.queue, [t]: STOP } }));
    },

    launchScene: (s) => {
      const st = get();
      st.session.scenes[s]?.clips.forEach((c, t) => {
        if (c) st.launch(t, s);
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

    tick: () => {
      const st = get();
      if (!st.rig || !st.playing) return;
      const { beat, bar } = songPosition(st.rig.now(), st.anchor, st.session.bpm, st.rig.sampleRate);
      if (beat !== st.beat || bar !== st.bar) set({ beat, bar });
    },
  };
});

/** Reset launcher state (used by tests). */
export function resetSeqStore(): void {
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
  });
}
