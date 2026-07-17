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

// WT-1 program picks (slots 2..3) draw on the expanded factory bank: acoustic
// & classic keys (20-30), engine-showcase sound design (31-40) and the
// classic-synth homages (41-50). Leads must stay "clean" voices (no fx.drive /
// filter.drive) per the patch contract test — except songs listed in the
// test's DRIVEN_LEADS set, where the distortion is the point.
const specs: Spec[] = [
  ['NEON TALE', 'NEON', 'ORIGINAL', 3, ['bright', 'balanced', 'wide'], [13, 0, 3, 11], 0], // CRYSTAL PLUCK / FUTURE CHORD
  ['NEON CHASE', 'NEON', 'CHASE', 5, ['bright', 'driving', 'wide'], [13, 2, 3, 40], 1], // CRYSTAL PLUCK / PUMP PAD
  ['GLASS CIRCUIT', 'NEON', 'GLASS', 2, ['clean', 'glassy', 'sparse'], [12, 9, 45, 35], 2], // FANTA BELLS / TWIN SKY
  ['AFTERGLOW', 'NEON', 'SOFT', 2, ['warm', 'soft', 'wide'], [3, 5, 21, 24], 3], // DYNO EPIANO / ANALOG STRINGS
  ['WAREHOUSE RAW', 'ACID', 'RAW', 5, ['hard', 'dark', 'driving'], [13, 4, 33, 40], 0], // DATA STREAM / PUMP PAD
  ['ACID FLASH', 'ACID', 'FLASH', 4, ['acid', 'bright', 'punchy'], [3, 0, 49, 1], 1], // LATELY BASS / VELVET PAD
  ['STEEL PULSE', 'ACID', 'METAL', 4, ['metallic', 'tight', 'industrial'], [12, 2, 29, 17], 2], // KALIMBA PLUCK / DARK DRONE
  ['PEAK SIGNAL', 'ACID', 'PEAK', 5, ['distorted', 'wide', 'peak-time'], [13, 5, 6, 40], 3], // NEURO WOBBLE / PUMP PAD
  ['DEEP FOG', 'AMBIENT', 'FOG', 1, ['dark', 'deep', 'slow'], [15, 7, 51, 34], 0], // FOG LIGHT / OCEAN AIR
  ['GLASS BLOOM', 'AMBIENT', 'BLOOM', 2, ['glassy', 'clean', 'lush'], [15, 0, 52, 25], 1], // GLASS RIBBON / CINEMA STRINGS
  ['FROZEN BELL', 'AMBIENT', 'FROZEN', 2, ['cold', 'bell', 'sparse'], [15, 5, 53, 32], 2], // NORTH WIRE / GHOST CHOIR
  ['AIR TEMPLE', 'AMBIENT', 'TEMPLE', 2, ['warm', 'ceremonial', 'wide'], [15, 7, 54, 27], 3], // TEMPLE BREATH / SOFT BRASS
  ['DUST HOUSE', 'HOUSE', 'DUST', 3, ['dusty', 'groovy', 'warm'], [12, 4, 36, 42], 0], // TAPE KEYS / JUNO DREAM
  ['MIDNIGHT FLOOR', 'HOUSE', 'NIGHT', 4, ['club', 'round', 'wide'], [13, 0, 21, 40], 1], // DYNO EPIANO / PUMP PAD
  ['TAPE DISCO', 'HOUSE', 'TAPE', 3, ['tape', 'soft', 'groovy'], [3, 7, 28, 27], 2], // NYLON PLUCK / SOFT BRASS
  ['CLEAN CLUB', 'HOUSE', 'CLEAN', 4, ['clean', 'tight', 'bright'], [12, 5, 22, 42], 3], // DRAWBAR ORGAN / JUNO DREAM
  ['VHS GARDEN', 'LO-FI', 'VHS', 2, ['tape', 'dark', 'nostalgic'], [3, 7, 36, 34], 0], // TAPE KEYS / OCEAN AIR
  ['POCKET DUST', 'LO-FI', 'POCKET', 2, ['dusty', 'small', 'warm'], [12, 5, 20, 27], 1], // MELLOW RHODES / SOFT BRASS
  ['TOY PARADE', 'LO-FI', 'TOY', 4, ['8-bit', 'playful', 'broken'], [13, 2, 29, 42], 2], // KALIMBA PLUCK / JUNO DREAM
  ['WORN SIGNAL', 'LO-FI', 'WORN', 3, ['distorted', 'dark', 'unstable'], [3, 0, 33, 17], 3], // DATA STREAM / DARK DRONE
  ['CHROME CATHEDRAL', 'CINEMATIC', 'CATHEDRAL', 3, ['large', 'metallic', 'ceremonial'], [13, 7, 39, 25], 0], // GAMELAN POT / CINEMA STRINGS
  ['MACHINE TENSION', 'CINEMATIC', 'TENSION', 4, ['industrial', 'tense', 'dark'], [12, 5, 47, 38], 1], // WAVE DANCER / AURORA RISER
  ['VOID MARCH', 'CINEMATIC', 'MARCH', 4, ['heavy', 'dark', 'driving'], [3, 4, 44, 25], 2], // BLADE BRASS / CINEMA STRINGS
  ['FINAL HORIZON', 'CINEMATIC', 'FINALE', 5, ['epic', 'wide', 'bright'], [13, 8, 43, 38], 3], // JUMP BRASS / AURORA RISER
].map(([name, family, variation, energy, tags, programs, variationIndex]) => spec(
  name as string, family as string, variation as string, energy as number, tags as string[], programs as [number, number, number, number], variationIndex as number,
));

