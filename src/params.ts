// Canonical parameter definitions. Every knob, preset and engine param keys off this.
// curve: 'lin' | 'log' (geometric min..max) | 'int'

export const fmtHz = (v: number) => (v >= 1000 ? (v / 1000).toFixed(2) + ' kHz' : v.toFixed(v < 100 ? 1 : 0) + ' Hz');
export const fmtSec = (v: number) => (v < 1 ? (v * 1000).toFixed(0) + ' ms' : v.toFixed(2) + ' s');
export const fmtPct = (v: number) => Math.round(v * 100) + '%';
export const fmtPan = (v: number) => (Math.abs(v) < 0.01 ? 'C' : (v < 0 ? Math.round(-v * 100) + 'L' : Math.round(v * 100) + 'R'));
export const fmtSigned = (v: number) => (v > 0 ? '+' : '') + Math.round(v);
export const fmtBi = (v: number) => (v > 0 ? '+' : '') + Math.round(v * 100);

export const TABLE_NAMES = ['PRIME', 'BLOOM', 'PULSE', 'VOX', 'CHIME', 'GLITCH'];
// SVF types keep indices 0..4; comb + vowel are appended so existing presets stay valid.
export const FILTER_TYPES = ['LP 12', 'LP 24', 'BP 12', 'HP 12', 'NOTCH', 'COMB', 'VOWEL'];
export const FILTER_ROUTES = ['SERIAL', 'PARALLEL', 'SPLIT'];
export const LFO_SHAPES = ['SINE', 'TRI', 'SAW', 'SQR', 'S&H'];
export const LFO_DIVS = ['1/1', '1/2', '1/4', '1/4.', '1/4T', '1/8', '1/8.', '1/8T', '1/16', '1/16T', '1/32'];
// Cycles per beat (beat = quarter note) for each LFO_DIVS entry. Mirrors
// LFO_DIV_F in engine/worklet.js — keep the two in sync.
export const LFO_DIV_F = [0.25, 0.5, 1, 2 / 3, 1.5, 2, 4 / 3, 3, 4, 6, 8];
// The standalone web build has no host transport; synced LFOs (and their
// displays) lock to this tempo. Mirrors the worklet's default bpm.
export const SYNC_BPM = 120;
export const SUB_SHAPES = ['SINE', 'SQR'];
export const NOISE_TYPES = ['WHITE', 'PINK'];
export const MOD_SOURCES = ['—', 'LFO 1', 'LFO 2', 'MOD ENV', 'VELO', 'NOTE'];
// Indices 0..8 are frozen for preset compatibility; F2 destinations are appended.
export const MOD_DESTS = ['—', 'A POS', 'B POS', 'F1 CUT', 'PITCH', 'AMP', 'PAN', 'A LVL', 'B LVL', 'F2 CUT', 'F2 RES'];

// A single modulation route: a source (MOD_SOURCES index) drives a destination
// (MOD_DESTS index) by `amt` (-1..1). Serum-style — any number of routes, built
// by dragging a source onto a control rather than filling fixed matrix slots.
export interface ModConnection {
  src: number;
  dst: number;
  amt: number;
}

// Tint per MOD_SOURCES index. Each source reuses the accent of the panel it
// lives in (LFO 1 cyan, LFO 2 amber, MOD ENV violet, velocity/note slate) so a
// mod ring's color tells you where the movement comes from at a glance.
export const SOURCE_COLORS = ['', '#4de8ff', '#ffa14d', '#b18cff', '#9fb4d8', '#9fb4d8'];

// Which MOD_DESTS slot a given knob/slider drives — used for drag-to-assign and
// the on-control modulation rings. Pitch/amp/pan are global (no single control)
// and so are only assignable from the matrix list, not by dragging onto a knob.
export const DEST_OF_PARAM: Record<string, number> = {
  'oscA.pos': 1,
  'oscB.pos': 2,
  'filter.cutoff': 3,
  'oscA.level': 7,
  'oscB.level': 8,
  'filter2.cutoff': 9,
  'filter2.res': 10,
};

export type Curve = 'lin' | 'log' | 'int';
export type ParamType = 'bool' | 'enum';

export interface ParamDef {
  id: string;
  label?: string;
  min?: number;
  max?: number;
  def: number;
  curve?: Curve;
  fmt?: (v: number) => string;
  type?: ParamType;
  options?: string[];
}

export type ParamId = string;
export type ParamValues = Record<ParamId, number>;

