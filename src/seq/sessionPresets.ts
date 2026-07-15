// SQ-4 factory session presets. Keep the same names, families, patch choices
// and clip-selection rule as JUCE's SeqFactory.cpp so the web and native
// instruments present one coherent factory library.

import { factorySession } from './factory';
import { bytesToB64, emptyClipBytes, noteIdx, type ClipDoc, type SessionDoc, wtNoteIdx } from './protocol';

export interface SessionPreset {
  name: string;
  family: string;
  variation: string;
  energy: number;
  tags: string[];
  session: SessionDoc;
}

type Spec = Omit<SessionPreset, 'session'> & { programs: [number, number, number, number]; variationIndex: number };

const spec = (name: string, family: string, variation: string, energy: number, tags: string[], programs: [number, number, number, number], variationIndex: number): Spec =>
  ({ name, family, variation, energy, tags, programs, variationIndex });

const specs: Spec[] = [
  ['NEON TALE', 'NEON', 'ORIGINAL', 3, ['bright', 'balanced', 'wide'], [0, 0, 3, 11], 0],
  ['NEON CHASE', 'NEON', 'CHASE', 5, ['bright', 'driving', 'wide'], [13, 2, 14, 11], 1],
  ['GLASS CIRCUIT', 'NEON', 'GLASS', 2, ['clean', 'glassy', 'sparse'], [12, 5, 6, 1], 2],
  ['AFTERGLOW', 'NEON', 'SOFT', 2, ['warm', 'soft', 'wide'], [3, 7, 19, 17], 3],
  ['WAREHOUSE RAW', 'ACID', 'RAW', 5, ['hard', 'dark', 'driving'], [13, 4, 14, 11], 0],
  ['ACID FLASH', 'ACID', 'FLASH', 4, ['acid', 'bright', 'punchy'], [3, 0, 3, 1], 1],
  ['STEEL PULSE', 'ACID', 'METAL', 4, ['metallic', 'tight', 'industrial'], [12, 2, 19, 17], 2],
  ['PEAK SIGNAL', 'ACID', 'PEAK', 5, ['distorted', 'wide', 'peak-time'], [13, 5, 6, 11], 3],
  ['DEEP FOG', 'AMBIENT', 'FOG', 1, ['dark', 'deep', 'slow'], [12, 7, 6, 17], 0],
  ['GLASS BLOOM', 'AMBIENT', 'BLOOM', 2, ['glassy', 'clean', 'lush'], [13, 0, 19, 1], 1],
  ['FROZEN BELL', 'AMBIENT', 'FROZEN', 2, ['cold', 'bell', 'sparse'], [12, 5, 6, 17], 2],
  ['AIR TEMPLE', 'AMBIENT', 'TEMPLE', 2, ['warm', 'ceremonial', 'wide'], [3, 7, 15, 1], 3],
  ['DUST HOUSE', 'HOUSE', 'DUST', 3, ['dusty', 'groovy', 'warm'], [12, 4, 14, 11], 0],
  ['MIDNIGHT FLOOR', 'HOUSE', 'NIGHT', 4, ['club', 'round', 'wide'], [13, 0, 3, 1], 1],
  ['TAPE DISCO', 'HOUSE', 'TAPE', 3, ['tape', 'soft', 'groovy'], [3, 7, 15, 17], 2],
  ['CLEAN CLUB', 'HOUSE', 'CLEAN', 4, ['clean', 'tight', 'bright'], [12, 5, 19, 1], 3],
  ['VHS GARDEN', 'LO-FI', 'VHS', 2, ['tape', 'dark', 'nostalgic'], [3, 7, 15, 17], 0],
  ['POCKET DUST', 'LO-FI', 'POCKET', 2, ['dusty', 'small', 'warm'], [12, 5, 3, 1], 1],
  ['TOY PARADE', 'LO-FI', 'TOY', 4, ['8-bit', 'playful', 'broken'], [13, 2, 15, 17], 2],
  ['WORN SIGNAL', 'LO-FI', 'WORN', 3, ['distorted', 'dark', 'unstable'], [3, 0, 19, 1], 3],
  ['CHROME CATHEDRAL', 'CINEMATIC', 'CATHEDRAL', 3, ['large', 'metallic', 'ceremonial'], [13, 7, 6, 17], 0],
  ['MACHINE TENSION', 'CINEMATIC', 'TENSION', 4, ['industrial', 'tense', 'dark'], [12, 5, 19, 11], 1],
  ['VOID MARCH', 'CINEMATIC', 'MARCH', 4, ['heavy', 'dark', 'driving'], [3, 4, 6, 17], 2],
  ['FINAL HORIZON', 'CINEMATIC', 'FINALE', 5, ['epic', 'wide', 'bright'], [13, 8, 19, 11], 3],
].map(([name, family, variation, energy, tags, programs, variationIndex]) => spec(
  name as string, family as string, variation as string, energy as number, tags as string[], programs as [number, number, number, number], variationIndex as number,
));

