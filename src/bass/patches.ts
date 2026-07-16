// BL-1 factory and user patches. A patch is the whole machine state — sound
// params + the four pitch patterns + chain (same "kit" model as DR-1, since a
// 303 line is inseparable from its pattern). Factory entries contain only
// authored overrides; patchToState fills the rest from the canonical defaults.

import type { ParamValues } from '../params';
import { defaultBassParams } from './params';
import { makeEmptyPatterns, writePattern, type Patterns, type Step } from './seq';

export interface BassPatch {
  name: string;
  params: Partial<ParamValues>;
  patterns: number[];
  chain: number[];
}

const S = (note: number, opts: Partial<Step> = {}): Partial<Step> => ({ on: true, note, ...opts });
const rest = (): Partial<Step> => ({ on: false });

// The design mock's A/B acid lines, verbatim.
function acidPatterns(): number[] {
  let p = makeEmptyPatterns();
  p = writePattern(p, 0, [
    S(0, { acc: true }), S(0, { oct: -1 }), rest(), S(3, { slide: true }),
    S(0), rest(), S(7, { acc: true }), S(6, { slide: true }),
    S(0), rest(), S(10, { oct: -1 }), S(0, { slide: true }),
    rest(), S(3, { oct: 1, acc: true }), S(0, { slide: true }), rest(),
  ]);
  p = writePattern(p, 1, [
    S(0, { acc: true }), rest(), S(0), S(5, { slide: true }),
    rest(), S(0, { oct: -1 }), S(7, { slide: true }), rest(),
    S(0, { acc: true }), S(3), rest(), S(10, { oct: -1, slide: true }),
    S(0), rest(), S(0, { oct: 1, acc: true, slide: true }), rest(),
  ]);
  return Array.from(p);
}

const ACID_PATTERNS = acidPatterns();

