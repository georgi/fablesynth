// Zustand store for BL-1. Parameter and sequencer state live here and every
// mutation is mirrored to the imperative audio engine.

import { create } from 'zustand';
import type { ParamValues } from '../params';
import { BassEngine } from './engine/bass-synth';
import {
  FACTORY_PATCHES, loadUserPatches, patchOptions, patchToState,
  saveUserPatch, stateToPatch, type BassPatch,
} from './patches';
import {
  cycleOct, EMPTY_STEP, getStep, LAYOUT, NOTE_LANES, NPATTERNS, randomPattern, setStep, STEPS, writePattern, type Patterns,
} from './seq';
import {
  clearRect, copyPattern, clearRange, copyRect, makeHistory, moveRect, pasteRect, pastePattern, rectNorm,
  type RectCells, type RectSel,
} from '../shared/seqEdit';
import { sequenceChain, sequenceLengthFromChain } from '../sequenceLength';

export let bassEngine = new BassEngine();
const initialState = patchToState(FACTORY_PATCHES[0]);

export type { RectSel } from '../shared/seqEdit';
export type SeqClipboard =
  | { kind: 'rect'; data: RectCells }
  | { kind: 'pattern'; data: Uint8Array };

// Module-level undo/redo stack over the full patterns buffer, pushed by every
// mutation that goes through `_setPatterns`. Not store state — mirrors
// `bassEngine` living outside the reactive slice.
const history = makeHistory<Patterns>();

// Continuous note-length drags fire setStepDuration once per column crossed;
// a whole gesture must cost ONE undo entry, not one per column. While the
// same (pattern, step) duration keeps changing with no other edit in between,
// only the first change pushes history; any other mutation (or undo/redo)
// ends the run.
let durationGestureKey: string | null = null;

export interface BassStore {
  params: ParamValues;
  patterns: Patterns;
  chain: number[];
  editPattern: number;
  playing: boolean;
  curStep: number;
  curPat: number;
  curSemi: number; // sounding seq/keyboard note (offset from root), -100 = none
  powered: boolean;
  hosted: boolean;
  midiActive: boolean;
  patchValue: string;
  userPatches: BassPatch[];
  heldSemis: number[];
  vizPos: number;
  vizEnv: number;
  vizFenv: number;
  vizCut: number;
  vizGate: boolean;
  hitTick: number; // performance.now() of the last (non-slid) seq hit
  hitAcc: boolean;
  dirty: boolean; // any param/pattern change since the patch was loaded/saved

  rectSel: RectSel | null;
  lastCell: { step: number; note: number } | null;
  clipboard: SeqClipboard | null;

  setParam: (id: string, v: number) => void;
  noteOn: (semi: number, vel: number) => void;
  noteOff: (semi: number) => void;
  _setPatterns: (next: Patterns) => void;
  _clearHistory: () => void;
  toggleCell: (step: number, note: number, pattern?: number) => void;
  cycleStepOct: (step: number, pattern?: number) => void;
  toggleStepAcc: (step: number, pattern?: number) => void;
  toggleStepSlide: (step: number, pattern?: number) => void;
  setStepDuration: (step: number, duration: number, pattern?: number) => void;
  randomize: () => void;
  setEditPattern: (i: number) => void;
  setSequenceLength: (length: number) => void;

  setRectSel: (sel: RectSel | null) => void;
  selectAllSteps: () => void;
  clearStepSelection: () => void;
  copySelection: () => void;
  cutSelection: () => void;
  pasteSelection: () => void;
  duplicateSelection: () => void;
  deleteSelection: () => void;
  moveRectSel: (dStep: number, dNote: number, opts?: { copy?: boolean }) => void;
  moveStepNote: (from: number, to: number, note: number, opts?: { copy?: boolean }, pattern?: number) => void;
  movePattern: (from: number, to: number, opts?: { copy?: boolean }) => void;
  undo: () => void;
  redo: () => void;
  setMidiActive: (on: boolean) => void;
  play: () => void;
  stop: () => void;
  attachHosted: (engine: BassEngine) => void;
  powerOn: () => Promise<void>;
  savePatch: (name: string) => void;
  loadPatchByValue: (value: string) => void;
  stepPatch: (delta: number) => void;
}