interface Harmony {
  roots: number[];
  minor: boolean[];
}

// Calibrated from the deterministic factory renders in scripts/measure-*.mjs.
// The fader curve is gain² × 1.4, so quieter voices need a higher fader value.
const BASS_FADERS: Record<number, number> = { 0: 0.72, 2: 0.73, 4: 0.87, 5: 0.67, 7: 0.71, 8: 0.70, 10: 0.65 };
const LEAD_FADERS: Record<number, number> = { 3: 1.0, 6: 0.62, 14: 1.0, 15: 0.95, 19: 0.45 };
const PAD_FADERS: Record<number, number> = { 1: 0.85, 6: 1.0, 11: 0.65, 17: 0.90 };

function calibratedTrackGains(programs: Spec['programs']): [number, number, number, number] {
  return [0.78, BASS_FADERS[programs[1]] ?? 0.72, LEAD_FADERS[programs[2]] ?? 0.8, PAD_FADERS[programs[3]] ?? 0.8];
}

// Four-bar harmonic plans. Each variation starts from a recognisable cadence,
// then transposes it for the preset family — a song is now one harmonic world,
// rather than four unrelated clips selected by genre.
function harmonyFor(spec: Spec): Harmony {
  // Give every family its own tonal centre. Combined with the four variation
  // plans below, this guarantees that changing session changes BL-1, lead and
  // pad clip data too, rather than only selecting a different drum clip.
  const tonic: Record<string, number> = { NEON: 0, ACID: 2, AMBIENT: 9, HOUSE: 5, 'LO-FI': 7, CINEMATIC: 4 };
  const plans = [
    { roots: [0, 8, 3, 10], minor: [true, false, false, false] }, // i–VI–III–VII
    { roots: [0, 5, 8, 7], minor: [true, true, false, false] },  // i–iv–VI–V
    { roots: [0, 3, 10, 5], minor: [true, false, false, true] }, // i–III–VII–iv
    { roots: [0, 7, 5, 10], minor: [true, false, true, false] }, // i–V–iv–VII
  ][spec.variationIndex];
  const key = tonic[spec.family] ?? 0;
  return { roots: plans.roots.map((root) => (root + key) % 12), minor: plans.minor };
}

const putNote = (bytes: Uint8Array, offset: number, absolute: number, duration = 1, accent = false) => {
  bytes[offset] = 1 | (accent ? 2 : 0) | (Math.min(63, Math.max(1, duration)) << 2);
  bytes[offset + 1] = ((absolute % 12) + 12) % 12;
  bytes[offset + 2] = Math.max(0, Math.min(2, Math.floor(absolute / 12) + 1));
};

