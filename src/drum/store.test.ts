// src/drum/store.test.ts
import { describe, it, expect, beforeEach } from 'vitest';
import { useDrumStore } from './store';
import { patIdx } from './seq';
import { defaultDrumParams } from './params';

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

describe('drum store', () => {
  beforeEach(() => {
    localStorage.clear();
    useDrumStore.setState({
      params: defaultDrumParams(), sel: 0, editPattern: 0, chain: [0], chaining: false, chainFresh: false,
      patterns: new Uint8Array(4 * 16 * 16),
    });
  });

  it('toggleStep cycles off→on→accent→off on the selected pad + edit pattern', () => {
    const s = useDrumStore.getState();
    s.selectPad(2);
    s.toggleStep(5);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 5)]).toBe(1);
    useDrumStore.getState().toggleStep(5);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 5)]).toBe(2);
    useDrumStore.getState().toggleStep(5);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 5)]).toBe(0);
  });

  it('setParam updates the store map', () => {
    useDrumStore.getState().setParam('pad0.oscA.tune', -14);
    expect(useDrumStore.getState().params['pad0.oscA.tune']).toBe(-14);
  });

  it('chain building: chaining mode appends, leaving keeps ≥1 entry', () => {
    const s = useDrumStore.getState();
    s.setChaining(true);
    s.chainClick(0); s.chainClick(1); s.chainClick(0);
    expect(useDrumStore.getState().chain).toEqual([0, 1, 0]);
    useDrumStore.getState().setChaining(false);
    // outside chaining, clicking a pattern selects it for editing and resets the chain to just it
    useDrumStore.getState().chainClick(2);
    expect(useDrumStore.getState().editPattern).toBe(2);
    expect(useDrumStore.getState().chain).toEqual([2]);
  });

  it('kit save + load round-trips patterns and params', () => {
    const s = useDrumStore.getState();
    s.setParam('pad1.penv.amt', 30);
    s.selectPad(1);
    s.toggleStep(0);
    s.saveKit('TEST KIT');
    s.setParam('pad1.penv.amt', 0);
    const val = useDrumStore.getState().kitValue;
    expect(val.startsWith('u')).toBe(true);
    useDrumStore.getState().loadKitByValue(val);
    expect(useDrumStore.getState().params['pad1.penv.amt']).toBe(30);
    expect(useDrumStore.getState().patterns[patIdx(0, 1, 0)]).toBe(1);
  });
});
