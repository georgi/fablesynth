// DR-1 factory and user kits. Factory entries contain only authored overrides;
// kitToState fills the rest from the canonical drum defaults.

import type { ParamValues } from '../params';
import type { SerializedUserTable } from '../engine/usertables';
import { defaultDrumParams, FX_DEFS, pad, PAD_COUNT } from './params';
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
    [0, -26, 22, 0.30], [0, -19, 16, 0.24], [1, -12, 5, 0.18], [1, 7, 2, 0.14],
    [2, 24, 0, 0.08], [2, 18, 0, 0.04], [2, 12, 0, 0.30], [2, 5, 0, 1.40],
    [0, -19, 12, 0.28], [0, -12, 10, 0.24], [0, -5, 8, 0.20], [2, 0, 0, 1.80],
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
  // Metallic pads use unrelated fixed-Hz carriers so their sidebands do not
  // collapse back onto the oscillator's harmonic series.
  Object.assign(params, {
    [pad(5, 'ring.freq')]: 6389, [pad(5, 'ring.mix')]: 0.18,
    [pad(6, 'ring.freq')]: 5197, [pad(6, 'ring.mix')]: 0.28,
    [pad(7, 'ring.freq')]: 1667, [pad(7, 'ring.mix')]: 0.46,
    [pad(11, 'ring.freq')]: 2741, [pad(11, 'ring.mix')]: 0.62,
    [pad(12, 'oscA.table')]: 8, [pad(12, 'oscA.tune')]: -5,
    [pad(12, 'ring.freq')]: 731, [pad(12, 'ring.mix')]: 0.78,
    [pad(12, 'aenv.dec')]: 0.16, [pad(12, 'aenv.curve')]: 0.22,
    [pad(13, 'oscA.table')]: 3, [pad(13, 'oscA.pos')]: 0.38,
    [pad(13, 'oscA.tune')]: 17, [pad(13, 'noise.level')]: 0.18,
    [pad(13, 'noise.color')]: 0.75, [pad(13, 'ring.freq')]: 3271,
    [pad(13, 'ring.mix')]: 0.88, [pad(13, 'aenv.dec')]: 0.20,
    [pad(13, 'flt.on')]: 1, [pad(13, 'flt.type')]: 3,
    [pad(13, 'flt.cut')]: 3600,
  });
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
  params['fx.drive.mix'] = 0.2;
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

function classic808Params(): Partial<ParamValues> {
  const params = { ...trVoidParams() };
  params['seq.bpm'] = 124;
  params['master.swing'] = 0.28;
  params['fx.reverb.mix'] = 0.09;
  const slots = [5, 5, 0, 1, 6, 2, 3, 4, 13, 14, 15, 4, 7, 9, 8, 12];
  const decays = [0.8, 0.55, 0.5, 0.7, 0.2, 0.1, 0.6, 2, 0.7, 0.65, 0.6, 2, 0.8, 0.25, 0.15, 0.6];
  slots.forEach((slot, padI) => {
    params[pad(padI, 'oscA.level')] = 0;
    params[pad(padI, 'oscB.table')] = slot;
    params[pad(padI, 'oscB.level')] = 0.9;
    params[pad(padI, 'aenv.dec')] = decays[padI];
  });
  params[pad(1, 'oscB.tune')] = 3;
  params[pad(2, 'noise.level')] = 0.12;
  return params;
}

function uzuParams(): Partial<ParamValues> {
  const params = { ...trVoidParams() };
  params['seq.bpm'] = 128;
  params['master.swing'] = 0.18;
  params['fx.reverb.mix'] = 0.12;
  const decays = [0.4, 2.4, 0.6, 0.6, 0.2, 0.45, 1.8, 1.1, 1, 0.6, 0.55, 1, 0.35, 0.1, 0.6, 0.2];
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'oscA.level')] = 0;
    params[pad(i, 'oscB.table')] = 16 + i;
    params[pad(i, 'oscB.level')] = 0.92;
    params[pad(i, 'noise.level')] = 0;
    params[pad(i, 'ring.mix')] = 0;
    params[pad(i, 'penv.amt')] = 0;
    params[pad(i, 'aenv.dec')] = decays[i];
  }
  return params;
}

