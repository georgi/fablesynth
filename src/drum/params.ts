// Canonical DR-1 parameter definitions. Per-pad params are namespaced
// `pad<i>.<field>`; every knob, kit, and worklet message keys off this table
// (same params-as-truth discipline as WT-1's src/params.ts).

import {
  fmtHz, fmtSec, fmtPct, fmtPan, fmtBi,
  type ParamDef, type ParamValues,
} from '../params';

export const PAD_COUNT = 16;
export const MIDI_BASE = 36; // pad 0 = MIDI C1, pads 0..15 = 36..51

// 4 drum tables (Task 2) followed by WT-1's six procedural tables, followed
// by 5 sample-derived TR-808 tables (Task 2 of the sampled-tables plan).
export const DRUM_TABLE_NAMES = [
  'THUD', 'CRACK', 'TINE', 'GRIT', 'PRIME', 'BLOOM', 'PULSE', 'VOX', 'CHIME', 'GLITCH',
  '808SD', '808CP', '808CH', '808OH', '808CY',
];
export const DRUM_FILTER_TYPES = ['LP 12', 'LP 24', 'BP 12', 'HP 12', 'NOTCH'];
export const NOISE_COLORS = ['WHITE'];
// Mod sources are per-pad: MOD ENV (per-pad decay env), VELO (hit velocity ×
// V→MOD), RAND (uniform ±1, drawn once per hit).
export const DMOD_SOURCES = ['—', 'MOD ENV', 'VELO', 'RAND'];
export const DMOD_DESTS = ['—', 'A POS', 'B POS', 'LEVEL', 'CUTOFF', 'PITCH', 'A FINE', 'B FINE', 'NOISE LVL', 'RES'];
export const CHOKE_NAMES = ['—', 'CHK 1', 'CHK 2', 'CHK 3', 'CHK 4'];
export const OUT_NAMES = ['MAIN', 'AUX 1', 'AUX 2', 'AUX 3', 'AUX 4'];

export const pad = (i: number, field: string): string => `pad${i}.${field}`;

const fmtSt = (v: number) => (v > 0 ? '+' : '') + Math.round(v) + ' ST';
const fmtCt = (v: number) => (v > 0 ? '+' : '') + Math.round(v) + ' CT';