const FACTORY_PATCHES_RAW: BassPatch[] = [
  {
    // The design defaults ARE the acid line — overrides only where the mock differs.
    name: 'ACID LINE',
    params: {},
    patterns: [...ACID_PATTERNS],
    chain: [0],
  },
  {
    name: 'RUBBER SUB',
    params: {
      'osc.table': 2, // PULSE
      'osc.pos': 0.12,
      'osc.level': 0.6,
      'sub.shape': 1,
      'sub.oct': -1,
      'sub.level': 0.8,
      'flt.cut': 210,
      'flt.res': 0.35,
      'flt.drive': 0.25,
      'flt.env': 0.45,
      'fenv.dec': 0.32,
      'aenv.sus': 0.72,
      'acc.amt': 0.5,
      'lfo.depth': 0.05,
      'fx.drive.amt': 0.2,
      'fx.reverb.mix': 0.06,
      'seq.bpm': 122,
      'master.swing': 0.42,
    },
    patterns: [...ACID_PATTERNS],
    chain: [0],
  },
  {
    name: 'NEON SQUELCH',
    params: {
      'osc.pos': 0.55,
      'osc.unison': 3,
      'osc.detune': 0.3,
      'osc.spread': 0.4,
      'sub.level': 0.3,
      'flt.cut': 520,
      'flt.res': 0.78,
      'flt.env': 0.85,
      'fenv.dec': 0.12,
      'acc.amt': 0.85,
      'slide.time': 0.09,
      'lfo.depth': 0.3,
      'lfo.rate': 8, // 1/16
      'fx.chorus.on': 1,
      'fx.delay.on': 1,
      'fx.delay.mix': 0.24,
      'seq.bpm': 142,
    },
    patterns: [...ACID_PATTERNS],
    chain: [0, 1],
  },
  {
    name: 'DEEP DUB',
    params: {
      'osc.table': 0, 'osc.pos': 0.18, 'osc.level': 0.62,
      'sub.shape': 0, 'sub.oct': -2, 'sub.level': 0.88,
      'flt.type': 1, 'flt.cut': 145, 'flt.res': 0.28, 'flt.drive': 0.22,
      'flt.env': 0.32, 'fenv.dec': 0.55,
      'aenv.dec': 0.7, 'aenv.sus': 0.82, 'aenv.rel': 0.22,
      'acc.amt': 0.38, 'slide.time': 0.13, 'lfo.depth': 0.04,
      'fx.drive.amt': 0.16, 'fx.delay.on': 1, 'fx.delay.time': 0.5,
      'fx.delay.fb': 0.5, 'fx.delay.mix': 0.14,
      'fx.reverb.size': 0.58, 'fx.reverb.mix': 0.16,
      'seq.bpm': 112, 'master.swing': 0.4,
    },
    patterns: [...ACID_PATTERNS], chain: [0],
  },
  {
    name: 'WAREHOUSE',
    params: {
      'osc.table': 3, 'osc.pos': 0.72, 'osc.unison': 2, 'osc.detune': 0.18,
      'osc.spread': 0.18, 'sub.level': 0.38,
      'flt.type': 1, 'flt.cut': 430, 'flt.res': 0.68, 'flt.drive': 0.72,
      'flt.env': 0.76, 'fenv.dec': 0.14, 'aenv.dec': 0.22, 'aenv.sus': 0.42,
      'acc.amt': 0.9, 'slide.time': 0.055,
      'fx.drive.amt': 0.7, 'fx.drive.mix': 0.2,
      'fx.reverb.mix': 0.05, 'seq.bpm': 136, 'master.swing': 0.16,
    },
    patterns: [...ACID_PATTERNS], chain: [0, 1],
  },
  {
    name: 'ROUNDHOUSE',
    params: {
      'osc.table': 1, 'osc.pos': 0.28, 'osc.unison': 1, 'osc.level': 0.68,
      'sub.shape': 0, 'sub.oct': -1, 'sub.level': 0.68,
      'flt.type': 0, 'flt.cut': 680, 'flt.res': 0.22, 'flt.drive': 0.34,
      'flt.env': 0.48, 'flt.track': 0.45, 'fenv.dec': 0.28,
      'aenv.dec': 0.36, 'aenv.sus': 0.62, 'aenv.rel': 0.12,
      'acc.amt': 0.52, 'lfo.depth': 0.08,
      'fx.drive.amt': 0.28, 'fx.chorus.on': 1, 'fx.chorus.rate': 0.42,
      'fx.chorus.depth': 0.22, 'fx.chorus.mix': 0.1,
      'seq.bpm': 124, 'master.swing': 0.32,
    },
    patterns: [...ACID_PATTERNS], chain: [0],
  },
  {
    name: 'METAL PULSE',
    params: {
      'osc.table': 4, 'osc.pos': 0.86, 'osc.fine': 9,
      'osc.unison': 3, 'osc.detune': 0.12, 'osc.spread': 0.32, 'osc.level': 0.72,
      'sub.shape': 1, 'sub.level': 0.3,
      'flt.type': 2, 'flt.cut': 920, 'flt.res': 0.58, 'flt.drive': 0.38,
      'flt.env': 0.62, 'fenv.dec': 0.19,
      'aenv.dec': 0.24, 'aenv.sus': 0.48, 'acc.amt': 0.72,
      'lfo.rate': 8, 'lfo.shape': 2, 'lfo.depth': 0.24,
      'fx.chorus.on': 1, 'fx.chorus.rate': 1.4, 'fx.chorus.depth': 0.5,
      'fx.chorus.mix': 0.22, 'fx.delay.on': 1, 'fx.delay.time': 0.22,
      'fx.delay.mix': 0.12, 'seq.bpm': 130, 'master.swing': 0.2,
    },
    patterns: [...ACID_PATTERNS], chain: [0, 1],
  },
  {
    name: 'TAPE BASS',
    params: {
      'osc.table': 0, 'osc.pos': 0.38, 'osc.fine': -7, 'osc.unison': 2,
      'osc.detune': 0.08, 'osc.spread': 0.12, 'osc.level': 0.66,
      'sub.shape': 0, 'sub.level': 0.62,
      'flt.type': 0, 'flt.cut': 510, 'flt.res': 0.18, 'flt.drive': 0.3,
      'flt.env': 0.38, 'fenv.att': 0.006, 'fenv.dec': 0.4,
      'aenv.att': 0.008, 'aenv.dec': 0.5, 'aenv.sus': 0.7, 'aenv.rel': 0.2,
      'acc.amt': 0.44, 'slide.time': 0.1, 'lfo.rate': 3, 'lfo.depth': 0.08,
      'fx.drive.amt': 0.22, 'fx.drive.mix': 0.2,
      'fx.chorus.on': 1, 'fx.chorus.rate': 0.18, 'fx.chorus.depth': 0.2,
      'fx.chorus.mix': 0.12, 'fx.reverb.mix': 0.08,
      'seq.bpm': 104, 'master.swing': 0.5,
    },
    patterns: [...ACID_PATTERNS], chain: [0],
  },
  {
    name: 'REESE MONO',
    params: {
      'osc.table': 0, 'osc.pos': 0.64, 'osc.unison': 7, 'osc.detune': 0.46,
      'osc.spread': 0.18, 'osc.level': 0.72, 'sub.shape': 0, 'sub.level': 0.54,
      'flt.type': 1, 'flt.cut': 360, 'flt.res': 0.3, 'flt.drive': 0.54,
      'flt.env': 0.3, 'flt.track': 0.22, 'fenv.dec': 0.65,
      'aenv.dec': 0.4, 'aenv.sus': 0.88, 'aenv.rel': 0.16,
      'acc.amt': 0.5, 'slide.time': 0.085, 'lfo.rate': 2, 'lfo.depth': 0.1,
      'fx.drive.amt': 0.48, 'fx.drive.mix': 0.2,
      'fx.chorus.on': 1, 'fx.chorus.rate': 0.32, 'fx.chorus.depth': 0.26,
      'fx.chorus.mix': 0.16, 'fx.reverb.mix': 0.05,
      'seq.bpm': 128, 'master.swing': 0.24,
    },
    patterns: [...ACID_PATTERNS], chain: [0, 1],
  },
  {
    name: 'PLUCKED WIRE',
    params: {
      'osc.table': 2, 'osc.pos': 0.62, 'osc.tune': 0, 'osc.unison': 2,
      'osc.detune': 0.1, 'osc.spread': 0.28, 'osc.level': 0.66,
      'sub.level': 0.18, 'flt.type': 2, 'flt.cut': 1500, 'flt.res': 0.42,
      'flt.drive': 0.26, 'flt.env': 0.88, 'flt.track': 0.62,
      'fenv.dec': 0.075, 'aenv.dec': 0.11, 'aenv.sus': 0.08, 'aenv.rel': 0.05,
      'acc.amt': 0.8, 'slide.time': 0.035, 'lfo.depth': 0,
      'fx.drive.amt': 0.24, 'fx.delay.on': 1, 'fx.delay.time': 0.31,
      'fx.delay.fb': 0.34, 'fx.delay.mix': 0.18,
      'fx.reverb.size': 0.42, 'fx.reverb.mix': 0.14,
      'seq.bpm': 132, 'master.swing': 0.18,
    },
    patterns: [...ACID_PATTERNS], chain: [1, 0],
  },
  {
    name: 'DARK CURRENT',
    params: {
      'osc.table': 5, 'osc.pos': 0.24, 'osc.unison': 4,
      'osc.detune': 0.32, 'osc.spread': 0.36, 'osc.level': 0.68,
      'sub.shape': 1, 'sub.oct': -2, 'sub.level': 0.62,
      'flt.type': 1, 'flt.cut': 260, 'flt.res': 0.46, 'flt.drive': 0.62,
      'flt.env': -0.38, 'flt.track': 0.18, 'fenv.att': 0.035, 'fenv.dec': 0.8,
      'aenv.att': 0.012, 'aenv.dec': 0.55, 'aenv.sus': 0.78, 'aenv.rel': 0.28,
      'acc.amt': 0.58, 'slide.time': 0.16, 'lfo.rate': 4, 'lfo.shape': 1,
      'lfo.depth': 0.34, 'fx.drive.amt': 0.56, 'fx.drive.mix': 0.2,
      'fx.delay.on': 1, 'fx.delay.time': 0.6, 'fx.delay.fb': 0.58,
      'fx.delay.mix': 0.16, 'fx.reverb.size': 0.68, 'fx.reverb.mix': 0.18,
      'seq.bpm': 118, 'master.swing': 0.36,
    },
    patterns: [...ACID_PATTERNS], chain: [0, 1],
  },
  {
    name: 'CLEAN SUB',
    params: {
      'osc.table': 1, 'osc.pos': 0, 'osc.unison': 1, 'osc.level': 0.36,
      'sub.shape': 0, 'sub.oct': -1, 'sub.level': 0.92,
      'flt.type': 0, 'flt.cut': 780, 'flt.res': 0.08, 'flt.drive': 0.08,
      'flt.env': 0.12, 'flt.track': 0.5, 'fenv.dec': 0.45,
      'aenv.att': 0.006, 'aenv.dec': 0.5, 'aenv.sus': 0.9, 'aenv.rel': 0.12,
      'acc.amt': 0.32, 'slide.time': 0.07, 'lfo.depth': 0,
      'fx.drive.on': 0, 'fx.chorus.on': 0, 'fx.delay.on': 0,
      'fx.reverb.on': 0, 'seq.bpm': 120, 'master.swing': 0.26,
    },
    patterns: [...ACID_PATTERNS], chain: [0],
  },
];