interface Harmony {
  roots: number[];
  minor: boolean[];
}

// Full-chain, in-context track faders — measured by
// juce/test/measure_track_levels.cpp, which renders every song's four tracks
// through their real engine+FX (incl. WT-1's leveling comp) and balances every
// track to the mean drum-bus RMS with perceptual per-role offsets (bass +4 dB,
// pad +2 dB). The fader curve is gain² × 1.4, so quieter voices need a higher
// fader value. Tables carry only currently-used programs; the ?? fallbacks
// cover future picks until the next measure_track_levels run.
const DRUM_FADERS: Record<number, number> = { 3: 0.80, 12: 0.80, 13: 0.87, 15: 0.63 };
const BASS_FADERS: Record<number, number> = { 0: 0.49, 2: 0.52, 4: 0.50, 5: 0.48, 7: 0.49, 8: 0.47, 9: 0.84 };
const LEAD_FADERS: Record<number, number> = {
  3: 0.67, 20: 0.70, 21: 0.54, 22: 0.60, 28: 0.84, 29: 0.96, 33: 0.51, 36: 0.75, 39: 1.00,
  43: 0.51, 44: 0.52, 45: 0.79, 47: 0.87, 49: 0.54, 51: 0.48, 52: 0.53, 53: 0.49, 54: 0.49,
};
const PAD_FADERS: Record<number, number> = {
  1: 0.65, 11: 0.53, 17: 0.83, 24: 0.63, 25: 0.62, 27: 0.63, 32: 1.00, 34: 0.60, 35: 0.59, 38: 0.83, 40: 0.60, 42: 0.82,
};

