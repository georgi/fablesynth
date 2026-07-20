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
  cycleStep, NPATTERNS, patIdx, randomizePadPattern, STEPS, type Patterns,
} from './seq';
import { sequenceChain, sequenceLengthFromChain } from '../sequenceLength';
import {
  clearPadRect, copyPadRect, copyPattern, makeHistory, movePadRect, padRectNorm, pastePadRect,
  pastePattern, type PadRectCells, type PadRectSel, type SeqLayout,
} from '../shared/seqEdit';

export type { PadRectSel } from '../shared/seqEdit';

export let drumEngine = new DrumEngine();
const initialKitState = kitToState(FACTORY_KITS[0]);

// The DR-1 pattern buffer as a step × pad grid: stride-1 step cells, a pad's
// row is a lane. One layout covers both the whole-pattern block ops
// (copyPattern/pastePattern span all pads) and the pad-rect ops (which compute
// the per-pad offset themselves from padCount = PAD_COUNT).
const DRUM_LAYOUT: SeqLayout = { stride: 1, stepsPerPattern: STEPS, patternSize: PAD_COUNT * STEPS };

export type DrumClipboard =
  | { kind: 'rect'; data: PadRectCells }
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
  rectSel: PadRectSel | null;
  lastCell: { step: number; pad: number } | null;
  clipboard: DrumClipboard;
  kitDirty: boolean;

  setParam: (id: string, v: number) => void;
  selectPad: (i: number) => void;
  triggerPad: (i: number, vel: number) => void;
  _setPatterns: (next: Patterns) => void;
  _pushHistory: () => void;
  _clearHistory: () => void;
  // padI defaults to the selected pad; STEP mode's 16-lane view passes the
  // lane's own pad so a click never depends on selection order.
  toggleStep: (step: number, padI?: number) => void;
  randomizePad: () => void;
  setEditPattern: (i: number) => void;
  setSequenceLength: (length: number) => void;
  setRectSel: (sel: PadRectSel | null) => void;
  moveRectSel: (dStep: number, dPad: number, opts?: { copy?: boolean }) => void;
  dropRect: (data: PadRectCells, atStep: number, atPad: number, clearSrc?: PadRectSel | null) => void;
  selectAllSteps: () => void;
  clearStepSel: () => void;
  copySelection: () => void;
  cutSelection: () => void;
  pasteSelection: () => void;
  duplicateSelection: () => void;
  deleteSelection: () => void;
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
  rectSel: null,
  lastCell: null,
  clipboard: null,
  kitDirty: false,

  setParam: (id, v) => {
    drumEngine.setParam(id, v);
    set((state) => ({ params: { ...state.params, [id]: v }, kitDirty: true }));
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
    set({ patterns: next, kitDirty: true });
    drumEngine.setPatterns(next);
  },

  // Bounded undo/redo (50 snapshots) over the patterns buffer. Every editing
  // verb — including plain step toggling — pushes the pre-mutation buffer
  // here first; knob/param changes never touch history.
  _pushHistory: () => patternHistory.push(get().patterns),
  _clearHistory: () => patternHistory.clear(),

  toggleStep: (step, padI) => {
    const { patterns, editPattern, sel } = get();
    const targetPad = padI ?? sel;
    get()._pushHistory();
    const next = patterns.slice();
    const index = patIdx(editPattern, targetPad, step);
    next[index] = cycleStep(next[index]);
    // Anchor for a no-selection paste (mirrors WT-1's lastCell).
    set({ lastCell: { step, pad: targetPad } });
    get()._setPatterns(next);
  },

  // RAND button: rewrites the selected pad's row in the current edit
  // pattern only — other pads' lanes are untouched (mirrors BL-1's randomize,
  // scoped down to one lane since DR-1 has no per-step pitch/duration).
  randomizePad: () => {
    const { patterns, editPattern, sel } = get();
    get()._pushHistory();
    get()._setPatterns(randomizePadPattern(patterns, editPattern, sel));
  },

  setEditPattern: (i) => set({ editPattern: i }),

  setSequenceLength: (length) => {
    const chain = sequenceChain(length);
    set({ chain });
    drumEngine.setChain(chain);
  },

  // Clamp a rectangle into the step × pad grid so the stored selection and any
  // pasted content always agree.
  setRectSel: (sel) => set({
    rectSel: sel
      ? {
          stepFrom: Math.min(STEPS - 1, Math.max(0, sel.stepFrom | 0)),
          stepTo: Math.min(STEPS - 1, Math.max(0, sel.stepTo | 0)),
          padFrom: Math.min(PAD_COUNT - 1, Math.max(0, sel.padFrom | 0)),
          padTo: Math.min(PAD_COUNT - 1, Math.max(0, sel.padTo | 0)),
        }
      : null,
  }),

  // Cmd-A: select the whole grid (every step of every pad in the edit pattern).
  selectAllSteps: () => set({ rectSel: { stepFrom: 0, stepTo: STEPS - 1, padFrom: 0, padTo: PAD_COUNT - 1 } }),

  clearStepSel: () => set({ rectSel: null }),

  // Copy/cut fall back to the whole edit pattern (all pads) when nothing is
  // selected; a rectangle scopes to its step × pad band.
  copySelection: () => {
    const { patterns, editPattern, rectSel } = get();
    if (rectSel) {
      set({ clipboard: { kind: 'rect', data: copyPadRect(patterns, DRUM_LAYOUT, editPattern, rectSel) } });
    } else {
      set({ clipboard: { kind: 'pattern', data: copyPattern(patterns, DRUM_LAYOUT, editPattern) } });
    }
  },

  // Copy then clear exactly what copySelection captured (rectangle, or the
  // whole edit pattern as fallback).
  cutSelection: () => {
    get().copySelection();
    const { patterns, editPattern, rectSel } = get();
    get()._pushHistory();
    if (rectSel) {
      get()._setPatterns(clearPadRect(patterns, DRUM_LAYOUT, editPattern, rectSel));
    } else {
      get()._setPatterns(pastePattern(patterns, DRUM_LAYOUT, editPattern, new Uint8Array(PAD_COUNT * STEPS)));
    }
  },

  // Distinct from cut: only clears an active selection, never the whole
  // pattern as an implicit fallback (Delete/Backspace with nothing selected
  // is a no-op, unlike Cmd/Ctrl-X).
  deleteSelection: () => {
    const { patterns, editPattern, rectSel } = get();
    if (!rectSel) return;
    get()._pushHistory();
    get()._setPatterns(clearPadRect(patterns, DRUM_LAYOUT, editPattern, rectSel));
  },

  // Rectangle payloads anchor at the current rect's top-left, else the
  // last-touched cell, else the grid origin; whole-pattern payloads always
  // land on the current edit pattern (all pads). Selection follows the paste.
  pasteSelection: () => {
    const { clipboard, patterns, editPattern, rectSel, lastCell } = get();
    if (!clipboard) return;
    get()._pushHistory();
    if (clipboard.kind === 'pattern') {
      get()._setPatterns(pastePattern(patterns, DRUM_LAYOUT, editPattern, clipboard.data));
      return;
    }
    const anchor = rectSel
      ? { step: padRectNorm(rectSel).stepLo, pad: padRectNorm(rectSel).padLo }
      : lastCell ?? { step: 0, pad: 0 };
    get()._setPatterns(pastePadRect(patterns, DRUM_LAYOUT, editPattern, anchor.step, anchor.pad, clipboard.data, PAD_COUNT));
    get().setRectSel({
      stepFrom: anchor.step,
      stepTo: anchor.step + clipboard.data.wSteps - 1,
      padFrom: anchor.pad,
      padTo: anchor.pad + clipboard.data.wPads - 1,
    });
  },

  // With a rectangle selected: paste a copy immediately to its right at the
  // same pads (dropped if it runs off the pattern), and reselect the copy.
  // With no selection: duplicate the current bar into the next one, extending
  // the sequence length if needed.
  duplicateSelection: () => {
    const { patterns, editPattern, rectSel } = get();
    if (rectSel) {
      const { stepLo, stepHi, padLo, padHi } = padRectNorm(rectSel);
      const at = stepHi + 1;
      if (at >= STEPS) return; // nothing past the last step — no-op
      get()._pushHistory();
      const data = copyPadRect(patterns, DRUM_LAYOUT, editPattern, rectSel);
      get()._setPatterns(pastePadRect(patterns, DRUM_LAYOUT, editPattern, at, padLo, data, PAD_COUNT));
      get().setRectSel({ stepFrom: at, stepTo: at + (stepHi - stepLo), padFrom: padLo, padTo: padHi });
      return;
    }
    const nextPat = Math.min(NPATTERNS - 1, editPattern + 1);
    if (nextPat === editPattern) return; // already the last bar
    get()._pushHistory();
    const data = copyPattern(patterns, DRUM_LAYOUT, editPattern);
    get()._setPatterns(pastePattern(patterns, DRUM_LAYOUT, nextPat, data));
    if (get().chain.length <= nextPat) get().setSequenceLength(nextPat + 1);
    set({ editPattern: nextPat, rectSel: null });
  },

  // In-rect drag: move (or Alt-copy) the whole block by (dStep, dPad), clamped
  // so the rectangle stays inside the grid. One undo entry.
  moveRectSel: (dStep, dPad, opts = {}) => {
    const { patterns, editPattern, rectSel } = get();
    if (!rectSel) return;
    const { stepLo, stepHi, padLo, padHi } = padRectNorm(rectSel);
    const ds = Math.min(STEPS - 1 - stepHi, Math.max(-stepLo, dStep | 0));
    const dp = Math.min(PAD_COUNT - 1 - padHi, Math.max(-padLo, dPad | 0));
    if (ds === 0 && dp === 0) return;
    get()._pushHistory();
    get()._setPatterns(movePadRect(patterns, DRUM_LAYOUT, editPattern, rectSel, ds, dp, PAD_COUNT, { copy: opts.copy }));
    set({ rectSel: { stepFrom: stepLo + ds, stepTo: stepHi + ds, padFrom: padLo + dp, padTo: padHi + dp } });
  },

  // Ghost-paste drop (menu CUT/COPY → drop on click): stamp the carried cells
  // with their top-left at (atStep, atPad); a CUT clears its source in the same
  // undo entry as the paste. Selection follows the dropped block.
  dropRect: (data, atStep, atPad, clearSrc) => {
    if (!data.cells.length) return;
    const { patterns, editPattern } = get();
    get()._pushHistory();
    const base = clearSrc ? clearPadRect(patterns, DRUM_LAYOUT, editPattern, clearSrc) : patterns;
    get()._setPatterns(pastePadRect(base, DRUM_LAYOUT, editPattern, atStep, atPad, data, PAD_COUNT));
    get().setRectSel({
      stepFrom: atStep,
      stepTo: atStep + data.wSteps - 1,
      padFrom: atPad,
      padTo: atPad + data.wPads - 1,
    });
  },

  // Bar-chip drag on SequenceLengthControl: move = swap patterns `from`/`to`
  // (all pads); Alt held = copy `from` over `to`, leaving `from` untouched.
  movePattern: (from, to, opts = {}) => {
    if (from === to) return;
    const { patterns } = get();
    const dataFrom = copyPattern(patterns, DRUM_LAYOUT, from);
    get()._pushHistory();
    if (opts.copy) {
      get()._setPatterns(pastePattern(patterns, DRUM_LAYOUT, to, dataFrom));
      return;
    }
    const dataTo = copyPattern(patterns, DRUM_LAYOUT, to);
    const next = pastePattern(pastePattern(patterns, DRUM_LAYOUT, to, dataFrom), DRUM_LAYOUT, from, dataTo);
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
    return { padNames, kitDirty: true };
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
    set({ userKits, kitValue: savedIndex >= 0 ? `u${savedIndex}` : state.kitValue, kitDirty: false });
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
      set({
        params: state.params, padNames: state.padNames, userTables, kitValue: value,
        patchValue: '', kitDirty: false,
      });
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
    // A kit replaces the entire song buffer, so snapshots from the prior kit
    // must never be allowed to overwrite it through undo.
    get()._clearHistory();
    set({
      params: state.params,
      padNames: state.padNames,
      patterns: state.patterns,
      chain,
      editPattern: chain[0] ?? 0,
      userTables,
      kitValue: value,
      patchValue: '',
      kitDirty: false,
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
