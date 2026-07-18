import { describe, expect, it } from 'vitest';
import type { ClipLibraryEntry } from './clipLibrary';
import { FACTORY_CLIP_LIBRARY } from './clipLibrary.gen';
import {
  filterClipLibrary,
  loadLibraryClipIntoSession,
  prepareLibraryClip,
  resolveVisibleClipSelection,
  rootTransposeSemitones,
  transposeNotePattern,
} from './clipLibraryActions';
import { factorySession } from './factory';
import { b64ToBytes, bytesToB64, emptyClipBytes, noteIdx } from './protocol';

const byMachine = (machine: ClipLibraryEntry['machine']) =>
  FACTORY_CLIP_LIBRARY.find((clip) => clip.machine === machine)!;

describe('clip-library browsing', () => {
  it('filters by machine and facets while preserving factory order', () => {
    const result = filterClipLibrary(FACTORY_CLIP_LIBRARY, {
      machine: 'DR1', families: ['techno'], energies: [4, 5],
    });
    expect(result.length).toBeGreaterThan(0);
    expect(result.every((clip) => clip.machine === 'DR1' && clip.family === 'techno')).toBe(true);
    expect(result.map((clip) => clip.id)).toEqual(
      FACTORY_CLIP_LIBRARY.filter((clip) => result.includes(clip)).map((clip) => clip.id),
    );
  });

  it('searches names, ids and taxonomy case-insensitively', () => {
    const clip = FACTORY_CLIP_LIBRARY.find((entry) => entry.machine === 'BL1')!;
    expect(filterClipLibrary(FACTORY_CLIP_LIBRARY, { query: clip.name.toUpperCase() })).toContain(clip);
    expect(filterClipLibrary(FACTORY_CLIP_LIBRARY, { query: clip.role })).toContain(clip);
  });

  it('matches every selected tag by default and supports any-tag discovery', () => {
    const entries = FACTORY_CLIP_LIBRARY.filter((clip) => clip.tags.length >= 2);
    const clip = entries[0];
    expect(filterClipLibrary(entries, { tags: clip.tags.slice(0, 2) })).toContain(clip);
    expect(filterClipLibrary(entries, { tags: [clip.tags[0], 'glitchy'], tagMatch: 'any' })).toContain(clip);
  });

  it('never resolves a remembered selection outside the visible filtered result', () => {
    const first = { id: 'first' }, second = { id: 'second' };
    expect(resolveVisibleClipSelection([first, second], 'second')).toBe(second);
    expect(resolveVisibleClipSelection([first], 'second')).toBe(first);
    expect(resolveVisibleClipSelection([], 'second')).toBeUndefined();
    expect(resolveVisibleClipSelection([first, second], 'second')).toBe(second);
  });
});

describe('clip-library transposition', () => {
  it('chooses the shortest pitch-class movement', () => {
    expect(rootTransposeSemitones(11, 1)).toBe(2);
    expect(rootTransposeSemitones(1, 11)).toBe(-2);
    expect(rootTransposeSemitones(0, 6)).toBe(6);
  });

  it('transposes active notes, preserves flags/rests and octave-folds boundaries', () => {
    const bytes = emptyClipBytes('BL1', 1);
    let o = noteIdx(0, 0);
    bytes[o] = 11; bytes[o + 1] = 0x80 | 11; bytes[o + 2] = 2; // sliding B+1, active/accent, two steps
    o = noteIdx(0, 1);
    bytes[o + 1] = 4; bytes[o + 2] = 1; // rest metadata remains untouched
    const next = transposeNotePattern(bytes, 2);
    expect(Array.from(next.slice(0, 3))).toEqual([11, 0x80 | 1, 2]); // sliding C#+1 after octave fold
    expect(Array.from(next.slice(3, 6))).toEqual([4, 4, 1]);
    expect(bytes[1]).toBe(0x80 | 11); // input is immutable
  });

  it('folds a top-octave B upward by one octave without jumping to the bottom octave', () => {
    const bytes = emptyClipBytes('BL1', 1);
    bytes.set([1, 11, 2], noteIdx(0, 0));
    expect(Array.from(transposeNotePattern(bytes, 1).slice(0, 3))).toEqual([1, 0, 2]);
  });

  it('only applies a target root to transposable melodic entries', () => {
    const melodic = byMachine('BL1');
    const shifted = prepareLibraryClip(melodic, { targetRoot: (melodic.root! + 2) % 12 });
    expect(shifted.transposedBy).toBe(2);
    expect(shifted.pattern).not.toBe(melodic.pattern);
    const drum = byMachine('DR1');
    expect(prepareLibraryClip(drum, { targetRoot: 5 }).pattern).toBe(drum.pattern);
  });

  it('supports a direct semitone offset for browser loading', () => {
    const melodic = byMachine('WT1');
    const shifted = prepareLibraryClip(melodic, { semitones: -3 });
    expect(shifted.transposedBy).toBe(-3);
    expect(shifted.pattern).not.toBe(melodic.pattern);
  });
});

describe('loading a library clip', () => {
  it('replaces only the focused cell, keeps patches and uses the library name', () => {
    const session = factorySession();
    const sceneIndex = 0;
    const trackIndex = 1;
    const entry = byMachine('BL1');
    const oldPatch = session.tracks[trackIndex].patch;
    const otherScenes = session.scenes.filter((_, i) => i !== sceneIndex);
    const loaded = loadLibraryClipIntoSession(session, sceneIndex, trackIndex, entry);

    expect(loaded.session.scenes[sceneIndex].clips[trackIndex]).toEqual({
      name: entry.name, bars: entry.bars, pattern: entry.pattern,
    });
    expect(loaded.session.tracks[trackIndex].patch).toBe(oldPatch);
    expect(loaded.session.tracks).toBe(session.tracks);
    otherScenes.forEach((scene, i) => {
      const originalIndex = i < sceneIndex ? i : i + 1;
      expect(loaded.session.scenes[originalIndex]).toBe(scene);
    });
    expect(session.scenes[sceneIndex]).not.toBe(loaded.session.scenes[sceneIndex]);
  });

  it('creates an empty focused cell and clears its stale pass-through flag', () => {
    const session = factorySession();
    const sceneIndex = session.scenes.findIndex((scene) => scene.clips[0] === null);
    session.scenes[sceneIndex] = { ...session.scenes[sceneIndex], pass: [0, 2] };
    const entry = byMachine('DR1');
    const loaded = loadLibraryClipIntoSession(session, sceneIndex, 0, entry);
    expect(loaded.session.scenes[sceneIndex].clips[0]?.name).toBe(entry.name);
    expect(loaded.session.scenes[sceneIndex].pass).toEqual([2]);
  });

  it('returns the exact loaded bytes for an immediate live hot-swap', () => {
    const entry = byMachine('WT1');
    const targetRoot = (entry.root! + 5) % 12;
    const loaded = loadLibraryClipIntoSession(factorySession(), 0, 2, entry, { targetRoot });
    expect(loaded.transposedBy).toBe(5);
    expect(bytesToB64(loaded.bytes)).toBe(loaded.session.scenes[0].clips[2]!.pattern);
    expect(b64ToBytes(loaded.pattern)).toEqual(loaded.bytes);
  });

  it('rejects loading a clip into the wrong machine', () => {
    expect(() => loadLibraryClipIntoSession(factorySession(), 0, 0, byMachine('BL1')))
      .toThrow('is for BL1, not DR1');
  });
});