// Factory BL-1 sounds stay dry so their sequence and filter character read
// clearly in a mix. Users can still enable either effect after loading a patch.
export const FACTORY_PATCHES: BassPatch[] = FACTORY_PATCHES_RAW.map((patch) => ({
  ...patch,
  params: { ...patch.params, 'fx.delay.on': 0, 'fx.reverb.on': 0 },
}));


export function patchToState(patch: BassPatch): {
  params: ParamValues;
  patterns: Patterns;
  chain: number[];
} {
  return {
    params: { ...defaultBassParams(), ...patch.params } as ParamValues,
    patterns: Uint8Array.from(patch.patterns),
    chain: patch.chain.length ? [...patch.chain] : [0],
  };
}

export function stateToPatch(
  name: string,
  params: ParamValues,
  patterns: Patterns,
  chain: number[],
): BassPatch {
  return { name, params: { ...params }, patterns: Array.from(patterns), chain: [...chain] };
}

export interface PatchOption {
  value: string;
  name: string;
  group: 'FACTORY' | 'USER';
}

export function patchOptions(userPatches: BassPatch[]): PatchOption[] {
  const options: PatchOption[] = FACTORY_PATCHES.map((p, i) => ({
    value: `f${i}`, name: p.name, group: 'FACTORY' as const,
  }));
  userPatches.forEach((p, i) => options.push({ value: `u${i}`, name: p.name, group: 'USER' }));
  return options;
}

const LS_KEY = 'fable-bl-patches';
const memoryStorage = new Map<string, string>();

function readStored(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return memoryStorage.get(key) ?? null;
  }
}

function writeStored(key: string, value: string): void {
  try {
    localStorage.setItem(key, value);
  } catch {
    memoryStorage.set(key, value);
  }
}

export function loadUserPatches(): BassPatch[] {
  try {
    const parsed = JSON.parse(readStored(LS_KEY) as string);
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

export function saveUserPatch(name: string, patch: BassPatch): BassPatch[] {
  const list = loadUserPatches().filter((entry) => entry.name !== name);
  list.push({ ...patch, name });
  writeStored(LS_KEY, JSON.stringify(list));
  return list;
}