function hybridParams(): Partial<ParamValues> {
  const params = { ...trVoidParams() };
  params['seq.bpm'] = 126;
  params['master.swing'] = 0.24;
  params['fx.drive.on'] = 1;
  params['fx.drive.amt'] = 0.16;
  params['fx.drive.mix'] = 0.2;
  params['fx.reverb.mix'] = 0.14;
  const samples = [16, 5, 18, 1, 20, 21, 3, 23, 13, 25, 15, 27, 7, 28, 30, 31];
  const oscLevels = [0.50, 0.45, 0.42, 0.25, 0.40, 0.18, 0.16, 0.12, 0.45, 0.42, 0.40, 0.12, 0.25, 0.22, 0.30, 0.35];
  const sampleLevels = [0.72, 0.68, 0.70, 0.76, 0.65, 0.76, 0.76, 0.72, 0.66, 0.68, 0.68, 0.72, 0.62, 0.70, 0.66, 0.68];
  const decays = [0.60, 0.70, 0.50, 0.70, 0.20, 0.15, 0.70, 1.20, 0.60, 0.60, 0.60, 1.20, 0.50, 0.35, 0.60, 0.25];
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'oscA.level')] = oscLevels[i];
    params[pad(i, 'oscB.table')] = samples[i];
    params[pad(i, 'oscB.level')] = sampleLevels[i];
    params[pad(i, 'aenv.dec')] = decays[i];
  }
  return params;
}

function deepDubParams(): Partial<ParamValues> {
  const params = { ...trVoidParams() };
  params['seq.bpm'] = 112;
  params['master.swing'] = 0.38;
  params['fx.delay.on'] = 1;
  params['fx.delay.time'] = 0.48;
  params['fx.delay.fb'] = 0.55;
  params['fx.delay.mix'] = 0.18;
  params['fx.reverb.size'] = 0.72;
  params['fx.reverb.mix'] = 0.24;
  for (const i of [0, 1, 8, 9, 10]) {
    params[pad(i, 'oscA.tune')] = (params[pad(i, 'oscA.tune')] ?? 0) - 5;
    params[pad(i, 'aenv.dec')] = Math.min(4, (params[pad(i, 'aenv.dec')] ?? 0.24) * 1.65);
  }
  for (const i of [5, 6]) params[pad(i, 'flt.cut')] = i === 5 ? 4300 : 3200;
  return params;
}

function dustHouseParams(): Partial<ParamValues> {
  const params = { ...roomOneParams() };
  params['seq.bpm'] = 122;
  params['master.swing'] = 0.46;
  params['fx.drive.on'] = 1;
  params['fx.drive.amt'] = 0.28;
  params['fx.drive.mix'] = 0.2;
  params['fx.reverb.mix'] = 0.2;
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'noise.level')] = i < 4 ? 0.12 : 0.04;
    params[pad(i, 'oscA.pos')] = 0.18 + (i % 3) * 0.08;
    params[pad(i, 'aenv.curve')] = 0.58;
  }
  return params;
}

function warehouseParams(): Partial<ParamValues> {
  const params = { ...trVoidParams() };
  params['seq.bpm'] = 136;
  params['master.swing'] = 0.14;
  params['fx.drive.on'] = 1;
  params['fx.drive.amt'] = 0.72;
  params['fx.drive.mix'] = 0.2;
  params['fx.comp.thr'] = -20;
  params['fx.comp.gain'] = 3;
  params['fx.reverb.mix'] = 0.1;
  for (const i of [0, 1, 2, 3, 8, 9, 10]) {
    params[pad(i, 'flt.on')] = 1;
    params[pad(i, 'flt.type')] = 1;
    params[pad(i, 'flt.cut')] = i < 2 ? 1800 : 5200;
    params[pad(i, 'flt.drive')] = 0.52;
  }
  return params;
}

function metalWorkParams(): Partial<ParamValues> {
  const params = { ...trVoidParams() };
  params['seq.bpm'] = 132;
  params['master.swing'] = 0.2;
  params['fx.chorus.on'] = 1;
  params['fx.chorus.rate'] = 1.8;
  params['fx.chorus.depth'] = 0.52;
  params['fx.chorus.mix'] = 0.24;
  params['fx.reverb.size'] = 0.8;
  params['fx.reverb.mix'] = 0.28;
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'oscA.table')] = i % 3 === 0 ? 8 : 2; // CHIME / TINE
    params[pad(i, 'oscA.tune')] = -12 + (i % 6) * 7;
    params[pad(i, 'oscA.fine')] = i % 2 ? 11 : -9;
    params[pad(i, 'aenv.dec')] = Math.min(2.2, 0.12 + (i % 5) * 0.16);
  }
  return params;
}

