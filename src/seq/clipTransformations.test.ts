import { describe, expect, it } from 'vitest';
import type { RuntimeClipLibraryEntry } from './clipLibrary';
import { exportSqclip, type SourcedClip } from './clipLibraryStorage';
import { DR_REMAP_ALTERNATE_KIT, transformClip } from './clipTransformations';
import { dr1Idx, emptyClipBytes, noteIdx, type MachineId } from './protocol';

function clip(machine: MachineId, bars = 1): RuntimeClipLibraryEntry {
  return {
    id: 'source', name: 'SOURCE', machine, bars, pattern: emptyClipBytes(machine, bars),
    family: 'experimental', role: machine === 'DR1' ? 'experimental' : machine === 'BL1' ? 'minimal' : 'texture',
    energy: 3, tags: [], transpose: machine !== 'DR1', ...(machine !== 'DR1' ? { root: 0, scale: 'minor' } : {}),
  };
}

describe('non-destructive clip transformations', () => {
  it('rotates and reverses full timelines without touching the source', () => {
    const source = clip('DR1'); source.pattern[dr1Idx(0, 0, 0)] = 2;
    const rotated = transformClip(source, { kind: 'rotate', steps: -1 });
    expect(rotated.pattern[dr1Idx(0, 0, 15)]).toBe(2);
    const reversed = transformClip(source, { kind: 'reverse' });
    expect(reversed.pattern[dr1Idx(0, 0, 15)]).toBe(2);
    expect(source.pattern[dr1Idx(0, 0, 0)]).toBe(2);
  });

  it('transposes note clips with octave folding and rejects drums', () => {
    const source = clip('BL1'); const o = noteIdx(0, 0);
    source.pattern.set([7, 11, 2], o);
    const shifted = transformClip(source, { kind: 'transpose', semitones: -2 });
    expect(Array.from(shifted.pattern.slice(o, o + 3))).toEqual([7, 9, 2]);
    expect(shifted.root).toBe(10);
    const exported = JSON.parse(exportSqclip([{ ...shifted, source: 'USER' } as SourcedClip])) as {
      clips: Array<{ root?: number }>;
    };
    expect(exported.clips[0].root).toBe(10);
    expect(() => transformClip(clip('DR1'), { kind: 'transpose', semitones: 2 })).toThrow('note clips');
  });

  it('preserves an absent root when transposing an unrooted note pattern', () => {
    const source = clip('WT1');
    delete source.root; delete source.scale; source.transpose = false;
    const shifted = transformClip(source, { kind: 'transpose', semitones: 7 });
    expect(shifted.root).toBeUndefined();
    expect(Object.prototype.hasOwnProperty.call(shifted, 'root')).toBe(false);
  });

  it('doubles into adjacent rests and halves alternating events per lane', () => {
    const source = clip('DR1');
    for (const step of [0, 4, 8]) source.pattern[dr1Idx(0, 2, step)] = 1;
    const doubled = transformClip(source, { kind: 'density', factor: 2 });
    expect([0, 1, 4, 5, 8, 9].map((s) => doubled.pattern[dr1Idx(0, 2, s)])).toEqual([1, 1, 1, 1, 1, 1]);
    const halved = transformClip(source, { kind: 'density', factor: 0.5 });
    expect([0, 4, 8].map((s) => halved.pattern[dr1Idx(0, 2, s)])).toEqual([1, 0, 1]);
  });

  it('shifts accents to active events and humanizes repeatably', () => {
    const source = clip('WT1');
    source.pattern.set([3, 0, 1], noteIdx(0, 0));
    source.pattern.set([1, 4, 1], noteIdx(0, 4));
    const shifted = transformClip(source, { kind: 'accent-shift', steps: 1 });
    expect(shifted.pattern[noteIdx(0, 0)] & 2).toBe(0);
    expect(shifted.pattern[noteIdx(0, 4)] & 2).toBe(2);
    const a = transformClip(source, { kind: 'humanize', seed: 42, amount: 1 });
    const b = transformClip(source, { kind: 'humanize', seed: 42, amount: 1 });
    expect(a.pattern).toEqual(b.pattern);
    expect(a.pattern[noteIdx(0, 0)] & 1).toBe(1);
    expect(a.pattern).not.toEqual(source.pattern);
  });

  it('extracts a bar and extends or truncates by repeating bars', () => {
    const source = clip('DR1', 2);
    source.pattern[dr1Idx(0, 0, 0)] = 1;
    source.pattern[dr1Idx(1, 0, 0)] = 2;
    const extracted = transformClip(source, { kind: 'extract-bar', bar: 1 });
    expect(extracted.bars).toBe(1);
    expect(extracted.pattern[dr1Idx(0, 0, 0)]).toBe(2);
    const repeated = transformClip(source, { kind: 'repeat', bars: 3 });
    expect(repeated.bars).toBe(3);
    expect(repeated.pattern[dr1Idx(2, 0, 0)]).toBe(1);
  });

  it('semantically remaps DR lanes without changing event values', () => {
    const source = clip('DR1'); source.pattern[dr1Idx(0, 0, 3)] = 2;
    const mapped = transformClip(source, { kind: 'dr-lane-remap', lanes: DR_REMAP_ALTERNATE_KIT });
    expect(mapped.pattern[dr1Idx(0, 1, 3)]).toBe(2);
    expect(mapped.pattern[dr1Idx(0, 0, 3)]).toBe(0);
    expect(() => transformClip(clip('BL1'), { kind: 'dr-lane-remap', lanes: DR_REMAP_ALTERNATE_KIT })).toThrow('DR1');
  });
});
