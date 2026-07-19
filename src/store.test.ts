// WT-1 step-sequencer editing verbs: selection, clipboard, undo/redo, DnD
// (docs/editing-concept.md). Mirrors src/bass/store.test.ts / src/drum/store.test.ts.
import { describe, it, expect, beforeEach } from 'vitest';
import { useStore } from './store';
import { defaultParams } from './params';
import { getStep, makeEmptyPatterns, NOTE_LANES, STEPS } from './noteseq';

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
  // Full note-lane band, mirroring the old 1D step-range tests' intent.
  const fullRect = (from: number, to: number) => ({ stepFrom: from, stepTo: to, noteFrom: 0, noteTo: NOTE_LANES - 1 });

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
      rectSel: null,
      lastCell: null,
      clipboard: null,
      hosted: false,
    });
  });

  it('setRectSel clamps to the pattern and clearStepSel/selectAllSteps toggle it', () => {
    const s = useStore.getState();
    s.setRectSel({ stepFrom: -3, stepTo: 99, noteFrom: -3, noteTo: 99 });
    expect(useStore.getState().rectSel).toEqual({ stepFrom: 0, stepTo: STEPS - 1, noteFrom: 0, noteTo: NOTE_LANES - 1 });
    s.selectAllSteps();
    expect(useStore.getState().rectSel).toEqual({ stepFrom: 0, stepTo: STEPS - 1, noteFrom: 0, noteTo: NOTE_LANES - 1 });
    s.setRectSel({ stepFrom: 5, stepTo: 2, noteFrom: 0, noteTo: 11 });
    expect(useStore.getState().rectSel).toEqual({ stepFrom: 5, stepTo: 2, noteFrom: 0, noteTo: 11 }); // helpers normalize, not the setter
    s.clearStepSel();
    expect(useStore.getState().rectSel).toBeNull();
  });

  it('copy/paste a step range preserves bytes and clamps at the pattern end', () => {
    const s = useStore.getState();
    s.toggleCell(0, 3);
    s.toggleCell(1, 7);
    s.setRectSel(fullRect(0, 1));
    s.copySteps();
    expect(useStore.getState().clipboard?.kind).toBe('rect');
    s.setRectSel(fullRect(15, 15)); // paste anchor at the last step: only 1 of 2 steps fit
    s.pasteSteps();
    const after = useStore.getState().patterns;
    expect(getStep(after, 0, 15)).toMatchObject({ on: true, note: 3 }); // first copied step lands
    expect(getStep(after, 1, 0)).toMatchObject({ on: false }); // clamped, not wrapped into pattern 1
  });

  it('cut clears the selection after capturing it, paste restores it elsewhere', () => {
    const s = useStore.getState();
    s.toggleCell(2, 5);
    s.setRectSel(fullRect(2, 2));
    s.cutSteps();
    expect(getStep(useStore.getState().patterns, 0, 2)).toMatchObject({ on: false });
    s.setRectSel(fullRect(8, 8));
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
    s.setRectSel(fullRect(0, 1));
    s.duplicateSteps();
    const p = useStore.getState().patterns;
    expect(getStep(p, 0, 2)).toMatchObject({ on: true, note: 4 });
    expect(getStep(p, 0, 3)).toMatchObject({ on: true, note: 6 });
    expect(useStore.getState().rectSel).toEqual(fullRect(2, 3));
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
    s.setRectSel(fullRect(14, 15));
    const before = useStore.getState().patterns;
    useStore.getState().duplicateSteps();
    expect(useStore.getState().patterns).toBe(before); // step 15 not corrupted
    expect(useStore.getState().rectSel).toEqual(fullRect(14, 15)); // selection unchanged
  });

  it('delete only clears when a selection exists', () => {
    const s = useStore.getState();
    s.toggleCell(3, 1);
    s.deleteSteps(); // no selection: no-op
    expect(getStep(useStore.getState().patterns, 0, 3)).toMatchObject({ on: true, note: 1 });
    s.setRectSel(fullRect(3, 3));
    s.deleteSteps();
    expect(getStep(useStore.getState().patterns, 0, 3)).toMatchObject({ on: false });
  });

  it('moveRectSel moves the range (drag) and copy leaves the source intact (Alt-drag)', () => {
    const s = useStore.getState();
    s.toggleCell(0, 4);
    s.setRectSel(fullRect(0, 0));
    s.moveRectSel(5, 0);
    expect(getStep(useStore.getState().patterns, 0, 0)).toMatchObject({ on: false });
    expect(getStep(useStore.getState().patterns, 0, 5)).toMatchObject({ on: true, note: 4 });
    expect(useStore.getState().rectSel).toEqual(fullRect(5, 5));

    s.setRectSel(fullRect(5, 5));
    s.moveRectSel(4, 0, { copy: true });
    expect(getStep(useStore.getState().patterns, 0, 5)).toMatchObject({ on: true, note: 4 }); // source intact
    expect(getStep(useStore.getState().patterns, 0, 9)).toMatchObject({ on: true, note: 4 });
  });

  it('moveRectSel overshoot clamps so the moved content and the selection agree', () => {
    const s = useStore.getState();
    s.toggleCell(0, 5);
    s.toggleCell(1, 7);
    s.setRectSel(fullRect(0, 1));
    s.moveRectSel(15, 0); // range of 2 cannot start at 15 — lands at 14
    expect(useStore.getState().rectSel).toEqual(fullRect(14, 15));
    expect(getStep(useStore.getState().patterns, 0, 14)).toMatchObject({ on: true, note: 5 });
    expect(getStep(useStore.getState().patterns, 0, 15)).toMatchObject({ on: true, note: 7 });
    expect(getStep(useStore.getState().patterns, 0, 0).on).toBe(false); // source cleared

    // A drop that clamps back onto the range's own start is a full no-op.
    useStore.getState().setRectSel(fullRect(12, 15));
    const before = useStore.getState().patterns;
    useStore.getState().moveRectSel(2, 0); // already at the last valid step — no movement
    expect(useStore.getState().patterns).toBe(before);
    expect(useStore.getState().rectSel).toEqual(fullRect(12, 15));
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
    s.setRectSel(fullRect(0, 0));
    s.duplicateSteps(); // no selection branch is taken since rectSel exists -> rect-duplicate

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

  it('moveStepNote moves a note preserving oct/acc/duration and clears the source', () => {
    const s = useStore.getState();
    s.toggleCell(2, 4);
    s.toggleStepAcc(2);
    s.cycleStepOct(2);
    s.setStepDuration(2, 3);
    s.moveStepNote(2, 6, 9);
    expect(getStep(useStore.getState().patterns, 0, 2)).toMatchObject({ on: false });
    expect(getStep(useStore.getState().patterns, 0, 6)).toMatchObject({ on: true, note: 9, acc: true, oct: 1, duration: 3 });
  });

  it('moveStepNote with copy keeps the source, and one undo unwinds the whole move', () => {
    const s = useStore.getState();
    s.toggleCell(0, 5);
    s.moveStepNote(0, 4, 5, { copy: true });
    expect(getStep(useStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 5 });
    expect(getStep(useStore.getState().patterns, 0, 4)).toMatchObject({ on: true, note: 5 });
    s.undoSeq();
    expect(getStep(useStore.getState().patterns, 0, 4)).toMatchObject({ on: false });
    expect(getStep(useStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 5 });
  });

  it('moveStepNote changes pitch in place and no-ops on an off source or same target', () => {
    const s = useStore.getState();
    s.toggleCell(3, 2);
    s.moveStepNote(3, 3, 10); // pitch change in place
    expect(getStep(useStore.getState().patterns, 0, 3)).toMatchObject({ on: true, note: 10 });
    const before = useStore.getState().patterns;
    s.moveStepNote(3, 3, 10); // nothing changes
    expect(useStore.getState().patterns).toBe(before);
    s.moveStepNote(7, 9, 4); // off source
    expect(useStore.getState().patterns).toBe(before);
    expect(getStep(useStore.getState().patterns, 0, 9)).toMatchObject({ on: false });
  });
});

