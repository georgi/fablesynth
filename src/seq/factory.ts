// The factory session: the SQ-4 mock's six scenes as real, playable clips.
// Patterns are handcrafted in each machine's packed clip layout (protocol.ts).
// DR-1 pad map (TR-VOID kit): 0 KICK · 2 SNARE · 3 CLAP · 4 RIM · 5 CH HAT ·
// 6 OH HAT · 8..10 TOMS · 12/13 PERC.

import {
  bytesPerBar, bytesToB64, type ClipDoc, dr1Idx, noteIdx, type SessionDoc,
} from './protocol';

// ---------- pattern builders ----------

type DrumHits = Array<[pad: number, steps: number[], accents?: number[]]>;

/** One entry per bar; each bar is a list of [pad, steps, accents]. */
function drumClip(name: string, barsSpec: DrumHits[]): ClipDoc {
  const bars = barsSpec.length;
  const data = new Uint8Array(bars * bytesPerBar('DR1'));
  barsSpec.forEach((bar, b) => {
    for (const [pad, steps, accents = []] of bar) {
      for (const s of steps) data[dr1Idx(b, pad, s)] = accents.includes(s) ? 2 : 1;
    }
  });
  return { name, bars, pattern: bytesToB64(data) };
}

interface NoteStep {
  s: number; // absolute step (0 .. bars*16-1)
  n: number; // lane 0..11
  o?: number; // octave -1/0/1
  a?: boolean; // accent
  t?: boolean; // tie (WT-1) / slide (BL-1) — same bit
}

function noteClip(name: string, bars: number, steps: NoteStep[]): ClipDoc {
  const data = new Uint8Array(bars * bytesPerBar('WT1'));
  // oct byte defaults to 1 (= oct 0) so rests read back neutral
  for (let i = 2; i < data.length; i += 3) data[i] = 1;
  for (const st of steps) {
    const o = noteIdx(Math.floor(st.s / 16), st.s % 16);
    data[o] = 1 | (st.a ? 2 : 0) | (st.t ? 4 : 0);
    data[o + 1] = st.n;
    data[o + 2] = (st.o ?? 0) + 1;
  }
  return { name, bars, pattern: bytesToB64(data) };
}

/**
 * A held swell: one attack, then a tie on EVERY following step of the span —
 * a tie only sustains when the immediately next step ties in, so gaps in the
 * tie chain would gate the note off (and slow-attack pads would never open).
 */
function held(s0: number, span: number, n: number, o = 0): NoteStep[] {
  const out: NoteStep[] = [{ s: s0, n, o }];
  for (let s = s0 + 1; s < s0 + span; s++) out.push({ s, n, o, t: true });
  return out;
}

// ---------- drum clips ----------

const KICK = 0, SNARE = 2, CLAP = 3, RIM = 4, CH = 5, OH = 6, TOM_LO = 8, TOM_HI = 10, PERC = 12;

const four = [0, 4, 8, 12];
const off8 = [2, 6, 10, 14];
const all16 = Array.from({ length: 16 }, (_, i) => i);

const SPARSE_KICK = drumClip('SPARSE KICK', [
  [[KICK, [0, 8], [0]], [RIM, [10]]],
]);

const HAT_RISE = drumClip('HAT RISE', [
  [[KICK, four, [0]], [CH, [0, 4, 8, 12]], [PERC, [14]]],
  [[KICK, four, [0]], [CH, all16, [4, 12]], [OH, [14]]],
]);

const FULL_KIT_A = drumClip('FULL KIT A', [
  [[KICK, four, [0]], [SNARE, [4, 12]], [CH, off8], [OH, [10]], [CLAP, [12]]],
  [[KICK, four, [0]], [SNARE, [4, 12], [12]], [CH, off8], [OH, [6, 14]], [PERC, [7, 15]]],
]);

const FULL_KIT_B = drumClip('FULL KIT B', [
  [[KICK, [0, 4, 7, 8, 12], [0]], [SNARE, [4, 12]], [CH, all16, [2, 6, 10, 14]], [CLAP, [12]]],
  [[KICK, [0, 4, 8, 10, 12], [0]], [SNARE, [4, 12, 15], [15]], [CH, all16, [2, 6, 10, 14]], [TOM_HI, [13]], [TOM_LO, [14]]],
]);

const TAIL_KICK = drumClip('TAIL KICK', [
  [[KICK, [0, 8]], [OH, [8]], [PERC, [12]]],
]);

// ---------- bass clips (BL-1 lanes: 0 = its C, slide bit glides) ----------

const ACID_CRAWL = noteClip('ACID CRAWL', 2, [
  { s: 0, n: 0 }, { s: 3, n: 0, t: true }, { s: 8, n: 3 }, { s: 11, n: 0 },
  { s: 16, n: 0 }, { s: 19, n: 10, o: -1 }, { s: 24, n: 5 }, { s: 27, n: 3, t: true },
]);

const ACID_303 = noteClip('ACID 303', 1, [
  { s: 0, n: 0, a: true }, { s: 2, n: 0 }, { s: 3, n: 0, o: 1, t: true }, { s: 4, n: 0 },
  { s: 6, n: 3, a: true }, { s: 7, n: 5, t: true }, { s: 8, n: 0 }, { s: 10, n: 10, o: -1 },
  { s: 11, n: 0, t: true }, { s: 12, n: 7, a: true }, { s: 14, n: 5 }, { s: 15, n: 3, t: true },
]);

