import { FACTORY_PRESETS, resolvePresetMods } from '../../presets';
import { bytesToB64, dr1Idx, emptyClipBytes, noteIdx, wtNoteIdx, type ClipDoc, type PatchDoc } from '../protocol';

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

