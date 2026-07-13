// DR-1 per-pad patches: save/load/switch a SINGLE pad's sound independent of
// kits. Factory entries contain only authored overrides on pad defaults; apply
// semantics are defaults ∪ overrides for every included field so a patch fully
// resets the pad. `out` and `choke` are routing/kit-level and never included.

import type { ParamValues } from '../params';
import { DRUM_PARAMS, PAD_FIELDS, pad } from './params';

export interface PadPatch {
  v: 1;
  name: string;                    // displayed uppercase, e.g. "BD DEEP"
  params: Record<string, number>;  // PAD-RELATIVE ids -> values (overrides)
}

// Fields excluded from patches everywhere (kit-level routing behavior).
const EXCLUDED = new Set(['out', 'choke']);

export const PATCH_FIELDS: string[] = PAD_FIELDS.filter((f) => !EXCLUDED.has(f));

// -- Factory bank -------------------------------------------------------------
// Table indices follow DRUM_TABLE_NAMES: THUD 0, CRACK 1, TINE 2, GRIT 3,
// PRIME 4, BLOOM 5, PULSE 6, VOX 7, CHIME 8, GLITCH 9, 808SD 10, 808CP 11,
// 808CH 12, 808OH 13, 808CY 14. Filter types: LP12 0, LP24 1, BP12 2, HP12 3.

const fp = (name: string, params: Record<string, number>): PadPatch => ({ v: 1, name, params });

export const FACTORY_PATCHES: PadPatch[] = [
  // Kicks — THUD body, pitch envelope does the punch.
  fp('BD DEEP', {
    'oscA.table': 0, 'oscA.tune': -14, 'penv.amt': 24, 'penv.dec': 0.05,
    'aenv.dec': 0.42, 'aenv.curve': 0.45, 'lvl': 0.9,
  }),
  fp('BD PUNCH', {
    'oscA.table': 0, 'oscA.tune': -7, 'penv.amt': 32, 'penv.dec': 0.028,
    'aenv.dec': 0.2, 'aenv.curve': 0.5, 'lvl': 0.9,
  }),
  fp('BD SUB', {
    'oscA.table': 0, 'oscA.tune': -22, 'penv.amt': 20, 'penv.dec': 0.06,
    'aenv.dec': 0.95, 'aenv.hold': 0.02, 'aenv.curve': 0.3, 'lvl': 0.92,
  }),
  fp('BD 808', {
    'oscA.table': 0, 'oscA.tune': -12, 'penv.amt': 26, 'penv.dec': 0.075,
    'aenv.dec': 0.65, 'aenv.curve': 0.35, 'flt.on': 1, 'flt.type': 0,
    'flt.cut': 900, 'flt.drive': 0.35, 'lvl': 0.92,
  }),
  // Snares — tonal crack plus a bright noise layer.
  fp('SD CRACK', {
    'oscA.table': 1, 'oscA.tune': 0, 'penv.amt': 5, 'penv.dec': 0.03,
    'noise.level': 0.5, 'noise.color': 0.3, 'aenv.dec': 0.18, 'lvl': 0.85,
  }),
  fp('SD 808', {
    'oscA.table': 10, 'oscA.tune': 0, 'penv.amt': 3, 'penv.dec': 0.025,
    'noise.level': 0.35, 'noise.color': 0.45, 'aenv.dec': 0.22, 'lvl': 0.85,
  }),
  fp('SD RIM', {
    'oscA.table': 1, 'oscA.tune': 12, 'penv.amt': 2, 'penv.dec': 0.015,
    'noise.level': 0.18, 'noise.color': 0.6, 'aenv.dec': 0.08, 'lvl': 0.8,
  }),
  // Clap — sampled 808 clap with a room-friendly tail.
  fp('CP 808', {
    'oscA.table': 11, 'oscA.tune': 0, 'noise.level': 0.3, 'noise.color': 0.5,
    'aenv.hold': 0.02, 'aenv.dec': 0.3, 'aenv.curve': 0.4, 'lvl': 0.85,
  }),
  // Hats — sampled 808 hats, high-passed to sit above the kit.
  fp('HH 808', {
    'oscA.table': 12, 'aenv.dec': 0.06, 'flt.on': 1, 'flt.type': 3,
    'flt.cut': 7200, 'ring.freq': 6389, 'ring.mix': 0.18, 'lvl': 0.7,
  }),
  fp('HH TIGHT', {
    'oscA.table': 12, 'aenv.dec': 0.035, 'flt.on': 1, 'flt.type': 3,
    'flt.cut': 9200, 'ring.freq': 7919, 'ring.mix': 0.22, 'lvl': 0.65,
  }),
  fp('OH 808', {
    'oscA.table': 13, 'aenv.dec': 0.55, 'flt.on': 1, 'flt.type': 3,
    'flt.cut': 5200, 'ring.freq': 5197, 'ring.mix': 0.28, 'lvl': 0.7,
  }),
  // Cymbal — long sizzle.
  fp('CY 808', {
    'oscA.table': 14, 'aenv.dec': 1.7, 'aenv.curve': 0.25, 'flt.on': 1,
    'flt.type': 3, 'flt.cut': 3800, 'ring.freq': 2741, 'ring.mix': 0.62,
    'lvl': 0.72,
  }),
  // Toms — THUD tuned across the range with a modest pitch sweep.
  fp('TM LO', {
    'oscA.table': 0, 'oscA.tune': -12, 'penv.amt': 12, 'penv.dec': 0.07,
    'aenv.dec': 0.42, 'lvl': 0.85,
  }),
  fp('TM MID', {
    'oscA.table': 0, 'oscA.tune': -5, 'penv.amt': 10, 'penv.dec': 0.06,
    'aenv.dec': 0.36, 'lvl': 0.85,
  }),
  fp('TM HI', {
    'oscA.table': 0, 'oscA.tune': 2, 'penv.amt': 8, 'penv.dec': 0.05,
    'aenv.dec': 0.3, 'lvl': 0.85,
  }),
  // Perc / vox / glitch flavors. The fixed-Hz ring carrier creates
  // inharmonic sidebands, avoiding the cartoonish pitched-table character.
  fp('PC TINE', {
    'oscA.table': 2, 'oscA.tune': 7, 'oscA.fine': -5, 'ring.freq': 1187,
    'ring.mix': 0.62, 'aenv.dec': 0.42, 'aenv.curve': 0.28, 'lvl': 0.75,
  }),
  fp('PC BELL', {
    'oscA.table': 8, 'oscA.tune': -5, 'oscA.level': 0.7, 'ring.freq': 731,
    'ring.mix': 0.78, 'aenv.att': 0.0005, 'aenv.hold': 0.018,
    'aenv.dec': 1.35, 'aenv.curve': 0.2, 'flt.on': 1, 'flt.type': 2,
    'flt.cut': 2400, 'flt.res': 0.35, 'lvl': 0.78,
  }),
  fp('PC CYMBAL', {
    'oscA.table': 3, 'oscA.pos': 0.38, 'oscA.tune': 17, 'noise.level': 0.22,
    'noise.color': 0.75, 'ring.freq': 3271, 'ring.mix': 0.88,
    'aenv.hold': 0.014, 'aenv.dec': 1.6, 'aenv.curve': 0.24,
    'flt.on': 1, 'flt.type': 3, 'flt.cut': 3600, 'lvl': 0.72,
  }),
  fp('PC VOX', {
    'oscA.table': 7, 'oscA.tune': -5, 'aenv.dec': 0.48, 'aenv.curve': 0.3,
    'lvl': 0.78,
  }),
  fp('PC GLITCH', {
    'oscA.table': 3, 'oscA.tune': -12, 'oscA.pos': 0.5, 'penv.amt': 9,
    'penv.dec': 0.04, 'aenv.dec': 0.22, 'lvl': 0.78,
  }),
];

