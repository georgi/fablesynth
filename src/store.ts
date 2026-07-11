// Zustand store — single source of truth for parameter values and transport
// state. Mirrors the imperative `engine.params` + control-registry wiring of the
// original vanilla build: every control reads its value from here and writes
// back through `setParam`, which also forwards to the audio engine.

import { create } from 'zustand';
import { SynthEngine } from './engine/synth';
import { defaultParams, TABLE_NAMES, type ModConnection, type ParamValues } from './params';
import { FACTORY_PRESETS, loadUserPresets, resolvePresetMods, saveUserPreset, type Preset } from './presets';
import { findFreeSlot, setMatSlot, clearSlot as clearSlotIn, MOD_DEFAULT_AMT } from './store/slotHelpers';
import {
  loadUserTablePool, saveUserTablePool, serializeUserTable, deserializeUserTable,
  makeUserTable, framesFromGenerated, type UserTable,
} from './engine/usertables';
import {
  cycleOct, getStep, loadSeqState, randomPattern, saveSeqState, setStep,
  writePattern, type Patterns,
} from './noteseq';
import { generateTables, SIZE, type GeneratedTable } from './engine/wavetables';

// Singleton audio engine (created once, initialized on power-on).
export const engine = new SynthEngine();

export interface PresetOption {
  value: string; // 'f<i>' factory | 'u<i>' user
  name: string;
  group: 'FACTORY' | 'USER';
}

export function presetOptions(userPresets: Preset[]): PresetOption[] {
  const opts: PresetOption[] = FACTORY_PRESETS.map((p, i) => ({ value: 'f' + i, name: p.name, group: 'FACTORY' as const }));
  userPresets.forEach((p, i) => opts.push({ value: 'u' + i, name: p.name, group: 'USER' }));
  return opts;
}

interface SynthStore {
  params: ParamValues;
  modDrag: number; // source being dragged (MOD_SOURCES index), 0 = none
  powered: boolean;
  voiceCount: number;
  modPosA: number;
  modPosB: number;
  midiActive: boolean;
  octave: number;
  activeNotes: Set<number>;
  userPresets: Preset[];
  presetValue: string;
  userTables: UserTable[];
  editorOsc: 'oscA' | 'oscB' | null;

  // note sequencer
  patterns: Patterns;
  chain: number[];
  chaining: boolean;
  chainFresh: boolean;
  editPattern: number;
  seqPlaying: boolean;
  curStep: number;
  curPat: number;

  setParam: (id: string, v: number) => void;
  // Modulation routing over the 16 fixed `mat{n}` slots (params-as-truth):
  // addRoute allocates the next free slot; updateSlot/clearSlot edit a slot by
  // its absolute number (1..16). All route through setParam so the engine stays
  // in sync exactly like any other param change.
  addRoute: (src: number, dst: number, amt?: number) => void;
  updateSlot: (slot: number, patch: Partial<ModConnection>) => void;
  clearSlot: (slot: number) => void;
  setModDrag: (src: number) => void;
  applyPreset: (presetParams: Partial<ParamValues>, mods?: ModConnection[]) => void;
  loadPresetByValue: (val: string) => void;
  stepPreset: (d: number) => void;
  savePreset: (name: string) => void;

  addUserTable: (u: UserTable) => void;
  deleteUserTable: (poolIndex: number) => void;
  renameUserTable: (poolIndex: number, name: string) => void;
  updateUserTable: (poolIndex: number, u: UserTable) => void;
  duplicateUserTable: (poolIndex: number) => void;
  duplicateFactoryTable: (factoryIndex: number) => void;
  openEditor: (osc: 'oscA' | 'oscB') => void;
  closeEditor: () => void;

  // note sequencer actions (mirror BL-1's pitch-seq conventions)
  _setPatterns: (next: Patterns) => void;
  toggleCell: (step: number, note: number) => void;
  cycleStepOct: (step: number) => void;
  toggleStepAcc: (step: number) => void;
  toggleStepTie: (step: number) => void;
  randomizeSeq: () => void;
  setEditPattern: (i: number) => void;
  setChaining: (on: boolean) => void;
  chainClick: (i: number) => void;
  seqPlay: () => void;
  seqStop: () => void;

  powerOn: () => Promise<void>;
  playNote: (n: number, vel: number) => void;
  setActive: (note: number, on: boolean) => void;
  panic: () => void;
  bend: (semis: number) => void;
  setOctave: (o: number) => void;
  setMidiActive: (on: boolean) => void;
}

// Sequencer patterns + chain persist independently of presets.
const initialSeq = loadSeqState();

// Factory tables regenerated once for the editor's library/duplicate needs.
let factoryTablesCache: GeneratedTable[] | null = null;
export function factoryTables(): GeneratedTable[] {
  if (!factoryTablesCache) factoryTablesCache = generateTables();
  return factoryTablesCache;
}

