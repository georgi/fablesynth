// Zustand store for SQ-4. All launcher state lives here; the beat clock in
// SeqApp calls onBeat() once per quarter note and queued launches resolve at
// quantize boundaries.

import { create } from 'zustand';
import {
  applyQueue, type OwnerMap, type Quant, QUANTS, type QueueMap, queueApplies,
  SCENES, STOP, TRACKS,
} from './model';

export interface SeqStore {
  playing: boolean;
  beat: number; // 0..3 within the bar
  bar: number; // 1-based, display only
  owner: OwnerMap;
  queue: QueueMap;
  sceneMute: Record<number, boolean>;
  trackMute: Record<number, boolean>;
  solo: Record<number, boolean>;
  quant: Quant;
  trackVol: number[]; // 0..1 knob positions
  masterVol: number;
  swing: number;

  onBeat: () => void;
  setClip: (t: number, v: number) => void;
  launchScene: (s: number) => void;
  stopScene: (s: number) => void;
  stopTrack: (t: number) => void;
  stopAll: () => void;
  togglePlay: () => void;
  toggleSceneMute: (s: number) => void;
  toggleTrackMute: (t: number) => void;
  toggleSolo: (t: number) => void;
  cycleQuant: (d: number) => void;
  setTrackVol: (t: number, v: number) => void;
  setMasterVol: (v: number) => void;
  setSwing: (v: number) => void;
}

// The session opens mid-set with DROP A live on every track, so the surface
// is moving the moment it loads.
const INITIAL = {
  playing: true,
  beat: 0,
  bar: 1,
  owner: { 0: 2, 1: 2, 2: 2, 3: 2 } as OwnerMap,
  queue: {} as QueueMap,
  sceneMute: {},
  trackMute: {},
  solo: {},
  quant: '1 BAR' as Quant,
  trackVol: TRACKS.map((t) => t.vol),
  masterVol: 0.66,
  swing: 0.42,
};

export const useSeqStore = create<SeqStore>((set, get) => ({
  ...INITIAL,

  onBeat: () => {
    const st = get();
    if (!st.playing) return;
    const beat = (st.beat + 1) % 4;
    const bar = st.bar + (beat === 0 ? 1 : 0);
    let { owner, queue } = st;
    if (Object.keys(queue).length && queueApplies(st.quant, beat)) {
      owner = applyQueue(owner, queue);
      queue = {};
    }
    set({ beat, bar, owner, queue });
  },

  // v is a scene index, or STOP. Launching also resumes the clock — tapping
  // a clip while paused should always make something happen.
  setClip: (t, v) => {
    const st = get();
    if (st.quant === 'OFF') {
      set({ owner: applyQueue(st.owner, { [t]: v }), playing: true });
    } else {
      set({ queue: { ...st.queue, [t]: v }, playing: true });
    }
  },

  launchScene: (s) => {
    SCENES[s].clips.forEach((c, t) => {
      if (c) get().setClip(t, s);
    });
  },

  stopScene: (s) => {
    TRACKS.forEach((_, t) => {
      if (get().owner[t] === s) get().setClip(t, STOP);
    });
  },

  stopTrack: (t) => get().setClip(t, STOP),

  stopAll: () => set({ owner: {}, queue: {} }),

  togglePlay: () => set((st) => ({ playing: !st.playing })),

  toggleSceneMute: (s) => set((st) => ({ sceneMute: { ...st.sceneMute, [s]: !st.sceneMute[s] } })),
  toggleTrackMute: (t) => set((st) => ({ trackMute: { ...st.trackMute, [t]: !st.trackMute[t] } })),
  toggleSolo: (t) => set((st) => ({ solo: { ...st.solo, [t]: !st.solo[t] } })),

  cycleQuant: (d) => set((st) => {
    const ix = (QUANTS.indexOf(st.quant) + d + QUANTS.length) % QUANTS.length;
    return { quant: QUANTS[ix] };
  }),

  setTrackVol: (t, v) => set((st) => {
    const trackVol = st.trackVol.slice();
    trackVol[t] = v;
    return { trackVol };
  }),
  setMasterVol: (v) => set({ masterVol: v }),
  setSwing: (v) => set({ swing: v }),
}));

/** Reset to the opening state (used by tests). */
export function resetSeqStore() {
  useSeqStore.setState({ ...INITIAL, owner: { ...INITIAL.owner }, queue: {}, trackVol: TRACKS.map((t) => t.vol) });
}
