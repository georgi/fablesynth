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
import { pad } from './params';
import { cycleStep, patIdx, type Patterns } from './seq';

export const drumEngine = new DrumEngine();
const initialKitState = kitToState(FACTORY_KITS[0]);

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
  chaining: boolean;
  chainFresh: boolean;
  editPattern: number;
  playing: boolean;
  curStep: number;
  curPat: number;
  powered: boolean;
  midiActive: boolean;
  kitValue: string;
  userKits: Kit[];
  padNames: string[];
  hitTick: Record<number, number>;
  mode: 'step' | 'pads';
  modPosA: number;
  modPosB: number;
  envLevel: number;
  userTables: GeneratedTable[];

  setParam: (id: string, v: number) => void;
  selectPad: (i: number) => void;
  triggerPad: (i: number, vel: number) => void;
  toggleStep: (step: number) => void;
  setEditPattern: (i: number) => void;
  setChaining: (on: boolean) => void;
  chainClick: (i: number) => void;
  setMode: (mode: 'step' | 'pads') => void;
  setMidiActive: (on: boolean) => void;
  setPadName: (i: number, name: string) => void;
  play: () => void;
  stop: () => void;
  powerOn: () => Promise<void>;
  saveKit: (name: string) => void;
  loadKitByValue: (value: string) => void;
  stepKit: (delta: number) => void;
  setParamsFromKit: (params: ParamValues) => void;
  importPadTable: (padI: number, table: GeneratedTable) => void;
}

export const useDrumStore = create<DrumStore>((set, get) => ({
  params: initialKitState.params,
  sel: 0,
  patterns: initialKitState.patterns,
  chain: initialKitState.chain,
  chaining: false,
  chainFresh: false,
  editPattern: 0,
  playing: false,
  curStep: -1,
  curPat: 0,
  powered: false,
  midiActive: false,
  kitValue: 'f0',
  userKits: loadUserKits(),
  padNames: initialKitState.padNames,
  hitTick: {},
  mode: 'step',
  modPosA: -1,
  modPosB: -1,
  envLevel: 0,
  userTables: [],

  setParam: (id, v) => {
    drumEngine.setParam(id, v);
    set((state) => ({ params: { ...state.params, [id]: v } }));
  },

  selectPad: (i) => {
    drumEngine.selectPad(i);
    drumEngine.trigger(i, 0.8);
    set((state) => ({ sel: i, hitTick: { ...state.hitTick, [i]: performance.now() } }));
  },

  triggerPad: (i, vel) => {
    drumEngine.trigger(i, vel);
    set((state) => ({ hitTick: { ...state.hitTick, [i]: performance.now() } }));
  },

  toggleStep: (step) => {
    const { patterns, editPattern, sel } = get();
    const next = patterns.slice();
    const index = patIdx(editPattern, sel, step);
    next[index] = cycleStep(next[index]);
    set({ patterns: next });
    drumEngine.setPatterns(next);
  },

  setEditPattern: (i) => {
    if (get().chaining) {
      set({ editPattern: i });
      return;
    }
    const chain = [i];
    set({ editPattern: i, chain });
    drumEngine.setChain(chain);
  },

  setChaining: (on) => {
    if (on) {
      set({ chaining: true, chainFresh: true });
      return;
    }
    const chain = get().chain.length ? get().chain : [get().editPattern];
    set({ chaining: false, chainFresh: false, chain });
    drumEngine.setChain(chain);
  },

  chainClick: (i) => {
    if (!get().chaining) {
      get().setEditPattern(i);
      return;
    }
    const chain = get().chainFresh ? [i] : [...get().chain, i];
    set({ chain, chainFresh: false, editPattern: i });
    drumEngine.setChain(chain);
  },

  setMode: (mode) => set({ mode }),
  setMidiActive: (on) => set({ midiActive: on }),

  setPadName: (i, name) => set((state) => {
    const padNames = state.padNames.slice();
    padNames[i] = name.toUpperCase().slice(0, 14);
    return { padNames };
  }),

  play: () => {
    drumEngine.play();
    set({ playing: true });
  },

  stop: () => {
    drumEngine.stop();
    set({ playing: false, curStep: -1 });
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
    const userTables = state.tables.map((table) => deserializeUserTable(table).table);
    const chain = state.chain.length ? state.chain : [0];
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
    get().setParam(pad(padI, 'oscA.table'), 10 + userTables.length - 1);
  },
}));
