// Chord comping for some generated factory sessions: the pad track's arp
// switches to COMP and plays a library groove over the song's chord track.
// The pads hold their written chords in the INTRO and the TAIL, and comp in
// the four scenes between them. Each pick suits the pad sound (a short,
// fast-attack patch) and the genre. Grooves with a bass voice use their NO
// BASS variation, because the bass track already plays the root.
import { COMP_LIBRARY, type CompLibraryEntry } from '../../compLibrary';
import type { ClipArp } from '../clipArp';

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

/** The scenes that comp (1 BUILD … 4 BREAK). */
export const COMPING_SCENES = [1, 2, 3, 4];

export function compEntry(name: string): CompLibraryEntry {
  const e = COMP_LIBRARY.find(x => x.name === name);
  if (!e) throw new Error(`No comp pattern ${name}`);
  return e;
}

/** The arp in COMP style with a library groove and its feel (CompBrowser loadCompEntry). */
export function withComp(arp: ClipArp, e: CompLibraryEntry, enabled: boolean): ClipArp {
  return { enabled, settings: { ...arp.settings, style: 'comp', comp: [...e.comp], ties: [...e.ties], accents: [...e.accents],
    rate: e.rate, gate: e.gate, strum: e.strum, strumDir: e.strumDir, voicing: e.voicing } };
}
