import { describe, it, expect, beforeEach } from 'vitest';
import {
  FACTORY_PATCHES, PATCH_FIELDS, applyPatchToParams, extractPatch,
  loadUserPatches, saveUserPatch, patchOptions,
} from './patches';
import { DRUM_PARAMS, PAD_FIELDS, defaultDrumParams, pad } from './params';

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

describe('patches', () => {
  beforeEach(() => localStorage.clear());

  it('factory bank has at least 16 patches with unique uppercase names', () => {
    expect(FACTORY_PATCHES.length).toBeGreaterThanOrEqual(16);
    const names = FACTORY_PATCHES.map((p) => p.name);
    expect(new Set(names).size).toBe(names.length);
    for (const name of names) expect(name).toBe(name.toUpperCase());
  });

  it('every factory param key is a PAD_FIELD, never out/choke, and in range', () => {
    for (const patch of FACTORY_PATCHES) {
      expect(patch.v).toBe(1);
      for (const [field, v] of Object.entries(patch.params)) {
        expect(PAD_FIELDS, `${patch.name}: ${field}`).toContain(field);
        expect(field).not.toBe('out');
        expect(field).not.toBe('choke');
        const d = DRUM_PARAMS[pad(0, field)];
        expect(d, `${patch.name}: ${field}`).toBeDefined();
        if (d.min !== undefined) {
          expect(v, `${patch.name}: ${field}`).toBeGreaterThanOrEqual(d.min!);
          expect(v, `${patch.name}: ${field}`).toBeLessThanOrEqual(d.max!);
        }
        if (d.options) {
          expect(Number.isInteger(v), `${patch.name}: ${field}`).toBe(true);
          expect(v).toBeGreaterThanOrEqual(0);
          expect(v).toBeLessThan(d.options.length);
        }
      }
    }
  });

  it('factory patches reference only built-in tables (index <= 14)', () => {
    for (const patch of FACTORY_PATCHES) {
      for (const field of ['oscA.table', 'oscB.table'] as const) {
        const v = patch.params[field];
        if (v !== undefined) expect(v, `${patch.name}: ${field}`).toBeLessThanOrEqual(14);
      }
    }
  });

  it('pitches kick and snare oscillators one octave lower', () => {
    const expected: Record<string, number> = {
      'BD DEEP': -26, 'BD PUNCH': -19, 'BD SUB': -34, 'BD 808': -24,
      'SD CRACK': -12, 'SD RIM': 0,
    };
    for (const [name, tune] of Object.entries(expected)) {
      expect(FACTORY_PATCHES.find((patch) => patch.name === name)?.params['oscA.tune']).toBe(tune);
    }
  });

  it('tunes toms seven semitones deeper with shorter tails', () => {
    const expected: Record<string, [number, number]> = {
      'TM LO': [-19, 0.28], 'TM MID': [-12, 0.24], 'TM HI': [-5, 0.20],
    };
    for (const [name, [tune, decay]] of Object.entries(expected)) {
      const patch = FACTORY_PATCHES.find((entry) => entry.name === name);
      expect(patch?.params['oscA.tune']).toBe(tune);
      expect(patch?.params['aenv.dec']).toBeCloseTo(decay);
    }
  });

  it('apply-then-extract round-trips a pad sound', () => {
    const padI = 5;
    const params = defaultDrumParams();
    const patch = FACTORY_PATCHES[0];
    Object.assign(params, applyPatchToParams(params, padI, patch));
    const extracted = extractPatch(params, padI, patch.name);
    // Extracted snapshots all included fields; each must equal defaults ∪ overrides.
    for (const field of PATCH_FIELDS) {
      const expected = patch.params[field] ?? DRUM_PARAMS[pad(padI, field)].def;
      expect(extracted.params[field], field).toBe(expected);
    }
    // Re-applying the extracted patch reproduces the exact same pad state.
    const reapplied = applyPatchToParams(params, padI, extracted);
    for (const [id, v] of Object.entries(reapplied)) expect(params[id]).toBe(v);
  });

  it('applying a patch fully resets non-default leftovers from a previous sound', () => {
    const padI = 3;
    const params = defaultDrumParams();
    // Previous sound left a bunch of non-default state behind.
    params[pad(padI, 'noise.level')] = 0.9;
    params[pad(padI, 'flt.on')] = 1;
    params[pad(padI, 'flt.cut')] = 300;
    params[pad(padI, 'mod2.src')] = 3;
    params[pad(padI, 'mod2.dst')] = 5;
    params[pad(padI, 'mod2.amt')] = -0.7;
    const kick = FACTORY_PATCHES.find((p) => p.name === 'BD DEEP')!;
    Object.assign(params, applyPatchToParams(params, padI, kick));
    // BD DEEP doesn't touch noise/filter/mod2 -> they must be back at defaults.
    expect(params[pad(padI, 'noise.level')]).toBe(DRUM_PARAMS[pad(padI, 'noise.level')].def);
    expect(params[pad(padI, 'flt.on')]).toBe(DRUM_PARAMS[pad(padI, 'flt.on')].def);
    expect(params[pad(padI, 'flt.cut')]).toBe(DRUM_PARAMS[pad(padI, 'flt.cut')].def);
    expect(params[pad(padI, 'mod2.src')]).toBe(0);
    expect(params[pad(padI, 'mod2.dst')]).toBe(0);
    expect(params[pad(padI, 'mod2.amt')]).toBe(0);
    // ...while the patch's own overrides landed.
    expect(params[pad(padI, 'oscA.tune')]).toBe(-26);
    expect(params[pad(padI, 'penv.amt')]).toBe(24);
  });

  it('out and choke are preserved through apply and never extracted', () => {
    const padI = 7;
    const params = defaultDrumParams();
    params[pad(padI, 'out')] = 2;
    params[pad(padI, 'choke')] = 1;
    const entries = applyPatchToParams(params, padI, FACTORY_PATCHES[8]);
    expect(entries[pad(padI, 'out')]).toBeUndefined();
    expect(entries[pad(padI, 'choke')]).toBeUndefined();
    Object.assign(params, entries);
    expect(params[pad(padI, 'out')]).toBe(2);
    expect(params[pad(padI, 'choke')]).toBe(1);
    const extracted = extractPatch(params, padI, 'X');
    expect(extracted.params['out']).toBeUndefined();
    expect(extracted.params['choke']).toBeUndefined();
  });

  it('apply only touches the target pad', () => {
    const params = defaultDrumParams();
    const entries = applyPatchToParams(params, 4, FACTORY_PATCHES[1]);
    for (const id of Object.keys(entries)) expect(id.startsWith('pad4.')).toBe(true);
    expect(Object.keys(entries)).toHaveLength(PATCH_FIELDS.length);
  });

  it('user patch save/load round-trip; same name overwrites', () => {
    const params = defaultDrumParams();
    params[pad(2, 'oscA.tune')] = -9;
    const patch = extractPatch(params, 2, 'MY SNARE');
    saveUserPatch('MY SNARE', patch);
    saveUserPatch('MY SNARE', patch);
    const loaded = loadUserPatches();
    expect(loaded).toHaveLength(1);
    expect(loaded[0].name).toBe('MY SNARE');
    expect(loaded[0].params['oscA.tune']).toBe(-9);
  });

  it('patchOptions lists factory then user with f/u value keys', () => {
    const user = [extractPatch(defaultDrumParams(), 0, 'MINE')];
    const options = patchOptions(user);
    expect(options).toHaveLength(FACTORY_PATCHES.length + 1);
    expect(options[0]).toEqual({ value: 'f0', name: FACTORY_PATCHES[0].name, group: 'FACTORY' });
    expect(options[options.length - 1]).toEqual({ value: 'u0', name: 'MINE', group: 'USER' });
  });
});
