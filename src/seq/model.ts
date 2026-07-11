// FableSeq SQ-4 — session data model and the pure clip-launcher rules.
// A scene is a row of clips (one slot per track). Launching a clip makes its
// scene the *owner* of that track; scenes layer freely and the latest launch
// wins each track. Launches quantize to the clock: they sit in a queue until
// the next quantize boundary, then flip ownership atomically.

export interface TrackDef {
  name: string;
  machine: string; // which FableSynth instrument the track drives
  color: string;
  patch: string;
  vol: number; // 0..1 knob position
}

export interface ClipDef {
  n: string; // clip name
  b: number; // length in bars
}

export interface SceneDef {
  name: string;
  clips: (ClipDef | null)[]; // one slot per track
}

export const BPM = 122;
export const BEAT_MS = 60000 / BPM;
export const BAR_SEC = 240 / BPM;

export const TRACKS: TrackDef[] = [
  { name: 'DRUMS', machine: 'DR-1', color: '#4de8ff', patch: 'NEON KIT', vol: 0.64 },
  { name: 'BASS', machine: 'BL-1', color: '#4dff9e', patch: 'ACID TALE', vol: 0.54 },
  { name: 'LEAD', machine: 'WT-1', color: '#ffa14d', patch: 'GLASS PRIME', vol: 0.69 },
  { name: 'PADS', machine: 'WT-1', color: '#b18cff', patch: 'FOG CHOIR', vol: 0.43 },
];

export const SCENES: SceneDef[] = [
  { name: 'INTRO', clips: [{ n: 'SPARSE KICK', b: 1 }, null, null, { n: 'AIR BED', b: 4 }] },
  { name: 'BUILD', clips: [{ n: 'HAT RISE', b: 2 }, { n: 'ACID CRAWL', b: 2 }, null, { n: 'AIR BED II', b: 4 }] },
  { name: 'DROP A', clips: [{ n: 'FULL KIT A', b: 2 }, { n: 'ACID 303', b: 1 }, { n: 'GLASS HOOK', b: 2 }, { n: 'FOG STABS', b: 2 }] },
  { name: 'DROP B', clips: [{ n: 'FULL KIT B', b: 2 }, { n: 'ACID SHIFT', b: 1 }, { n: 'GLASS HOOK II', b: 2 }, { n: 'FOG STABS', b: 2 }] },
  { name: 'BREAK', clips: [null, { n: 'SUB HOLD', b: 4 }, { n: 'GLASS SOLO', b: 4 }, { n: 'FOG SWELL', b: 8 }] },
  { name: 'OUTRO', clips: [{ n: 'TAIL KICK', b: 1 }, null, null, { n: 'AIR OUT', b: 8 }] },
];

export const QUANTS = ['1 BAR', '1/4', 'OFF'] as const;
export type Quant = (typeof QUANTS)[number];

/** Queue value meaning "stop the track" (instead of a scene index). */
export const STOP = -1;

/** owner[track] = scene index currently playing on that track. */
export type OwnerMap = Record<number, number>;
/** queue[track] = scene index (or STOP) waiting for the next quantize boundary. */
export type QueueMap = Record<number, number>;

/** Resolve a launch queue into a new ownership map. */
export function applyQueue(owner: OwnerMap, queue: QueueMap): OwnerMap {
  const out: OwnerMap = { ...owner };
  for (const t of Object.keys(queue)) {
    const v = queue[Number(t)];
    if (v === STOP) delete out[Number(t)];
    else out[Number(t)] = v;
  }
  return out;
}

/** Does the queue fire on this beat (the beat we just advanced onto)? */
export function queueApplies(quant: Quant, beat: number): boolean {
  return quant === '1/4' || (quant === '1 BAR' && beat === 0);
}

/**
 * Is a track audible right now? Muted by its own mute button, by another
 * track's solo, or by a scene-mute on the scene that owns it.
 */
export function isTrackAudible(
  t: number,
  owner: OwnerMap,
  trackMute: Record<number, boolean>,
  sceneMute: Record<number, boolean>,
  solo: Record<number, boolean>,
): boolean {
  const o = owner[t];
  if (o == null) return false;
  if (trackMute[t]) return false;
  const anySolo = Object.values(solo).some(Boolean);
  if (anySolo && !solo[t]) return false;
  return !sceneMute[o];
}

/** Deterministic PRNG so every clip always shows the same step pattern. */
export function seededRand(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export interface StepBar {
  h: number; // bar height in px (of a 20px lane)
  on: boolean;
}

const stepCache = new Map<string, StepBar[]>();

/** 16-step preview pattern for the clip at (track, scene). */
export function stepsFor(t: number, s: number): StepBar[] {
  const key = `${t}_${s}`;
  const hit = stepCache.get(key);
  if (hit) return hit;
  const r = seededRand(t * 131 + s * 17 + 9);
  const out: StepBar[] = [];
  for (let i = 0; i < 16; i++) {
    let on: boolean, h: number;
    if (t === 0) {
      on = i % 4 === 0 || r() < 0.4;
      h = on ? 7 + Math.round(r() * 12) : 3;
    } else if (t === 1) {
      on = r() < 0.62;
      h = on ? 5 + Math.round(r() * 14) : 3;
    } else {
      const base = 0.5 + 0.45 * Math.sin(i * 0.72 + s * 1.3 + t);
      on = r() < 0.8;
      h = on ? 4 + Math.round(base * 15) : 3;
    }
    out.push({ h, on });
  }
  stepCache.set(key, out);
  return out;
}

export const pad2 = (n: number) => String(n).padStart(2, '0');
