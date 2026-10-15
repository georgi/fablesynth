import { FACTORY_PRESETS, resolvePresetMods } from '../../presets';
import { bytesToB64, dr1Idx, emptyClipBytes, noteIdx, wtNoteIdx, type ClipDoc, type PatchDoc } from '../protocol';
import { assertDrumRhythm, type DrumLaneRhythm } from '../../drum/rhythm';
import type { ChordQuality } from '../../chords';
import { chordTrackArp, clipChordOctave } from '../clipArp';

// Absolute sixteenth in a four-bar phrase, duration, pitch(es) relative to C3,
// accent. Chords retain their written inversions; no pitch-class folding.
export type Note = [step: number, duration: number, pitch: number | number[], accent?: boolean];
export function notes(machine: 'BL1' | 'WT1', name: string, score: Note[]): ClipDoc {
  const bytes = emptyClipBytes(machine, 4);
  for (const [at, duration, pitch, accent = false] of score) {
    const pitches = typeof pitch === 'number' ? [pitch] : pitch;
    if (at < 0 || at >= 64 || duration < 1 || duration > 63 || at + duration > 64
      || pitches.length > (machine === 'BL1' ? 1 : 8)) throw new Error(`Invalid score: ${name}`);
    pitches.forEach((p, lane) => {
      if (!Number.isInteger(p) || p < -12 || p > 23) throw new Error(`Invalid pitch: ${name}`);
      const offset = machine === 'BL1' ? noteIdx(at >> 4, at % 16) : wtNoteIdx(at >> 4, at % 16, lane);
      bytes[offset] = 1 | (accent ? 2 : 0) | (duration << 2);
      bytes[offset + 1] = ((p % 12) + 12) % 12;
      bytes[offset + 2] = Math.floor(p / 12) + 1;
    });
  }
  return { name, bars: 4, pattern: bytesToB64(bytes) };
}

// Each string is one explicitly scored bar; x = hit, X = accent, . = rest.
export function drums(name: string, score: Record<number, string>): ClipDoc {
  const bytes = emptyClipBytes('DR1', 4);
  for (const [pad, line] of Object.entries(score)) {
    const grid = line.replace(/[ |]/g, '');
    if (!/^[.xX]{64}$/.test(grid)) throw new Error(`Invalid drum score: ${name} pad ${pad}`);
    [...grid].forEach((hit, at) => { bytes[dr1Idx(at >> 4, +pad, at % 16)] = hit === 'X' ? 2 : hit === 'x' ? 1 : 0; });
  }
  return { name, bars: 4, pattern: bytesToB64(bytes) };
}

export function wtPatch(base: number, overrides: Record<string, number>): PatchDoc {
  const preset = FACTORY_PRESETS[base];
  return { kind: 'inline', base, data: { params: { ...resolvePresetMods(preset.params, preset.mods), ...overrides } } };
}

// DR-1 POLY and micro-timing, scored per pad. A POLY lane loops the first
// `steps` cells of the clip's bar one: GRID on the sixteenth clock, FIT
// spread evenly over one or two bars. Offsets are milliseconds (+ = late).
export const grid = (steps: number, rotation = 0): DrumLaneRhythm =>
  ({ enabled: true, sourceBar: 0, steps, rotation, timing: { mode: 'grid' } });
export const fit = (steps: number, cycleBeats: 4 | 8, rotation = 0): DrumLaneRhythm =>
  ({ enabled: true, sourceBar: 0, steps, rotation, timing: { mode: 'fit', cycleBeats } });
/** Micro-timing: a lane offset plus per-step offsets of one bar, repeated on all four bars. */
export function feel(delayMs: number, bar: Record<number, number> = {}, lane?: DrumLaneRhythm): DrumLaneRhythm {
  const steps = Array.from({ length: 16 }, (_, s) => bar[s] ?? 0);
  return { ...(lane ?? { enabled: false, sourceBar: 0, steps: 16, rotation: 0, timing: { mode: 'grid' } }),
    delayMs, ...(steps.some(Boolean) ? { stepDelayMs: [0, 1, 2, 3].flatMap(() => steps) } : {}) };
}
/** The clip with POLY and micro-timing lanes by pad. */
export function rhythm(clip: ClipDoc, lanes: Record<number, DrumLaneRhythm>): ClipDoc {
  const drumRhythm = { v: 1 as const, lanes: Array.from({ length: 16 }, (_, pad) => lanes[pad] ?? null) };
  return { ...clip, drumRhythm: assertDrumRhythm(drumRhythm, clip.bars) };
}

// The harmony of a scene as [root pitch class, quality, beats], from beat one.
export type Harmony = Array<[root: number, quality: ChordQuality, beats: number]>;
/** The clip with its scene's harmony as a switched-off arp chord track, in the clip's register. */
export function harmonize(machine: 'BL1' | 'WT1', clip: ClipDoc, key: number, harmony: Harmony): ClipDoc {
  let start = 0;
  const chords = harmony.map(([root, quality, beats]) => {
    const c = { start, length: beats * 4, root, quality, inversion: 0 };
    start += beats * 4;
    return c;
  });
  if (start !== clip.bars * 16) throw new Error(`Harmony of ${clip.name} does not fill the clip`);
  return { ...clip, arp: chordTrackArp(machine, chords, key, clipChordOctave(machine, clip)) };
}
