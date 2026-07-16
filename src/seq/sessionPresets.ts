// SQ-4 factory session presets. Keep the same names, families, patch choices
// and clip-selection rule as JUCE's SeqFactory.cpp so the web and native
// instruments present one coherent factory library.

import { factorySession } from './factory';
import { bytesToB64, dr1Idx, emptyClipBytes, noteIdx, type ClipDoc, type SessionDoc, wtNoteIdx } from './protocol';

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

// Full-chain, in-context track faders — measured by
// juce/test/measure_track_levels.cpp, which renders every song's four tracks
// through their real engine+FX (incl. WT-1's leveling comp) and balances bass /
// lead / pad to the drum-bus RMS (drums = fixed 0.78 reference) with perceptual
// per-role offsets (bass +4 dB, pad +2 dB). The fader curve is gain² × 1.4, so
// quieter voices need a higher fader value.
const BASS_FADERS: Record<number, number> = { 0: 0.59, 2: 0.59, 4: 0.61, 5: 0.52, 7: 0.55, 8: 0.53 };
const LEAD_FADERS: Record<number, number> = { 3: 0.77, 6: 0.50, 14: 0.99, 15: 0.66, 19: 0.60 };
const PAD_FADERS: Record<number, number> = { 1: 0.60, 11: 0.54, 17: 0.64 };

