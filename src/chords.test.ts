import { describe, expect, it } from 'vitest';
import { arpCycle, arpSequence, loadArp, newArp, type ArpState } from './arp';
import { chordNotes, placeChord, progressionFamily, progressionIndex, PROGRESSIONS, progressionChords, type ArpChord } from './chords';

const chord = (start: number, length: number, root: number, quality: ArpChord['quality'] = 'maj'): ArpChord =>
  ({ start, length, root, quality, inversion: 0 });
const chordArp = (chords: ArpChord[], patch: Partial<ArpState> = {}): ArpState =>
  ({ ...newArp(), input: 'chords', chords, chordKey: 0, chordOctave: 3, ...patch });

describe('arp chord track', () => {
  it('voices chords with inversions from the octave base', () => {
    expect(chordNotes(chord(0, 4, 9, 'min'), 48)).toEqual([57, 60, 64]);
    expect(chordNotes({ ...chord(0, 4, 0, 'maj7'), inversion: 1 }, 48)).toEqual([52, 55, 59, 60]);
  });
  it('trims and splits overlapped chords when placing one', () => {
    const out = placeChord([chord(0, 16, 0)], chord(4, 4, 7));
    expect(out.map(c => [c.start, c.length, c.root])).toEqual([[0, 4, 0], [4, 4, 7], [8, 8, 0]]);
  });
  it('restarts the pattern per chord, rests in gaps and loops over the sequence length', () => {
    const seq = arpSequence(chordArp([chord(0, 6, 0), chord(8, 8, 7)]), [], 1);
    expect(seq).toEqual([48, 52, 55, 48, 52, 55, -1, -1, 55, 59, 62, 55, 59, 62, 55, 59]);
    expect(arpSequence(chordArp([chord(0, 16, 0), chord(16, 16, 5)]), [], 2)).toHaveLength(32);
  });
  it('realigns odd rates with the chord track in multiples of 16 steps', () => {
    for (const rate of [.125, .25, .5, 1, 1/3, 1/6, .375, .75]) for (const bars of [1, 2, 3, 4]) {
      const n = arpCycle(rate, bars);
      expect(n % 16).toBe(0);
      expect(Math.abs(n * rate * 4 / (bars * 16) % 1)).toBeLessThan(1e-6);
    }
    expect(arpCycle(.375, 4)).toBe(128);
  });
  it('keeps the 16-step stored-note loop and persists chord settings', () => {
    expect(arpSequence({ ...newArp(), input: 'stored' }, [50, 53], 4)).toHaveLength(16);
    const store = new Map<string, string>();
    Object.assign(globalThis, { localStorage: { getItem: (k: string) => store.get(k) ?? null, setItem: (k: string, v: string) => store.set(k, v) } });
    localStorage.setItem('arp-chords-test', JSON.stringify(chordArp([chord(0, 64, 2, 'm9')], { chordKey: 2 })));
    const a = loadArp('arp-chords-test');
    expect([a.input, a.chordKey, a.chords[0].quality]).toEqual(['chords', 2, 'm9']);
  });
  it('fits every progression preset into four bars', () => {
    for (const p of PROGRESSIONS) {
      const { chords, bars } = progressionChords(p, 0);
      const last = chords[chords.length - 1];
      expect(last.start + last.length).toBe(bars * 16);
    }
  });

  it('groups every progression and recognises it in any key', () => {
    PROGRESSIONS.forEach((p, i) => {
      expect(progressionFamily(p)).not.toBe('OTHER');
      for (const key of [0, 5, 11]) {
        const { chords } = progressionChords(p, key);
        expect(progressionIndex(chords, key)).toBe(i);
        expect(progressionIndex(chords.map(c => ({ ...c, inversion: 1 })), key)).toBe(i);
      }
    });
    expect(progressionIndex([chord(0, 3, 0)], 0)).toBe(-1);
  });
});
