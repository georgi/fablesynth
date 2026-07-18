// src/drum/store.test.ts
import { describe, it, expect, beforeEach } from 'vitest';
import { drumEngine, useDrumStore } from './store';
import { DrumEngine } from './engine/drum-synth';
import { patIdx, STEPS } from './seq';
import { defaultDrumParams } from './params';
import { FACTORY_PATCHES, patchOptions } from './patches';

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
      params: defaultDrumParams(), sel: 0, editPattern: 0, chain: [0],
      patterns: new Uint8Array(4 * 16 * 16), hosted: false,
      stepSel: null, selAllPads: false, clipboard: null,
    });
    useDrumStore.getState()._clearHistory();
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

  it('sequence length plays bars from 1 through N and editing does not change it', () => {
    const s = useDrumStore.getState();
    s.setSequenceLength(3);
    expect(useDrumStore.getState().chain).toEqual([0, 1, 2]);
    useDrumStore.getState().setEditPattern(2);
    expect(useDrumStore.getState().editPattern).toBe(2);
    expect(useDrumStore.getState().chain).toEqual([0, 1, 2]);
    useDrumStore.getState().setSequenceLength(99);
    expect(useDrumStore.getState().chain).toEqual([0, 1, 2, 3]);
  });

  it('stepPatch cycles from empty: +1 lands on first factory patch, -1 on last option', () => {
    useDrumStore.setState({ patchValue: '', userPatches: [] });
    useDrumStore.getState().stepPatch(1);
    expect(useDrumStore.getState().patchValue).toBe('f0');
    useDrumStore.setState({ patchValue: '' });
    useDrumStore.getState().stepPatch(-1);
    const options = patchOptions(useDrumStore.getState().userPatches);
    expect(useDrumStore.getState().patchValue).toBe(options[options.length - 1].value);
  });

  it('applyPatchByValue applies to the selected pad only and preserves out/choke', () => {
    useDrumStore.setState({ patchValue: '', userPatches: [] });
    const s = useDrumStore.getState();
    s.selectPad(3);
    s.setParam('pad3.out', 2);
    s.setParam('pad2.oscA.tune', 7);
    useDrumStore.getState().applyPatchByValue('f0');
    const state = useDrumStore.getState();
    expect(state.patchValue).toBe('f0');
    const patch = FACTORY_PATCHES[0];
    for (const [rel, value] of Object.entries(patch.params)) {
      expect(state.params[`pad3.${rel}`]).toBe(value);
    }
    expect(state.params['pad3.out']).toBe(2);
    expect(state.params['pad2.oscA.tune']).toBe(7);
    // selecting another pad clears the patch readout
    useDrumStore.getState().selectPad(0);
    expect(useDrumStore.getState().patchValue).toBe('');
  });

  it('savePatch stores a user patch and points patchValue at it', () => {
    useDrumStore.setState({ patchValue: '', userPatches: [] });
    const s = useDrumStore.getState();
    s.selectPad(1);
    s.setParam('pad1.oscA.tune', -9);
    useDrumStore.getState().savePatch('MY TUNE');
    const state = useDrumStore.getState();
    expect(state.patchValue).toBe('u0');
    expect(state.userPatches[0].name).toBe('MY TUNE');
    expect(state.userPatches[0].params['oscA.tune']).toBe(-9);
    // re-applying the saved patch on another pad reproduces the value
    useDrumStore.getState().selectPad(4);
    useDrumStore.getState().applyPatchByValue('u0');
    expect(useDrumStore.getState().params['pad4.oscA.tune']).toBe(-9);
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

  it('clears pattern undo history when a standalone kit replaces the song buffer', () => {
    useDrumStore.getState().toggleStep(0);
    useDrumStore.getState().loadKitByValue('f1');
    const loaded = useDrumStore.getState().patterns;
    useDrumStore.getState().undo();
    expect(useDrumStore.getState().patterns).toBe(loaded);
  });
});

describe('step selection', () => {
  beforeEach(() => {
    useDrumStore.setState({
      sel: 0, editPattern: 0, chain: [0], patterns: new Uint8Array(4 * 16 * 16),
      stepSel: null, selAllPads: false, clipboard: null,
    });
    useDrumStore.getState()._clearHistory();
  });

  it('setStepSelHead starts a single-step range, then extends the head from the fixed anchor', () => {
    const s = useDrumStore.getState();
    s.setStepSelHead(4);
    expect(useDrumStore.getState().stepSel).toEqual({ anchor: 4, head: 4 });
    useDrumStore.getState().setStepSelHead(9);
    expect(useDrumStore.getState().stepSel).toEqual({ anchor: 4, head: 9 });
    // extending back past the anchor keeps the anchor fixed (reversed range)
    useDrumStore.getState().setStepSelHead(1);
    expect(useDrumStore.getState().stepSel).toEqual({ anchor: 4, head: 1 });
  });

  it('selectAllSteps spans the whole row and flags selAllPads; clearStepSel resets both', () => {
    useDrumStore.getState().selectAllSteps();
    expect(useDrumStore.getState().stepSel).toEqual({ anchor: 0, head: STEPS - 1 });
    expect(useDrumStore.getState().selAllPads).toBe(true);
    useDrumStore.getState().clearStepSel();
    expect(useDrumStore.getState().stepSel).toBeNull();
    expect(useDrumStore.getState().selAllPads).toBe(false);
  });
});

