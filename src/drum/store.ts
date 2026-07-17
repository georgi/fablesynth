// Zustand store for DR-1. Parameter and sequencer state live here and every
// mutation is mirrored to the imperative audio engine.

import { create } from 'zustand';
import type { ParamValues } from '../params';
import {
  deserializeUserTable,
  framesFromGenerated,
  makeUserTable,
  serializeUserTable,
} from '../engine/usertables';
import type { GeneratedTable } from '../engine/wavetables';
import { DrumEngine } from './engine/drum-synth';
import { FACTORY_KITS, kitToState, loadUserKits, saveUserKit, stateToKit, type Kit } from './kits';
import {
  FACTORY_PATCHES, applyPatchToParams, extractPatch, loadUserPatches,
  patchOptions, saveUserPatch, type PadPatch,
} from './patches';
import { DRUM_TABLE_NAMES, PAD_COUNT, pad } from './params';
import {
  cycleStep, NPATTERNS, patIdx, STEPS, stepSelRange, type Patterns, type StepSel,
} from './seq';
import { sequenceChain, sequenceLengthFromChain } from '../sequenceLength';
import {
  clearRange, copyPattern, copyRange, makeHistory, pastePattern, pasteRange, shiftRange,
  type SeqLayout,
} from '../shared/seqEdit';

export let drumEngine = new DrumEngine();
const initialKitState = kitToState(FACTORY_KITS[0]);

// A pad's row is a lane within the flat pattern buffer (all pads share the
// same patternSize, so whole-pattern ops are lane-independent — see
// copyPattern/pastePattern in seqEdit.ts).
const padLane = (padI: number): SeqLayout => ({
  stride: 1,
  stepsPerPattern: STEPS,
  patternSize: PAD_COUNT * STEPS,
  laneOffset: padI * STEPS,
});

// Whole-pattern block ops (copyPattern/pastePattern) span all pads and
// ignore laneOffset, so a single layout (no lane) covers every pad.
const WHOLE_PATTERN_LAYOUT: SeqLayout = { stride: 1, stepsPerPattern: STEPS, patternSize: PAD_COUNT * STEPS };

export type DrumClipboard =
  | { kind: 'range'; data: Uint8Array }
  | { kind: 'pattern'; data: Uint8Array }
  | null;

// Bounded undo/redo over the patterns buffer. Module-level (not store state)
// since snapshots aren't rendered; `_pushHistory` captures the pre-mutation
// buffer before every editing verb (knob/param changes never touch this).
const patternHistory = makeHistory<Patterns>();

export interface KitOption {
  value: string;
  name: string;
  group: 'FACTORY' | 'USER';
}

export function kitOptions(userKits: Kit[]): KitOption[] {
  const options: KitOption[] = FACTORY_KITS.map((kit, i) => ({
    value: `f${i}`,
    name: kit.name,
    group: 'FACTORY' as const,
  }));
  userKits.forEach((kit, i) => options.push({ value: `u${i}`, name: kit.name, group: 'USER' }));
  return options;
}

export interface DrumStore {
  params: ParamValues;
  sel: number;
  patterns: Patterns;
  chain: number[];
  editPattern: number;
  playing: boolean;
  curStep: number;
  curPat: number;
  powered: boolean;
  hosted: boolean;
  midiActive: boolean;
  kitValue: string;
  userKits: Kit[];
  patchValue: string;
  userPatches: PadPatch[];
  padNames: string[];
  hitTick: Record<number, number>;
  mode: 'step' | 'pads';
  modPosA: number;
  modPosB: number;
  envLevel: number;
  userTables: GeneratedTable[];
  stepSel: StepSel | null;
  selAllPads: boolean;
  clipboard: DrumClipboard;

