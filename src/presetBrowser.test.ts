import { describe, expect, it } from 'vitest';
import { PARAMS } from './params';
import { FACTORY_PRESETS } from './presets';
import { factoryPresetTags } from './presetTags';
import { browserEntries, filterEntries, favoriteKey, varyParams, type BrowserFilter } from './presetBrowser';

const none: BrowserFilter = { query: '', roles: new Set(), tags: new Set(), favoritesOnly: false, userOnly: false };

describe('preset browser', () => {
  it('tags every factory preset', () => {
    const missing = FACTORY_PRESETS.filter((p) => !factoryPresetTags(p.name)).map((p) => p.name);
    expect(missing).toEqual([]);
  });

  it('keeps factory order and appends user presets with load keys', () => {
    const user = [{ name: 'MINE', params: {} }];
    const e = browserEntries(user);
    expect(e.slice(0, 3).map((x) => x.value)).toEqual(['f0', 'f1', 'f2']);
    expect(e[e.length - 1]).toMatchObject({ value: 'u0', name: 'MINE', user: true, role: null });
  });

  it('combines role, character, search, and favorite filters', () => {
    const e = browserEntries([]);
    const pads = filterEntries(e, { ...none, roles: new Set(['pad']) }, new Set());
    expect(pads.length).toBeGreaterThan(0);
    expect(pads.every((x) => x.role === 'pad')).toBe(true);

    const darkPads = filterEntries(e, { ...none, roles: new Set(['pad']), tags: new Set(['dark']) }, new Set());
    expect(darkPads.every((x) => x.tags.includes('dark'))).toBe(true);
    expect(darkPads.length).toBeLessThan(pads.length);

    expect(filterEntries(e, { ...none, query: 'velvet' }, new Set()).map((x) => x.name)).toEqual(['VELVET PAD']);

    const fav = new Set([favoriteKey({ name: 'ACID LINE', user: false })]);
    expect(filterEntries(e, { ...none, favoritesOnly: true }, fav).map((x) => x.name)).toEqual(['ACID LINE']);
  });

  it('varies a preset within parameter ranges and keeps pitch, levels, and FX', () => {
    const base = browserEntries([]).find((x) => x.name === 'VELVET PAD')!.params;
    let seed = 1;
    const rand = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
    const v = varyParams(base, rand);
    const changed = Object.keys(base).filter((k) => v[k] !== base[k]);
    expect(changed).toContain('filter.cutoff');
    for (const k of changed) {
      const d = PARAMS[k];
      expect(v[k]).toBeGreaterThanOrEqual(d.min!);
      expect(v[k]).toBeLessThanOrEqual(d.max!);
      expect(k).not.toMatch(/\.(oct|semi|fine|level|table)$|^fx\.|^master\./);
    }
  });
});