function calibratedTrackGains(programs: Spec['programs']): [number, number, number, number] {
  return [0.78, BASS_FADERS[programs[1]] ?? 0.56, LEAD_FADERS[programs[2]] ?? 0.65, PAD_FADERS[programs[3]] ?? 0.59];
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
    // Close-voice the triad as pitch classes inside the root octave (+0..+11):
    // the pad bed stays strictly below the +12..+23 lead band for every root.
    const chord = [root, root + (harmony.minor[bar] ? 3 : 4), root + 7];
    chord.forEach((note, lane) => putNote(bytes, wtNoteIdx(bar, 0, lane), ((note % 12) + 12) % 12, 16));
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

function leadProgression(harmony: Harmony, spec: Spec): ClipDoc {
  const bytes = emptyClipBytes('WT1', 4);
  const tonic = harmony.roots[0];
  const line = LEAD_LINES[spec.variationIndex];
  const rhythm = LEAD_RHYTHMS[spec.family] ?? LEAD_RHYTHMS.NEON;
  line.forEach((barNotes, bar) => {
    barNotes.forEach((relativePitch, event) => {
      // Voice the melody strictly one octave above the pad bed (+12..+23):
      // the authored lines carry the contour, so no octave search is needed.
      const pitchClass = (tonic + relativePitch) % 12;
      const [step, duration] = rhythm[event];
      putNote(bytes, wtNoteIdx(bar, step, 0), pitchClass + 12, duration, event === 0);
    });
  });
  return { name: `${spec.variation} MELODY · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
}

// ---------- procedural drums ----------
// Every song gets its own kit patterns: a per-family groove archetype,
// mutated deterministically by variation and energy, then masked per scene
// role. No preset references a shared library clip.

const DRUM = { KICK: 0, SNARE: 2, CLAP: 3, RIM: 4, CH: 5, OH: 6, TOM_LO: 8, TOM_MID: 9, TOM_HI: 10, PERC_A: 12, PERC_B: 13 } as const;

interface DrumVoice { pad: number; steps: number[]; accents: number[] }
interface DrumArchetype { kick: DrumVoice; back: DrumVoice; hat: DrumVoice; open: DrumVoice; perc: DrumVoice[] }

const DRUM_ARCHETYPES: Record<string, DrumArchetype> = {
  NEON: { // driving synthwave: four-on-floor, clap backbeat, offbeat hats
    kick: { pad: DRUM.KICK, steps: [0, 4, 8, 12], accents: [0] },
    back: { pad: DRUM.CLAP, steps: [4, 12], accents: [] },
    hat: { pad: DRUM.CH, steps: [2, 6, 10, 14], accents: [2, 10] },
    open: { pad: DRUM.OH, steps: [2, 10], accents: [] },
    perc: [{ pad: DRUM.PERC_A, steps: [15], accents: [] }],
  },
  ACID: { // warehouse: relentless floor + ghost kick, rolling 16th hats
    kick: { pad: DRUM.KICK, steps: [0, 4, 8, 12, 14], accents: [0] },
    back: { pad: DRUM.SNARE, steps: [4, 12], accents: [4, 12] },
    hat: { pad: DRUM.CH, steps: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15], accents: [2, 6, 10, 14] },
    open: { pad: DRUM.OH, steps: [7], accents: [] },
    perc: [],
  },
  AMBIENT: { // deep: minimal kick, soft rim, airy hats, lots of space
    kick: { pad: DRUM.KICK, steps: [0, 8], accents: [] },
    back: { pad: DRUM.RIM, steps: [8], accents: [] },
    hat: { pad: DRUM.CH, steps: [4, 12], accents: [] },
    open: { pad: DRUM.OH, steps: [], accents: [] },
    perc: [{ pad: DRUM.PERC_A, steps: [2], accents: [] }],
  },
  HOUSE: { // club: swung floor (global swing 0.12), open-hat offbeat "tss"
    kick: { pad: DRUM.KICK, steps: [0, 4, 8, 12], accents: [] },
    back: { pad: DRUM.CLAP, steps: [4, 12], accents: [] },
    hat: { pad: DRUM.CH, steps: [2, 6, 10, 14], accents: [] },
    open: { pad: DRUM.OH, steps: [2, 6, 10, 14], accents: [6, 14] },
    perc: [],
  },
  'LO-FI': { // dusty boom-bap: broken kick, laid-back snare, swung hats
    kick: { pad: DRUM.KICK, steps: [0, 7, 10], accents: [0] },
    back: { pad: DRUM.SNARE, steps: [4, 12], accents: [] },
    hat: { pad: DRUM.CH, steps: [0, 3, 6, 8, 11, 14], accents: [] },
    open: { pad: DRUM.OH, steps: [14], accents: [] },
    perc: [{ pad: DRUM.PERC_B, steps: [6], accents: [] }],
  },
  CINEMATIC: { // epic half-time: sparse kick, big snare on 3, tom colour
    kick: { pad: DRUM.KICK, steps: [0, 10], accents: [0] },
    back: { pad: DRUM.SNARE, steps: [8], accents: [8] },
    hat: { pad: DRUM.CH, steps: [], accents: [] },
    open: { pad: DRUM.OH, steps: [], accents: [] },
    perc: [{ pad: DRUM.TOM_LO, steps: [13], accents: [] }],
  },
};

// Family-flavoured bar-4 fills: [pad, step, accent].
const DRUM_FILLS: Record<string, Array<[number, number, boolean]>> = {
  NEON: [[DRUM.TOM_HI, 10, false], [DRUM.TOM_MID, 12, false], [DRUM.TOM_LO, 14, true]],
  ACID: [[DRUM.SNARE, 13, false], [DRUM.SNARE, 14, false], [DRUM.SNARE, 15, true]],
  AMBIENT: [[DRUM.OH, 12, false], [DRUM.PERC_A, 14, false]],
  HOUSE: [[DRUM.CLAP, 13, false], [DRUM.CLAP, 15, true]],
  'LO-FI': [[DRUM.PERC_B, 13, false], [DRUM.PERC_B, 15, false]],
  CINEMATIC: [[DRUM.TOM_HI, 8, false], [DRUM.TOM_MID, 10, false], [DRUM.TOM_LO, 12, true], [DRUM.TOM_LO, 14, true]],
};

// One ghost hit per variation (index 0 adds none): [pad, step].
const DRUM_GHOSTS: Array<[number, number] | null> = [null, [DRUM.KICK, 14], [DRUM.SNARE, 11], [DRUM.PERC_B, 3]];

function drumProgression(spec: Spec, scene: number): ClipDoc {
  const family = DRUM_ARCHETYPES[spec.family] ?? DRUM_ARCHETYPES.NEON!;
  const bytes = new Uint8Array(4 * 256);
  const hit = (bar: number, pad: number, step: number, accent = false) => { bytes[dr1Idx(bar, pad, step)] = accent ? 2 : 1; };
  const intro = scene === 0, build = scene === 1, outro = scene === 5;

  // Energy scales hat density; variation (plus one extra for DROP B) rotates
  // which hat steps carry the accents, so sibling songs land differently.
  const hatSteps = spec.energy >= 4 ? Array.from({ length: 16 }, (_, s) => s)
    : spec.energy <= 2 ? family.hat.steps.filter((_, i) => i % 2 === 0)
      : family.hat.steps;
  const shift = spec.variationIndex + (scene === 3 ? 1 : 0);
  const hatAccents = family.hat.accents.map((_, k) => (hatSteps.length ? hatSteps[(k * 2 + shift) % hatSteps.length]! : -1));
  const ghost = DRUM_GHOSTS[spec.variationIndex] ?? null;

  for (let bar = 0; bar < 4; bar++) {
    const lastBar = bar === 3;
    const kickSteps = intro || outro ? family.kick.steps.filter((step) => step % 8 === 0) : family.kick.steps;
    for (const step of kickSteps) hit(bar, family.kick.pad, step, family.kick.accents.includes(step));
    if (!intro && !outro) {
      for (const step of family.back.steps) hit(bar, family.back.pad, step, family.back.accents.includes(step));
      if (!build && ghost) hit(bar, ghost[0], ghost[1]);
    }
    if (!outro) {
      // Intros thin the hats and stagger the picks by variation, so each
      // song opens with its own sparse figure instead of a shared "ticks" loop.
      const steps = intro ? family.hat.steps.filter((_, i) => (i + spec.variationIndex) % 2 === 0) : hatSteps;
      for (const step of steps) hit(bar, family.hat.pad, step, !intro && hatAccents.includes(step));
      if (!intro) for (const step of family.open.steps) hit(bar, family.open.pad, step, family.open.accents.includes(step));
    }
    if (!intro) for (const voice of family.perc) for (const step of voice.steps) hit(bar, voice.pad, step, voice.accents.includes(step));
    if ((intro || outro) && lastBar && ghost && spec.variationIndex >= 2) hit(bar, ghost[0], ghost[1]);
    if (build && lastBar) for (const step of [12, 13, 14, 15]) hit(bar, DRUM.PERC_A, step, step === 15);
    if (!intro && !build && !outro && (lastBar || (scene === 3 && bar === 1))) {
      for (const [pad, step, accent] of DRUM_FILLS[spec.family] ?? []) hit(bar, pad, step, accent);
    }
  }
  const role = intro ? 'INTRO' : build ? 'BUILD' : outro ? 'TAIL' : scene === 3 ? 'DRIVE II' : 'DRIVE';
  return { name: `${spec.variation} ${role} · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
}

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
    const drums = drumProgression(spec, s);
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
