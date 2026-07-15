import { describe, it, expect, beforeEach } from 'vitest';
import { useBassStore } from './store';
import { getStep, makeEmptyPatterns } from './seq';
import { defaultBassParams } from './params';
import { FACTORY_PATCHES } from './patches';

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

describe('bass store', () => {
  beforeEach(() => {
    localStorage.clear();
    useBassStore.setState({
      params: defaultBassParams(),
      patterns: makeEmptyPatterns(),
      chain: [0],
      editPattern: 0,
      playing: false,
      heldSemis: [],
      curSemi: -100,
      patchValue: 'f0',
      userPatches: [],
    });
  });

  it('toggleCell sets a note, same lane clears, other lane moves', () => {
    const s = useBassStore.getState();
    s.toggleCell(4, 7);
    expect(getStep(useBassStore.getState().patterns, 0, 4)).toMatchObject({ on: true, note: 7 });
    useBassStore.getState().toggleCell(4, 3); // different lane: move, keep on
    expect(getStep(useBassStore.getState().patterns, 0, 4)).toMatchObject({ on: true, note: 3 });
    useBassStore.getState().toggleCell(4, 3); // same lane: rest + clear flags
    expect(getStep(useBassStore.getState().patterns, 0, 4)).toMatchObject({ on: false, acc: false, slide: false });
  });

  it('acc/slide toggles only apply to on steps; oct cycles', () => {
    const s = useBassStore.getState();
    s.toggleStepAcc(0);
    s.toggleStepSlide(0);
    expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ acc: false, slide: false });
    useBassStore.getState().toggleCell(0, 0);
    useBassStore.getState().toggleStepAcc(0);
    useBassStore.getState().toggleStepSlide(0);
    expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, acc: true, slide: true });
    useBassStore.getState().cycleStepOct(0);
    expect(getStep(useBassStore.getState().patterns, 0, 0).oct).toBe(1);
    useBassStore.getState().cycleStepOct(0);
    expect(getStep(useBassStore.getState().patterns, 0, 0).oct).toBe(-1);
  });

  it('setParam updates the store map', () => {
    useBassStore.getState().setParam('flt.cut', 777);
    expect(useBassStore.getState().params['flt.cut']).toBe(777);
  });

  it('sequence length plays bars from 1 through N and editing does not change it', () => {
    const s = useBassStore.getState();
    s.setSequenceLength(3);
    expect(useBassStore.getState().chain).toEqual([0, 1, 2]);
    useBassStore.getState().setEditPattern(2);
    expect(useBassStore.getState().editPattern).toBe(2);
    expect(useBassStore.getState().chain).toEqual([0, 1, 2]);
    useBassStore.getState().setSequenceLength(0);
    expect(useBassStore.getState().chain).toEqual([0]);
  });

  it('note tracking: audition updates curSemi via held stack', () => {
    const s = useBassStore.getState();
    s.noteOn(5, 1);
    s.noteOn(9, 1);
    expect(useBassStore.getState().curSemi).toBe(9);
    useBassStore.getState().noteOff(9);
    expect(useBassStore.getState().curSemi).toBe(5);
    useBassStore.getState().noteOff(5);
    expect(useBassStore.getState().curSemi).toBe(-100);
  });

  it('randomize only rewrites the edit pattern', () => {
    const before = useBassStore.getState().patterns;
    useBassStore.getState().randomize();
    const after = useBassStore.getState().patterns;
    // pattern B..D untouched
    expect(Array.from(after.slice(16 * 3))).toEqual(Array.from(before.slice(16 * 3)));
  });

  it('loadPatchByValue applies factory patch params + patterns', () => {
    useBassStore.getState().loadPatchByValue('f1');
    const state = useBassStore.getState();
    expect(state.patchValue).toBe('f1');
    expect(state.params['flt.cut']).toBe(FACTORY_PATCHES[1].params['flt.cut']);
    expect(getStep(state.patterns, 0, 0)).toMatchObject({ on: true, note: 0, acc: true });
  });

  it('savePatch stores and selects the user patch', () => {
    useBassStore.getState().savePatch('MY LINE');
    const state = useBassStore.getState();
    expect(state.userPatches.length).toBe(1);
    expect(state.patchValue).toBe('u0');
  });
});
