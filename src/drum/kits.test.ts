import { describe, it, expect, beforeEach } from 'vitest';
import { FACTORY_KITS, loadUserKits, saveUserKit, kitToState, stateToKit, type Kit } from './kits';
import { DRUM_PARAMS, defaultDrumParams, PAD_COUNT } from './params';
import { NPATTERNS, STEPS, patIdx } from './seq';

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

describe('kits', () => {
  beforeEach(() => localStorage.clear());

  it('ships TR-VOID, ROOM ONE, BITCRUSH; TR-VOID matches the mockup names', () => {
    expect(FACTORY_KITS.map((k) => k.name)).toEqual(['TR-VOID', 'ROOM ONE', 'BITCRUSH']);
    const tv = FACTORY_KITS[0];
    expect(tv.padNames).toHaveLength(PAD_COUNT);
    expect(tv.padNames[0]).toBe('KICK');
    expect(tv.padNames[2]).toBe('SNARE');
    expect(tv.patterns).toHaveLength(NPATTERNS * PAD_COUNT * STEPS);
    expect(tv.patterns[patIdx(0, 0, 0)]).toBe(2); // kick accent on the one
  });

  it('every factory kit param id exists in DRUM_PARAMS and is in range', () => {
    for (const k of FACTORY_KITS) {
      for (const [id, v] of Object.entries(k.params)) {
        const d = DRUM_PARAMS[id];
        expect(d, `${k.name}: ${id}`).toBeDefined();
        if (d.min !== undefined) { expect(v).toBeGreaterThanOrEqual(d.min!); expect(v).toBeLessThanOrEqual(d.max!); }
      }
    }
  });

  it('user kit save/load round-trip preserves everything', () => {
    const params = defaultDrumParams();
    params['pad3.oscA.tune'] = -7;
    const patterns = new Uint8Array(NPATTERNS * PAD_COUNT * STEPS);
    patterns[patIdx(2, 3, 5)] = 2;
    const kit = stateToKit('MY KIT', params, FACTORY_KITS[0].padNames, patterns, [0, 2], []);
    saveUserKit('MY KIT', kit);
    const loaded = loadUserKits();
    expect(loaded).toHaveLength(1);
    const st = kitToState(loaded[0]);
    expect(st.params['pad3.oscA.tune']).toBe(-7);
    expect(st.patterns[patIdx(2, 3, 5)]).toBe(2);
    expect(st.chain).toEqual([0, 2]);
  });

  it('saving under an existing user name overwrites, not duplicates', () => {
    const kit: Kit = stateToKit('X', defaultDrumParams(), FACTORY_KITS[0].padNames, new Uint8Array(NPATTERNS * PAD_COUNT * STEPS), [0], []);
    saveUserKit('X', kit);
    saveUserKit('X', kit);
    expect(loadUserKits()).toHaveLength(1);
  });
});