  setParam: (id: string, v: number) => void;
  selectPad: (i: number) => void;
  triggerPad: (i: number, vel: number) => void;
  _setPatterns: (next: Patterns) => void;
  _pushHistory: () => void;
  _clearHistory: () => void;
  toggleStep: (step: number) => void;
  setEditPattern: (i: number) => void;
  setSequenceLength: (length: number) => void;
  setStepSelHead: (step: number) => void;
  selectAllSteps: () => void;
  clearStepSel: () => void;
  copySelection: () => void;
  cutSelection: () => void;
  pasteSelection: () => void;
  duplicateSelection: () => void;
  deleteSelection: () => void;
  shiftSelection: (dest: number, opts?: { copy?: boolean }) => void;
  movePattern: (from: number, to: number, opts?: { copy?: boolean }) => void;
  undo: () => void;
  redo: () => void;
  setMode: (mode: 'step' | 'pads') => void;
  setMidiActive: (on: boolean) => void;
  setPadName: (i: number, name: string) => void;
  play: () => void;
  stop: () => void;
  attachHosted: (engine: DrumEngine) => void;
  powerOn: () => Promise<void>;
  saveKit: (name: string) => void;
  loadKitByValue: (value: string) => void;
  stepKit: (delta: number) => void;
  applyPatchByValue: (value: string) => void;
  stepPatch: (delta: number) => void;
  savePatch: (name: string) => void;
  setParamsFromKit: (params: ParamValues) => void;
  importPadTable: (padI: number, table: GeneratedTable) => void;
}

