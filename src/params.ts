// Canonical parameter definitions. Every knob, preset and engine param keys off this.
// curve: 'lin' | 'log' (geometric min..max) | 'int'

export const fmtHz = (v: number) => (v >= 1000 ? (v / 1000).toFixed(2) + ' kHz' : v.toFixed(v < 100 ? 1 : 0) + ' Hz');
export const fmtSec = (v: number) => (v < 1 ? (v * 1000).toFixed(0) + ' ms' : v.toFixed(2) + ' s');
export const fmtPct = (v: number) => Math.round(v * 100) + '%';
export const fmtPan = (v: number) => (Math.abs(v) < 0.01 ? 'C' : (v < 0 ? Math.round(-v * 100) + 'L' : Math.round(v * 100) + 'R'));
export const fmtSigned = (v: number) => (v > 0 ? '+' : '') + Math.round(v);
export const fmtBi = (v: number) => (v > 0 ? '+' : '') + Math.round(v * 100);

export const TABLE_NAMES = ['PRIME', 'BLOOM', 'PULSE', 'VOX', 'CHIME', 'GLITCH'];
export const FILTER_TYPES = ['LP 12', 'LP 24', 'BP 12', 'HP 12', 'NOTCH'];
export const LFO_SHAPES = ['SINE', 'TRI', 'SAW', 'SQR', 'S&H'];
export const SUB_SHAPES = ['SINE', 'SQR'];
export const NOISE_TYPES = ['WHITE', 'PINK'];
export const MOD_SOURCES = ['—', 'LFO 1', 'LFO 2', 'MOD ENV', 'VELO', 'NOTE'];
export const MOD_DESTS = ['—', 'A POS', 'B POS', 'CUTOFF', 'PITCH', 'AMP', 'PAN', 'A LVL', 'B LVL'];

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

function matSlot(n: number): ParamDef[] {
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

  { id: 'filter.on', type: 'bool', def: 1 },
  { id: 'filter.type', type: 'enum', options: FILTER_TYPES, def: 1 },
  { id: 'filter.cutoff', label: 'CUTOFF', min: 20, max: 20000, def: 9000, curve: 'log', fmt: fmtHz },
  { id: 'filter.res', label: 'RES', min: 0, max: 1, def: 0.18, curve: 'lin', fmt: fmtPct },
  { id: 'filter.drive', label: 'DRIVE', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
  { id: 'filter.env', label: 'ENV', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtBi },
  { id: 'filter.key', label: 'KEY', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },

  { id: 'env1.a', label: 'ATK', min: 0.001, max: 8, def: 0.004, curve: 'log', fmt: fmtSec },
  { id: 'env1.d', label: 'DEC', min: 0.005, max: 10, def: 0.25, curve: 'log', fmt: fmtSec },
  { id: 'env1.s', label: 'SUS', min: 0, max: 1, def: 0.8, curve: 'lin', fmt: fmtPct },
  { id: 'env1.r', label: 'REL', min: 0.005, max: 12, def: 0.3, curve: 'log', fmt: fmtSec },
  { id: 'env2.a', label: 'ATK', min: 0.001, max: 8, def: 0.01, curve: 'log', fmt: fmtSec },
  { id: 'env2.d', label: 'DEC', min: 0.005, max: 10, def: 0.35, curve: 'log', fmt: fmtSec },
  { id: 'env2.s', label: 'SUS', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
  { id: 'env2.r', label: 'REL', min: 0.005, max: 12, def: 0.3, curve: 'log', fmt: fmtSec },

  { id: 'lfo1.shape', type: 'enum', options: LFO_SHAPES, def: 0 },
  { id: 'lfo1.rate', label: 'RATE', min: 0.02, max: 30, def: 2, curve: 'log', fmt: (v) => v.toFixed(2) + ' Hz' },
  { id: 'lfo2.shape', type: 'enum', options: LFO_SHAPES, def: 0 },
  { id: 'lfo2.rate', label: 'RATE', min: 0.02, max: 30, def: 5, curve: 'log', fmt: (v) => v.toFixed(2) + ' Hz' },

  ...matSlot(1), ...matSlot(2), ...matSlot(3), ...matSlot(4),

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
