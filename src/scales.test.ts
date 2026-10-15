import { describe, expect, it } from 'vitest';
import { PROGRESSIONS, progressionChords, type ArpChord, type ChordQuality } from './chords';
import { chordScale, followShift, keyScale, quantizeNote, scaleMap, scaleMask, scaleRemap, scaleSegments, scaleStepMasks } from './scales';

const ch = (root: number, quality: ChordQuality, start = 0, length = 16): ArpChord => ({ start, length, root, quality, inversion: 0 });
const prog = (name: string, key: number) => progressionChords(PROGRESSIONS.find(p => p.name === name)!, key).chords;

describe('key scale', () => {
  it('fits the progression to its parent scale on the key', () => {
    expect(keyScale(prog('i–VI–III–VII', 9), 9)).toBe('minor');
    expect(keyScale(prog('I–V–vi–IV', 0), 0)).toBe('major');
    expect(keyScale(prog('i–iv–V–i', 9), 9)).toBe('harmMinor');
    expect(keyScale(prog('I–♭VII–IV–I', 7), 7)).toBe('mixolydian');
    expect(keyScale(prog('i7–IV7', 2), 2)).toBe('dorian');
  });
});

describe('chord scale', () => {
  const cMajor = scaleMask(0, 'major');
  it('gives diatonic chords their mode of the key', () => {
    expect(chordScale(ch(0, 'maj7'), cMajor)).toBe('major');
    expect(chordScale(ch(2, 'm7'), cMajor)).toBe('dorian');
    expect(chordScale(ch(4, 'm7'), cMajor)).toBe('phrygian');
    expect(chordScale(ch(5, 'maj7'), cMajor)).toBe('lydian');
    expect(chordScale(ch(7, 'dom7'), cMajor)).toBe('mixolydian');
    expect(chordScale(ch(9, 'm7'), cMajor)).toBe('minor');
    expect(chordScale(ch(11, 'm7b5'), cMajor)).toBe('locrian');
  });
  it('picks the closest altered scale for borrowed chords', () => {
    expect(chordScale(ch(4, 'dom7'), scaleMask(9, 'minor'))).toBe('phrygDom');
    expect(chordScale(ch(10, 'dom7'), cMajor)).toBe('lydDom');
    expect(chordScale(ch(0, 'dim7'), cMajor)).toBe('dimWH');
    expect(chordScale(ch(0, 'aug'), cMajor)).toBe('wholeTone');
    expect(chordScale(ch(4, 'dom7s9'), cMajor)).toBe('dimHW');
  });
});

describe('remap', () => {
  it('snaps to the nearest scale tone, ties to a chord tone, else down', () => {
    const t = scaleRemap(scaleMask(0, 'major'));
    expect(t[1]).toBe(-1); expect(t[6]).toBe(-1); expect(t[4]).toBe(0);
    expect(scaleRemap(scaleMask(0, 'major'), 1 << 2)[1]).toBe(1);
    expect(scaleRemap(scaleMask(0, 'minPent'))[1]).toBe(-1);
  });
});

describe('scale map', () => {
  const a = { chordKey: 9, chords: prog('i–VI–III–VII', 9), scaleType: 'auto' as const };
  it('is undefined when off and one table in KEY mode', () => {
    expect(scaleMap({ ...a, scale: 'off' }, 4)).toBeUndefined();
    const key = scaleMap({ ...a, scale: 'key' }, 4)!;
    expect(key).toHaveLength(1);
    expect(quantizeNote(key, 37, 61)).toBe(60); // C♯ → C in A minor
  });
  it('follows the chords per sixteenth and fills gaps with the key', () => {
    const b = { chordKey: 0, chords: [ch(0, 'maj'), ch(10, 'dom7', 32, 16)], scale: 'chords' as const, scaleType: 'major' as const };
    const map = scaleMap(b, 4)!;
    expect(map).toHaveLength(64);
    expect(scaleSegments(b, 4).map(s => [s.start, s.length, s.root, s.scale]))
      .toEqual([[0, 16, 0, 'major'], [16, 16, 0, 'major'], [32, 16, 10, 'lydDom'], [48, 16, 0, 'major']]);
    expect(quantizeNote(map, 2, 63)).toBe(64); // E♭ over C → the chord tone E
    expect(quantizeNote(map, 34, 63)).toBe(62); // E♭ over B♭7 → the chord tone D
    expect(quantizeNote(map, 34, 64)).toBe(64); // E is the ♯11 of B♭ lydian dominant
  });
});

describe('FOLLOW', () => {
  const written = [ch(2, 'm9', 0, 32), ch(10, 'maj9', 32, 32)];
  const a = { chordKey: 2, chords: written, followChords: written, scale: 'follow' as const };
  it('moves by the nearest root distance', () => {
    expect(followShift(2, 7)).toBe(5);
    expect(followShift(7, 2)).toBe(-5);
    expect(followShift(0, 6)).toBe(-6);
  });
  it('leaves notes over their written chords alone', () => {
    expect(scaleMap(a, 4)!.flat().every(x => x === 0)).toBe(true);
    expect(scaleStepMasks(a, 4, 64)!.every(m => m === 0xfff)).toBe(true);
  });
  it('moves and snaps notes where the chord changed', () => {
    const b = { ...a, chords: [ch(2, 'm9', 0, 32), ch(7, 'dom7', 32, 32)] };
    const map = scaleMap(b, 4)!;
    expect(quantizeNote(map, 4, 62)).toBe(62); // bar one is unchanged
    expect(quantizeNote(map, 40, 58)).toBe(55); // B♭ root → G root
    expect(quantizeNote(map, 40, 62)).toBe(59); // D (3rd of B♭) → B (3rd of G7)
  });
  it('loops over both chord-track lengths', () => {
    const b = { ...a, chords: [ch(7, 'min', 0, 16)] };
    expect(scaleMap(b, 1)).toHaveLength(64);
    expect(quantizeNote(scaleMap(b, 1)!, 40, 58)).toBe(55);
  });
});

