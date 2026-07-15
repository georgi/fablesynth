// SQ-4 factory session presets. Keep the same names, families, patch choices
// and clip-selection rule as JUCE's SeqFactory.cpp so the web and native
// instruments present one coherent factory library.

import { FACTORY_CLIP_LIBRARY } from './clipLibrary.gen';
import { factorySession } from './factory';
import type { MachineId, SessionDoc } from './protocol';

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
  ['NEON CHASE', 'NEON', 'CHASE', 5, ['bright', 'driving', 'wide'], [13, 2, 4, 11], 1],
  ['GLASS CIRCUIT', 'NEON', 'GLASS', 2, ['clean', 'glassy', 'sparse'], [12, 9, 3, 6], 2],
  ['AFTERGLOW', 'NEON', 'SOFT', 2, ['warm', 'soft', 'wide'], [3, 1, 5, 1], 3],
  ['WAREHOUSE RAW', 'ACID', 'RAW', 5, ['hard', 'dark', 'driving'], [13, 4, 12, 17], 0],
  ['ACID FLASH', 'ACID', 'FLASH', 4, ['acid', 'bright', 'punchy'], [3, 0, 14, 4], 1],
  ['STEEL PULSE', 'ACID', 'METAL', 4, ['metallic', 'tight', 'industrial'], [12, 6, 13, 9], 2],
  ['PEAK SIGNAL', 'ACID', 'PEAK', 5, ['distorted', 'wide', 'peak-time'], [13, 2, 19, 11], 3],
  ['DEEP FOG', 'AMBIENT', 'FOG', 1, ['dark', 'deep', 'slow'], [12, 3, 5, 17], 0],
  ['GLASS BLOOM', 'AMBIENT', 'BLOOM', 2, ['glassy', 'clean', 'lush'], [13, 11, 3, 1], 1],
  ['FROZEN BELL', 'AMBIENT', 'FROZEN', 2, ['cold', 'bell', 'sparse'], [12, 11, 6, 17], 2],
  ['AIR TEMPLE', 'AMBIENT', 'TEMPLE', 2, ['warm', 'ceremonial', 'wide'], [3, 7, 15, 1], 3],
  ['DUST HOUSE', 'HOUSE', 'DUST', 3, ['dusty', 'groovy', 'warm'], [12, 5, 14, 1], 0],
  ['MIDNIGHT FLOOR', 'HOUSE', 'NIGHT', 4, ['club', 'round', 'wide'], [13, 1, 13, 11], 1],
  ['TAPE DISCO', 'HOUSE', 'TAPE', 3, ['tape', 'soft', 'groovy'], [3, 7, 19, 1], 2],
  ['CLEAN CLUB', 'HOUSE', 'CLEAN', 4, ['clean', 'tight', 'bright'], [12, 11, 14, 4], 3],
  ['VHS GARDEN', 'LO-FI', 'VHS', 2, ['tape', 'dark', 'nostalgic'], [3, 7, 16, 17], 0],
  ['POCKET DUST', 'LO-FI', 'POCKET', 2, ['dusty', 'small', 'warm'], [12, 5, 3, 1], 1],
  ['TOY PARADE', 'LO-FI', 'TOY', 4, ['8-bit', 'playful', 'broken'], [13, 9, 16, 15], 2],
  ['WORN SIGNAL', 'LO-FI', 'WORN', 3, ['distorted', 'dark', 'unstable'], [3, 10, 5, 17], 3],
  ['CHROME CATHEDRAL', 'CINEMATIC', 'CATHEDRAL', 3, ['large', 'metallic', 'ceremonial'], [13, 3, 6, 1], 0],
  ['MACHINE TENSION', 'CINEMATIC', 'TENSION', 4, ['industrial', 'tense', 'dark'], [12, 10, 12, 17], 1],
  ['VOID MARCH', 'CINEMATIC', 'MARCH', 4, ['heavy', 'dark', 'driving'], [3, 8, 9, 17], 2],
  ['FINAL HORIZON', 'CINEMATIC', 'FINALE', 5, ['epic', 'wide', 'bright'], [13, 8, 19, 11], 3],
].map(([name, family, variation, energy, tags, programs, variationIndex]) => spec(
  name as string, family as string, variation as string, energy as number, tags as string[], programs as [number, number, number, number], variationIndex as number,
));

function wantedFamily(family: string): string {
  return ({ NEON: 'techno', ACID: 'acid', AMBIENT: 'ambient', HOUSE: 'house', 'LO-FI': 'lo-fi' } as Record<string, string>)[family] ?? 'cinematic';
}

function buildSession(spec: Spec): SessionDoc {
  const session = factorySession();
  session.name = spec.name;
  session.bpm = 96 + spec.energy * 7 + spec.variationIndex;
  session.swing = spec.family === 'HOUSE' ? 0.12 : spec.family === 'LO-FI' ? 0.18 : 0;
  session.tracks.forEach((track, t) => { track.patch = { kind: 'factory', index: spec.programs[t] }; });
  session.scenes.forEach((scene, s) => {
    scene.clips = session.tracks.map((track, t) => {
      const machine = track.machine as MachineId;
      const candidates = FACTORY_CLIP_LIBRARY.filter((clip) => clip.machine === machine && clip.family === wantedFamily(spec.family));
      const pool = candidates.length ? candidates : FACTORY_CLIP_LIBRARY.filter((clip) => clip.machine === machine);
      const clip = pool[(s + spec.variationIndex * 3 + t * 2) % pool.length];
      return { name: clip.name, bars: clip.bars, pattern: clip.pattern };
    });
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
