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
      stepSel: null,
      clipboard: null,
    });
  });

  it('toggleCell sets a note, same lane clears, other lane moves', () => {
    const s = useBassStore.getState();
    s.toggleCell(4, 7);
    expect(getStep(useBassStore.getState().patterns, 0, 4)).toMatchObject({ on: true, note: 7 });
    useBassStore.getState().toggleCell(4, 3); // different lane: move, keep on
    expect(getStep(useBassStore.getState().patterns, 0, 4)).toMatchObject({ on: true, note: 3 });
    useBassStore.getState().toggleCell(4, 3); // same lane: rest + clear flags
    expect(getStep(useBassStore.getState().patterns, 0, 4)).toMatchObject({ on: false, acc: false, slide: false, duration: 1 });
  });

  it('acc/slide/duration controls only apply to on steps; oct cycles', () => {
    const s = useBassStore.getState();
    s.toggleStepAcc(0);
    s.setStepDuration(0, 4);
    s.toggleStepSlide(0);
    expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ acc: false, slide: false, duration: 1 });
    useBassStore.getState().toggleCell(0, 0);
    useBassStore.getState().toggleStepAcc(0);
    useBassStore.getState().setStepDuration(0, 2);
    useBassStore.getState().toggleStepSlide(0);
    expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, acc: true, slide: true, duration: 2 });
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

  describe('step selection', () => {
    it('shiftClickStep anchors then extends; selectAllSteps/clearStepSelection', () => {
      const s = useBassStore.getState();
      expect(s.stepSel).toBeNull();
      s.shiftClickStep(4);
      expect(useBassStore.getState().stepSel).toEqual({ from: 4, to: 4 });
      useBassStore.getState().shiftClickStep(9); // extends from the same anchor
      expect(useBassStore.getState().stepSel).toEqual({ from: 4, to: 9 });
      useBassStore.getState().selectAllSteps();
      expect(useBassStore.getState().stepSel).toEqual({ from: 0, to: 15 });
      useBassStore.getState().clearStepSelection();
      expect(useBassStore.getState().stepSel).toBeNull();
    });
  });

  describe('clipboard verbs', () => {
    it('copy/paste a range: pastes at the selection start, leaves source alone', () => {
      const s = useBassStore.getState();
      s.toggleCell(2, 5); // on, note 5
      s.toggleStepAcc(2);
      s.setStepSelection({ from: 2, to: 2 });
      s.copySelection();
      s.setStepSelection({ from: 10, to: 10 });
      useBassStore.getState().pasteSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 10)).toMatchObject({ on: true, note: 5, acc: true });
      expect(getStep(useBassStore.getState().patterns, 0, 2)).toMatchObject({ on: true, note: 5, acc: true });
    });

    it('cut clears the selected range after capturing it', () => {
      const s = useBassStore.getState();
      s.toggleCell(3, 6);
      s.setStepSelection({ from: 3, to: 3 });
      s.cutSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 3).on).toBe(false);
      s.setStepSelection({ from: 0, to: 0 });
      useBassStore.getState().pasteSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 6 });
    });

    it('copy/paste with no selection operates on the whole edit pattern', () => {
      const s = useBassStore.getState();
      s.toggleCell(0, 1);
      s.setEditPattern(0);
      s.copySelection(); // whole-pattern clipboard
      s.setEditPattern(1);
      useBassStore.getState().pasteSelection();
      expect(getStep(useBassStore.getState().patterns, 1, 0)).toMatchObject({ on: true, note: 1 });
    });

    it('duplicate with a range selected pastes a copy right after it (clamped)', () => {
      const s = useBassStore.getState();
      s.toggleCell(14, 4);
      s.setStepSelection({ from: 14, to: 14 });
      s.duplicateSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 15)).toMatchObject({ on: true, note: 4 });
      expect(useBassStore.getState().stepSel).toEqual({ from: 15, to: 15 });
    });

    it('duplicate with no selection copies the edit pattern to the next bar, extending the chain', () => {
      const s = useBassStore.getState();
      s.setSequenceLength(1);
      s.toggleCell(0, 2, 0);
      s.setEditPattern(0);
      s.duplicateSelection();
      expect(useBassStore.getState().chain.length).toBe(2);
      expect(useBassStore.getState().editPattern).toBe(1);
      expect(getStep(useBassStore.getState().patterns, 1, 0)).toMatchObject({ on: true, note: 2 });
    });

    it('duplicate with a range ending on the last step is a full no-op', () => {
      const s = useBassStore.getState();
      s.toggleCell(14, 3);
      s.toggleCell(15, 9);
      s.setStepSelection({ from: 14, to: 15 });
      const before = useBassStore.getState().patterns;
      useBassStore.getState().duplicateSelection();
      expect(useBassStore.getState().patterns).toBe(before); // step 15 not corrupted
      expect(useBassStore.getState().stepSel).toEqual({ from: 14, to: 15 });
    });

    it('delete zeros the selected range only', () => {
      const s = useBassStore.getState();
      s.toggleCell(5, 3);
      s.toggleCell(6, 3);
      s.setStepSelection({ from: 5, to: 5 });
      s.deleteSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 5).on).toBe(false);
      expect(getStep(useBassStore.getState().patterns, 0, 6).on).toBe(true);
    });

    it('shiftSelection moves the range to dest (or copies with {copy:true}) and updates the selection', () => {
      const s = useBassStore.getState();
      s.toggleCell(0, 8);
      s.setStepSelection({ from: 0, to: 0 });
      s.shiftSelection(5);
      expect(getStep(useBassStore.getState().patterns, 0, 5)).toMatchObject({ on: true, note: 8 });
      expect(getStep(useBassStore.getState().patterns, 0, 0).on).toBe(false);
      expect(useBassStore.getState().stepSel).toEqual({ from: 5, to: 5 });

      useBassStore.getState().setStepSelection({ from: 5, to: 5 });
      useBassStore.getState().shiftSelection(8, { copy: true });
      expect(getStep(useBassStore.getState().patterns, 0, 8)).toMatchObject({ on: true, note: 8 });
      expect(getStep(useBassStore.getState().patterns, 0, 5).on).toBe(true); // copy keeps source
    });
  });

  describe('bar-chip move', () => {
    it('movePattern swaps two patterns by default', () => {
      const s = useBassStore.getState();
      s.toggleCell(0, 1, 0);
      s.toggleCell(0, 2, 1);
      s.movePattern(0, 1);
      expect(getStep(useBassStore.getState().patterns, 1, 0)).toMatchObject({ on: true, note: 1 });
      expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 2 });
    });

    it('movePattern with {copy:true} leaves the source pattern untouched', () => {
      const s = useBassStore.getState();
      s.toggleCell(0, 1, 0);
      s.movePattern(0, 2, { copy: true });
      expect(getStep(useBassStore.getState().patterns, 2, 0)).toMatchObject({ on: true, note: 1 });
      expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 1 }); // source kept
    });
  });

  describe('undo/redo', () => {
    it('undoes and redoes a sequence mutation via the _setPatterns chokepoint', () => {
      const before = useBassStore.getState().patterns;
      useBassStore.getState().toggleCell(0, 3);
      expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 3 });
      useBassStore.getState().undo();
      expect(getStep(useBassStore.getState().patterns, 0, 0).on).toBe(false);
      expect(Array.from(useBassStore.getState().patterns)).toEqual(Array.from(before));
      useBassStore.getState().redo();
      expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 3 });
    });

    it('a continuous note-length drag coalesces to a single undo entry', () => {
      useBassStore.getState()._clearHistory();
      useBassStore.getState().toggleCell(0, 3);
      // One setStepDuration per column crossed, like NoteLengthHandle emits.
      for (let d = 2; d <= 8; d++) useBassStore.getState().setStepDuration(0, d);
      expect(getStep(useBassStore.getState().patterns, 0, 0).duration).toBe(8);
      useBassStore.getState().undo(); // the whole drag unwinds in one step
      expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, duration: 1 });
      useBassStore.getState().undo(); // then the toggle
      expect(getStep(useBassStore.getState().patterns, 0, 0).on).toBe(false);
    });

    it('a fresh mutation after undo clears the redo stack', () => {
      useBassStore.getState().toggleCell(1, 3);
      useBassStore.getState().undo();
      useBassStore.getState().toggleCell(1, 5); // new edit branch
      useBassStore.getState().redo(); // no-op: nothing to redo
      expect(getStep(useBassStore.getState().patterns, 0, 1)).toMatchObject({ on: true, note: 5 });
    });
  });
});