// -- Apply / extract ----------------------------------------------------------

// Absolute id->value entries that apply `patch` to pad `padI`: defaults ∪
// overrides for every included field. `out`/`choke` are untouched (not
// returned), so their current values in `params` survive.
export function applyPatchToParams(
  _params: ParamValues,
  padI: number,
  patch: PadPatch,
): Record<string, number> {
  const entries: Record<string, number> = {};
  for (const field of PATCH_FIELDS) {
    const id = pad(padI, field);
    entries[id] = patch.params[field] ?? DRUM_PARAMS[id].def;
  }
  return entries;
}

// Snapshot pad `padI`'s current sound as a patch (all included fields).
export function extractPatch(params: ParamValues, padI: number, name: string): PadPatch {
  const patch: PadPatch = { v: 1, name, params: {} };
  for (const field of PATCH_FIELDS) {
    const id = pad(padI, field);
    patch.params[field] = params[id] ?? DRUM_PARAMS[id].def;
  }
  return patch;
}

// -- User patch persistence (same resilience pattern as kits.ts) --------------

const LS_KEY = 'fable-dr-patches';
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

export function loadUserPatches(): PadPatch[] {
  try {
    const parsed = JSON.parse(readStored(LS_KEY) as string);
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

export function saveUserPatch(name: string, patch: PadPatch): PadPatch[] {
  const list = loadUserPatches().filter((entry) => entry.name !== name);
  list.push({ ...patch, name });
  writeStored(LS_KEY, JSON.stringify(list));
  return list;
}

// -- Options (factory then user, value keys f0…/u0… like kitOptions) ----------

export interface PatchOption {
  value: string;
  name: string;
  group: 'FACTORY' | 'USER';
}

export function patchOptions(userPatches: PadPatch[]): PatchOption[] {
  const options: PatchOption[] = FACTORY_PATCHES.map((patch, i) => ({
    value: `f${i}`,
    name: patch.name,
    group: 'FACTORY' as const,
  }));
  userPatches.forEach((patch, i) => options.push({ value: `u${i}`, name: patch.name, group: 'USER' }));
  return options;
}
