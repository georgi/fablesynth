import { describe, expect, it } from 'vitest';
import { compileClipArp, compileClipScale, followClip, linkChordWrites, newClipArp, sceneChords, sceneChordWrites, validClipArp, type ClipArp } from './clipArp';
import { PROGRESSIONS, progressionChords } from '../chords';
import { factorySession } from './factory';
import { copySession } from './sessionPresets';
import { embedSessionPatches } from './sessionExport';
import { bytesToB64, emptyClipBytes, validateSession } from './protocol';

describe('portable clip arp data', () => {
  it('round-trips complete settings and deep copies them on recall and export', () => {
    const doc = factorySession(); const clip = doc.scenes[2].clips[1]!;
    clip.arp = { ...newClipArp('BL1'), enabled: true };
    clip.arp.settings.slides![3] = true;
    const recalled = copySession(doc), exported = embedSessionPatches(doc);
    expect(validateSession(JSON.parse(JSON.stringify(exported)))).toBeNull();
    expect(exported.scenes[2].clips[1]?.arp).toEqual(clip.arp);
    recalled.scenes[2].clips[1]!.arp!.settings.notes[0] = 99;
    exported.scenes[2].clips[1]!.arp!.settings.slides![3] = false;
    expect(clip.arp.settings.notes[0]).toBe(36); expect(clip.arp.settings.slides![3]).toBe(true);
  });
  it('rejects malformed timing, lanes, live-input state and arp data on drums', () => {
    const arp = newClipArp('WT1');
    expect(validClipArp(arp)).toBe(true);
    for (const patch of [{ rate: NaN }, { notes: [Infinity] }, { hits: [] }, { input: 'keys' }, { gate: 2 }]) {
      expect(validClipArp({ ...arp, settings: { ...arp.settings, ...patch } })).toBe(false);
    }
    const doc = factorySession(); doc.scenes[2].clips[0]!.arp = arp;
    expect(validateSession(doc)).toContain('invalid arpeggiator');
  });
  it('retains settings while disabled without sending an arp to the audio engine', () => {
    const clip = factorySession().scenes[2].clips[1]!;
    clip.arp = newClipArp('BL1'); expect(compileClipArp(clip)).toBeUndefined();
    clip.arp.enabled = true; expect(compileClipArp(clip)?.notes).toHaveLength(16);
  });
});

describe('chord-track clips', () => {
  const chordArp = (patch: Partial<ClipArp['settings']> = {}): ClipArp => ({ enabled: true, settings: { ...newClipArp('WT1').settings, input: 'chords', ...patch } });
  it('validates chord input and compiles one chord cycle over the chord bars', () => {
    expect(validClipArp(chordArp({ chordBars: 2, chordLink: true }))).toBe(true);
    for (const patch of [{ chordBars: 5 }, { chordKey: 12 }, { chords: [{ start: 0, length: 0, root: 0, quality: 'maj', inversion: 0 }] }]) {
      expect(validClipArp(chordArp(patch as Partial<ClipArp['settings']>))).toBe(false);
    }
    const clip = factorySession().scenes[2].clips[2]!;
    clip.arp = chordArp({ rate: .5 }); expect(compileClipArp(clip)?.notes).toHaveLength(32);
    clip.arp = chordArp({ chordBars: 1, rate: .25 }); expect(compileClipArp(clip)?.notes).toHaveLength(16);
  });
  it('adopts the scene progression on joining and shares later edits with linked clips only', () => {
    const doc = factorySession();
    for (const t of [1, 2, 3]) doc.scenes[1].clips[t] = { name: 'ARP', bars: 1, pattern: bytesToB64(emptyClipBytes(t === 1 ? 'BL1' : 'WT1', 1)) };
    const jazz = progressionChords(PROGRESSIONS[20], 2).chords;
    doc.scenes[1].clips[2]!.arp = chordArp({ chordLink: true, chords: jazz, chordKey: 2, chordBars: 4, chordOctave: 4 });
    doc.scenes[1].clips[3]!.arp = chordArp({ chordLink: false });
    // BL-1 joins: it takes the scene's progression but keeps its own octave.
    const joined = linkChordWrites(doc, 1, 1, chordArp({ chordLink: true, chordOctave: 1 }));
    expect(joined.others).toEqual([]);
    expect(joined.arp.settings).toMatchObject({ chords: jazz, chordKey: 2, chordBars: 4, chordOctave: 1 });
    doc.scenes[1].clips[1]!.arp = joined.arp;
    // An edit on a linked clip reaches the other linked clip, not the unlinked one or other scenes.
    const edited = chordArp({ ...joined.arp.settings, chordKey: 7, chordBars: 2 });
    const { others } = linkChordWrites(doc, 1, 1, edited);
    expect(others.map(o => o.t)).toEqual([2]);
    expect(others[0].clip.arp!.settings).toMatchObject({ chordKey: 7, chordBars: 2, chordOctave: 4 });
    expect(doc.scenes[1].clips[2]!.arp!.settings.chordKey).toBe(2);
  });
  it('loads a scene progression into every linked clip of that scene only', () => {
    const doc = factorySession();
    for (const t of [1, 2, 3]) doc.scenes[1].clips[t] = { name: 'ARP', bars: 1, pattern: bytesToB64(emptyClipBytes(t === 1 ? 'BL1' : 'WT1', 1)) };
    doc.scenes[1].clips[1]!.arp = chordArp({ chordLink: true, chordOctave: 1 });
    doc.scenes[1].clips[2]!.arp = chordArp({ chordLink: true, chordOctave: 4 });
    doc.scenes[1].clips[3]!.arp = chordArp({ chordLink: false });
    expect(sceneChords(doc, 1)).toMatchObject({ key: 9, bars: 4 });
    expect(sceneChords(doc, 0)).toBeNull();
    const blues = progressionChords(PROGRESSIONS.find(p => p.mood === 'BLUES')!, 4).chords;
    const writes = sceneChordWrites(doc, 1, { chords: blues, key: 4, bars: 4 });
    expect(writes.map(w => w.t)).toEqual([1, 2]);
    expect(writes[1].clip.arp!.settings).toMatchObject({ chords: blues, chordKey: 4, chordOctave: 4 });
  });
});

describe('FOLLOW clips', () => {
  it('play as written over their own chords and move with a new progression', () => {
    const clip = factorySession().scenes[2].clips[1]!;
    clip.arp = { enabled: false, settings: { ...newClipArp('BL1').settings, input: 'chords', chordKey: 2, chordBars: 1,
      chords: [{ start: 0, length: 16, root: 2, quality: 'min', inversion: 0 }] } };
    const follow = followClip(clip);
    expect(validClipArp(follow.arp)).toBe(true);
    expect(compileClipScale(follow)!.flat().every(x => x === 0)).toBe(true);
    // The scene moves to G minor: every note moves up a fourth.
    const moved = { ...follow, arp: { ...follow.arp!, settings: { ...follow.arp!.settings, chords: [{ start: 0, length: 16, root: 7, quality: 'min' as const, inversion: 0 }] } } };
    const map = compileClipScale(moved)!;
    expect(map[0][2]).toBe(5); // D → G
    expect(map[0][9]).toBe(5); // A → D
  });
});

