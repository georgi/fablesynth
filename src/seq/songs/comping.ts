// Chord comping for the generated factory sessions: every pad track plays
// its song's chord track in CHORDS mode (arp in COMP style). The pads hold
// whole-note chords by default. Some sessions comp a library groove in the
// four scenes between the INTRO and the TAIL. Each pick suits the genre, and
// SESSION_COMP_SOUNDS shapes the pad sound for its groove. Grooves with a bass voice use their NO
// BASS variation, because the bass track already plays the root.
import { COMP_LIBRARY, type CompLibraryEntry } from '../../compLibrary';
import type { ClipArp } from '../clipArp';
import type { CompVoicing } from '../../comp';

/** Changes to a library groove's voicing or accents. */
export interface CompFeel { voicing?: CompVoicing; accents?: boolean[] }

export const SESSION_COMPING: Record<string, string> = {
  'ECHO CHAMBER': 'SKANK', // DUB SKANK: upstrokes on two and four
  'ROOTS RADAR': 'DOUBLE SKANK', // DUB SKANK: the skank with its echo
  'KING STEPPER': 'ORGAN BUBBLE', // DRAWBAR ORGAN: the reggae organ shuffle
  'MIDNIGHT FLOOR': 'DEEP STAB', // PUMP PAD: deep-house chord stabs
  'TAPE DISCO': 'PIANO HOUSE', // SOFT BRASS: piano-house chord rhythm
  'POCKET DUST': 'SOUL CHOP', // SOFT BRASS: chopped soul-sample chords
  'NEON TALE': 'POP SYNCOPATION · NO BASS', // FUTURE CHORD: pushed synth-pop chords
  'SUGAR RUSH': 'SUPERSAW OFFBEAT', // FUTURE CHORD: held offbeat supersaws
  'STARBURST': 'FUTURE BOUNCE', // PUMP PAD: future-bass bounce
  'WAREHOUSE RAW': 'RAVE STAB', // PUMP PAD: rave stabs over the acid line
};

// The pad sound of a comping session, written for its groove. Stabs need a
// fast attack, a short release and a filter snap. PUMP PAD loses its
// quarter-note duck, because the stabs make the rhythm and a downbeat stab
// would land in the duck. DUB SKANK (skanks) and DRAWBAR ORGAN (bubble)
// already speak short. The held scenes keep enough sustain for whole notes.
// The keys change the track's factory patch; it stays the patch's base.
export const SESSION_COMP_SOUNDS: Record<string, Record<string, number>> = {
  // PUMP PAD → deep-house stab: three dark voices and dotted-eighth echoes.
  'MIDNIGHT FLOOR': {
    'env1.a': 0.002, 'env1.d': 0.35, 'env1.s': 0.5, 'env1.r': 0.16, 'mat1.amt': 0,
    'oscA.unison': 3, 'oscA.detune': 0.18, 'filter.cutoff': 1100, 'filter.res': 0.2, 'filter.env': 0.45,
    'env2.a': 0.001, 'env2.d': 0.22, 'env2.s': 0, 'env2.r': 0.2,
    'fx.delay.on': 1, 'fx.delay.sync': 1, 'fx.delay.div': 3, 'fx.delay.fb': 0.32, 'fx.delay.mix': 0.2,
  },
  // SOFT BRASS → brass keys: the 120 ms swell becomes a struck chord.
  'TAPE DISCO': {
    'env1.a': 0.003, 'env1.d': 0.4, 'env1.s': 0.45, 'env1.r': 0.18,
    'env2.a': 0.002, 'env2.d': 0.25, 'env2.s': 0.2, 'env2.r': 0.2,
    'filter.cutoff': 1500, 'filter.env': 0.55, 'fx.chorus.mix': 0.35,
  },
  // SOFT BRASS → sample chop: flat level and a hard 60 ms cut.
  'POCKET DUST': {
    'env1.a': 0.004, 'env1.d': 0.3, 'env1.s': 0.75, 'env1.r': 0.06,
    'env2.a': 0.003, 'env2.d': 0.4, 'env2.s': 0.4, 'env2.r': 0.1,
    'filter.cutoff': 1000, 'filter.env': 0.3,
  },
  // FUTURE CHORD → pushed pop chords that stop on their rests.
  'NEON TALE': { 'env1.a': 0.004, 'env1.d': 0.35, 'env1.s': 0.6, 'env1.r': 0.2, 'filter.env': 0.35, 'fx.reverb.mix': 0.3 },
  // FUTURE CHORD → tied offbeat saws with a clean cut before the next hit.
  'SUGAR RUSH': { 'env1.a': 0.005, 'env1.d': 0.3, 'env1.s': 0.8, 'env1.r': 0.12, 'fx.reverb.mix': 0.32 },
  // PUMP PAD → bouncy future-bass chord with eighth echoes.
  'STARBURST': {
    'env1.a': 0.002, 'env1.d': 0.28, 'env1.s': 0.5, 'env1.r': 0.14, 'mat1.amt': 0,
    'filter.cutoff': 2200, 'filter.env': 0.4, 'env2.a': 0.001, 'env2.d': 0.18, 'env2.s': 0.1, 'env2.r': 0.15,
    'fx.delay.on': 1, 'fx.delay.sync': 1, 'fx.delay.div': 2, 'fx.delay.fb': 0.25, 'fx.delay.mix': 0.15,
  },
  // PUMP PAD → rave stab: resonant filter snap, short body, dotted-eighth echoes.
  'WAREHOUSE RAW': {
    'env1.a': 0.001, 'env1.d': 0.2, 'env1.s': 0.3, 'env1.r': 0.1, 'mat1.amt': 0,
    'oscA.unison': 5, 'filter.cutoff': 1600, 'filter.res': 0.3, 'filter.env': 0.55,
    'env2.a': 0.001, 'env2.d': 0.14, 'env2.s': 0, 'env2.r': 0.12, 'fx.reverb.mix': 0.22,
    'fx.delay.on': 1, 'fx.delay.sync': 1, 'fx.delay.div': 3, 'fx.delay.fb': 0.3, 'fx.delay.mix': 0.18,
  },
};

/** The held pad: one close-voiced chord per bar without the accent, so it
 *  plays at the written pads' plain velocity. */
export const PAD_HOLD = 'WHOLE NOTES · NO BASS';
export const PAD_HOLD_FEEL: CompFeel = { voicing: 'close', accents: Array(16).fill(false) };

/** The scenes that comp (1 BUILD … 4 BREAK). */
export const COMPING_SCENES = [1, 2, 3, 4];

export function compEntry(name: string): CompLibraryEntry {
  const e = COMP_LIBRARY.find(x => x.name === name);
  if (!e) throw new Error(`No comp pattern ${name}`);
  return e;
}

/** The arp in COMP style with a library groove and its feel (CompBrowser loadCompEntry). */
export function withComp(arp: ClipArp, e: CompLibraryEntry, enabled: boolean, feel: CompFeel = {}): ClipArp {
  return { enabled, settings: { ...arp.settings, style: 'comp', comp: [...e.comp], ties: [...e.ties], accents: [...(feel.accents ?? e.accents)],
    rate: e.rate, gate: e.gate, strum: e.strum, strumDir: e.strumDir, voicing: feel.voicing ?? e.voicing } };
}
