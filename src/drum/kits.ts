// DR-1 factory and user kits. Factory entries contain only authored overrides;
// kitToState fills the rest from the canonical drum defaults.

import type { ParamValues } from '../params';
import type { SerializedUserTable } from '../engine/usertables';
import { defaultDrumParams, pad, PAD_COUNT } from './params';
import { makeEmptyPatterns, patIdx, type Patterns } from './seq';

export interface Kit {
  name: string;
  params: Partial<ParamValues>;
  padNames: string[];
  patterns: number[];
  chain: number[];
  tables?: SerializedUserTable[];
}

const PAD_NAMES = [
  'KICK', 'KICK 2', 'SNARE', 'CLAP', 'RIM', 'CH HAT', 'OH HAT', 'RIDE',
  'TOM LO', 'TOM MD', 'TOM HI', 'CRASH', 'PERC 1', 'PERC 2', 'VOX', 'GLITCH',
];

function trVoidPatterns(): number[] {
  const patterns = makeEmptyPatterns();
  const set = (padI: number, steps: number[], accents: number[] = []) => {
    for (const step of steps) patterns[patIdx(0, padI, step)] = accents.includes(step) ? 2 : 1;
  };
  set(0, [0, 4, 8, 12], [0]);
  set(2, [4, 12], [12]);
  set(3, [4, 12]);
  set(4, [7]);
  set(5, [0, 2, 4, 6, 8, 10, 12], [4, 12]);
  set(6, [14], [14]);
  set(12, [3, 11]);
  set(14, [10]);
  return Array.from(patterns);
}

function trVoidParams(): Partial<ParamValues> {
  const params: Partial<ParamValues> = {
    'seq.bpm': 126,
    'master.swing': 0.22,
    'fx.comp.on': 1,
    'fx.reverb.on': 1,
    'fx.reverb.mix': 0.16,
  };
  const sounds: Array<[number, number, number, number]> = [
    [0, -14, 22, 0.30], [0, -7, 16, 0.24], [1, 0, 5, 0.18], [1, 7, 2, 0.14],
    [2, 24, 0, 0.08], [2, 18, 0, 0.04], [2, 12, 0, 0.30], [2, 5, 0, 1.40],
    [0, -12, 12, 0.42], [0, -5, 10, 0.36], [0, 2, 8, 0.30], [2, 0, 0, 1.80],
    [1, 12, -5, 0.16], [2, 19, 0, 0.20], [7, -5, 0, 0.48], [3, -12, 9, 0.22],
  ];
  sounds.forEach(([table, tune, penv, decay], i) => {
    params[pad(i, 'oscA.table')] = table;
    params[pad(i, 'oscA.tune')] = tune;
    params[pad(i, 'penv.amt')] = penv;
    params[pad(i, 'aenv.dec')] = decay;
  });
  params[pad(0, 'penv.dec')] = 0.06;
  params[pad(2, 'noise.level')] = 0.5;
  params[pad(2, 'noise.color')] = 0.3;
  params[pad(3, 'noise.level')] = 0.35;
  for (const i of [5, 6]) {
    params[pad(i, 'choke')] = 1;
    params[pad(i, 'flt.on')] = 1;
    params[pad(i, 'flt.type')] = 3;
    params[pad(i, 'flt.cut')] = i === 5 ? 7200 : 5200;
  }
  return params;
}

function roomOneParams(): Partial<ParamValues> {
  const params = { ...trVoidParams() };
  params['seq.bpm'] = 116;
  params['master.swing'] = 0.16;
  params['fx.reverb.mix'] = 0.3;
  params['fx.reverb.size'] = 0.62;
  params['fx.chorus.on'] = 1;
  params['fx.chorus.mix'] = 0.14;
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'penv.amt')] = Math.round((params[pad(i, 'penv.amt')] ?? 0) * 0.55);
    params[pad(i, 'aenv.hold')] = i < 4 ? 0.025 : 0.015;
    params[pad(i, 'aenv.dec')] = Math.min(4, (params[pad(i, 'aenv.dec')] ?? 0.24) * 1.25);
  }
  return params;
}

function bitcrushParams(): Partial<ParamValues> {
  const params = { ...trVoidParams() };
  params['seq.bpm'] = 140;
  params['master.swing'] = 0.08;
  params['fx.drive.on'] = 1;
  params['fx.drive.amt'] = 0.6;
  params['fx.drive.mix'] = 0.9;
  params['fx.delay.on'] = 1;
  params['fx.delay.time'] = 0.18;
  params['fx.delay.fb'] = 0.42;
  params['fx.delay.mix'] = 0.22;
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'oscA.table')] = 3;
    params[pad(i, 'oscA.pos')] = (i % 4) / 4;
    params[pad(i, 'aenv.dec')] = Math.max(0.04, Math.min(0.65, params[pad(i, 'aenv.dec')] ?? 0.24));
  }
  return params;
}

const PATTERNS = trVoidPatterns();

export const FACTORY_KITS: Kit[] = [
  { name: 'TR-VOID', params: trVoidParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'ROOM ONE', params: roomOneParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'BITCRUSH', params: bitcrushParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
];

export function kitToState(kit: Kit): {
  params: ParamValues;
  padNames: string[];
  patterns: Patterns;
  chain: number[];
  tables: SerializedUserTable[];
} {
  return {
    params: { ...defaultDrumParams(), ...kit.params } as ParamValues,
    padNames: [...kit.padNames],
    patterns: Uint8Array.from(kit.patterns),
    chain: [...kit.chain],
    tables: kit.tables ? [...kit.tables] : [],
  };
}

export function stateToKit(
  name: string,
  params: ParamValues,
  padNames: string[],
  patterns: Patterns,
  chain: number[],
  tables: SerializedUserTable[] = [],
): Kit {
  const kit: Kit = {
    name,
    params: { ...params },
    padNames: [...padNames],
    patterns: Array.from(patterns),
    chain: [...chain],
  };
  if (tables.length) kit.tables = [...tables];
  return kit;
}

const LS_KEY = 'fable-dr-kits';
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

export function loadUserKits(): Kit[] {
  try {
    const parsed = JSON.parse(readStored(LS_KEY) as string);
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

export function saveUserKit(name: string, kit: Kit): Kit[] {
  const list = loadUserKits().filter((entry) => entry.name !== name);
  list.push({ ...kit, name });
  writeStored(LS_KEY, JSON.stringify(list));
  return list;
}
