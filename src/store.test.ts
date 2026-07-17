// WT-1 step-sequencer editing verbs: selection, clipboard, undo/redo, DnD
// (docs/editing-concept.md). Mirrors src/bass/store.test.ts / src/drum/store.test.ts.
import { describe, it, expect, beforeEach } from 'vitest';
import { useStore } from './store';
import { defaultParams } from './params';
import { getStep, makeEmptyPatterns, STEPS } from './noteseq';

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

describe('WT-1 step-sequencer editing', () => {
  beforeEach(() => {
    localStorage.clear();
    useStore.getState()._clearSeqHistory();
    useStore.setState({
      params: defaultParams(),
      patterns: makeEmptyPatterns(),
      chain: [0],
      editPattern: 0,
      seqPlaying: false,
      curStep: -1,
      curPat: 0,
      stepSel: null,
      clipboard: null,
      hosted: false,
    });
  });

  it('setStepSel clamps to the pattern and clearStepSel/selectAllSteps toggle it', () => {
    const s = useStore.getState();
    s.setStepSel({ from: -3, to: 99 });
    expect(useStore.getState().stepSel).toEqual({ from: 0, to: STEPS - 1 });
    s.selectAllSteps();
    expect(useStore.getState().stepSel).toEqual({ from: 0, to: STEPS - 1 });
    s.setStepSel({ from: 5, to: 2 });
    expect(useStore.getState().stepSel).toEqual({ from: 5, to: 2 }); // helpers normalize, not the setter
    s.clearStepSel();
    expect(useStore.getState().stepSel).toBeNull();
  });

  it('copy/paste a step range preserves bytes and clamps at the pattern end', () => {
    const s = useStore.getState();
    s.toggleCell(0, 3);
    s.toggleCell(1, 7);
    s.setStepSel({ from: 0, to: 1 });
    s.copySteps();
    expect(useStore.getState().clipboard?.kind).toBe('range');
    s.setStepSel({ from: 15, to: 15 }); // paste anchor at the last step: only 1 of 2 steps fit
    s.pasteSteps();
    const after = useStore.getState().patterns;
    expect(getStep(after, 0, 15)).toMatchObject({ on: true, note: 3 }); // first copied step lands
    expect(getStep(after, 1, 0)).toMatchObject({ on: false }); // clamped, not wrapped into pattern 1
  });

  it('cut clears the selection after capturing it, paste restores it elsewhere', () => {
    const s = useStore.getState();
    s.toggleCell(2, 5);
    s.setStepSel({ from: 2, to: 2 });
    s.cutSteps();
    expect(getStep(useStore.getState().patterns, 0, 2)).toMatchObject({ on: false });
    s.setStepSel({ from: 8, to: 8 });
    s.pasteSteps();
    expect(getStep(useStore.getState().patterns, 0, 8)).toMatchObject({ on: true, note: 5 });
  });

  it('copy/paste with no selection round-trips the whole pattern', () => {
    const s = useStore.getState();
    s.toggleCell(0, 1);
    s.toggleCell(15, 9);
    s.copySteps();
    expect(useStore.getState().clipboard?.kind).toBe('pattern');
    s.setEditPattern(1);
    s.pasteSteps();
    const patB = useStore.getState().patterns;
    expect(getStep(patB, 1, 0)).toMatchObject({ on: true, note: 1 });
    expect(getStep(patB, 1, 15)).toMatchObject({ on: true, note: 9 });
  });

  it('duplicate with a selection pastes a copy immediately after it, clamped', () => {
    const s = useStore.getState();
    s.toggleCell(0, 4);
    s.toggleCell(1, 6);
    s.setStepSel({ from: 0, to: 1 });
    s.duplicateSteps();
    const p = useStore.getState().patterns;
    expect(getStep(p, 0, 2)).toMatchObject({ on: true, note: 4 });
    expect(getStep(p, 0, 3)).toMatchObject({ on: true, note: 6 });
    expect(useStore.getState().stepSel).toEqual({ from: 2, to: 3 });
  });

  it('duplicate with no selection copies the edit pattern to the next bar and extends the sequence', () => {
    const s = useStore.getState();
    s.toggleCell(0, 2);
    expect(useStore.getState().chain).toEqual([0]);
    s.duplicateSteps();
    expect(useStore.getState().chain).toEqual([0, 1]);
    expect(useStore.getState().editPattern).toBe(1);
    expect(getStep(useStore.getState().patterns, 1, 0)).toMatchObject({ on: true, note: 2 });
  });

  it('duplicate with a range ending on the last step is a full no-op', () => {
    const s = useStore.getState();
    s.toggleCell(14, 4);
    s.toggleCell(15, 9);
    s.setStepSel({ from: 14, to: 15 });
    const before = useStore.getState().patterns;
    useStore.getState().duplicateSteps();
    expect(useStore.getState().patterns).toBe(before); // step 15 not corrupted
    expect(useStore.getState().stepSel).toEqual({ from: 14, to: 15 }); // selection unchanged
  });

  it('delete only clears when a selection exists', () => {
    const s = useStore.getState();
    s.toggleCell(3, 1);
    s.deleteSteps(); // no selection: no-op
    expect(getStep(useStore.getState().patterns, 0, 3)).toMatchObject({ on: true, note: 1 });
    s.setStepSel({ from: 3, to: 3 });
    s.deleteSteps();
    expect(getStep(useStore.getState().patterns, 0, 3)).toMatchObject({ on: false });
  });

  it('shiftStepSel moves the range (drag) and copy leaves the source intact (Alt-drag)', () => {
    const s = useStore.getState();
    s.toggleCell(0, 4);
    s.setStepSel({ from: 0, to: 0 });
    s.shiftStepSel(5);
    expect(getStep(useStore.getState().patterns, 0, 0)).toMatchObject({ on: false });
    expect(getStep(useStore.getState().patterns, 0, 5)).toMatchObject({ on: true, note: 4 });
    expect(useStore.getState().stepSel).toEqual({ from: 5, to: 5 });

    s.setStepSel({ from: 5, to: 5 });
    s.shiftStepSel(9, { copy: true });
    expect(getStep(useStore.getState().patterns, 0, 5)).toMatchObject({ on: true, note: 4 }); // source intact
    expect(getStep(useStore.getState().patterns, 0, 9)).toMatchObject({ on: true, note: 4 });
  });

  it('shiftStepSel overshoot clamps so the moved content and the selection agree', () => {
    const s = useStore.getState();
    s.toggleCell(0, 5);
    s.toggleCell(1, 7);
    s.setStepSel({ from: 0, to: 1 });
    s.shiftStepSel(15); // range of 2 cannot start at 15 — lands at 14
    expect(useStore.getState().stepSel).toEqual({ from: 14, to: 15 });
    expect(getStep(useStore.getState().patterns, 0, 14)).toMatchObject({ on: true, note: 5 });
    expect(getStep(useStore.getState().patterns, 0, 15)).toMatchObject({ on: true, note: 7 });
    expect(getStep(useStore.getState().patterns, 0, 0).on).toBe(false); // source cleared

    // A drop that clamps back onto the range's own start is a full no-op.
    useStore.getState().setStepSel({ from: 12, to: 15 });
    const before = useStore.getState().patterns;
    useStore.getState().shiftStepSel(14); // clamps to 12 = current start
    expect(useStore.getState().patterns).toBe(before);
    expect(useStore.getState().stepSel).toEqual({ from: 12, to: 15 });
  });

  it('movePattern swaps two patterns, and copy overwrites the target only', () => {
    const s = useStore.getState();
    s.toggleCell(0, 3, 0);
    s.toggleCell(0, 7, 1);
    s.movePattern(0, 1);
    expect(getStep(useStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 7 });
    expect(getStep(useStore.getState().patterns, 1, 0)).toMatchObject({ on: true, note: 3 });

    s.movePattern(0, 2, { copy: true });
    expect(getStep(useStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 7 }); // source untouched
    expect(getStep(useStore.getState().patterns, 2, 0)).toMatchObject({ on: true, note: 7 });
  });

  it('undo/redo round-trip through the sequencer history, including a length-extending duplicate', () => {
    const s = useStore.getState();
    s.toggleCell(0, 2);
    s.setStepSel({ from: 0, to: 0 });
    s.duplicateSteps(); // no selection branch is taken since stepSel exists -> range-duplicate

    // Undo the range-duplicate.
    const beforeUndo = useStore.getState().patterns;
    s.undoSeq();
    expect(getStep(useStore.getState().patterns, 0, 1)).toMatchObject({ on: false });
    s.redoSeq();
    expect(useStore.getState().patterns).toEqual(beforeUndo);

    // A no-selection duplicate that extends the chain undoes both together.
    s.clearStepSel();
    s.duplicateSteps();
    expect(useStore.getState().chain).toEqual([0, 1]);
    s.undoSeq();
    expect(useStore.getState().chain).toEqual([0]);
    expect(getStep(useStore.getState().patterns, 1, 0)).toMatchObject({ on: false }); // pattern 1 write undone too
  });
});