function tapeKitParams(): Partial<ParamValues> {
  const params = { ...roomOneParams() };
  params['seq.bpm'] = 98;
  params['master.swing'] = 0.52;
  params['fx.chorus.on'] = 1;
  params['fx.chorus.rate'] = 0.22;
  params['fx.chorus.depth'] = 0.24;
  params['fx.chorus.mix'] = 0.18;
  params['fx.drive.on'] = 1;
  params['fx.drive.amt'] = 0.18;
  params['fx.drive.mix'] = 0.2;
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'oscA.fine')] = (i % 5) * 3 - 6;
    params[pad(i, 'flt.on')] = 1;
    params[pad(i, 'flt.type')] = 0;
    params[pad(i, 'flt.cut')] = i < 4 ? 4800 : 7600;
  }
  return params;
}

function minimalParams(): Partial<ParamValues> {
  const params = { ...trVoidParams() };
  params['seq.bpm'] = 128;
  params['master.swing'] = 0.12;
  params['fx.reverb.mix'] = 0.07;
  params['fx.comp.gain'] = 2;
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'aenv.dec')] = Math.max(0.025, Math.min(0.22, (params[pad(i, 'aenv.dec')] ?? 0.24) * 0.52));
    params[pad(i, 'lvl')] = [0, 2, 5, 6].includes(i) ? 0.82 : 0.5;
  }
  return params;
}

function brokenToysParams(): Partial<ParamValues> {
  const params = { ...bitcrushParams() };
  params['seq.bpm'] = 150;
  params['master.swing'] = 0.34;
  params['fx.delay.time'] = 0.11;
  params['fx.delay.fb'] = 0.64;
  params['fx.delay.mix'] = 0.3;
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'oscA.table')] = i % 2 ? 9 : 7; // GLITCH / VOX
    params[pad(i, 'oscA.tune')] = -24 + (i * 11) % 47;
    params[pad(i, 'pan')] = ((i % 5) - 2) * 0.28;
    params[pad(i, 'mod1.src')] = 3;
    params[pad(i, 'mod1.dst')] = 1;
    params[pad(i, 'mod1.amt')] = 0.22;
  }
  return params;
}

function liveRoomParams(): Partial<ParamValues> {
  const params = { ...classic808Params() };
  params['seq.bpm'] = 110;
  params['master.swing'] = 0.2;
  params['fx.reverb.size'] = 0.88;
  params['fx.reverb.mix'] = 0.36;
  params['fx.comp.thr'] = -12;
  params['fx.comp.gain'] = 2;
  for (let i = 0; i < PAD_COUNT; i++) {
    params[pad(i, 'aenv.hold')] = i < 4 ? 0.04 : 0.02;
    params[pad(i, 'aenv.dec')] = Math.min(4, (params[pad(i, 'aenv.dec')] ?? 0.24) * 1.35);
    params[pad(i, 'v2l')] = 0.82;
  }
  return params;
}

const PATTERNS = trVoidPatterns();

export const FACTORY_KITS: Kit[] = [
  { name: 'TR-VOID', params: trVoidParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'ROOM ONE', params: roomOneParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'BITCRUSH', params: bitcrushParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: '808 CLASSIC', params: classic808Params(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'DEEP DUB', params: deepDubParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'DUST HOUSE', params: dustHouseParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'WAREHOUSE', params: warehouseParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'METAL WORK', params: metalWorkParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'TAPE KIT', params: tapeKitParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'MINIMAL', params: minimalParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'BROKEN TOYS', params: brokenToysParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'LIVE ROOM', params: liveRoomParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: 'UZU', params: uzuParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
  { name: '808+UZU HYBRID', params: hybridParams(), padNames: [...PAD_NAMES], patterns: [...PATTERNS], chain: [0] },
];

export function kitToState(kit: Kit): {
  params: ParamValues;
  padNames: string[];
  patterns: Patterns;
  chain: number[];
  tables: SerializedUserTable[];
} {
  const params = { ...defaultDrumParams(), ...kit.params } as ParamValues;

  // v1 kits stored one global rack under `fx.*`. Interpret that rack as the
  // initial settings for every pad unless the kit already contains the newer
  // pad-scoped value. This keeps old localStorage kits and all factory kits
  // sounding intentional while allowing pads to diverge after loading.
  for (const def of FX_DEFS) {
    const legacy = kit.params[def.id];
    if (legacy === undefined) continue;
    for (let i = 0; i < PAD_COUNT; i++) {
      const id = pad(i, def.id);
      if (kit.params[id] === undefined) params[id] = legacy;
    }
    delete params[def.id];
  }
  return {
    params,
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