function oscFields(pre: 'oscA' | 'oscB'): ParamDef[] {
  return [
    { id: `${pre}.table`, type: 'enum', options: DRUM_TABLE_NAMES, def: pre === 'oscA' ? 0 : 1 },
    { id: `${pre}.pos`, label: 'POS', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
    { id: `${pre}.tune`, label: 'TUNE', min: -48, max: 48, def: 0, curve: 'int', fmt: fmtSt },
    { id: `${pre}.fine`, label: 'FINE', min: -100, max: 100, def: 0, curve: 'int', fmt: fmtCt },
    { id: `${pre}.phase`, label: 'PHASE', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
    { id: `${pre}.unison`, label: 'UNI', min: 1, max: 7, def: 1, curve: 'int', fmt: (v) => String(Math.round(v)) },
    { id: `${pre}.detune`, label: 'DET', min: 0, max: 1, def: 0.2, curve: 'lin', fmt: fmtPct },
    { id: `${pre}.level`, label: 'LVL', min: 0, max: 1, def: pre === 'oscA' ? 0.75 : 0, curve: 'lin', fmt: fmtPct },
  ];
}

// One pad's field suffixes + defs (id fields are RELATIVE here; padded below).
const PAD_DEFS: ParamDef[] = [
  ...oscFields('oscA'),
  ...oscFields('oscB'),
  { id: 'noise.color', label: 'COLOR', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtBi },
  { id: 'noise.level', label: 'LVL', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
  { id: 'ring.freq', label: 'RING Hz', min: 20, max: 12000, def: 1200, curve: 'log', fmt: fmtHz },
  { id: 'ring.mix', label: 'RING', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
  { id: 'penv.amt', label: 'AMT', min: -48, max: 48, def: 0, curve: 'int', fmt: fmtSt },
  { id: 'penv.dec', label: 'DEC', min: 0.005, max: 2, def: 0.06, curve: 'log', fmt: fmtSec },
  { id: 'aenv.att', label: 'ATT', min: 0.0005, max: 0.5, def: 0.001, curve: 'log', fmt: fmtSec },
  { id: 'aenv.hold', label: 'HOLD', min: 0, max: 0.25, def: 0.01, curve: 'lin', fmt: fmtSec },
  { id: 'aenv.dec', label: 'DEC', min: 0.005, max: 4, def: 0.24, curve: 'log', fmt: fmtSec },
  { id: 'aenv.curve', label: 'CURVE', min: 0, max: 1, def: 0.35, curve: 'lin', fmt: fmtPct },
  { id: 'flt.on', type: 'bool', def: 0 },
  { id: 'flt.type', type: 'enum', options: DRUM_FILTER_TYPES, def: 0 },
  { id: 'flt.cut', label: 'CUT', min: 20, max: 20000, def: 1800, curve: 'log', fmt: fmtHz },
  { id: 'flt.res', label: 'RES', min: 0, max: 1, def: 0.18, curve: 'lin', fmt: fmtPct },
  { id: 'flt.drive', label: 'DRIVE', min: 0, max: 1, def: 0, curve: 'lin', fmt: fmtPct },
  ...[1, 2, 3, 4].flatMap((n): ParamDef[] => [
    { id: `mod${n}.src`, type: 'enum', options: DMOD_SOURCES, def: 0 },
    { id: `mod${n}.dst`, type: 'enum', options: DMOD_DESTS, def: 0 },
    { id: `mod${n}.amt`, label: '', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtBi },
  ]),
  { id: 'modenv.dec', label: 'DEC', min: 0.005, max: 2, def: 0.084, curve: 'log', fmt: fmtSec },
  { id: 'lvl', label: 'LVL', min: 0, max: 1, def: 0.8, curve: 'lin', fmt: fmtPct },
  { id: 'pan', label: 'PAN', min: -1, max: 1, def: 0, curve: 'lin', fmt: fmtPan },
  { id: 'v2l', label: 'V→LVL', min: 0, max: 1, def: 0.6, curve: 'lin', fmt: fmtPct },
  { id: 'v2m', label: 'V→MOD', min: 0, max: 1, def: 0.4, curve: 'lin', fmt: fmtPct },
  { id: 'choke', type: 'enum', options: CHOKE_NAMES, def: 0, min: 0, max: 4, curve: 'int' },
  { id: 'out', type: 'enum', options: OUT_NAMES, def: 0, min: 0, max: 4, curve: 'int' },
];

export const PAD_FIELDS: string[] = PAD_DEFS.map((d) => d.id);

const GLOBAL_DEFS: ParamDef[] = [
  { id: 'seq.bpm', label: 'BPM', min: 60, max: 200, def: 126, curve: 'int', fmt: (v) => String(Math.round(v)) },
  { id: 'master.swing', label: 'SWING', min: 0, max: 1, def: 0.22, curve: 'lin', fmt: fmtPct },
  { id: 'master.volume', label: 'VOL', min: 0, max: 1, def: 0.78, curve: 'lin', fmt: fmtPct },
  { id: 'fx.drive.on', type: 'bool', def: 0 },
  { id: 'fx.drive.amt', label: 'AMT', min: 0, max: 1, def: 0.3, curve: 'lin', fmt: fmtPct },
  { id: 'fx.drive.mix', label: 'MIX', min: 0, max: 1, def: 1, curve: 'lin', fmt: fmtPct },
  { id: 'fx.comp.on', type: 'bool', def: 1 },
  { id: 'fx.comp.thr', label: 'THRESH', min: -40, max: 0, def: -16, curve: 'lin', fmt: (v) => Math.round(v) + ' dB' },
  { id: 'fx.comp.gain', label: 'MAKEUP', min: 0, max: 12, def: 4, curve: 'lin', fmt: (v) => '+' + v.toFixed(1) + ' dB' },
  { id: 'fx.chorus.on', type: 'bool', def: 0 },
  { id: 'fx.chorus.rate', label: 'RATE', min: 0.05, max: 8, def: 0.6, curve: 'log', fmt: (v) => v.toFixed(2) + ' Hz' },
  { id: 'fx.chorus.depth', label: 'DEPTH', min: 0, max: 1, def: 0.4, curve: 'lin', fmt: fmtPct },
  { id: 'fx.chorus.mix', label: 'MIX', min: 0, max: 1, def: 0.2, curve: 'lin', fmt: fmtPct },
  { id: 'fx.delay.on', type: 'bool', def: 0 },
  { id: 'fx.delay.time', label: 'TIME', min: 0.02, max: 1.5, def: 0.36, curve: 'log', fmt: fmtSec },
  { id: 'fx.delay.fb', label: 'FDBK', min: 0, max: 0.92, def: 0.35, curve: 'lin', fmt: fmtPct },
  { id: 'fx.delay.mix', label: 'MIX', min: 0, max: 1, def: 0.15, curve: 'lin', fmt: fmtPct },
  { id: 'fx.reverb.on', type: 'bool', def: 1 },
  { id: 'fx.reverb.size', label: 'SIZE', min: 0, max: 1, def: 0.4, curve: 'lin', fmt: fmtPct },
  { id: 'fx.reverb.mix', label: 'MIX', min: 0, max: 1, def: 0.16, curve: 'lin', fmt: fmtPct },
];

export const DRUM_PARAM_DEFS: ParamDef[] = [
  ...Array.from({ length: PAD_COUNT }, (_, i) =>
    PAD_DEFS.map((d) => ({ ...d, id: pad(i, d.id) })),
  ).flat(),
  ...GLOBAL_DEFS,
];

export const DRUM_PARAMS: Record<string, ParamDef> = Object.fromEntries(DRUM_PARAM_DEFS.map((d) => [d.id, d]));

export function defaultDrumParams(): ParamValues {
  const o: ParamValues = {};
  for (const d of DRUM_PARAM_DEFS) o[d.id] = d.def;
  return o;
}