describe('rect selection verbs', () => {
  // helper: light a step via the store
  const light = (step: number, note: number) => useStore.getState().toggleCell(step, note, 0);

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
      rectSel: null,
      lastCell: null,
      clipboard: null,
      hosted: false,
    });
  });

  it('deleteSteps clears only notes inside the pitch band', () => {
    light(2, 5); light(3, 11);
    useStore.getState().setRectSel({ stepFrom: 2, stepTo: 3, noteFrom: 4, noteTo: 7 });
    useStore.getState().deleteSteps();
    const p = useStore.getState().patterns;
    expect(getStep(p, 0, 2).on).toBe(false);
    expect(getStep(p, 0, 3).on).toBe(true);
  });

  it('cutSteps copies then clears; pasteSteps anchors at the rect top-left', () => {
    light(0, 5); light(1, 7);
    useStore.getState().setRectSel({ stepFrom: 0, stepTo: 1, noteFrom: 0, noteTo: 11 });
    useStore.getState().cutSteps();
    expect(getStep(useStore.getState().patterns, 0, 0).on).toBe(false);
    useStore.getState().setRectSel({ stepFrom: 8, stepTo: 9, noteFrom: 0, noteTo: 11 });
    useStore.getState().pasteSteps();
    const p = useStore.getState().patterns;
    expect(getStep(p, 0, 8).note).toBe(5);
    expect(getStep(p, 0, 9).note).toBe(7);
  });

  it('pasteSteps with no selection anchors at lastCell, transposing to it', () => {
    // Two-step rect on different lanes: dStep0's note (7) is the rect's
    // noteHi, so it lands exactly on the anchor note (8); dStep1's note (5)
    // must transpose by the same +1 to land on 6. Both assertions only pass
    // if paste actually ran and applied the transpose.
    light(0, 7);
    light(1, 5);
    useStore.getState().setRectSel({ stepFrom: 0, stepTo: 1, noteFrom: 5, noteTo: 7 });
    useStore.getState().copySteps();
    useStore.getState().clearStepSel();
    light(10, 8); // sets lastCell = { step: 10, note: 8 }
    useStore.getState().pasteSteps();
    const p = useStore.getState().patterns;
    expect(getStep(p, 0, 10).on).toBe(true);
    expect(getStep(p, 0, 10).note).toBe(8);
    expect(getStep(p, 0, 11).on).toBe(true);
    expect(getStep(p, 0, 11).note).toBe(6);
  });

  it('duplicateSteps pastes right of the rect and reselects', () => {
    light(2, 5);
    useStore.getState().setRectSel({ stepFrom: 2, stepTo: 3, noteFrom: 0, noteTo: 11 });
    useStore.getState().duplicateSteps();
    expect(getStep(useStore.getState().patterns, 0, 4).note).toBe(5);
    expect(useStore.getState().rectSel).toEqual({ stepFrom: 4, stepTo: 5, noteFrom: 0, noteTo: 11 });
  });

  it('moveRectSel moves in both dimensions as a single undo entry', () => {
    light(2, 5);
    useStore.getState().setRectSel({ stepFrom: 2, stepTo: 2, noteFrom: 5, noteTo: 5 });
    useStore.getState().moveRectSel(3, -2);
    let p = useStore.getState().patterns;
    expect(getStep(p, 0, 2).on).toBe(false);
    expect(getStep(p, 0, 5).note).toBe(3);
    useStore.getState().undoSeq();
    p = useStore.getState().patterns;
    expect(getStep(p, 0, 2).note).toBe(5);
    expect(getStep(p, 0, 5).on).toBe(false);
  });

  it('selectAllSteps selects the full grid rect', () => {
    useStore.getState().selectAllSteps();
    expect(useStore.getState().rectSel).toEqual({ stepFrom: 0, stepTo: 15, noteFrom: 0, noteTo: 11 });
  });
});