function oscParams(prefix: string, defOn: number, defTable: number): ParamDef[] {
  return [
    { id: `${prefix}.on`, type: 'bool', def: defOn },
    { id: `${prefix}.table`, type: 'enum', options: TABLE_NAMES, def: defTable },
    { id: `${prefix}.pos`, label: 'POS', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.oct`, label: 'OCT', min: -3, max: 3, def: 0, curve: 'int', fmt: fmtSigned },
    { id: `${prefix}.semi`, label: 'SEMI', min: -12, max: 12, def: 0, curve: 'int', fmt: fmtSigned },
    { id: `${prefix}.fine`, label: 'FINE', min: -100, max: 100, def: 0, curve: 'int', fmt: fmtSigned },
    { id: `${prefix}.unison`, label: 'UNI', min: 1, max: 7, def: 1, curve: 'int', fmt: (v) => String(Math.round(v)) },
    { id: `${prefix}.detune`, label: 'DETUNE', min: 0, max: 1, def: 0.2, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.spread`, label: 'SPREAD', min: 0, max: 1, def: 0.6, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.level`, label: 'LEVEL', min: 0, max: 1, def: 0.75, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.pan`, label: 'PAN', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtPan },
  ];
}

// One filter's parameter set. For COMB the cutoff knob tunes the comb pitch and
// RES sets feedback; for VOWEL cutoff morphs A-E-I-O-U and RES sharpens the formants.
function filterParams(prefix: string, defOn: number, defType: number, defCut: number): ParamDef[] {
  return [
    { id: `${prefix}.on`, type: 'bool', def: defOn },
    { id: `${prefix}.type`, type: 'enum', options: FILTER_TYPES, def: defType },
    { id: `${prefix}.cutoff`, label: 'CUTOFF', min: 20, max: 20000, def: defCut, curve: 'log', fmt: fmtHz },
    { id: `${prefix}.res`, label: 'RES', min: 0, max: 1, def: 0.18, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.drive`, label: 'DRIVE', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.env`, label: 'ENV', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtBi },
    { id: `${prefix}.key`, label: 'KEY', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
  ];
}

function lfoParams(prefix: string, defRate: number): ParamDef[] {
  return [
    { id: `${prefix}.shape`, type: 'enum', options: LFO_SHAPES, def: 0 },
    { id: `${prefix}.rate`, label: 'RATE', min: 0.02, max: 30, def: defRate, curve: 'log', fmt: (v) => v.toFixed(2) + ' Hz' },
    { id: `${prefix}.sync`, type: 'bool', def: 0 },
    { id: `${prefix}.syncrate`, type: 'enum', options: LFO_DIVS, def: 2 },
    { id: `${prefix}.rise`, label: 'RISE', min: 0, max: 5, def: 0, curve: 'lin', fmt: fmtSec },
    { id: `${prefix}.phase`, label: 'PHASE', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.retrig`, type: 'bool', def: 1 },
  ];
}

// One mod-matrix slot (mat1..mat16): a source enum, a dest enum and a bipolar
// amount. Mirrors the C++ addMat order in Params.cpp so preset ids line up 1:1.
function matParams(n: number): ParamDef[] {
  return [
    { id: `mat${n}.src`, type: 'enum', options: MOD_SOURCES, def: 0 },
    { id: `mat${n}.dst`, type: 'enum', options: MOD_DESTS, def: 0 },
    { id: `mat${n}.amt`, label: 'AMT', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtBi },
  ];
}

export const PARAM_DEFS: ParamDef[] = [
  ...oscParams('oscA', 1, 0),
  ...oscParams('oscB', 0, 1),

  { id: 'sub.on', type: 'bool', def: 0 },
  { id: 'sub.shape', type: 'enum', options: SUB_SHAPES, def: 0 },
  { id: 'sub.oct', label: 'OCT', min: -2, max: -1, def: -1, curve: 'int', fmt: fmtSigned },
  { id: 'sub.level', label: 'LEVEL', min: 0, max: 1, def: 0.5, curve: 'lin', fmt: fmtPct },
  { id: 'noise.on', type: 'bool', def: 0 },
  { id: 'noise.type', type: 'enum', options: NOISE_TYPES, def: 0 },
  { id: 'noise.level', label: 'LEVEL', min: 0, max: 1, def: 0.25, curve: 'lin', fmt: fmtPct },

  ...filterParams('filter', 1, 1, 9000),
  { id: 'filter.route', type: 'enum', options: FILTER_ROUTES, def: 0 },
  ...filterParams('filter2', 0, 0, 2000),

  { id: 'env1.a', label: 'ATK', min: 0.001, max: 8, def: 0.004, curve: 'log', fmt: fmtSec },
  { id: 'env1.d', label: 'DEC', min: 0.005, max: 10, def: 0.25, curve: 'log', fmt: fmtSec },
  { id: 'env1.s', label: 'SUS', min: 0, max: 1, def: 0.8, curve: 'lin', fmt: fmtPct },
  { id: 'env1.r', label: 'REL', min: 0.005, max: 12, def: 0.3, curve: 'log', fmt: fmtSec },
  { id: 'env2.a', label: 'ATK', min: 0.001, max: 8, def: 0.01, curve: 'log', fmt: fmtSec },
  { id: 'env2.d', label: 'DEC', min: 0.005, max: 10, def: 0.35, curve: 'log', fmt: fmtSec },
  { id: 'env2.s', label: 'SUS', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
  { id: 'env2.r', label: 'REL', min: 0.005, max: 12, def: 0.3, curve: 'log', fmt: fmtSec },

  ...lfoParams('lfo1', 2),
  ...lfoParams('lfo2', 5),

  ...Array.from({ length: 16 }, (_, i) => matParams(i + 1)).flat(),

  { id: 'fx.drive.on', type: 'bool', def: 0 },
  { id: 'fx.drive.amt', label: 'AMOUNT', min: 0, max: 1, def: 0.3, curve: 'lin', fmt: fmtPct },
  { id: 'fx.drive.mix', label: 'MIX', min: 0, max: 1, def: 1, curve: 'lin', fmt: fmtPct },
  { id: 'fx.chorus.on', type: 'bool', def: 0 },
  { id: 'fx.chorus.rate', label: 'RATE', min: 0.05, max: 8, def: 0.6, curve: 'log', fmt: (v) => v.toFixed(2) + ' Hz' },
  { id: 'fx.chorus.depth', label: 'DEPTH', min: 0, max: 1, def: 0.5, curve: 'lin', fmt: fmtPct },
  { id: 'fx.chorus.mix', label: 'MIX', min: 0, max: 1, def: 0.5, curve: 'lin', fmt: fmtPct },
  { id: 'fx.delay.on', type: 'bool', def: 0 },
  { id: 'fx.delay.time', label: 'TIME', min: 0.02, max: 1.5, def: 0.36, curve: 'log', fmt: fmtSec },
  { id: 'fx.delay.fb', label: 'FDBK', min: 0, max: 0.92, def: 0.35, curve: 'lin', fmt: fmtPct },
  { id: 'fx.delay.mix', label: 'MIX', min: 0, max: 1, def: 0.3, curve: 'lin', fmt: fmtPct },
  { id: 'fx.reverb.on', type: 'bool', def: 0 },
  { id: 'fx.reverb.size', label: 'SIZE', min: 0, max: 1, def: 0.5, curve: 'lin', fmt: fmtPct },
  { id: 'fx.reverb.mix', label: 'MIX', min: 0, max: 1, def: 0.3, curve: 'lin', fmt: fmtPct },

  { id: 'master.volume', label: 'MASTER', min: 0, max: 1, def: 0.75, curve: 'lin', fmt: fmtPct },
  { id: 'master.glide', label: 'GLIDE', min: 0, max: 0.5, def: 0, curve: 'lin', fmt: fmtSec },
];

export const PARAMS: Record<string, ParamDef> = Object.fromEntries(PARAM_DEFS.map((d) => [d.id, d]));

export function defaultParams(): ParamValues {
  const o: ParamValues = {};
  for (const d of PARAM_DEFS) o[d.id] = d.def;
  return o;
}

// normalized [0,1] <-> value mapping
export function normToValue(def: ParamDef, n: number): number {
  n = Math.min(1, Math.max(0, n));
  const min = def.min as number;
  const max = def.max as number;
  if (def.curve === 'log') return min * Math.pow(max / min, n);
  const v = min + (max - min) * n;
  return def.curve === 'int' ? Math.round(v) : v;
}
export function valueToNorm(def: ParamDef, v: number): number {
  const min = def.min as number;
  const max = def.max as number;
  if (def.curve === 'log') return Math.log(v / min) / Math.log(max / min);
  return (v - min) / (max - min);
}
