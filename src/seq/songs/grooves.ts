// Scored POLY lanes and micro-timing for some of the generated factory
// sessions. Every value is written here by hand; nothing is derived from the
// family or the energy. `feel` lanes go on every drum clip of the session;
// a figure writes its POLY cells into bar one of the drum clip in the listed
// scenes (0 INTRO … 5 TAIL) and loops them with its lane. Figures use pads
// that the session's drum parts leave empty.
import type { DrumLaneRhythm } from '../../drum/rhythm';
import { b64ToBytes, bytesToB64, dr1Idx, type SessionDoc } from '../protocol';
import { feel, fit, grid, rhythm } from './score';

interface Figure { pad: number; cells: string; lane: DrumLaneRhythm; scenes: number[] }
interface Groove { feel?: Record<number, DrumLaneRhythm>; figures?: Figure[] }

const KICK = 0, SNARE = 2, CLAP = 3, CH = 5, OH = 6, TOM_MID = 9, TOM_HI = 10, PERC = 12;

export const SESSION_GROOVES: Record<string, Groove> = {
  // Minimal polymeter: five- and seven-step tom and perc loops slide against
  // the straight floor, so each bar of the drop lands differently.
  'CLICK FIELD': {
    figures: [
      { pad: TOM_MID, cells: 'x..x.', lane: grid(5), scenes: [2, 3] },
      { pad: PERC, cells: 'X...x..', lane: grid(7, 3), scenes: [1, 2, 3] },
    ],
  },
  // Three against four: a dotted-eighth perc pulse, and from the drop high
  // toms in half-note triplets (three across one bar).
  'COLD ROTOR': {
    figures: [
      { pad: PERC, cells: 'x..', lane: grid(3), scenes: [1, 2, 3] },
      { pad: TOM_HI, cells: 'Xxx', lane: fit(3, 4), scenes: [2, 3] },
    ],
  },
  // Drunk boom bap: the snare drags, the hats wander around the swing and
  // the kick's pickup comes a little early.
  'POCKET DUST': {
    feel: { [SNARE]: feel(14), [CH]: feel(3, { 3: 6, 6: -4, 11: 9, 14: -3 }), [KICK]: feel(0, { 10: -6 }) },
  },
  'NIGHT BUS': {
    feel: { [SNARE]: feel(20), [CH]: feel(6, { 6: 6, 10: 8 }), [OH]: feel(10) },
  },
  // Broken trip hop: a five-beat tom loop over two bars plus a late snare.
  'CRACKED LENS': {
    feel: { [SNARE]: feel(16), [CH]: feel(5, { 6: 7, 14: -4 }) },
    figures: [{ pad: TOM_MID, cells: 'x.xx.', lane: fit(5, 8), scenes: [2, 3] }],
  },
  // House push and pull: the offbeat hats sit late, the clap a touch late.
  'DUST HOUSE': {
    feel: { [CH]: feel(0, { 2: 9, 6: 5, 10: 9, 14: 5 }), [OH]: feel(6), [CLAP]: feel(4) },
  },
  // The skank hats lag the one-drop.
  'KING STEPPER': {
    feel: { [CH]: feel(10, { 6: 4, 14: 4 }), [TOM_MID]: feel(8) },
  },
};

/** The session with its scored groove; sessions without one return unchanged. */
export function applyGroove(session: SessionDoc): SessionDoc {
  const groove = SESSION_GROOVES[session.name];
  if (!groove) return session;
  const drum = session.tracks.findIndex(t => t.machine === 'DR1');
  return {
    ...session,
    scenes: session.scenes.map((scene, s) => {
      const clip = scene.clips[drum];
      if (!clip) return scene;
      const lanes: Record<number, DrumLaneRhythm> = { ...groove.feel };
      const bytes = b64ToBytes(clip.pattern);
      for (const f of groove.figures ?? []) {
        if (!f.scenes.includes(s)) continue;
        if (f.cells.length !== f.lane.steps) throw new Error(`${session.name}: figure length ≠ steps`);
        for (let bar = 0; bar < clip.bars; bar++) for (let step = 0; step < 16; step++) {
          if (bytes[dr1Idx(bar, f.pad, step)]) throw new Error(`${session.name}: pad ${f.pad} is in use`);
        }
        [...f.cells].forEach((c, step) => { bytes[dr1Idx(0, f.pad, step)] = c === 'X' ? 2 : c === 'x' ? 1 : 0; });
        lanes[f.pad] = f.lane;
      }
      if (!Object.keys(lanes).length) return scene;
      const clips = [...scene.clips];
      clips[drum] = rhythm({ ...clip, pattern: bytesToB64(bytes) }, lanes);
      return { ...scene, clips };
    }),
  };
}