const ACID_SHIFT = noteClip('ACID SHIFT', 1, [
  { s: 0, n: 3, a: true }, { s: 2, n: 3 }, { s: 4, n: 10, o: -1 }, { s: 5, n: 3, t: true },
  { s: 7, n: 7 }, { s: 8, n: 3, a: true }, { s: 10, n: 5, t: true }, { s: 12, n: 0 },
  { s: 13, n: 0, o: 1, t: true }, { s: 15, n: 10, o: -1 },
]);

const SUB_HOLD = noteClip('SUB HOLD', 4, [
  ...held(0, 16, 0, -1),
  ...held(16, 16, 10, -1),
  ...held(32, 16, 3, -1),
  ...held(48, 12, 5, -1), ...held(60, 4, 7, -1),
]);

// ---------- lead / pad clips (WT-1, lanes relative to seq.root C3) ----------

const GLASS_HOOK = noteClip('GLASS HOOK', 2, [
  { s: 0, n: 0, o: 1, a: true }, { s: 3, n: 10 }, { s: 6, n: 7 }, { s: 8, n: 3, o: 1 },
  { s: 10, n: 10, t: true }, { s: 14, n: 5 },
  { s: 16, n: 0, o: 1, a: true }, { s: 19, n: 10 }, { s: 22, n: 7 }, { s: 24, n: 2, o: 1 },
  { s: 26, n: 0, o: 1, t: true }, { s: 30, n: 7, t: true },
]);

const GLASS_HOOK_II = noteClip('GLASS HOOK II', 2, [
  { s: 0, n: 3, o: 1, a: true }, { s: 3, n: 0, o: 1 }, { s: 6, n: 10 }, { s: 8, n: 5, o: 1 },
  { s: 10, n: 3, o: 1, t: true }, { s: 14, n: 7 },
  { s: 16, n: 3, o: 1, a: true }, { s: 19, n: 2, o: 1 }, { s: 22, n: 0, o: 1 }, { s: 24, n: 10 },
  { s: 26, n: 7, t: true }, { s: 30, n: 10, t: true },
]);

const GLASS_SOLO = noteClip('GLASS SOLO', 4, [
  { s: 0, n: 0, o: 1 }, { s: 4, n: 10, t: true }, { s: 8, n: 7, t: true }, { s: 12, n: 10, t: true },
  { s: 16, n: 3, o: 1 }, { s: 20, n: 2, o: 1, t: true }, { s: 24, n: 0, o: 1, t: true }, { s: 28, n: 10, t: true },
  { s: 32, n: 5, o: 1 }, { s: 36, n: 3, o: 1, t: true }, { s: 40, n: 2, o: 1, t: true }, { s: 44, n: 0, o: 1, t: true },
  { s: 48, n: 10, a: true }, { s: 52, n: 7, t: true }, { s: 56, n: 3, t: true }, { s: 60, n: 0, t: true },
]);

const AIR_BED = noteClip('AIR BED', 4, [
  ...held(0, 16, 0),
  ...held(16, 16, 10, -1),
  ...held(32, 16, 8, -1),
  ...held(48, 16, 3),
]);

const AIR_BED_II = noteClip('AIR BED II', 4, [
  ...held(0, 16, 3),
  ...held(16, 16, 2),
  ...held(32, 16, 0),
  ...held(48, 16, 10, -1),
]);

const FOG_STABS = noteClip('FOG STABS', 2, [
  { s: 0, n: 0 }, { s: 2, n: 0, t: true }, { s: 7, n: 3 }, { s: 10, n: 5, a: true }, { s: 12, n: 5, t: true },
  { s: 16, n: 10, o: -1 }, { s: 18, n: 10, o: -1, t: true }, { s: 23, n: 0 }, { s: 26, n: 2 }, { s: 28, n: 3, t: true },
]);

const FOG_SWELL = noteClip('FOG SWELL', 8, [
  ...held(0, 32, 0),
  ...held(32, 32, 10, -1),
  ...held(64, 32, 5),
  ...held(96, 32, 3),
]);

const AIR_OUT = noteClip('AIR OUT', 8, [
  ...held(0, 32, 0),
  ...held(32, 32, 10, -1),
  ...held(64, 64, 0, -1),
]);

// ---------- the session ----------

export function factorySession(): SessionDoc {
  return {
    v: 1,
    name: 'NEON TALE',
    bpm: 122,
    swing: 0,
    quant: '1 BAR',
    tracks: [
      { machine: 'DR1', name: 'DRUMS', color: '#4de8ff', gain: 0.8, patch: { kind: 'factory', index: 0 } }, // TR-VOID
      { machine: 'BL1', name: 'BASS', color: '#4dff9e', gain: 0.75, patch: { kind: 'factory', index: 0 } }, // ACID LINE
      { machine: 'WT1', name: 'LEAD', color: '#ffa14d', gain: 0.85, patch: { kind: 'factory', index: 3 } }, // CRYSTAL PLUCK
      { machine: 'WT1', name: 'PADS', color: '#b18cff', gain: 1, patch: { kind: 'factory', index: 11 } }, // FUTURE CHORD
    ],
    scenes: [
      { name: 'INTRO', clips: [SPARSE_KICK, null, null, AIR_BED] },
      { name: 'BUILD', clips: [HAT_RISE, ACID_CRAWL, null, AIR_BED_II] },
      { name: 'DROP A', clips: [FULL_KIT_A, ACID_303, GLASS_HOOK, FOG_STABS] },
      { name: 'DROP B', clips: [FULL_KIT_B, ACID_SHIFT, GLASS_HOOK_II, FOG_STABS] },
      { name: 'BREAK', clips: [null, SUB_HOLD, GLASS_SOLO, FOG_SWELL] },
      { name: 'OUTRO', clips: [TAIL_KICK, null, null, AIR_OUT] },
    ],
  };
}
