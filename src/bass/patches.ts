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

export const FACTORY_PATCHES: BassPatch[] = [
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
];

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