export const useDrumStore = create<DrumStore>((set, get) => ({
  params: initialKitState.params,
  sel: 0,
  patterns: initialKitState.patterns,
  chain: sequenceChain(sequenceLengthFromChain(initialKitState.chain)),
  editPattern: 0,
  playing: false,
  curStep: -1,
  curPat: 0,
  powered: false,
  hosted: false,
  midiActive: false,
  kitValue: 'f0',
  userKits: loadUserKits(),
  patchValue: '',
  userPatches: loadUserPatches(),
  padNames: initialKitState.padNames,
  hitTick: {},
  mode: 'step',
  modPosA: -1,
  modPosB: -1,
  envLevel: 0,
  userTables: [],
  stepSel: null,
  selAllPads: false,
  clipboard: null,

  setParam: (id, v) => {
    drumEngine.setParam(id, v);
    set((state) => ({ params: { ...state.params, [id]: v } }));
  },

  selectPad: (i) => {
    drumEngine.selectPad(i);
    drumEngine.trigger(i, 0.8);
    set((state) => ({ sel: i, patchValue: '', hitTick: { ...state.hitTick, [i]: performance.now() } }));
  },

  triggerPad: (i, vel) => {
    drumEngine.trigger(i, vel);
    set((state) => ({ hitTick: { ...state.hitTick, [i]: performance.now() } }));
  },

  // Every pattern mutation writes the store and pushes to the engine in one
  // place. (Patterns persist inside kits, not standalone — no localStorage.)
  _setPatterns(next: Patterns) {
    set({ patterns: next });
    drumEngine.setPatterns(next);
  },

  // Bounded undo/redo (50 snapshots) over the patterns buffer. Every editing
  // verb — including plain step toggling — pushes the pre-mutation buffer
  // here first; knob/param changes never touch history.
  _pushHistory: () => patternHistory.push(get().patterns),
  _clearHistory: () => patternHistory.clear(),

  toggleStep: (step) => {
    const { patterns, editPattern, sel } = get();
    get()._pushHistory();
    const next = patterns.slice();
    const index = patIdx(editPattern, sel, step);
    next[index] = cycleStep(next[index]);
    get()._setPatterns(next);
  },

  setEditPattern: (i) => set({ editPattern: i }),

  setSequenceLength: (length) => {
    const chain = sequenceChain(length);
    set({ chain });
    drumEngine.setChain(chain);
  },

  // Any head move drops the Cmd-A whole-pattern flag: the verbs must operate
  // on exactly the range the UI highlights.
  setStepSelHead: (step) => set((state) => (state.stepSel
    ? { stepSel: { anchor: state.stepSel.anchor, head: step }, selAllPads: false }
    : { stepSel: { anchor: step, head: step }, selAllPads: false })),

  selectAllSteps: () => set({ stepSel: { anchor: 0, head: STEPS - 1 }, selAllPads: true }),

  clearStepSel: () => set({ stepSel: null, selAllPads: false }),

  // Copy/cut fall back to the whole edit pattern (all pads) when nothing is
  // selected; a Cmd-A "select all" selection also copies as a whole pattern.
  copySelection: () => {
    const { patterns, editPattern, sel, stepSel, selAllPads } = get();
    const range = selAllPads ? null : stepSelRange(stepSel);
    if (range) {
      set({ clipboard: { kind: 'range', data: copyRange(patterns, padLane(sel), editPattern, range[0], range[1]) } });
    } else {
      set({ clipboard: { kind: 'pattern', data: copyPattern(patterns, WHOLE_PATTERN_LAYOUT, editPattern) } });
    }
  },

  // Clears exactly the region copySelection just captured (selection, or the
  // whole edit pattern as fallback) — shared by cutSelection and (guarded)
  // deleteSelection.
  cutSelection: () => {
    get().copySelection();
    const { patterns, editPattern, sel, stepSel, selAllPads } = get();
    const range = selAllPads ? null : stepSelRange(stepSel);
    get()._pushHistory();
    if (range) {
      get()._setPatterns(clearRange(patterns, padLane(sel), editPattern, range[0], range[1]));
    } else {
      get()._setPatterns(pastePattern(patterns, WHOLE_PATTERN_LAYOUT, editPattern, new Uint8Array(PAD_COUNT * STEPS)));
    }
  },

  // Distinct from cut: only clears an active selection, never the whole
  // pattern as an implicit fallback (Delete/Backspace with nothing selected
  // is a no-op, unlike Cmd/Ctrl-X).
  deleteSelection: () => {
    const { patterns, editPattern, sel, stepSel, selAllPads } = get();
    const range = selAllPads ? [0, STEPS - 1] as [number, number] : stepSelRange(stepSel);
    if (!range && !selAllPads) return;
    get()._pushHistory();
    if (selAllPads) {
      get()._setPatterns(pastePattern(patterns, WHOLE_PATTERN_LAYOUT, editPattern, new Uint8Array(PAD_COUNT * STEPS)));
    } else if (range) {
      get()._setPatterns(clearRange(patterns, padLane(sel), editPattern, range[0], range[1]));
    }
  },

  // Paste anchors at the selection start for range payloads (step 0 with no
  // selection); whole-pattern payloads always land on the current edit
  // pattern (all pads).
  pasteSelection: () => {
    const { clipboard, patterns, editPattern, sel, stepSel } = get();
    if (!clipboard) return;
    get()._pushHistory();
    if (clipboard.kind === 'pattern') {
      get()._setPatterns(pastePattern(patterns, WHOLE_PATTERN_LAYOUT, editPattern, clipboard.data));
      return;
    }
    const range = stepSelRange(stepSel);
    const at = range ? range[0] : 0;
    get()._setPatterns(pasteRange(patterns, padLane(sel), editPattern, at, clipboard.data));
  },

  // With a range selected: paste a copy of it immediately after (clamped).
  // With no selection (or the whole-pattern Cmd-A flag): duplicate the
  // current bar into the next one, extending the sequence length if needed.
  duplicateSelection: () => {
    const { patterns, editPattern, sel, stepSel, selAllPads } = get();
    const range = selAllPads ? null : stepSelRange(stepSel);
    if (range) {
      const [from, to] = range;
      // Range ends on the last step: nothing past it to paste onto — no-op.
      if (to + 1 >= STEPS) return;
      const data = copyRange(patterns, padLane(sel), editPattern, from, to);
      get()._pushHistory();
      get()._setPatterns(pasteRange(patterns, padLane(sel), editPattern, to + 1, data));
      const span = to - from;
      const newFrom = to + 1;
      set({ stepSel: { anchor: newFrom, head: Math.min(STEPS - 1, newFrom + span) } });
      return;
    }
    const nextPat = Math.min(NPATTERNS - 1, editPattern + 1);
    if (nextPat === editPattern) return; // already the last bar
    get()._pushHistory();
    const data = copyPattern(patterns, WHOLE_PATTERN_LAYOUT, editPattern);
    get()._setPatterns(pastePattern(patterns, WHOLE_PATTERN_LAYOUT, nextPat, data));
    if (get().chain.length <= nextPat) get().setSequenceLength(nextPat + 1);
    set({ editPattern: nextPat, stepSel: null, selAllPads: false });
  },

  // Step-range drag: moves (or Alt-copies) the selection to start at `dest`.
  shiftSelection: (dest, opts = {}) => {
    const { patterns, editPattern, sel, stepSel } = get();
    const range = stepSelRange(stepSel);
    if (!range) return;
    const [from, to] = range;
    get()._pushHistory();
    get()._setPatterns(shiftRange(patterns, padLane(sel), editPattern, from, to, dest, { copy: opts.copy }));
    const span = to - from;
    set({ stepSel: { anchor: dest, head: Math.min(STEPS - 1, dest + span) }, selAllPads: false });
  },

  // Bar-chip drag on SequenceLengthControl: move = swap patterns `from`/`to`
  // (all pads); Alt held = copy `from` over `to`, leaving `from` untouched.
  movePattern: (from, to, opts = {}) => {
    if (from === to) return;
    const { patterns } = get();
    const dataFrom = copyPattern(patterns, WHOLE_PATTERN_LAYOUT, from);
    get()._pushHistory();
    if (opts.copy) {
      get()._setPatterns(pastePattern(patterns, WHOLE_PATTERN_LAYOUT, to, dataFrom));
      return;
    }
    const dataTo = copyPattern(patterns, WHOLE_PATTERN_LAYOUT, to);
    const next = pastePattern(pastePattern(patterns, WHOLE_PATTERN_LAYOUT, to, dataFrom), WHOLE_PATTERN_LAYOUT, from, dataTo);
    get()._setPatterns(next);
  },

  undo: () => {
    const restored = patternHistory.undo(get().patterns);
    if (restored) get()._setPatterns(restored);
  },

  redo: () => {
    const restored = patternHistory.redo(get().patterns);
    if (restored) get()._setPatterns(restored);
  },

  setMode: (mode) => set({ mode }),
  setMidiActive: (on) => set({ midiActive: on }),

  setPadName: (i, name) => set((state) => {
    const padNames = state.padNames.slice();
    padNames[i] = name.toUpperCase().slice(0, 14);
    return { padNames };
  }),

  play: () => {
    if (get().hosted) return;
    drumEngine.play();
    set({ playing: true });
  },

  stop: () => {
    if (get().hosted) return;
    drumEngine.stop();
    set({ playing: false, curStep: -1 });
  },

  // SQ-4 hosted mode: adopt the rig's running engine. The conductor owns the
  // transport and the clip owns the patterns; this store keeps patch editing.
  attachHosted: (engine) => {
    drumEngine = engine;
    // The conductor drives the engine from the SQ-4 clip, so onstep never
    // fires here — flash pad LEDs from the hosted step's reported hits.
    engine.onhit = (pads) => set((state) => {
      const hitTick = { ...state.hitTick };
      const now = performance.now();
      pads.forEach((p) => { hitTick[p] = now; });
      return { hitTick };
    });
    set({
      hosted: true,
      powered: true,
      playing: false,
      curStep: -1,
      params: { ...engine.params },
    });
  },

  powerOn: async () => {
    await drumEngine.init();
    const userTables = get().userTables;
    if (userTables.length) drumEngine.setUserTables(userTables);
    drumEngine.onstep = (data) => set((state) => {
      const hitTick = { ...state.hitTick };
      const now = performance.now();
      data.hits.forEach((hit) => { hitTick[hit] = now; });
      return { curStep: data.s, curPat: data.pat, hitTick };
    });
    drumEngine.onviz = (data) => set({ modPosA: data.a, modPosB: data.b, envLevel: data.env });
    drumEngine.params = { ...get().params };
    drumEngine.applyAllParams();
    drumEngine.setPatterns(get().patterns);
    drumEngine.setChain(get().chain);
    set({ powered: true });
  },

  saveKit: (name) => {
    const state = get();
    const tables = state.userTables.map((table) =>
      serializeUserTable(makeUserTable(table.name, framesFromGenerated(table))));
    const kit = stateToKit(name, state.params, state.padNames, state.patterns, state.chain, tables);
    const userKits = saveUserKit(name, kit);
    const savedIndex = userKits.findIndex((entry) => entry.name === name);
    set({ userKits, kitValue: savedIndex >= 0 ? `u${savedIndex}` : state.kitValue });
  },

  loadKitByValue: (value) => {
    const kit = value[0] === 'f'
      ? FACTORY_KITS[Number(value.slice(1))]
      : get().userKits[Number(value.slice(1))];
    if (!kit) return;

    const state = kitToState(kit);
    if (get().hosted) {
      // Hosted: a kit is a sound, not a song — params only, clip keeps its pattern.
      const userTables = state.tables.map((table) => deserializeUserTable(table).table);
      drumEngine.panic();
      drumEngine.setUserTables(userTables);
      drumEngine.params = { ...state.params };
      drumEngine.applyAllParams();
      set({ params: state.params, padNames: state.padNames, userTables, kitValue: value, patchValue: '' });
      return;
    }
    const userTables = state.tables.map((table) => deserializeUserTable(table).table);
    const chain = sequenceChain(sequenceLengthFromChain(state.chain));
    drumEngine.panic();
    drumEngine.setUserTables(userTables);
    drumEngine.params = { ...state.params };
    drumEngine.applyAllParams();
    drumEngine.setPatterns(state.patterns);
    drumEngine.setChain(chain);
    set({
      params: state.params,
      padNames: state.padNames,
      patterns: state.patterns,
      chain,
      editPattern: chain[0] ?? 0,
      userTables,
      kitValue: value,
      patchValue: '',
    });
  },

  stepKit: (delta) => {
    const options = kitOptions(get().userKits);
    if (!options.length) return;
    let index = options.findIndex((option) => option.value === get().kitValue);
    if (index < 0) index = 0;
    index = (index + delta + options.length) % options.length;
    get().loadKitByValue(options[index].value);
  },

  applyPatchByValue: (value) => {
    const patch = value[0] === 'f'
      ? FACTORY_PATCHES[Number(value.slice(1))]
      : get().userPatches[Number(value.slice(1))];
    if (!patch) return;
    const entries = applyPatchToParams(get().params, get().sel, patch);
    for (const [id, v] of Object.entries(entries)) get().setParam(id, v);
    set({ patchValue: value });
  },

  stepPatch: (delta) => {
    const options = patchOptions(get().userPatches);
    if (!options.length) return;
    let index = options.findIndex((option) => option.value === get().patchValue);
    index = index < 0
      ? (delta > 0 ? 0 : options.length - 1)
      : (index + delta + options.length) % options.length;
    get().applyPatchByValue(options[index].value);
  },

  savePatch: (name) => {
    const state = get();
    const patch = extractPatch(state.params, state.sel, name);
    const userPatches = saveUserPatch(name, patch);
    const savedIndex = userPatches.findIndex((entry) => entry.name === name);
    set({ userPatches, patchValue: savedIndex >= 0 ? `u${savedIndex}` : state.patchValue });
  },

  setParamsFromKit: (params) => {
    drumEngine.panic();
    drumEngine.params = { ...params };
    drumEngine.applyAllParams();
    set({ params: { ...params } });
  },

  importPadTable: (padI, table) => {
    const userTables = [...get().userTables, table];
    drumEngine.setUserTables(userTables);
    set({ userTables });
    get().setParam(pad(padI, 'oscA.table'), DRUM_TABLE_NAMES.length + userTables.length - 1);
  },
}));
