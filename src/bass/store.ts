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
  cycleOct, EMPTY_STEP, getStep, LAYOUT, NPATTERNS, randomPattern, setStep, STEPS, writePattern, type Patterns,
} from './seq';
import {
  copyPattern, copyRange, clearRange, makeHistory, pasteRange, pastePattern, shiftRange,
} from '../shared/seqEdit';
import { sequenceChain, sequenceLengthFromChain } from '../sequenceLength';

export let bassEngine = new BassEngine();
const initialState = patchToState(FACTORY_PATCHES[0]);

// Contiguous step range within the currently edited pattern.
export interface StepSel { from: number; to: number }
export type SeqClipboard =
  | { kind: 'range'; data: Uint8Array }
  | { kind: 'pattern'; data: Uint8Array };

const seqNorm = (sel: StepSel): [number, number] =>
  sel.from <= sel.to ? [sel.from, sel.to] : [sel.to, sel.from];

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

  stepSel: StepSel | null;
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

  setStepSelection: (sel: StepSel | null) => void;
  shiftClickStep: (step: number) => void;
  selectAllSteps: () => void;
  clearStepSelection: () => void;
  copySelection: () => void;
  cutSelection: () => void;
  pasteSelection: () => void;
  duplicateSelection: () => void;
  deleteSelection: () => void;
  shiftSelection: (dest: number, opts?: { copy?: boolean }) => void;
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

  stepSel: null,
  clipboard: null,

  setParam: (id, v) => {
    bassEngine.setParam(id, v);
    set((state) => ({ params: { ...state.params, [id]: v } }));
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
    set({ patterns: next });
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
    set({ patterns: next });
    bassEngine.setPatterns(next);
  },

  randomize: () => {
    const { patterns, editPattern } = get();
    get()._setPatterns(writePattern(patterns, editPattern, randomPattern()));
  },

  setEditPattern: (i) => set({ editPattern: i }),

  // ---------- selection ----------

  setStepSelection: (sel) => set({ stepSel: sel }),

  // Shift-click: first click anchors a single-step range; further shift-clicks
  // extend the range from that anchor (plain click keeps toggling cells).
  shiftClickStep: (step) => {
    const cur = get().stepSel;
    set({ stepSel: cur ? { from: cur.from, to: step } : { from: step, to: step } });
  },

  selectAllSteps: () => set({ stepSel: { from: 0, to: STEPS - 1 } }),

  clearStepSelection: () => set({ stepSel: null }),

  // ---------- verbs ----------

  copySelection: () => {
    const { patterns, editPattern, stepSel } = get();
    if (stepSel) {
      const [lo, hi] = seqNorm(stepSel);
      set({ clipboard: { kind: 'range', data: copyRange(patterns, LAYOUT, editPattern, lo, hi) } });
    } else {
      set({ clipboard: { kind: 'pattern', data: copyPattern(patterns, LAYOUT, editPattern) } });
    }
  },

  cutSelection: () => {
    const { patterns, editPattern, stepSel } = get();
    if (stepSel) {
      const [lo, hi] = seqNorm(stepSel);
      set({ clipboard: { kind: 'range', data: copyRange(patterns, LAYOUT, editPattern, lo, hi) } });
      get()._setPatterns(clearRange(patterns, LAYOUT, editPattern, lo, hi, EMPTY_STEP));
    } else {
      set({ clipboard: { kind: 'pattern', data: copyPattern(patterns, LAYOUT, editPattern) } });
      get()._setPatterns(clearRange(patterns, LAYOUT, editPattern, 0, STEPS - 1, EMPTY_STEP));
    }
  },

  pasteSelection: () => {
    const { patterns, editPattern, stepSel, clipboard } = get();
    if (!clipboard) return;
    if (clipboard.kind === 'range') {
      const at = stepSel ? seqNorm(stepSel)[0] : 0;
      get()._setPatterns(pasteRange(patterns, LAYOUT, editPattern, at, clipboard.data));
    } else {
      get()._setPatterns(pastePattern(patterns, LAYOUT, editPattern, clipboard.data));
    }
  },

  // With a range selected: paste a copy immediately after it (clamped).
  // With no selection: copy the edit pattern to the next bar, extending the
  // sequence length if needed (up to 4 bars) — the classic "duplicate bar".
  duplicateSelection: () => {
    const { patterns, editPattern, stepSel, chain } = get();
    if (stepSel) {
      const [lo, hi] = seqNorm(stepSel);
      const dest = hi + 1;
      // Range ends on the last step: nothing past it to paste onto — no-op.
      if (dest >= STEPS) return;
      const data = copyRange(patterns, LAYOUT, editPattern, lo, hi);
      get()._setPatterns(pasteRange(patterns, LAYOUT, editPattern, dest, data));
      set({ stepSel: { from: dest, to: Math.min(STEPS - 1, dest + (hi - lo)) } });
      return;
    }
    const target = Math.min(NPATTERNS - 1, editPattern + 1);
    const data = copyPattern(patterns, LAYOUT, editPattern);
    get()._setPatterns(pastePattern(patterns, LAYOUT, target, data));
    if (chain.length <= target) get().setSequenceLength(target + 1);
    set({ editPattern: target });
  },

  deleteSelection: () => {
    const { patterns, editPattern, stepSel } = get();
    if (!stepSel) return;
    const [lo, hi] = seqNorm(stepSel);
    get()._setPatterns(clearRange(patterns, LAYOUT, editPattern, lo, hi, EMPTY_STEP));
  },

  // Step-range drag: with a range selected, dragging inside it shifts the
  // range so it starts at `dest` (move; Alt = copy) via shared `shiftRange`.
  // Cancelling (Esc) never calls this, so nothing to undo there.
  shiftSelection: (dest, opts) => {
    const { patterns, editPattern, stepSel } = get();
    if (!stepSel) return;
    const [lo, hi] = seqNorm(stepSel);
    const clampedDest = Math.max(0, Math.min(STEPS - 1, dest | 0));
    get()._setPatterns(
      shiftRange(patterns, LAYOUT, editPattern, lo, hi, clampedDest, { copy: opts?.copy, emptyStep: EMPTY_STEP }),
    );
    set({ stepSel: { from: clampedDest, to: Math.min(STEPS - 1, clampedDest + (hi - lo)) } });
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
    set({ chain });
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
    set({ userPatches, patchValue: savedIndex >= 0 ? `u${savedIndex}` : state.patchValue });
  },

  loadPatchByValue: (value) => {
    const patch = value[0] === 'f'
      ? FACTORY_PATCHES[Number(value.slice(1))]
      : get().userPatches[Number(value.slice(1))];
    if (!patch) return;
    get()._clearHistory();
    set({ stepSel: null, clipboard: null });

    const state = patchToState(patch);
    if (get().hosted) {
      bassEngine.panic();
      bassEngine.params = { ...state.params };
      bassEngine.applyAllParams();
      set({ params: state.params, patchValue: value });
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
