import { describe, expect, it } from 'vitest';
import { parseBar } from './stepNotation';
import { BASS_PATTERN_PRESETS, bassPresetSequence } from '../bass/patternPresets';
import { NOTE_PATTERN_PRESETS, notePresetSequence } from '../patternPresets';

describe('step notation', () => {
  it('parses notes, octaves, flags, holds and rests', () => {
    const steps = parseBar("C! - . D#,~ Bb' . . . . . . . . . . .");
    expect(steps[0]).toEqual({ on: true, note: 0, oct: 0, acc: true, slide: false, duration: 2 });
    expect(steps[1].on).toBe(false);
    expect(steps[3]).toEqual({ on: true, note: 3, oct: -1, acc: false, slide: true, duration: 1 });
    expect(steps[4]).toMatchObject({ note: 10, oct: 1 });
  });

  it('rejects malformed bars', () => {
    expect(() => parseBar('C D E')).toThrow();
    expect(() => parseBar('- . . . . . . . . . . . . . . .')).toThrow();
    expect(() => parseBar('H . . . . . . . . . . . . . . .')).toThrow();
  });

  it.each([
    ['BL-1', BASS_PATTERN_PRESETS, bassPresetSequence],
    ['WT-1', NOTE_PATTERN_PRESETS, notePresetSequence],
  ] as const)('%s catalog holds distinct, named lines', (_, presets, build) => {
    expect(new Set(presets.map((p) => p.name)).size).toBe(presets.length);
    const packed = presets.map((p) => build(p));
    expect(new Set(packed.map((s) => Array.from(s.patterns).join(','))).size).toBe(presets.length);
    presets.forEach((p, i) => expect(packed[i].chain).toEqual(p.bars.map((_, bar) => bar)));
  });
});