export const useBassStore = create<BassStore>((set, get) => ({
  params: initialState.params,
  patterns: initialState.patterns,
  chain: sequenceChain(sequenceLengthFromChain(initialState.chain)),
  editPattern: 0,
  playing: false,
  curStep: -1,
  curPat: 0,
  curSemi: -100,
  powered: false,
  hosted: false,
  midiActive: false,
  patchValue: 'f0',
  userPatches: loadUserPatches(),
  heldSemis: [],
  vizPos: -1,
  vizEnv: 0,
  vizFenv: 0,
  vizCut: -1,
  vizGate: false,
  hitTick: 0,
  hitAcc: false,
  dirty: false,

  rectSel: null,
  lastCell: null,
  clipboard: null,

  setParam: (id, v) => {
    bassEngine.setParam(id, v);
    set((state) => ({ params: { ...state.params, [id]: v }, dirty: true }));
  },

  noteOn: (semi, vel) => {
    bassEngine.noteOn(semi, vel);
    set((state) => {
      if (state.playing) return {};
      const heldSemis = [...state.heldSemis.filter((s) => s !== semi), semi];
      return { heldSemis, curSemi: semi, hitTick: performance.now(), hitAcc: false };
    });
  },

  noteOff: (semi) => {
    bassEngine.noteOff(semi);
    set((state) => {
      const heldSemis = state.heldSemis.filter((s) => s !== semi);
      if (state.playing) return { heldSemis };
      return { heldSemis, curSemi: heldSemis.length ? heldSemis[heldSemis.length - 1] : -100 };
    });
  },

  // Every pattern mutation writes the store and pushes to the engine in one
  // place. (Patterns persist inside patches, not standalone — no localStorage.)
  // Also the one place that feeds the undo/redo history: every edit verb
  // (toggle, randomize, cut/paste/duplicate/delete, pattern move) routes
  // through here, so undo covers them all uniformly.
  _setPatterns(next: Patterns) {
    durationGestureKey = null;
    history.push(get().patterns);
    set({ patterns: next, dirty: true });
    bassEngine.setPatterns(next);
  },

  _clearHistory: () => {
    history.clear();
    durationGestureKey = null;
  },

  toggleCell: (step, note, pattern) => {
    const { patterns, editPattern } = get();
    const pat = pattern ?? editPattern;
    const cur = getStep(patterns, pat, step);
    const next = cur.on && cur.note === note
      ? setStep(patterns, pat, step, { on: false, acc: false, slide: false, duration: 1 })
      : setStep(patterns, pat, step, { on: true, note });
    get()._setPatterns(next);
    if (pat === get().editPattern) set({ lastCell: { step, note } });
  },

  cycleStepOct: (step, pattern) => {
    const { patterns, editPattern } = get();
    const pat = pattern ?? editPattern;
    const cur = getStep(patterns, pat, step);
    get()._setPatterns(setStep(patterns, pat, step, { oct: cycleOct(cur.oct) }));
  },

  toggleStepAcc: (step, pattern) => {
    const { patterns, editPattern } = get();
    const pat = pattern ?? editPattern;
    const cur = getStep(patterns, pat, step);
    if (!cur.on) return;
    get()._setPatterns(setStep(patterns, pat, step, { acc: !cur.acc }));
  },

  toggleStepSlide: (step, pattern) => {
    const { patterns, editPattern } = get();
    const pat = pattern ?? editPattern;
    const cur = getStep(patterns, pat, step);
    if (!cur.on) return;
    get()._setPatterns(setStep(patterns, pat, step, { slide: !cur.slide }));
  },

  // Routed around _setPatterns so a continuous length-drag coalesces to a
  // single history entry (see durationGestureKey above).
  setStepDuration: (step, duration, pattern) => {
    const { patterns, editPattern } = get();
    const pat = pattern ?? editPattern;
    const cur = getStep(patterns, pat, step);
    if (!cur.on || cur.duration === duration) return;
    const key = `${pat}:${step}`;
    if (durationGestureKey !== key) history.push(patterns);
    durationGestureKey = key;
    const next = setStep(patterns, pat, step, { duration });
    set({ patterns: next, dirty: true });
    bassEngine.setPatterns(next);
  },

  randomize: () => {
    const { patterns, editPattern } = get();
    get()._setPatterns(writePattern(patterns, editPattern, randomPattern()));
  },

  setEditPattern: (i) => set({ editPattern: i }),

  // ---------- selection ----------

  setRectSel: (sel) => set({
    rectSel: sel
      ? {
          stepFrom: Math.min(STEPS - 1, Math.max(0, sel.stepFrom | 0)),
          stepTo: Math.min(STEPS - 1, Math.max(0, sel.stepTo | 0)),
          noteFrom: Math.min(NOTE_LANES - 1, Math.max(0, sel.noteFrom | 0)),
          noteTo: Math.min(NOTE_LANES - 1, Math.max(0, sel.noteTo | 0)),
        }
      : null,
  }),

  selectAllSteps: () => set({ rectSel: { stepFrom: 0, stepTo: STEPS - 1, noteFrom: 0, noteTo: NOTE_LANES - 1 } }),

  clearStepSelection: () => set({ rectSel: null }),

  // ---------- verbs ----------
  // _setPatterns pushes the undo entry itself, so each mutating verb below
  // makes exactly one _setPatterns call; copySelection makes none.

  copySelection: () => {
    const { patterns, editPattern, rectSel } = get();
    if (rectSel) {
      set({ clipboard: { kind: 'rect', data: copyRect(patterns, LAYOUT, editPattern, rectSel) } });
    } else {
      set({ clipboard: { kind: 'pattern', data: copyPattern(patterns, LAYOUT, editPattern) } });
    }
  },

  cutSelection: () => {
    const { patterns, editPattern, rectSel } = get();
    get().copySelection();
    if (rectSel) {
      get()._setPatterns(clearRect(patterns, LAYOUT, editPattern, rectSel, EMPTY_STEP));
    } else {
      get()._setPatterns(clearRange(patterns, LAYOUT, editPattern, 0, STEPS - 1, EMPTY_STEP));
    }
  },

  pasteSelection: () => {
    const { patterns, editPattern, rectSel, lastCell, clipboard } = get();
    if (!clipboard) return;
    if (clipboard.kind === 'pattern') {
      get()._setPatterns(pastePattern(patterns, LAYOUT, editPattern, clipboard.data));
      return;
    }
    // Anchor: current rect's top-left, else the last-touched cell, else in place.
    const anchor = rectSel
      ? { step: rectNorm(rectSel).stepLo, note: rectNorm(rectSel).noteHi }
      : lastCell ?? { step: 0, note: clipboard.data.noteHi };
    const dNote = anchor.note - clipboard.data.noteHi;
    get()._setPatterns(pasteRect(patterns, LAYOUT, editPattern, anchor.step, dNote, clipboard.data, NOTE_LANES - 1));
    get().setRectSel({
      stepFrom: anchor.step,
      stepTo: anchor.step + clipboard.data.wSteps - 1,
      noteFrom: anchor.note - (clipboard.data.noteHi - clipboard.data.noteLo),
      noteTo: anchor.note,
    });
  },

  // With a rect selected: paste a copy immediately after it (clamped).
  // With no selection: copy the edit pattern to the next bar, extending the
  // sequence length if needed (up to 4 bars) — the classic "duplicate bar".
  duplicateSelection: () => {
    const { patterns, editPattern, rectSel, chain } = get();
    if (rectSel) {
      const { stepLo, stepHi, noteLo, noteHi } = rectNorm(rectSel);
      const at = stepHi + 1;
      // Rect ends on the last step: nothing past it to paste onto — no-op.
      if (at >= STEPS) return;
      const data = copyRect(patterns, LAYOUT, editPattern, rectSel);
      get()._setPatterns(pasteRect(patterns, LAYOUT, editPattern, at, 0, data, NOTE_LANES - 1));
      get().setRectSel({ stepFrom: at, stepTo: at + (stepHi - stepLo), noteFrom: noteLo, noteTo: noteHi });
      return;
    }
    const target = Math.min(NPATTERNS - 1, editPattern + 1);
    const data = copyPattern(patterns, LAYOUT, editPattern);
    get()._setPatterns(pastePattern(patterns, LAYOUT, target, data));
    if (chain.length <= target) get().setSequenceLength(target + 1);
    set({ editPattern: target });
  },

  deleteSelection: () => {
    const { patterns, editPattern, rectSel } = get();
    if (!rectSel) return;
    get()._setPatterns(clearRect(patterns, LAYOUT, editPattern, rectSel, EMPTY_STEP));
  },

  // Rect drag: with a rect selected, dragging inside it moves (Alt = copy)
  // it by (dStep, dNote), clamped so the whole rect stays inside the grid —
  // stored selection and moved content must always agree. Cancelling (Esc)
  // never calls this, so nothing to undo there.
  moveRectSel: (dStep, dNote, opts = {}) => {
    const { patterns, editPattern, rectSel } = get();
    if (!rectSel) return;
    const { stepLo, stepHi, noteLo, noteHi } = rectNorm(rectSel);
    const ds = Math.min(STEPS - 1 - stepHi, Math.max(-stepLo, dStep | 0));
    const dn = Math.min(NOTE_LANES - 1 - noteHi, Math.max(-noteLo, dNote | 0));
    if (ds === 0 && dn === 0) return;
    get()._setPatterns(moveRect(patterns, LAYOUT, editPattern, rectSel, ds, dn, { copy: opts.copy, emptyStep: EMPTY_STEP, maxNote: NOTE_LANES - 1 }));
    set({ rectSel: { stepFrom: stepLo + ds, stepTo: stepHi + ds, noteFrom: noteLo + dn, noteTo: noteHi + dn } });
  },

  // Grid note drag: move (or Alt-copy) one lit step to another step/lane,
  // carrying oct/acc/slide/duration along. _setPatterns pushes the single
  // history entry.
  moveStepNote: (from, to, note, opts, pattern) => {
    const { patterns, editPattern } = get();
    const pat = pattern ?? editPattern;
    const src = getStep(patterns, pat, from);
    if (!src.on) return;
    if (from === to && src.note === note) return;
    let next = patterns;
    if (from !== to && !opts?.copy) next = setStep(next, pat, from, { on: false, acc: false, slide: false, duration: 1 });
    next = setStep(next, pat, to, { on: true, note, oct: src.oct, acc: src.acc, slide: src.slide, duration: src.duration });
    get()._setPatterns(next);
  },

  // Bar-chip drag: move pattern `from` onto `to` (swap); Alt-drag copies
  // `from` over `to` and leaves `from` untouched.
  movePattern: (from, to, opts) => {
    if (from === to) return;
    const { patterns } = get();
    const a = copyPattern(patterns, LAYOUT, from);
    let next = pastePattern(patterns, LAYOUT, to, a);
    if (!opts?.copy) {
      const b = copyPattern(patterns, LAYOUT, to);
      next = pastePattern(next, LAYOUT, from, b);
    }
    get()._setPatterns(next);
  },

  undo: () => {
    durationGestureKey = null;
    const prev = history.undo(get().patterns);
    if (!prev) return;
    set({ patterns: prev });
    bassEngine.setPatterns(prev);
  },

  redo: () => {
    durationGestureKey = null;
    const next = history.redo(get().patterns);
    if (!next) return;
    set({ patterns: next });
    bassEngine.setPatterns(next);
  },

  setSequenceLength: (length) => {
    const chain = sequenceChain(length);
    set({ chain, dirty: true });
    bassEngine.setChain(chain);
  },

  setMidiActive: (on) => set({ midiActive: on }),

  play: () => {
    if (get().hosted) return;
    bassEngine.play();
    set({ playing: true, heldSemis: [] });
  },

  stop: () => {
    if (get().hosted) return;
    bassEngine.stop();
    set({ playing: false, curStep: -1, curSemi: -100 });
  },

  attachHosted: (engine) => {
    bassEngine = engine;
    set({
      hosted: true,
      powered: true,
      playing: false,
      curStep: -1,
      curSemi: -100,
      params: { ...engine.params },
    });
  },

  powerOn: async () => {
    await bassEngine.init();
    bassEngine.onstep = (data) => set(() => {
      const base: Partial<BassStore> = { curStep: data.s, curPat: data.pat };
      if (data.semi > -100) {
        base.curSemi = data.semi;
        base.hitTick = performance.now();
        base.hitAcc = data.acc;
      }
      return base;
    });
    bassEngine.onviz = (data) => set({
      vizPos: data.pos, vizEnv: data.env, vizFenv: data.fenv, vizCut: data.cut, vizGate: data.gate,
    });
    bassEngine.params = { ...get().params };
    bassEngine.applyAllParams();
    bassEngine.setPatterns(get().patterns);
    bassEngine.setChain(get().chain);
    set({ powered: true });
  },

  savePatch: (name) => {
    const state = get();
    const patch = stateToPatch(name, state.params, state.patterns, state.chain);
    const userPatches = saveUserPatch(name, patch);
    const savedIndex = userPatches.findIndex((entry) => entry.name === name);
    set({ userPatches, patchValue: savedIndex >= 0 ? `u${savedIndex}` : state.patchValue, dirty: false });
  },

  loadPatchByValue: (value) => {
    const patch = value[0] === 'f'
      ? FACTORY_PATCHES[Number(value.slice(1))]
      : get().userPatches[Number(value.slice(1))];
    if (!patch) return;
    get()._clearHistory();
    set({ rectSel: null, lastCell: null, clipboard: null });

    const state = patchToState(patch);
    if (get().hosted) {
      bassEngine.panic();
      bassEngine.params = { ...state.params };
      bassEngine.applyAllParams();
      set({ params: state.params, patchValue: value, dirty: false });
      return;
    }
    bassEngine.panic();
    bassEngine.params = { ...state.params };
    bassEngine.applyAllParams();
    bassEngine.setPatterns(state.patterns);
    const chain = sequenceChain(sequenceLengthFromChain(state.chain));
    bassEngine.setChain(chain);
    set({
      params: state.params,
      patterns: state.patterns,
      chain,
      editPattern: 0,
      patchValue: value,
      dirty: false,
    });
  },

  stepPatch: (delta) => {
    const options = patchOptions(get().userPatches);
    if (!options.length) return;
    let index = options.findIndex((option) => option.value === get().patchValue);
    if (index < 0) index = 0;
    else index = (index + delta + options.length) % options.length;
    get().loadPatchByValue(options[index].value);
  },
}));