export const useStore = create<SynthStore>((set, get) => ({
  params: defaultParams(),
  modDrag: 0,
  powered: false,
  voiceCount: 0,
  modPosA: -1,
  modPosB: -1,
  midiActive: false,
  octave: 0,
  activeNotes: new Set<number>(),
  userPresets: loadUserPresets(),
  presetValue: 'f0',
  userTables: loadUserTablePool(),
  editorOsc: null,
  patterns: initialSeq.patterns,
  chain: initialSeq.chain,
  chaining: false,
  chainFresh: false,
  editPattern: 0,
  seqPlaying: false,
  curStep: -1,
  curPat: 0,

  setParam: (id, v) => {
    engine.setParam(id, v);
    set((s) => ({ params: { ...s.params, [id]: v } }));
  },

  // Allocate the next fully-empty slot and write the route into it. The matrix
  // ADD ROUTE button passes dst=0 to claim a slot that is editable but inactive
  // until a destination is chosen.
  addRoute: (src, dst, amt = MOD_DEFAULT_AMT) => {
    const slot = findFreeSlot(get().params);
    if (!slot) return; // pool full
    const next = { ...get().params };
    setMatSlot(next, slot, { src, dst, amt });
    get().setParam(`mat${slot}.src`, next[`mat${slot}.src`]);
    get().setParam(`mat${slot}.dst`, next[`mat${slot}.dst`]);
    get().setParam(`mat${slot}.amt`, next[`mat${slot}.amt`]);
  },

  updateSlot: (slot, patch) => {
    if (patch.src !== undefined) get().setParam(`mat${slot}.src`, patch.src);
    if (patch.dst !== undefined) get().setParam(`mat${slot}.dst`, patch.dst);
    if (patch.amt !== undefined) get().setParam(`mat${slot}.amt`, patch.amt);
  },

  clearSlot: (slot) => {
    const next = { ...get().params };
    clearSlotIn(next, slot);
    get().setParam(`mat${slot}.src`, next[`mat${slot}.src`]);
    get().setParam(`mat${slot}.dst`, next[`mat${slot}.dst`]);
    get().setParam(`mat${slot}.amt`, next[`mat${slot}.amt`]);
  },

  setModDrag: (src) => set({ modDrag: src }),

  applyPreset: (presetParams, presetMods) => {
    const params = resolvePresetMods(presetParams, presetMods);
    engine.panic();
    engine.params = { ...params };
    set({ params });
    engine.applyAllParams();
  },

  loadPresetByValue: (val) => {
    const preset = val[0] === 'f' ? FACTORY_PRESETS[+val.slice(1)] : get().userPresets[+val.slice(1)];
    // A preset is self-describing: if it carries user tables, they become the
    // active pool so the table indices in its params resolve deterministically.
    if (preset.tables && preset.tables.length) {
      const userTables = preset.tables.map(deserializeUserTable);
      saveUserTablePool(userTables);
      engine.setUserTables(userTables.map((t) => t.table));
      set({ userTables });
    }
    get().applyPreset(preset.params, preset.mods);
    set({ presetValue: val });
  },

  stepPreset: (d) => {
    const opts = presetOptions(get().userPresets);
    let i = opts.findIndex((o) => o.value === get().presetValue);
    i = (i + d + opts.length) % opts.length;
    get().loadPresetByValue(opts[i].value);
  },

  savePreset: (name) => {
    // Embed the current user-table pool so the preset is portable on its own.
    // Routes are in the `mat*` params, so the param map alone is self-describing.
    const tables = get().userTables.map(serializeUserTable);
    const userPresets = saveUserPreset(name, get().params, tables);
    const opts = presetOptions(userPresets);
    // USER only: a factory preset with the same name sorts first in opts.
    const found = opts.find((o) => o.group === 'USER' && o.name === name);
    set({ userPresets, presetValue: found ? found.value : get().presetValue });
  },

  addUserTable: (u) => {
    const userTables = [...get().userTables, u];
    saveUserTablePool(userTables);
    engine.setUserTables(userTables.map((t) => t.table));
    set({ userTables });
    const osc = get().editorOsc;
    if (osc) get().setParam(`${osc}.table`, TABLE_NAMES.length + userTables.length - 1);
  },

  deleteUserTable: (poolIndex) => {
    const userTables = get().userTables.filter((_, i) => i !== poolIndex);
    saveUserTablePool(userTables);
    engine.setUserTables(userTables.map((t) => t.table));
    set({ userTables });
    // Repair osc table references around the removed slot.
    const removed = TABLE_NAMES.length + poolIndex;
    for (const id of ['oscA.table', 'oscB.table']) {
      const v = get().params[id] | 0;
      if (v === removed) get().setParam(id, 0);
      else if (v > removed) get().setParam(id, v - 1);
    }
  },

  renameUserTable: (poolIndex, name) => {
    const nm = (name.trim().toUpperCase() || 'USER').slice(0, 14);
    const userTables = get().userTables.map((t, i) => (i === poolIndex ? { ...t, name: nm } : t));
    saveUserTablePool(userTables);
    set({ userTables });
  },

  // In-place replace of an existing user table's waveform (and name), keeping
  // its pool index — so the editor's row selection and any osc references to
  // that table stay valid (unlike delete+add, which would shift indices).
  updateUserTable: (poolIndex, u) => {
    if (poolIndex < 0 || poolIndex >= get().userTables.length) return;
    const userTables = get().userTables.map((t, i) => (i === poolIndex ? u : t));
    saveUserTablePool(userTables);
    engine.setUserTables(userTables.map((t) => t.table));
    set({ userTables });
  },

  duplicateUserTable: (poolIndex) => {
    const src = get().userTables[poolIndex];
    if (!src) return;
    const frames: Float32Array[] = [];
    for (let f = 0; f < src.frames; f++) frames.push(src.wave.slice(f * SIZE, (f + 1) * SIZE));
    const copy = makeUserTable((src.name + ' COPY').slice(0, 14), frames);
    get().addUserTable(copy); // appends, persists, re-pushes engine, assigns to osc
  },

  duplicateFactoryTable: (factoryIndex) => {
    const ft = factoryTables()[factoryIndex];
    if (!ft) return;
    const copy = makeUserTable((ft.name + ' COPY').slice(0, 14), framesFromGenerated(ft));
    get().addUserTable(copy);
  },

  openEditor: (osc) => set({ editorOsc: osc }),
  closeEditor: () => set({ editorOsc: null }),

  // ---------- note sequencer ----------
  // Every pattern mutation writes the store, pushes to the worklet and
  // persists to localStorage in one place.
  _setPatterns(next: Patterns) {
    set({ patterns: next });
    engine.setSeqPatterns(next);
    saveSeqState(next, get().chain);
  },

  toggleCell: (step, note) => {
    const { patterns, editPattern } = get();
    const cur = getStep(patterns, editPattern, step);
    const next = cur.on && cur.note === note
      ? setStep(patterns, editPattern, step, { on: false, acc: false, tie: false })
      : setStep(patterns, editPattern, step, { on: true, note });
    get()._setPatterns(next);
  },

  cycleStepOct: (step) => {
    const { patterns, editPattern } = get();
    const cur = getStep(patterns, editPattern, step);
    get()._setPatterns(setStep(patterns, editPattern, step, { oct: cycleOct(cur.oct) }));
  },

  toggleStepAcc: (step) => {
    const { patterns, editPattern } = get();
    const cur = getStep(patterns, editPattern, step);
    if (!cur.on) return;
    get()._setPatterns(setStep(patterns, editPattern, step, { acc: !cur.acc }));
  },

  toggleStepTie: (step) => {
    const { patterns, editPattern } = get();
    const cur = getStep(patterns, editPattern, step);
    if (!cur.on) return;
    get()._setPatterns(setStep(patterns, editPattern, step, { tie: !cur.tie }));
  },

  randomizeSeq: () => {
    const { patterns, editPattern } = get();
    get()._setPatterns(writePattern(patterns, editPattern, randomPattern()));
  },

  setEditPattern: (i) => {
    if (get().chaining) {
      set({ editPattern: i });
      return;
    }
    const chain = [i];
    set({ editPattern: i, chain });
    engine.setSeqChain(chain);
    saveSeqState(get().patterns, chain);
  },

  setChaining: (on) => {
    if (on) {
      set({ chaining: true, chainFresh: true });
      return;
    }
    const chain = get().chain.length ? get().chain : [get().editPattern];
    set({ chaining: false, chainFresh: false, chain });
    engine.setSeqChain(chain);
    saveSeqState(get().patterns, chain);
  },

  chainClick: (i) => {
    if (!get().chaining) {
      get().setEditPattern(i);
      return;
    }
    const chain = get().chainFresh ? [i] : [...get().chain, i];
    set({ chain, chainFresh: false, editPattern: i });
    engine.setSeqChain(chain);
    saveSeqState(get().patterns, chain);
  },

  seqPlay: () => {
    engine.seqPlay();
    set({ seqPlaying: true });
  },

  seqStop: () => {
    engine.seqStop();
    set({ seqPlaying: false, curStep: -1 });
  },

  powerOn: async () => {
    await engine.init();
    engine.setUserTables(get().userTables.map((t) => t.table));
    engine.onviz = (d) => set({
      voiceCount: d.n,
      modPosA: d.a >= 0 ? d.a : -1,
      modPosB: d.b >= 0 ? d.b : -1,
    });
    engine.onstep = (d) => set({ curStep: d.s, curPat: d.pat });
    engine.setSeqPatterns(get().patterns);
    engine.setSeqChain(get().chain);
    set({ powered: true });
  },

  playNote: (n, vel) => {
    if (vel > 0) {
      engine.noteOn(n, vel);
      get().setActive(n, true);
    } else {
      engine.noteOff(n);
      get().setActive(n, false);
    }
  },

  setActive: (note, on) => {
    set((s) => {
      if (on === s.activeNotes.has(note)) return s;
      const next = new Set(s.activeNotes);
      if (on) next.add(note); else next.delete(note);
      return { activeNotes: next };
    });
  },

  panic: () => {
    if (get().seqPlaying) get().seqStop();
    engine.panic();
  },

  bend: (semis) => engine.bend(semis),

  setOctave: (o) => set({ octave: o }),
  setMidiActive: (on) => set({ midiActive: on }),
}));