function calibratedTrackGains(programs: Spec['programs']): [number, number, number, number] {
  return [DRUM_FADERS[programs[0]] ?? 0.78, BASS_FADERS[programs[1]] ?? 0.50, LEAD_FADERS[programs[2]] ?? 0.65, PAD_FADERS[programs[3]] ?? 0.59];
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

function bassProgression(harmony: Harmony, spec: Spec): ClipDoc {
  const bytes = emptyClipBytes('BL1', 4);
  if (spec.family === 'NEON') {
    // Synthwave engine room: staccato 16ths pumping the low root, one 16th of
    // every beat lifted an octave (rotated per variation so sibling songs pump
    // differently), accents on the quarters, and a pickup into each new bar.
    const lift = 2 + (spec.variationIndex % 2);
    harmony.roots.forEach((root, bar) => {
      const next = harmony.roots[(bar + 1) % 4]!;
      for (let step = 0; step < 16; step++) {
        const pitch = step === 15 ? next - 12 : step % 4 === lift ? root : root - 12;
        putNote(bytes, noteIdx(bar, step), pitch, 1, step % 4 === 0);
      }
    });
    return { name: `${spec.variation} DRIVE · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
  }
  harmony.roots.forEach((root, bar) => {
    // One low root, then one fifth: deliberate space for the drums and pad.
    putNote(bytes, noteIdx(bar, 0), root - 12, 8, true);
    putNote(bytes, noteIdx(bar, 8), root - 5, 8);
  });
  return { name: `${spec.variation} ROOTS · 4 BAR`, bars: 4, pattern: bytesToB64(bytes) };
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

// Hand-composed lead phrases — one per song, since every (family, variation)
// pair names exactly one preset. Four bars separated by '|'; a note is
// `step,duration,semitones` above the session tonic, and '!' marks an accent.
// Each bar opens on a tone of that bar's chord (see harmonyFor()) and stays in
// natural minor — 11 appears only as the leading tone over the major V. The
// phrases are written to breathe rather than fill a grid: dense question bars
// answered by sparser ones, rests before entrances, and a long held tone to
// close bar 4. The same strings are transcribed verbatim in SeqFactory.cpp.
const LEAD_PHRASES: Record<string, [string, string, string, string]> = {
  NEON: [ // gliding synthwave lines that bloom upward, then settle
    '0,2,0! 2,2,3 4,2,7 8,3,10 12,2,7 14,2,3 | 0,3,8! 4,2,0 8,4,10 14,2,7 | 0,2,7! 2,2,10 4,2,0 6,2,2 8,2,3 10,2,7 12,4,10 | 0,4,5! 4,2,2 8,8,0',
    '0,1,0! 2,1,0 4,2,3 6,2,7 8,2,10 11,2,7 14,2,8 | 0,2,5! 3,2,8 6,2,5 8,3,3 12,4,0 | 0,2,8! 2,2,10 4,2,0 8,2,3 10,2,0 12,2,10 14,2,8 | 0,3,7! 4,3,11 8,2,7 10,6,2',
    '0,3,7! 4,3,3 8,2,5 12,4,7 | 0,3,10! 4,2,7 7,2,5 10,2,3 12,4,2 | 0,2,2! 2,2,5 5,2,7 8,3,10 12,4,7 | 0,4,8! 6,2,7 8,8,5',
    '0,4,3! 4,3,2 8,4,0 13,3,7 | 0,4,2! 4,2,11 6,2,7 8,4,2 12,4,11 | 0,3,0! 3,3,8 8,2,5 10,2,3 12,4,5 | 0,4,2! 4,2,3 6,2,2 8,8,0',
  ],
  ACID: [ // coiling sixteenth cells that wind tight, then release
    '0,1,0! 1,1,0 3,1,3 4,2,0 8,2,10 11,1,8! 12,4,7 | 0,2,8! 3,1,7 4,2,8 8,2,0 10,1,10 12,3,8 | 0,1,3! 1,1,3 3,1,5 4,2,7 8,2,10 10,2,7 13,3,3 | 0,2,10! 4,2,7 8,2,5 12,4,2',
    '0,1,0! 2,1,3 4,1,0 6,2,7 8,1,5 10,2,3 13,3,0 | 0,2,8! 2,2,5 5,1,3 8,2,8 10,2,10 13,3,5 | 0,1,0! 1,1,0 4,2,8 6,2,10 8,2,8 12,4,3 | 0,2,11! 3,2,7 8,2,2 10,6,7',
    '0,2,7! 3,1,5 4,2,7 8,2,3 10,2,0 14,2,7 | 0,1,10! 1,1,10 4,2,7 6,2,3 8,3,10 12,4,7 | 0,2,5! 2,2,7 4,1,5 8,2,2 10,2,0 12,2,10 14,2,5 | 0,3,8! 4,2,5 8,8,0',
    '0,1,7! 1,1,7 3,1,10 4,2,7 8,2,0 10,2,10 13,3,7 | 0,2,2! 2,1,0 4,2,11 8,2,7 10,2,2 13,3,11 | 0,1,8! 2,1,8 4,2,5 6,2,8 8,2,10 12,4,5 | 0,2,10! 4,2,5 6,2,7 8,8,10',
  ],
  AMBIENT: [ // gapless winding tones for the glide leads — each note starts as
    // the last one ends, so the portamento snakes continuously through the bar
    '0,8,0! 8,4,3 12,4,5 | 0,6,3! 6,6,2 12,4,0 | 0,8,10! 8,4,7 12,4,8 | 0,6,2! 6,10,0',
    '0,4,7! 4,4,8 8,4,7 12,4,10 | 0,6,8! 6,4,7 10,6,5 | 0,4,0! 4,4,10 8,8,8 | 0,4,11! 4,4,7 8,8,2',
    '0,4,7! 4,4,3 8,8,10 | 0,6,10! 6,10,7 | 0,8,2! 8,8,0 | 0,4,5! 4,12,3',
    '0,6,0! 6,4,2 10,6,3 | 0,4,7! 4,4,8 8,8,11 | 0,6,8! 6,4,7 10,6,5 | 0,4,10! 4,12,7',
  ],
  HOUSE: [ // off-beat stabs that skip across the bar line
    '0,2,0! 3,2,3 7,2,7 10,2,3 14,2,0 | 0,2,8! 3,2,10 7,2,8 11,2,7 14,2,5 | 0,2,3! 3,2,7 7,3,10 11,2,7 14,2,10 | 0,2,2! 3,2,5 7,6,0',
    '0,2,7! 3,2,10 6,1,7 8,2,3 11,2,5 14,2,7 | 0,2,8! 3,2,7 7,2,5 10,2,8 14,2,10 | 0,2,0! 3,2,10 7,2,8 10,2,7 14,2,8 | 0,2,11! 3,2,7 7,3,2 12,4,7',
    '0,2,3! 4,2,0 7,2,3 10,2,7 13,3,5 | 0,2,7! 4,2,10 7,2,0 10,2,10 13,3,7 | 0,2,10! 4,2,2 7,2,10 10,2,8 13,3,7 | 0,2,8! 4,2,7 7,2,5 10,6,3',
    '0,1,0! 3,1,0 6,2,7 8,1,5 11,2,3 14,2,0 | 0,1,2! 3,1,2 6,2,11 8,2,7 11,2,2 14,2,11 | 0,1,5! 3,1,5 6,2,8 8,2,0 11,2,8 14,2,5 | 0,2,2! 3,2,5 6,2,7 8,8,10',
  ],
  'LO-FI': [ // unhurried, nostalgic lines that trail off mid-bar
    '0,3,3! 4,3,7 9,2,5 12,4,3 | 0,3,0! 4,3,10 9,5,8 | 0,3,7! 4,3,5 9,2,7 12,4,10 | 0,3,5! 4,2,3 7,2,2 9,7,0',
    '0,2,0! 3,3,3 8,2,2 10,2,3 13,3,0 | 0,3,8! 4,2,5 8,3,3 12,4,5 | 0,3,3! 4,2,0 8,4,10 13,3,8 | 0,3,7! 4,2,5 7,2,3 9,7,2',
    '0,1,0! 2,1,3 4,1,7 6,1,3 8,2,10 11,1,7 13,3,0 | 0,1,7! 2,1,10 4,1,7 6,1,3 8,3,2 12,4,3 | 0,1,10! 2,1,2 4,1,5 6,1,2 8,2,7 11,1,5 13,3,10 | 0,2,8! 3,1,7 5,2,5 8,8,0',
    '0,4,0! 5,2,10 8,3,8 12,4,7 | 0,4,11! 5,2,7 8,4,2 13,3,7 | 0,4,8! 5,2,7 8,3,5 12,4,8 | 0,4,5! 5,2,3 8,8,2',
  ],
  CINEMATIC: [ // bold dotted figures and long horizon tones
    '0,4,0! 6,2,3 8,6,7 14,2,10 | 0,4,8! 6,2,7 8,4,5 12,4,3 | 0,4,10! 6,2,7 8,6,3 14,2,5 | 0,3,5! 3,3,7 8,8,10',
    '0,2,0! 4,1,0 6,2,0 8,4,3 12,4,2 | 0,2,5! 4,1,5 6,2,5 8,4,8 12,4,7 | 0,2,3! 4,1,3 6,2,3 8,4,10 12,4,8 | 0,2,2! 4,2,2 6,2,2 8,8,11',
    '0,3,0! 4,3,0 8,3,3 12,2,2 14,2,0 | 0,3,3! 4,3,3 8,3,7 12,2,5 14,2,3 | 0,3,5! 4,3,5 8,3,10 12,2,8 14,2,5 | 0,3,8! 4,3,7 8,8,5',
    '0,3,0! 4,2,3 6,2,5 8,4,7 12,4,10 | 0,3,11! 4,2,7 6,2,2 8,8,7 | 0,3,8! 4,2,5 6,2,8 8,4,0 12,4,10 | 0,2,10! 2,2,7 4,2,5 6,2,7 8,8,10',
  ],
};

interface LeadNote { step: number; duration: number; pitch: number; accent: boolean }

function parseLeadPhrase(phrase: string): LeadNote[][] {
  return phrase.split('|').map((bar) => bar.trim().split(/\s+/).map((token) => {
    const accent = token.endsWith('!');
    const [step, duration, pitch] = (accent ? token.slice(0, -1) : token).split(',').map(Number);
    return { step: step!, duration: duration!, pitch: pitch!, accent };
  }));
}

function leadProgression(harmony: Harmony, spec: Spec): ClipDoc {
  const bytes = emptyClipBytes('WT1', 4);
  const tonic = harmony.roots[0];
  const phrase = (LEAD_PHRASES[spec.family] ?? LEAD_PHRASES.NEON!)[spec.variationIndex]!;
  parseLeadPhrase(phrase).forEach((barNotes, bar) => {
    for (const note of barNotes) {
      // Voice the melody strictly one octave above the pad bed (+12..+23):
      // the authored phrases carry the contour, so no octave search is needed.
      putNote(bytes, wtNoteIdx(bar, note.step, 0), ((tonic + note.pitch) % 12) + 12, note.duration, note.accent);
    }
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
    perc: [],
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
    perc: [],
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
  AMBIENT: [[DRUM.OH, 12, false]],
  HOUSE: [[DRUM.CLAP, 13, false], [DRUM.CLAP, 15, true]],
  'LO-FI': [[DRUM.PERC_B, 13, false], [DRUM.PERC_B, 15, false]],
  CINEMATIC: [[DRUM.TOM_HI, 8, false], [DRUM.TOM_MID, 10, false], [DRUM.TOM_LO, 12, true], [DRUM.TOM_LO, 14, true]],
};

// One ghost hit per variation (index 0 adds none): [pad, step].
const DRUM_GHOSTS: Array<[number, number] | null> = [null, [DRUM.KICK, 14], [DRUM.SNARE, 11], [DRUM.PERC_B, 3]];
// AMBIENT keeps its ghosts on the eighth grid with soft voices — off-grid
// kick/snare pokes read as glitches in so sparse a field. Each variation still
// gets a distinct hit so sibling songs stay unique.
const AMBIENT_GHOSTS: Array<[number, number] | null> = [null, [DRUM.CH, 10], [DRUM.RIM, 12], [DRUM.PERC_B, 6]];

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
  const ghost = (spec.family === 'AMBIENT' ? AMBIENT_GHOSTS : DRUM_GHOSTS)[spec.variationIndex] ?? null;

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
    // Builds climb into the drop on their own backbeat voice (snare/clap/rim)
    // rather than a perc pad — perc voices vary too wildly across kits.
    if (build && lastBar) for (const step of [12, 13, 14, 15]) hit(bar, family.back.pad, step, step === 15);
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
  const bass = bassProgression(harmony, spec);
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

export const FACTORY_SESSION_PRESETS: SessionPreset[] = specs.map((spec) => ({
  ...spec,
  session: buildSession(spec),
}));

/** Session data is edited in the store, so preset recall always needs a copy. */
export function copySession(session: SessionDoc): SessionDoc {
  return {
    ...session,
    tracks: session.tracks.map((track) => ({ ...track, patch: { ...track.patch } })),
    scenes: session.scenes.map((scene) => ({ ...scene, pass: scene.pass ? [...scene.pass] : undefined, clips: scene.clips.map((clip) => clip && { ...clip }) })),
  };
}
