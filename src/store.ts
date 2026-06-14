// Zustand store — single source of truth for parameter values and transport
// state. Mirrors the imperative `engine.params` + control-registry wiring of the
// original vanilla build: every control reads its value from here and writes
// back through `setParam`, which also forwards to the audio engine.

import { create } from 'zustand';
import { SynthEngine } from './engine/synth';
import { defaultParams, TABLE_NAMES, type ParamValues } from './params';
import { FACTORY_PRESETS, loadUserPresets, saveUserPreset, type Preset } from './presets';
import {
  loadUserTablePool, saveUserTablePool, serializeUserTable, deserializeUserTable,
  type UserTable,
} from './engine/usertables';

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

  setParam: (id: string, v: number) => void;
  applyPreset: (presetParams: Partial<ParamValues>) => void;
  loadPresetByValue: (val: string) => void;
  stepPreset: (d: number) => void;
  savePreset: (name: string) => void;

  addUserTable: (u: UserTable) => void;
  deleteUserTable: (poolIndex: number) => void;
  openEditor: (osc: 'oscA' | 'oscB') => void;
  closeEditor: () => void;

  powerOn: () => Promise<void>;
  playNote: (n: number, vel: number) => void;
  setActive: (note: number, on: boolean) => void;
  panic: () => void;
  bend: (semis: number) => void;
  setOctave: (o: number) => void;
  setMidiActive: (on: boolean) => void;
}

export const useStore = create<SynthStore>((set, get) => ({
  params: defaultParams(),
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

  setParam: (id, v) => {
    engine.setParam(id, v);
    set((s) => ({ params: { ...s.params, [id]: v } }));
  },

  applyPreset: (presetParams) => {
    const merged = { ...defaultParams(), ...presetParams } as ParamValues;
    engine.panic();
    engine.params = { ...merged };
    set({ params: merged });
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
    get().applyPreset(preset.params);
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
    const tables = get().userTables.map(serializeUserTable);
    const userPresets = saveUserPreset(name, get().params, tables);
    const opts = presetOptions(userPresets);
    const found = opts.find((o) => o.name === name);
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

  openEditor: (osc) => set({ editorOsc: osc }),
  closeEditor: () => set({ editorOsc: null }),

  powerOn: async () => {
    await engine.init();
    engine.setUserTables(get().userTables.map((t) => t.table));
    engine.onviz = (d) => set({
      voiceCount: d.n,
      modPosA: d.a >= 0 ? d.a : -1,
      modPosB: d.b >= 0 ? d.b : -1,
    });
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

  panic: () => engine.panic(),

  bend: (semis) => engine.bend(semis),

  setOctave: (o) => set({ octave: o }),
  setMidiActive: (on) => set({ midiActive: on }),
}));