function bassProgression(harmony: Harmony, variation: string): ClipDoc {
  const bytes = emptyClipBytes('BL1', 4);
  harmony.roots.forEach((root, bar) => {
    // One low root, then one fifth: deliberate space for the drums and pad.
    putNote(bytes, noteIdx(bar, 0), root - 12, 8, true);
    putNote(bytes, noteIdx(bar, 8), root - 5, 8);
  });
  return { name: `${variation} ROOTS · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
}

function padProgression(harmony: Harmony, variation: string): ClipDoc {
  const bytes = emptyClipBytes('WT1', 4);
  harmony.roots.forEach((root, bar) => {
    const chord = [root, root + (harmony.minor[bar] ? 3 : 4), root + 7];
    chord.forEach((note, lane) => putNote(bytes, wtNoteIdx(bar, 0, lane), note, 16));
  });
  return { name: `${variation} CHORDS · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
}

// Authored pitch-class melodies for the four progressions in harmonyFor().
// Values are semitones above the session tonic. Event 0 and event 3 in each
// bar are chord-tone anchors; the remaining notes are scale motion, pickups,
// and resolutions. 11 is the harmonic-minor leading tone over the major V.
const LEAD_LINES: number[][][] = [
  [ // i – VI – III – VII
    [7, 3, 5, 7, 10, 7], [8, 0, 10, 0, 3, 0],
    [10, 7, 5, 7, 10, 2], [5, 2, 3, 2, 10, 0],
  ],
  [ // i – iv – VI – V
    [3, 7, 10, 7, 5, 3], [5, 8, 0, 8, 7, 5],
    [3, 0, 8, 0, 3, 0], [2, 7, 11, 2, 11, 7],
  ],
  [ // i – III – VII – iv
    [0, 3, 7, 7, 10, 3], [7, 10, 2, 3, 2, 10],
    [10, 2, 5, 2, 0, 10], [8, 5, 0, 8, 7, 0],
  ],
  [ // i – V – iv – VII
    [7, 10, 0, 3, 2, 0], [7, 11, 2, 7, 5, 2],
    [5, 8, 0, 0, 3, 8], [5, 2, 10, 2, 0, 7],
  ],
];

const LEAD_RHYTHMS: Record<string, Array<[step: number, duration: number]>> = {
  NEON:     [[0, 3], [3, 3], [6, 2], [8, 3], [11, 3], [14, 2]],
  ACID:     [[0, 2], [2, 3], [5, 3], [8, 2], [10, 4], [14, 2]],
  AMBIENT:  [[0, 4], [4, 4], [8, 2], [10, 2], [12, 2], [14, 2]],
  HOUSE:    [[0, 3], [3, 3], [6, 3], [9, 2], [11, 3], [14, 2]],
  'LO-FI':  [[0, 4], [4, 3], [7, 2], [9, 3], [12, 3], [15, 1]],
  CINEMATIC: [[0, 4], [4, 3], [7, 1], [8, 4], [12, 2], [14, 2]],
};

function voicedLeadPitch(pitchClass: number, previous: number | null): number {
  const candidates = [pitchClass - 12, pitchClass, pitchClass + 12].filter((note) => note >= -12 && note <= 23);
  if (previous === null) return pitchClass + 12;
  return candidates.reduce((best, note) => {
    const score = Math.abs(note - previous) + (note < 5 ? 3 : 0);
    const bestScore = Math.abs(best - previous) + (best < 5 ? 3 : 0);
    return score < bestScore ? note : best;
  }, candidates[0]);
}

function leadProgression(harmony: Harmony, spec: Spec): ClipDoc {
  const bytes = emptyClipBytes('WT1', 4);
  const tonic = harmony.roots[0];
  const line = LEAD_LINES[spec.variationIndex];
  const rhythm = LEAD_RHYTHMS[spec.family] ?? LEAD_RHYTHMS.NEON;
  let previous: number | null = null;
  line.forEach((barNotes, bar) => {
    barNotes.forEach((relativePitch, event) => {
      const pitchClass = (tonic + relativePitch) % 12;
      const absolute = voicedLeadPitch(pitchClass, previous);
      const [step, duration] = rhythm[event];
      putNote(bytes, wtNoteIdx(bar, step, 0), absolute, duration, event === 0);
      previous = absolute;
    });
  });
  return { name: `${spec.variation} MELODY · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
}

function drumClip(spec: Spec, scene: number): ClipDoc {
  // The rhythm can vary per scene, but it is repeated to the same four-bar
  // form as the harmonic parts, keeping launches musically aligned.
  const ids: Record<string, string[]> = {
    NEON: ['dr1-distant-ticks', 'dr1-hat-rise', 'dr1-neon-drive', 'dr1-neon-drive', 'dr1-hat-rise', 'dr1-ghost-shuffle'],
    ACID: ['dr1-distant-ticks', 'dr1-hat-rise', 'dr1-neon-drive', 'dr1-jungle-sparks', 'dr1-hat-rise', 'dr1-ghost-shuffle'],
    AMBIENT: ['dr1-distant-ticks', 'dr1-distant-ticks', 'dr1-hat-rise', 'dr1-hat-rise', 'dr1-distant-ticks', 'dr1-ghost-shuffle'],
    HOUSE: ['dr1-house-pocket', 'dr1-house-pocket', 'dr1-house-pocket', 'dr1-house-pocket', 'dr1-distant-ticks', 'dr1-ghost-shuffle'],
    'LO-FI': ['dr1-lofi-break', 'dr1-lofi-break', 'dr1-lofi-break', 'dr1-lofi-break', 'dr1-distant-ticks', 'dr1-ghost-shuffle'],
    CINEMATIC: ['dr1-cinema-half', 'dr1-cinema-half', 'dr1-cinema-half', 'dr1-cinema-half', 'dr1-distant-ticks', 'dr1-ghost-shuffle'],
  };
  const source = (awaitlessFactoryClips.find((clip) => clip.id === ids[spec.family][scene]) ?? awaitlessFactoryClips[0]);
  const src = source.pattern;
  const bytes = new Uint8Array(4 * 256);
  for (let bar = 0; bar < 4; bar++) bytes.set(src.subarray((bar % source.bars) * 256, (bar % source.bars + 1) * 256), bar * 256);
  return { name: `${source.name} · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
}

// Decode once at module load; the generated library is browser-safe data.
import { FACTORY_CLIP_LIBRARY } from './clipLibrary.gen';
import { b64ToBytes } from './protocol';
const awaitlessFactoryClips = FACTORY_CLIP_LIBRARY.filter((clip) => clip.machine === 'DR1').map((clip) => ({ ...clip, pattern: b64ToBytes(clip.pattern) }));

function buildSession(spec: Spec): SessionDoc {
  const session = factorySession();
  session.name = spec.name;
  session.bpm = 96 + spec.energy * 7 + spec.variationIndex;
  session.swing = spec.family === 'HOUSE' ? 0.12 : spec.family === 'LO-FI' ? 0.18 : 0;
  const gains = calibratedTrackGains(spec.programs);
  session.tracks.forEach((track, t) => {
    track.patch = { kind: 'factory', index: spec.programs[t] };
    track.gain = gains[t];
  });
  const harmony = harmonyFor(spec);
  const bass = bassProgression(harmony, spec.variation);
  const lead = leadProgression(harmony, spec);
  const pads = padProgression(harmony, spec.variation);
  session.scenes.forEach((scene, s) => {
    const drums = drumClip(spec, s);
    // Arrange density intentionally; the three tonal parts retain the same
    // progression whenever they enter, so every scene belongs to one song.
    scene.clips = s === 0 ? [drums, null, null, pads]
      : s === 1 ? [drums, bass, null, pads]
        : s === 4 ? [null, bass, lead, pads]
          : s === 5 ? [drums, null, null, pads]
            : [drums, bass, lead, pads];
  });
  return session;
}

export const FACTORY_SESSION_PRESETS: SessionPreset[] = specs.map((spec, index) => ({
  ...spec,
  session: index === 0 ? factorySession() : buildSession(spec),
}));

/** Session data is edited in the store, so preset recall always needs a copy. */
export function copySession(session: SessionDoc): SessionDoc {
  return {
    ...session,
    tracks: session.tracks.map((track) => ({ ...track, patch: { ...track.patch } })),
    scenes: session.scenes.map((scene) => ({ ...scene, pass: scene.pass ? [...scene.pass] : undefined, clips: scene.clips.map((clip) => clip && { ...clip }) })),
  };
}
