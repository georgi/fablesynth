import { describe, it, expect, beforeEach } from 'vitest';
import { useBassStore } from './store';
import { getStep, LAYOUT, makeEmptyPatterns, NOTE_LANES, STEPS } from './seq';
import { copyRect } from '../shared/seqEdit';
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
      rectSel: null,
      lastCell: null,
      clipboard: null,
    });
  });

  // Full note-lane band, mirroring the old 1D step-range tests' intent.
  const fullRect = (from: number, to: number) => ({ stepFrom: from, stepTo: to, noteFrom: 0, noteTo: NOTE_LANES - 1 });

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

  describe('rect selection', () => {
    it('setRectSel clamps to the pattern and clearStepSelection/selectAllSteps toggle it', () => {
      const s = useBassStore.getState();
      expect(s.rectSel).toBeNull();
      s.setRectSel({ stepFrom: -3, stepTo: 99, noteFrom: -3, noteTo: 99 });
      expect(useBassStore.getState().rectSel).toEqual({ stepFrom: 0, stepTo: STEPS - 1, noteFrom: 0, noteTo: NOTE_LANES - 1 });
      useBassStore.getState().selectAllSteps();
      expect(useBassStore.getState().rectSel).toEqual({ stepFrom: 0, stepTo: STEPS - 1, noteFrom: 0, noteTo: NOTE_LANES - 1 });
      useBassStore.getState().clearStepSelection();
      expect(useBassStore.getState().rectSel).toBeNull();
    });
  });

  describe('clipboard verbs', () => {
    it('copy/paste a range: pastes at the selection start, leaves source alone', () => {
      const s = useBassStore.getState();
      s.toggleCell(2, 5); // on, note 5
      s.toggleStepAcc(2);
      s.setRectSel(fullRect(2, 2));
      s.copySelection();
      s.setRectSel(fullRect(10, 10));
      useBassStore.getState().pasteSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 10)).toMatchObject({ on: true, note: 5, acc: true });
      expect(getStep(useBassStore.getState().patterns, 0, 2)).toMatchObject({ on: true, note: 5, acc: true });
    });

    it('cut clears the selected range after capturing it', () => {
      const s = useBassStore.getState();
      s.toggleCell(3, 6);
      s.setRectSel(fullRect(3, 3));
      s.cutSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 3).on).toBe(false);
      s.setRectSel(fullRect(0, 0));
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
      s.setRectSel(fullRect(14, 14));
      s.duplicateSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 15)).toMatchObject({ on: true, note: 4 });
      expect(useBassStore.getState().rectSel).toEqual(fullRect(15, 15));
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
      s.setRectSel(fullRect(14, 15));
      const before = useBassStore.getState().patterns;
      useBassStore.getState().duplicateSelection();
      expect(useBassStore.getState().patterns).toBe(before); // step 15 not corrupted
      expect(useBassStore.getState().rectSel).toEqual(fullRect(14, 15));
    });

    it('delete zeros the selected range only', () => {
      const s = useBassStore.getState();
      s.toggleCell(5, 3);
      s.toggleCell(6, 3);
      s.setRectSel(fullRect(5, 5));
      s.deleteSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 5).on).toBe(false);
      expect(getStep(useBassStore.getState().patterns, 0, 6).on).toBe(true);
    });

    it('moveRectSel moves the range to dest (or copies with {copy:true}) and updates the selection', () => {
      const s = useBassStore.getState();
      s.toggleCell(0, 8);
      s.setRectSel(fullRect(0, 0));
      s.moveRectSel(5, 0);
      expect(getStep(useBassStore.getState().patterns, 0, 5)).toMatchObject({ on: true, note: 8 });
      expect(getStep(useBassStore.getState().patterns, 0, 0).on).toBe(false);
      expect(useBassStore.getState().rectSel).toEqual(fullRect(5, 5));

      useBassStore.getState().setRectSel(fullRect(5, 5));
      useBassStore.getState().moveRectSel(3, 0, { copy: true });
      expect(getStep(useBassStore.getState().patterns, 0, 8)).toMatchObject({ on: true, note: 8 });
      expect(getStep(useBassStore.getState().patterns, 0, 5).on).toBe(true); // copy keeps source
    });
  });

  describe('rect selection verbs (Task 3 mirror)', () => {
    // helper: light a step via the store
    const light = (step: number, note: number) => useBassStore.getState().toggleCell(step, note, 0);

    it('deleteSelection clears only notes inside the pitch band', () => {
      light(2, 5); light(3, 11);
      useBassStore.getState().setRectSel({ stepFrom: 2, stepTo: 3, noteFrom: 4, noteTo: 7 });
      useBassStore.getState().deleteSelection();
      const p = useBassStore.getState().patterns;
      expect(getStep(p, 0, 2).on).toBe(false);
      expect(getStep(p, 0, 3).on).toBe(true);
    });

    it('cutSelection copies then clears; pasteSelection anchors at the rect top-left', () => {
      light(0, 5); light(1, 7);
      useBassStore.getState().setRectSel({ stepFrom: 0, stepTo: 1, noteFrom: 0, noteTo: 11 });
      useBassStore.getState().cutSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 0).on).toBe(false);
      useBassStore.getState().setRectSel({ stepFrom: 8, stepTo: 9, noteFrom: 0, noteTo: 11 });
      useBassStore.getState().pasteSelection();
      const p = useBassStore.getState().patterns;
      expect(getStep(p, 0, 8).note).toBe(5);
      expect(getStep(p, 0, 9).note).toBe(7);
    });

    it('pasteSelection with no selection anchors at lastCell, transposing to it', () => {
      // Two-step rect on different lanes: dStep0's note (7) is the rect's
      // noteHi, so it lands exactly on the anchor note (8); dStep1's note (5)
      // must transpose by the same +1 to land on 6. Both assertions only pass
      // if paste actually ran and applied the transpose.
      light(0, 7);
      light(1, 5);
      useBassStore.getState().setRectSel({ stepFrom: 0, stepTo: 1, noteFrom: 5, noteTo: 7 });
      useBassStore.getState().copySelection();
      useBassStore.getState().clearStepSelection();
      light(10, 8); // sets lastCell = { step: 10, note: 8 }
      useBassStore.getState().pasteSelection();
      const p = useBassStore.getState().patterns;
      expect(getStep(p, 0, 10).on).toBe(true);
      expect(getStep(p, 0, 10).note).toBe(8);
      expect(getStep(p, 0, 11).on).toBe(true);
      expect(getStep(p, 0, 11).note).toBe(6);
    });

    it('duplicateSelection pastes right of the rect and reselects', () => {
      light(2, 5);
      useBassStore.getState().setRectSel({ stepFrom: 2, stepTo: 3, noteFrom: 0, noteTo: 11 });
      useBassStore.getState().duplicateSelection();
      expect(getStep(useBassStore.getState().patterns, 0, 4).note).toBe(5);
      expect(useBassStore.getState().rectSel).toEqual({ stepFrom: 4, stepTo: 5, noteFrom: 0, noteTo: 11 });
    });

    it('moveRectSel moves in both dimensions as a single undo entry', () => {
      light(2, 5);
      useBassStore.getState().setRectSel({ stepFrom: 2, stepTo: 2, noteFrom: 5, noteTo: 5 });
      useBassStore.getState().moveRectSel(3, -2);
      let p = useBassStore.getState().patterns;
      expect(getStep(p, 0, 2).on).toBe(false);
      expect(getStep(p, 0, 5).note).toBe(3);
      useBassStore.getState().undo();
      p = useBassStore.getState().patterns;
      expect(getStep(p, 0, 2).note).toBe(5);
      expect(getStep(p, 0, 5).on).toBe(false);
    });

    it('selectAllSteps selects the full grid rect', () => {
      useBassStore.getState().selectAllSteps();
      expect(useBassStore.getState().rectSel).toEqual({ stepFrom: 0, stepTo: 15, noteFrom: 0, noteTo: 11 });
    });

    it('rect move preserves the slide flag', () => {
      useBassStore.getState().toggleCell(2, 5, 0);
      useBassStore.getState().toggleStepSlide(2, 0);
      useBassStore.getState().setRectSel({ stepFrom: 2, stepTo: 2, noteFrom: 5, noteTo: 5 });
      useBassStore.getState().moveRectSel(1, 1);
      const s = getStep(useBassStore.getState().patterns, 0, 3);
      expect(s.on).toBe(true);
      expect(s.note).toBe(6);
      expect(s.slide).toBe(true);
    });

    it('dropRect stamps picked-up cells at the drop anchor, transposed', () => {
      light(0, 5); light(1, 7);
      const data = copyRect(useBassStore.getState().patterns, LAYOUT, 0, { stepFrom: 0, stepTo: 1, noteFrom: 4, noteTo: 8 });
      useBassStore.getState().dropRect(data, 8, -3); // COPY drop: source untouched
      const p = useBassStore.getState().patterns;
      expect(getStep(p, 0, 0).note).toBe(5);
      expect(getStep(p, 0, 8).note).toBe(2);
      expect(getStep(p, 0, 9).note).toBe(4);
      expect(useBassStore.getState().rectSel).toEqual({ stepFrom: 8, stepTo: 9, noteFrom: 1, noteTo: 5 });
    });

    it('dropRect across bars cuts from the source pattern, pastes into the target and switches the edit bar', () => {
      light(2, 5); // pattern 0
      const src = { stepFrom: 2, stepTo: 2, noteFrom: 5, noteTo: 5 };
      const data = copyRect(useBassStore.getState().patterns, LAYOUT, 0, src);
      useBassStore.getState().dropRect(data, 6, 0, src, { src: 0, dst: 1 });
      const p = useBassStore.getState().patterns;
      expect(getStep(p, 0, 2).on).toBe(false); // cleared in pattern 0
      expect(getStep(p, 1, 6).note).toBe(5); // landed in pattern 1
      expect(useBassStore.getState().editPattern).toBe(1);
    });

    it('dropRect with clearSrc (CUT) clears the source in the same undo entry', () => {
      light(2, 5);
      useBassStore.getState().toggleStepSlide(2, 0);
      const src = { stepFrom: 2, stepTo: 2, noteFrom: 5, noteTo: 5 };
      const data = copyRect(useBassStore.getState().patterns, LAYOUT, 0, src);
      useBassStore.getState().dropRect(data, 10, 1, src);
      let p = useBassStore.getState().patterns;
      expect(getStep(p, 0, 2).on).toBe(false);
      const dropped = getStep(p, 0, 10);
      expect(dropped.note).toBe(6);
      expect(dropped.slide).toBe(true); // bit7 rides along through the drop
      useBassStore.getState().undo(); // one entry restores source AND removes the drop
      p = useBassStore.getState().patterns;
      expect(getStep(p, 0, 2).note).toBe(5);
      expect(getStep(p, 0, 10).on).toBe(false);
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

  describe('moveStepNote', () => {
    it('moves a note preserving oct/acc/slide/duration and clears the source', () => {
      const s = useBassStore.getState();
      s.toggleCell(2, 4);
      s.toggleStepAcc(2);
      s.toggleStepSlide(2);
      s.cycleStepOct(2);
      s.setStepDuration(2, 3);
      s.moveStepNote(2, 6, 9);
      expect(getStep(useBassStore.getState().patterns, 0, 2)).toMatchObject({ on: false });
      expect(getStep(useBassStore.getState().patterns, 0, 6)).toMatchObject({ on: true, note: 9, acc: true, slide: true, oct: 1, duration: 3 });
    });

    it('copy keeps the source and one undo unwinds the whole move', () => {
      const s = useBassStore.getState();
      s.toggleCell(0, 5);
      s.moveStepNote(0, 4, 5, { copy: true });
      expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 5 });
      expect(getStep(useBassStore.getState().patterns, 0, 4)).toMatchObject({ on: true, note: 5 });
      useBassStore.getState().undo();
      expect(getStep(useBassStore.getState().patterns, 0, 4)).toMatchObject({ on: false });
      expect(getStep(useBassStore.getState().patterns, 0, 0)).toMatchObject({ on: true, note: 5 });
    });

    it('changes pitch in place and no-ops on an off source or unchanged target', () => {
      const s = useBassStore.getState();
      s.toggleCell(3, 2);
      s.moveStepNote(3, 3, 10);
      expect(getStep(useBassStore.getState().patterns, 0, 3)).toMatchObject({ on: true, note: 10 });
      const before = useBassStore.getState().patterns;
      s.moveStepNote(3, 3, 10);
      expect(useBassStore.getState().patterns).toBe(before);
      s.moveStepNote(7, 9, 4);
      expect(useBassStore.getState().patterns).toBe(before);
    });
  });
});
