import { describe, expect, it } from 'vitest';
import { arpCompile, newArp, type ArpState } from './arp';
import type { ArpChord } from './chords';
import { COMP_BASS, COMP_LOW, COMP_MID, COMP_TOP, compSequence, compVoices, formatCompSteps, parseCompSteps, spreadVoicing, strumDown } from './comp';
import { COMP_FAMILIES, COMP_LIBRARY, compLibraryIndex } from './compLibrary';
import { loadCompEntry } from './components/panels/CompBrowser';
import { compileClipArp, validClipArp } from './seq/clipArp';

const chord = (start: number, length: number, root: number, quality: ArpChord['quality'] = 'maj'): ArpChord =>
  ({ start, length, root, quality, inversion: 0 });
const comp = (steps: string, patch: Partial<ArpState> = {}): ArpState =>
  ({ ...newArp(), style: 'comp', ...parseCompSteps(steps), ...patch });

describe('chord comping', () => {
  it('selects bass, low, middle and top voices for any chord size', () => {
    expect(compVoices(COMP_LOW | COMP_MID | COMP_TOP, [48, 52, 55], 36)).toEqual([48, 52, 55]);
    expect(compVoices(COMP_MID, [48, 52, 55, 59, 62], 36)).toEqual([52, 55, 59]);
    expect(compVoices(COMP_BASS | COMP_TOP, [48], 36)).toEqual([36, 48]);
    expect(compVoices(COMP_LOW, [], 36)).toEqual([]);
  });
  it('strikes voicings, holds ties and gates the end of each run', () => {
    const s = compSequence(comp('BC - - . U . . . . . . . . . . -', { input: 'chords', chords: [chord(0, 16, 0)] }), [], 1);
    expect(s.stack[0]).toEqual([36, 48, 52, 55]);
    expect(s.lens.slice(0, 5)).toEqual([3, 0, 0, 1, 1]);
    expect(s.stack[4]).toEqual([52, 55]);
    // A trailing tie after a rest stays silent.
    expect([s.stack[15], s.lens[15]]).toEqual([[], 1]);
  });
  it('re-strikes a held voicing on a chord change and rests in gaps', () => {
    const s = compSequence(comp('BC - - - - - - - - - - - - - - -', { input: 'chords', chords: [chord(0, 8, 0), chord(8, 4, 7)] }), [], 1);
    expect([s.lens[0], s.stack[8], s.lens[8], s.stack[12]]).toEqual([8, [43, 55, 59, 62], 4, []]);
  });
  it('wraps a tie on the first step to the end of the loop', () => {
    const s = compSequence(comp('- . . . . . . . . . . . . . . C'), [60, 64, 67], 1);
    expect([s.stack[15], s.lens[15], s.lens[0]]).toEqual([[60, 64, 67], 2, 0]);
  });
  it('compiles stored notes, clip arps and every preset', () => {
    expect(arpCompile(comp('B . . . . . . . . . . . . . . .'), [64, 60, 67], 1).stack![0]).toEqual([48]);
    expect(arpCompile(newArp(), [60], 1).stack).toBeUndefined();
    const clip = { enabled: true, settings: comp('C . . . . . . . . . . . . . . .', { notes: [60, 63, 67] }) };
    expect(validClipArp(clip)).toBe(true);
    expect(compileClipArp({ arp: clip } as never)!.stack![0]).toEqual([60, 63, 67]);
  });
  it('spreads voicings and orders strums', () => {
    expect(spreadVoicing([48, 52, 55], 'drop2')).toEqual([40, 48, 55]);
    expect(spreadVoicing([48, 52, 55, 59], 'drop24')).toEqual([36, 43, 52, 59]);
    expect(spreadVoicing([48, 52, 55, 59], 'open')).toEqual([48, 55, 64, 71]);
    expect(spreadVoicing([48, 52], 'open')).toEqual([48, 52]);
    expect([strumDown('alt', 0, false), strumDown('alt', 2, false), strumDown('alt', 1, true), strumDown('up', 0, false)]).toEqual([true, false, false, false]);
    const s = compSequence(comp('C . C . . . . . . . . . . . . .', { strumDir: 'alt', voicing: 'drop2' }), [60, 64, 67], 1);
    expect([s.stack[0], s.stack[2], s.notes[0]]).toEqual([[67, 60, 52], [52, 60, 67], 52]);
  });
});

describe('comp pattern library', () => {
  it('holds hundreds of distinct, playable patterns across every family', () => {
    expect(COMP_LIBRARY.length).toBeGreaterThan(400);
    const keys = COMP_LIBRARY.map(e => `${formatCompSteps(e.comp, e.ties, e.accents)}|${e.rate}`);
    expect(new Set(keys).size).toBe(keys.length);
    expect(new Set(COMP_LIBRARY.map(e => e.name)).size).toBe(COMP_LIBRARY.length);
    for (const f of COMP_FAMILIES) expect(COMP_LIBRARY.filter(e => e.family === f.name).length).toBeGreaterThanOrEqual(f.grooves.length * 3);
    for (const e of COMP_LIBRARY) {
      expect(e.comp).toHaveLength(16);
      expect(e.comp.some((m, i) => m && !e.ties[i])).toBe(true);
      expect(compSequence({ ...newArp(), ...loadCompEntry(e) } as ArpState, [60, 64, 67], 1).stack.some(x => x.length)).toBe(true);
    }
  });
  it('round-trips step notation and identifies loaded entries', () => {
    COMP_LIBRARY.forEach((e, i) => {
      expect(parseCompSteps(formatCompSteps(e.comp, e.ties, e.accents))).toEqual({ comp: e.comp, ties: e.ties, accents: e.accents });
      if (i % 37 === 0) expect(compLibraryIndex({ ...newArp(), ...loadCompEntry(e) } as ArpState)).toBe(i);
    });
  });
  it('accepts strum and voicing in SQ-4 clips', () => {
    const settings = { ...newArp(), ...loadCompEntry(COMP_LIBRARY[0]), notes: [60, 64, 67] } as ArpState;
    expect(validClipArp({ enabled: true, settings })).toBe(true);
    expect(validClipArp({ enabled: true, settings: { ...settings, strum: 99 } })).toBe(false);
    expect(compileClipArp({ arp: { enabled: true, settings: { ...settings, strum: 12 } } } as never)!.strum).toBe(12);
  });
});