describe('step editing verbs', () => {
  beforeEach(() => {
    useDrumStore.setState({
      sel: 2, editPattern: 0, chain: [0], patterns: new Uint8Array(4 * 16 * 16),
      stepSel: null, selAllPads: false, clipboard: null,
    });
    useDrumStore.getState()._clearHistory();
  });

  it('copySelection captures a range on the selected pad row', () => {
    const s = useDrumStore.getState();
    s.toggleStep(3); // on
    s.toggleStep(4); s.toggleStep(4); // accent
    s.setStepSelHead(3);
    s.setStepSelHead(4);
    s.copySelection();
    const clip = useDrumStore.getState().clipboard;
    expect(clip).toEqual({ kind: 'range', data: Uint8Array.of(1, 2) });
  });

  it('copySelection with no selection falls back to the whole edit pattern (all pads)', () => {
    const s = useDrumStore.getState();
    s.selectPad(5);
    s.toggleStep(2);
    s.copySelection();
    const clip = useDrumStore.getState().clipboard;
    expect(clip?.kind).toBe('pattern');
    expect(clip?.data.length).toBe(16 * 16);
  });

  it('cutSelection copies then clears the selected range only', () => {
    const s = useDrumStore.getState();
    s.toggleStep(5); s.toggleStep(5); // accent at step 5
    s.setStepSelHead(5);
    s.cutSelection();
    expect(useDrumStore.getState().clipboard).toEqual({ kind: 'range', data: Uint8Array.of(2) });
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 5)]).toBe(0);
  });

  it('deleteSelection is a no-op with nothing selected, clears when a range is active', () => {
    const s = useDrumStore.getState();
    s.toggleStep(6);
    const before = useDrumStore.getState().patterns;
    s.deleteSelection(); // no selection
    expect(useDrumStore.getState().patterns).toBe(before);
    s.setStepSelHead(6);
    useDrumStore.getState().deleteSelection();
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 6)]).toBe(0);
  });

  it('pasteSelection (range) lands at the selection start on the selected pad row', () => {
    const s = useDrumStore.getState();
    s.toggleStep(0); s.toggleStep(0); // accent at step 0
    s.setStepSelHead(0);
    s.copySelection();
    s.clearStepSel();
    s.selectPad(7);
    s.setStepSelHead(10);
    useDrumStore.getState().pasteSelection();
    expect(useDrumStore.getState().patterns[patIdx(0, 7, 10)]).toBe(2);
  });

  it('pasteSelection (whole pattern) with no selection lands on the current edit pattern', () => {
    const s = useDrumStore.getState();
    s.selectPad(1);
    s.toggleStep(0);
    s.copySelection(); // whole-pattern clipboard
    s.setEditPattern(1);
    useDrumStore.getState().pasteSelection();
    expect(useDrumStore.getState().patterns[patIdx(1, 1, 0)]).toBe(1);
  });

  it('duplicateSelection with a range copies it immediately after and moves the selection there', () => {
    const s = useDrumStore.getState();
    s.toggleStep(0); s.toggleStep(1);
    s.setStepSelHead(0);
    s.setStepSelHead(1);
    s.duplicateSelection();
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 2)]).toBe(1);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 3)]).toBe(1);
    expect(useDrumStore.getState().stepSel).toEqual({ anchor: 2, head: 3 });
  });

  it('duplicateSelection with a range ending on the last step is a full no-op', () => {
    const s = useDrumStore.getState();
    s.toggleStep(15);
    s.setStepSelHead(14);
    s.setStepSelHead(15); // range {14..15}
    const before = useDrumStore.getState().patterns;
    useDrumStore.getState().duplicateSelection();
    expect(useDrumStore.getState().patterns).toBe(before); // step 15 not corrupted
  });

  it('narrowing the selection after Cmd-A drops the whole-pattern flag', () => {
    useDrumStore.setState({ sel: 0 });
    const s = useDrumStore.getState();
    s.toggleStep(0); // pad 0, step 0
    useDrumStore.setState({ sel: 1 });
    useDrumStore.getState().toggleStep(4); // pad 1, step 4
    useDrumStore.getState().selectAllSteps();
    useDrumStore.getState().setStepSelHead(4); // shift-click narrows to {0..4} on pad 1
    expect(useDrumStore.getState().selAllPads).toBe(false);
    useDrumStore.getState().deleteSelection();
    expect(useDrumStore.getState().patterns[patIdx(0, 1, 4)]).toBe(0); // narrowed range cleared
    expect(useDrumStore.getState().patterns[patIdx(0, 0, 0)]).toBe(1); // other pads untouched
  });

  it('duplicateSelection with no selection copies the current bar to the next, extending sequence length', () => {
    const s = useDrumStore.getState();
    s.toggleStep(0);
    s.duplicateSelection();
    expect(useDrumStore.getState().editPattern).toBe(1);
    expect(useDrumStore.getState().patterns[patIdx(1, 2, 0)]).toBe(1);
    expect(useDrumStore.getState().chain).toEqual([0, 1]);
  });

  it('duplicateSelection is a no-op on the last bar with no selection', () => {
    useDrumStore.setState({ editPattern: 3 });
    const before = useDrumStore.getState().patterns;
    useDrumStore.getState().duplicateSelection();
    expect(useDrumStore.getState().patterns).toBe(before);
    expect(useDrumStore.getState().editPattern).toBe(3);
  });

  it('shiftSelection moves a range to a new column and relocates the selection', () => {
    const s = useDrumStore.getState();
    s.toggleStep(0); s.toggleStep(0); // accent
    s.setStepSelHead(0);
    s.shiftSelection(8);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 0)]).toBe(0);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 8)]).toBe(2);
    expect(useDrumStore.getState().stepSel).toEqual({ anchor: 8, head: 8 });
  });

  it('shiftSelection with copy leaves the source hit in place', () => {
    const s = useDrumStore.getState();
    s.toggleStep(0);
    s.setStepSelHead(0);
    s.shiftSelection(8, { copy: true });
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 0)]).toBe(1);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 8)]).toBe(1);
  });

  it('movePattern swaps two patterns across every pad', () => {
    const s = useDrumStore.getState();
    s.selectPad(0);
    s.setEditPattern(0);
    s.toggleStep(0); // pad 0, pattern 0, step 0 -> on
    s.setEditPattern(2);
    s.toggleStep(3); // pad 0, pattern 2, step 3 -> on
    useDrumStore.getState().movePattern(0, 2);
    expect(useDrumStore.getState().patterns[patIdx(0, 0, 0)]).toBe(0);
    expect(useDrumStore.getState().patterns[patIdx(2, 0, 0)]).toBe(1);
    expect(useDrumStore.getState().patterns[patIdx(0, 0, 3)]).toBe(1);
    expect(useDrumStore.getState().patterns[patIdx(2, 0, 3)]).toBe(0);
  });

  it('movePattern with copy leaves the source pattern untouched', () => {
    const s = useDrumStore.getState();
    s.setEditPattern(0);
    s.toggleStep(0);
    useDrumStore.getState().movePattern(0, 1, { copy: true });
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 0)]).toBe(1);
    expect(useDrumStore.getState().patterns[patIdx(1, 2, 0)]).toBe(1);
  });

  it('undo/redo roll the patterns buffer back and forward across an editing verb', () => {
    const s = useDrumStore.getState();
    s.toggleStep(0);
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 0)]).toBe(1);
    useDrumStore.getState().undo();
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 0)]).toBe(0);
    useDrumStore.getState().redo();
    expect(useDrumStore.getState().patterns[patIdx(0, 2, 0)]).toBe(1);
  });

  it('undo is a no-op past the bottom of the history stack', () => {
    const before = useDrumStore.getState().patterns;
    useDrumStore.getState().undo();
    expect(useDrumStore.getState().patterns).toBe(before);
  });
});

describe('hosted mode', () => {
  it('attachHosted swaps the engine singleton and mirrors its params', () => {
    const foreign = new DrumEngine();
    foreign.params = { 'pad0.oscA.table': 3 };
    useDrumStore.getState().attachHosted(foreign);
    const s = useDrumStore.getState();
    expect(s.hosted).toBe(true);
    expect(s.powered).toBe(true);
    expect(s.playing).toBe(false);
    expect(s.params['pad0.oscA.table']).toBe(3);
    expect(drumEngine).toBe(foreign);
  });

  it('play/stop are inert while hosted', () => {
    useDrumStore.getState().attachHosted(new DrumEngine());
    useDrumStore.getState().play();
    expect(useDrumStore.getState().playing).toBe(false);
  });

  it('kit loads apply params only — patterns stay untouched', () => {
    useDrumStore.getState().attachHosted(new DrumEngine());
    const before = useDrumStore.getState().patterns;
    useDrumStore.getState().loadKitByValue('f1');
    const s = useDrumStore.getState();
    expect(s.patterns).toBe(before);
    expect(s.kitValue).toBe('f1');
  });
});
