import { beforeEach, describe, expect, it } from 'vitest';
import { FACTORY_KITS } from './kits';
import { factoryKitTags } from './kitTags';
import { DRUM_PARAMS, defaultDrumParams, pad } from './params';
import { FACTORY_PATCHES, PATCH_FIELDS } from './patches';
import { factoryPatchTags } from './patchTags';
import { kitEntries, patchEntries, resolvePatchFields, varyPadParams } from './drumBrowser';
import { useDrumStore } from './store';

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

const seeded = (seed: number) => () => ((seed = (seed * 16807) % 2147483647) / 2147483647);

describe('drum browser', () => {
  it('tags every factory kit and patch', () => {
    expect(FACTORY_KITS.filter((k) => !factoryKitTags(k.name)).map((k) => k.name)).toEqual([]);
    expect(FACTORY_PATCHES.filter((p) => !factoryPatchTags(p.name)).map((p) => p.name)).toEqual([]);
  });

  it('keeps factory order and appends user entries with load keys', () => {
    const k = kitEntries([FACTORY_KITS[0]]);
    expect(k.slice(0, 2).map((e) => e.value)).toEqual(['f0', 'f1']);
    expect(k[k.length - 1]).toMatchObject({ value: 'u0', user: true, role: null });
    const p = patchEntries([FACTORY_PATCHES[0]]);
    expect(p[p.length - 1]).toMatchObject({ value: 'u0', user: true, role: null });
  });

  it('varies one pad within parameter ranges and only timbre fields', () => {
    const padI = 3;
    for (const patch of FACTORY_PATCHES) {
      const base = resolvePatchFields(patch);
      const v = varyPadParams(base, padI, seeded(7));
      expect(Object.keys(v).sort()).toEqual(PATCH_FIELDS.map((f) => pad(padI, f)).sort());
      for (const f of PATCH_FIELDS) {
        const id = pad(padI, f);
        if (v[id] === base[f]) continue;
        const d = DRUM_PARAMS[id];
        expect(v[id]).toBeGreaterThanOrEqual(d.min!);
        expect(v[id]).toBeLessThanOrEqual(d.max!);
        expect(f).toMatch(/^(oscA\.fine|oscB\.fine|aenv\.(dec|hold)|penv\.(amt|dec)|flt\.(cut|res)|noise\.color)$/);
      }
    }
  });
});

describe('drum browser snapshots', () => {
  beforeEach(() => {
    localStorage.clear();
    useDrumStore.setState({ params: defaultDrumParams(), sel: 0, hosted: false, patchValue: '', kitValue: 'f0', kitDirty: false });
  });

  it('restores a kit exactly after loading another', () => {
    const s = useDrumStore.getState();
    s.loadKitByValue('f4');
    useDrumStore.getState().setParam(pad(2, 'lvl'), 0.3);
    useDrumStore.getState().setPadName(1, 'MINE');
    const snap = useDrumStore.getState().captureKit();
    const before = useDrumStore.getState();
    const expected = { params: { ...before.params }, padNames: [...before.padNames], kitValue: before.kitValue, kitDirty: before.kitDirty };

    useDrumStore.getState().loadKitByValue('f9');
    expect(useDrumStore.getState().params).not.toEqual(expected.params);
    useDrumStore.getState().restoreKit(snap);

    const after = useDrumStore.getState();
    expect(after.params).toEqual(expected.params);
    expect(after.padNames).toEqual(expected.padNames);
    expect(after.kitValue).toBe(expected.kitValue);
    expect(after.kitDirty).toBe(true);
    expect(after.patchValue).toBe(snap.patchValue);
  });

  it('restores a pad patch onto the pad it was captured from', () => {
    const s = useDrumStore.getState();
    s.selectPad(2);
    useDrumStore.getState().applyPatchByValue('f4');
    useDrumStore.setState({ kitDirty: false });
    const snap = useDrumStore.getState().capturePad();
    const original = Object.fromEntries(PATCH_FIELDS.map((f) => [f, useDrumStore.getState().params[pad(2, f)]]));

    useDrumStore.getState().applyPatchByValue('f0');
    useDrumStore.getState().selectPad(5);
    const otherBefore = Object.fromEntries(PATCH_FIELDS.map((f) => [f, useDrumStore.getState().params[pad(5, f)]]));
    useDrumStore.getState().restorePad(snap);

    const after = useDrumStore.getState();
    for (const f of PATCH_FIELDS) {
      expect(after.params[pad(2, f)]).toBe(original[f]);
      expect(after.params[pad(5, f)]).toBe(otherBefore[f]);
    }
    expect(after.kitDirty).toBe(false);
    expect(after.sel).toBe(5);
    expect(after.patchValue).toBe('');
  });

  it('marks the kit dirty when a variant applies', () => {
    const s = useDrumStore.getState();
    s.applyPatchByValue('f1');
    useDrumStore.setState({ kitDirty: false });
    const base = patchEntries([])[1].fields;
    useDrumStore.getState().applyPadVariant(varyPadParams(base, 0, seeded(3)));
    expect(useDrumStore.getState().kitDirty).toBe(true);
    expect(useDrumStore.getState().patchValue).toBe('f1');
  });
});
