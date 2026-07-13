import { describe, it, expect, beforeEach } from 'vitest';
import {
  FACTORY_PATCHES, loadUserPatches, patchOptions, patchToState,
  saveUserPatch, stateToPatch,
} from './patches';
import { BASS_PARAMS, defaultBassParams } from './params';
import { getStep, makeEmptyPatterns, NPATTERNS, STEP_STRIDE, STEPS } from './seq';

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

describe('bass patches', () => {
  beforeEach(() => localStorage.clear());

  it('ships 12 distinct factory patches without moving the original programs', () => {
    expect(FACTORY_PATCHES.map((p) => p.name)).toEqual([
      'ACID LINE', 'RUBBER SUB', 'NEON SQUELCH', 'DEEP DUB', 'WAREHOUSE', 'ROUNDHOUSE',
      'METAL PULSE', 'TAPE BASS', 'REESE MONO', 'PLUCKED WIRE', 'DARK CURRENT', 'CLEAN SUB',
    ]);
  });

  it('factory patches only override known params, within range', () => {
    for (const patch of FACTORY_PATCHES) {
      for (const [id, v] of Object.entries(patch.params)) {
        const def = BASS_PARAMS[id];
        expect(def, `${patch.name}: unknown param ${id}`).toBeDefined();
        if (def.options) {
          expect(v).toBeGreaterThanOrEqual(0);
          expect(v).toBeLessThan(def.options.length);
        } else if (def.type !== 'bool') {
          expect(v).toBeGreaterThanOrEqual(def.min as number);
          expect(v).toBeLessThanOrEqual(def.max as number);
        }
      }
      expect(patch.patterns.length).toBe(NPATTERNS * STEPS * STEP_STRIDE);
      expect(patch.chain.length).toBeGreaterThan(0);
    }
  });

  it('patchToState fills every default and clones patterns', () => {
    const state = patchToState(FACTORY_PATCHES[0]);
    const defs = defaultBassParams();
    for (const id of Object.keys(defs)) expect(state.params[id]).toBeDefined();
    // the ACID LINE pattern A starts on an accented root
    const s0 = getStep(state.patterns, 0, 0);
    expect(s0).toMatchObject({ on: true, note: 0, acc: true });
    // slides land where the design mock put them
    expect(getStep(state.patterns, 0, 3).slide).toBe(true);
    expect(getStep(state.patterns, 0, 7).slide).toBe(true);
  });

  it('state → patch → state round-trips', () => {
    const params = defaultBassParams();
    params['flt.cut'] = 999;
    const patterns = makeEmptyPatterns();
    const patch = stateToPatch('MINE', params, patterns, [0, 2]);
    const state = patchToState(patch);
    expect(state.params['flt.cut']).toBe(999);
    expect(state.chain).toEqual([0, 2]);
    expect(Array.from(state.patterns)).toEqual(Array.from(patterns));
  });

  it('user patches persist and replace by name', () => {
    const patch = stateToPatch('SQUELCH', defaultBassParams(), makeEmptyPatterns(), [0]);
    saveUserPatch('SQUELCH', patch);
    saveUserPatch('SQUELCH', { ...patch, params: { ...patch.params, 'flt.res': 0.9 } });
    const loaded = loadUserPatches();
    expect(loaded.length).toBe(1);
    expect(loaded[0].params['flt.res']).toBe(0.9);
  });

  it('patchOptions lists factory then user', () => {
    saveUserPatch('MINE', stateToPatch('MINE', defaultBassParams(), makeEmptyPatterns(), [0]));
    const options = patchOptions(loadUserPatches());
    expect(options[0]).toMatchObject({ value: 'f0', group: 'FACTORY' });
    expect(options[options.length - 1]).toMatchObject({ value: 'u0', name: 'MINE', group: 'USER' });
  });
});
