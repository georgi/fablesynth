import { describe, expect, it } from 'vitest';
import { filterEntries, favoriteKey, type BrowserFilter } from '../shared/presetBrowser';
import { BASS_PARAMS } from './params';
import { FACTORY_PATCHES } from './patches';
import { factoryPatchTags } from './patchTags';
import { bassBrowserEntries, bassVaryParams } from './patchBrowser';
import { useBassStore } from './store';

if (typeof localStorage === 'undefined') {
  const store = new Map<string, string>();
  Object.defineProperty(globalThis, 'localStorage', {
    value: {
      get length() { return store.size; },
      clear: () => store.clear(),
      getItem: (key: string) => store.get(key) ?? null,
      key: (index: number) => [...store.keys()][index] ?? null,
      removeItem: (key: string) => store.delete(key),
      setItem: (key: string, value: string) => store.set(key, value),
    } satisfies Storage,
  });
}

const none: BrowserFilter = { query: '', roles: new Set(), tags: new Set(), favoritesOnly: false, userOnly: false };

describe('bass patch browser', () => {
  it('tags every factory patch', () => {
    const missing = FACTORY_PATCHES.filter((p) => !factoryPatchTags(p.name)).map((p) => p.name);
    expect(missing).toEqual([]);
  });

  it('keeps factory order and appends user patches with load keys', () => {
    const user = [{ name: 'MINE', params: {}, patterns: [], chain: [0] }];
    const e = bassBrowserEntries(user);
    expect(e.slice(0, 3).map((x) => x.value)).toEqual(['f0', 'f1', 'f2']);
    expect(e[e.length - 1]).toMatchObject({ value: 'u0', name: 'MINE', user: true, role: null });
  });

  it('combines role, character, search, user, and favorite filters', () => {
    const e = bassBrowserEntries([{ name: 'MINE', params: {}, patterns: [], chain: [0] }]);
    const subs = filterEntries(e, { ...none, roles: new Set(['sub']) }, new Set());
    expect(subs.length).toBeGreaterThan(0);
    expect(subs.every((x) => x.role === 'sub')).toBe(true);

    const darkSubs = filterEntries(e, { ...none, roles: new Set(['sub']), tags: new Set(['dark']) }, new Set());
    expect(darkSubs.every((x) => x.tags.includes('dark'))).toBe(true);
    expect(darkSubs.length).toBeLessThan(subs.length);

    expect(filterEntries(e, { ...none, query: 'reese' }, new Set()).map((x) => x.name)).toEqual(['REESE MONO']);
    expect(filterEntries(e, { ...none, userOnly: true }, new Set()).map((x) => x.name)).toEqual(['MINE']);
    const fav = new Set([favoriteKey({ name: 'ACID LINE', user: false })]);
    expect(filterEntries(e, { ...none, favoritesOnly: true }, fav).map((x) => x.name)).toEqual(['ACID LINE']);
  });

  it('varies a patch within parameter ranges and keeps pitch, levels, and FX', () => {
    const base = bassBrowserEntries([]).find((x) => x.name === 'REESE MONO')!.params;
    let seed = 1;
    const rand = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
    const v = bassVaryParams(base, rand);
    const changed = Object.keys(base).filter((k) => v[k] !== base[k]);
    expect(changed).toContain('flt.cut');
    for (const k of changed) {
      const d = BASS_PARAMS[k];
      expect(v[k]).toBeGreaterThanOrEqual(d.min!);
      expect(v[k]).toBeLessThanOrEqual(d.max!);
      expect(k).not.toMatch(/\.(oct|tune|fine|level|volume|bpm|swing|shape|table|type)$|^fx\.|^master\.|^seq\.|^sub\.|\.unison$/);
    }
  });

  it('restores params, patterns, chain, patch value, and dirty from a snapshot', () => {
    const s = useBassStore.getState();
    s.loadPatchByValue('f2'); // NEON SQUELCH: chain of two patterns
    s.setParam('flt.cut', 777);
    const snap = useBassStore.getState().captureSound();
    expect(snap.dirty).toBe(true);

    useBassStore.getState().loadPatchByValue('f5');
    expect(useBassStore.getState().patchValue).toBe('f5');
    useBassStore.getState().restoreSound(snap);

    const after = useBassStore.getState();
    expect(after.patchValue).toBe('f2');
    expect(after.dirty).toBe(true);
    expect(after.params).toEqual(snap.params);
    expect(after.params['flt.cut']).toBe(777);
    expect(Array.from(after.patterns)).toEqual(Array.from(snap.patterns));
    expect(after.chain).toEqual(snap.chain);
    expect(after.sequenceRhythm).toEqual(snap.sequenceRhythm);
    expect(after.editPattern).toBe(snap.editPattern);

    // later edits must not mutate the snapshot
    after.setParam('flt.cut', 100);
    expect(snap.params['flt.cut']).toBe(777);
  });

  it('applyVariant sets params and marks the patch dirty', () => {
    const s = useBassStore.getState();
    s.loadPatchByValue('f0');
    expect(useBassStore.getState().dirty).toBe(false);
    s.applyVariant({ ...useBassStore.getState().params, 'flt.cut': 500 });
    expect(useBassStore.getState().params['flt.cut']).toBe(500);
    expect(useBassStore.getState().dirty).toBe(true);
    expect(useBassStore.getState().patchValue).toBe('f0');
  });
});
