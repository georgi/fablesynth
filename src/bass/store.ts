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
  cycleOct, getStep, randomPattern, setStep, writePattern, type Patterns,
} from './seq';
import { sequenceChain, sequenceLengthFromChain } from '../sequenceLength';

export let bassEngine = new BassEngine();
const initialState = patchToState(FACTORY_PATCHES[0]);

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

  setParam: (id: string, v: number) => void;
  noteOn: (semi: number, vel: number) => void;
  noteOff: (semi: number) => void;
  toggleCell: (step: number, note: number) => void;
  cycleStepOct: (step: number) => void;
  toggleStepAcc: (step: number) => void;
  toggleStepSlide: (step: number) => void;
  randomize: () => void;
  setEditPattern: (i: number) => void;
  setSequenceLength: (length: number) => void;
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

  toggleCell: (step, note) => {
    const { patterns, editPattern } = get();
    const cur = getStep(patterns, editPattern, step);
    const next = cur.on && cur.note === note
      ? setStep(patterns, editPattern, step, { on: false, acc: false, slide: false })
      : setStep(patterns, editPattern, step, { on: true, note });
    set({ patterns: next });
    bassEngine.setPatterns(next);
  },

  cycleStepOct: (step) => {
    const { patterns, editPattern } = get();
    const cur = getStep(patterns, editPattern, step);
    const next = setStep(patterns, editPattern, step, { oct: cycleOct(cur.oct) });
    set({ patterns: next });
    bassEngine.setPatterns(next);
  },

  toggleStepAcc: (step) => {
    const { patterns, editPattern } = get();
    const cur = getStep(patterns, editPattern, step);
    if (!cur.on) return;
    const next = setStep(patterns, editPattern, step, { acc: !cur.acc });
    set({ patterns: next });
    bassEngine.setPatterns(next);
  },

  toggleStepSlide: (step) => {
    const { patterns, editPattern } = get();
    const cur = getStep(patterns, editPattern, step);
    if (!cur.on) return;
    const next = setStep(patterns, editPattern, step, { slide: !cur.slide });
    set({ patterns: next });
    bassEngine.setPatterns(next);
  },

  randomize: () => {
    const { patterns, editPattern } = get();
    const next = writePattern(patterns, editPattern, randomPattern());
    set({ patterns: next });
    bassEngine.setPatterns(next);
  },

  setEditPattern: (i) => set({ editPattern: i }),

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
        if (!data.slide) {
          base.hitTick = performance.now();
          base.hitAcc = data.acc;
        }
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
