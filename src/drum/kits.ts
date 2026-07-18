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
  // Snare: the sample layer (808SD, the default) carries the crack at full
  // level while the oscillator drops two octaves and sits half back, so it
  // reads as body under the sample rather than as a second pitched hit. A
  // 100 ms decay keeps it short enough for the delay to do the dub work.
  params[pad(2, 'oscA.tune')] = -24;
  params[pad(2, 'oscA.level')] = 0.5;
  params[pad(2, 'oscB.level')] = 1;
  params[pad(2, 'aenv.dec')] = 0.1;
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

// -- Authored kits ------------------------------------------------------------
// The kits below are written from scratch (not derived from TR-VOID). Each
// ships a main groove in pattern slots 1-3 and a fill in slot 4; the store
// treats chain as a sequence length, so [0,1,2,3] plays a 4-bar A A A B loop.

type PatternSpec = Record<number, { on: number[]; acc?: number[] }>;

function buildPatterns(groove: PatternSpec, fill: PatternSpec): number[] {
  const patterns = makeEmptyPatterns();
  [groove, groove, groove, fill].forEach((spec, patI) => {
    for (const [padS, { on, acc = [] }] of Object.entries(spec)) {
      for (const step of on) {
        patterns[patIdx(patI, Number(padS), step)] = acc.includes(step) ? 2 : 1;
      }
    }
  });
  return Array.from(patterns);
}

const AB_CHAIN = [0, 1, 2, 3];

// NEON GRID — 118 BPM synthwave / electro. Wavetable kick with a sampled UZU
// transient glued on top, PULSE synth-toms, pitch-envelope zaps, and a
// mod-env noise sweep on the last pad.
const NEON_GRID_PADS = [
  'KICK', 'SUB KICK', 'SNARE', 'CLAP', 'RIM ZAP', 'CH HAT', 'OH HAT', 'RIDE',
  'SYN TOM L', 'SYN TOM M', 'SYN TOM H', 'CRASH', 'ZAP DOWN', 'ZAP UP', 'VOX', 'SWEEP',
];

function neonGridParams(): Partial<ParamValues> {
  const params: Partial<ParamValues> = {
    'seq.bpm': 118,
    'master.swing': 0.1,
    'fx.comp.on': 1,
    'fx.chorus.on': 1, 'fx.chorus.rate': 0.8, 'fx.chorus.depth': 0.35, 'fx.chorus.mix': 0.16,
    'fx.reverb.on': 1, 'fx.reverb.size': 0.58, 'fx.reverb.mix': 0.2,
  };
  Object.assign(params, {
    // Kick: THUD body + UZU BD2 sample click, dry so the low end stays tight.
    [pad(0, 'oscA.tune')]: -22, [pad(0, 'penv.amt')]: 30, [pad(0, 'penv.dec')]: 0.03,
    [pad(0, 'oscB.table')]: 17, [pad(0, 'oscB.level')]: 0.55,
    [pad(0, 'aenv.dec')]: 0.34, [pad(0, 'lvl')]: 0.9, [pad(0, 'fx.reverb.on')]: 0,
    [pad(1, 'oscA.tune')]: -34, [pad(1, 'penv.amt')]: 10, [pad(1, 'penv.dec')]: 0.06,
    [pad(1, 'aenv.dec')]: 1.3, [pad(1, 'lvl')]: 0.85, [pad(1, 'fx.reverb.on')]: 0,
    // Snare: CRACK + noise with an 808SD layer underneath, gated tail.
    [pad(2, 'oscA.table')]: 1, [pad(2, 'oscA.tune')]: -8,
    [pad(2, 'penv.amt')]: 6, [pad(2, 'penv.dec')]: 0.03,
    [pad(2, 'noise.level')]: 0.4, [pad(2, 'noise.color')]: 0.5,
    [pad(2, 'oscB.table')]: 0, [pad(2, 'oscB.level')]: 0.35,
    [pad(2, 'aenv.dec')]: 0.24, [pad(2, 'aenv.curve')]: 0.7,
    [pad(3, 'oscA.level')]: 0, [pad(3, 'oscB.table')]: 1, [pad(3, 'oscB.level')]: 0.85,
    [pad(3, 'aenv.hold')]: 0.02, [pad(3, 'aenv.dec')]: 0.6, [pad(3, 'fx.reverb.mix')]: 0.34,
    // Rim zap: PULSE with a fast steep pitch drop — pure electro.
    [pad(4, 'oscA.table')]: 6, [pad(4, 'oscA.tune')]: 14,
    [pad(4, 'penv.amt')]: 40, [pad(4, 'penv.dec')]: 0.02, [pad(4, 'aenv.dec')]: 0.09,
    [pad(7, 'oscA.level')]: 0, [pad(7, 'oscB.table')]: 23, [pad(7, 'oscB.level')]: 0.8,
    [pad(7, 'aenv.dec')]: 1.1,
    [pad(11, 'oscA.level')]: 0, [pad(11, 'oscB.table')]: 27, [pad(11, 'oscB.level')]: 0.8,
    [pad(11, 'aenv.dec')]: 2.0,
    // Zap pair: GRIT lasers, one falling and one rising, panned apart.
    [pad(12, 'oscA.table')]: 3, [pad(12, 'oscA.tune')]: 18,
    [pad(12, 'penv.amt')]: 48, [pad(12, 'penv.dec')]: 0.05,
    [pad(12, 'aenv.dec')]: 0.16, [pad(12, 'pan')]: -0.25,
    [pad(13, 'oscA.table')]: 3, [pad(13, 'oscA.tune')]: 6,
    [pad(13, 'penv.amt')]: -40, [pad(13, 'penv.dec')]: 0.09,
    [pad(13, 'aenv.dec')]: 0.2, [pad(13, 'pan')]: 0.25,
    [pad(14, 'oscA.table')]: 7, [pad(14, 'oscA.tune')]: -5,
    [pad(14, 'aenv.dec')]: 0.4, [pad(14, 'fx.chorus.mix')]: 0.3,
    // Sweep: pure noise through a band-pass whose cutoff rides the mod env.
    [pad(15, 'oscA.level')]: 0, [pad(15, 'noise.level')]: 0.85, [pad(15, 'noise.color')]: 0.2,
    [pad(15, 'flt.on')]: 1, [pad(15, 'flt.type')]: 2, [pad(15, 'flt.cut')]: 900,
    [pad(15, 'flt.res')]: 0.55, [pad(15, 'mod1.src')]: 1, [pad(15, 'mod1.dst')]: 4,
    [pad(15, 'mod1.amt')]: 0.85, [pad(15, 'modenv.dec')]: 0.9, [pad(15, 'aenv.dec')]: 1.4,
  });
  // 808 hats + disco PULSE toms panned across the field.
  const hats: Array<[number, number, number, number]> = [[5, 2, 0.06, 7500], [6, 3, 0.42, 5600]];
  for (const [padI, sample, dec, cut] of hats) {
    params[pad(padI, 'oscA.level')] = 0;
    params[pad(padI, 'oscB.table')] = sample;
    params[pad(padI, 'oscB.level')] = 0.8;
    params[pad(padI, 'aenv.dec')] = dec;
    params[pad(padI, 'flt.on')] = 1;
    params[pad(padI, 'flt.type')] = 3;
    params[pad(padI, 'flt.cut')] = cut;
    params[pad(padI, 'choke')] = 1;
  }
  [[-9, -0.4], [-2, 0], [5, 0.4]].forEach(([tune, panV], i) => {
    params[pad(8 + i, 'oscA.table')] = 6;
    params[pad(8 + i, 'oscA.tune')] = tune;
    params[pad(8 + i, 'penv.amt')] = 16;
    params[pad(8 + i, 'penv.dec')] = 0.05;
    params[pad(8 + i, 'aenv.dec')] = 0.3 - i * 0.04;
    params[pad(8 + i, 'pan')] = panV;
  });
  return params;
}

const NEON_GRID_PATTERNS = buildPatterns(
  {
    0: { on: [0, 7, 10], acc: [0] },
    1: { on: [0, 8] },
    2: { on: [4, 12], acc: [12] },
    3: { on: [12] },
    4: { on: [3] },
    5: { on: [0, 2, 4, 6, 8, 10, 12, 14], acc: [2, 6, 10, 14] },
    6: { on: [14] },
    12: { on: [6] },
    13: { on: [13] },
    15: { on: [8] },
  },
  {
    0: { on: [0, 7, 10], acc: [0] },
    2: { on: [4, 12, 15], acc: [12] },
    3: { on: [4, 12] },
    5: { on: [0, 2, 4, 6, 8, 10], acc: [2, 6] },
    8: { on: [8] },
    9: { on: [9, 10] },
    10: { on: [11], acc: [11] },
    11: { on: [0] },
    12: { on: [6, 14] },
    13: { on: [3, 13] },
    14: { on: [12] },
  },
);

// ACID CAVE — 138 BPM dark techno. A dry punch kick over a cavernous rumble
// pad, squelching GRIT blips whose filter rides the mod env, and metallic
// ring-mod percussion.
const ACID_CAVE_PADS = [
  'KICK', 'RUMBLE', 'SNARE', 'CLAP', 'RIM', 'CH HAT', 'OH HAT', 'RIDE',
  'BLIP LO', 'BLIP MD', 'BLIP HI', 'CRASH', 'TINE HIT', 'PERC', 'STAB', 'GLITCH',
];

function acidCaveParams(): Partial<ParamValues> {
  const params: Partial<ParamValues> = {
    'seq.bpm': 138,
    'master.swing': 0.04,
    'fx.comp.on': 1, 'fx.comp.thr': -22, 'fx.comp.gain': 4,
    'fx.drive.on': 1, 'fx.drive.amt': 0.5, 'fx.drive.mix': 0.2,
    'fx.delay.on': 1, 'fx.delay.time': 0.33, 'fx.delay.fb': 0.5, 'fx.delay.mix': 0.14,
    'fx.reverb.on': 1, 'fx.reverb.size': 0.7, 'fx.reverb.mix': 0.12,
  };
  Object.assign(params, {
    [pad(0, 'oscA.tune')]: -25, [pad(0, 'penv.amt')]: 28, [pad(0, 'penv.dec')]: 0.035,
    [pad(0, 'aenv.dec')]: 0.3, [pad(0, 'aenv.curve')]: 0.5,
    [pad(0, 'lvl')]: 0.92, [pad(0, 'fx.reverb.on')]: 0,
    // Rumble: same THUD an octave under the kick, low-passed and drowned in a
    // huge per-pad reverb — the classic sub-rumble trick.
    [pad(1, 'oscA.tune')]: -25, [pad(1, 'penv.amt')]: 8, [pad(1, 'penv.dec')]: 0.08,
    [pad(1, 'aenv.dec')]: 2.6, [pad(1, 'lvl')]: 0.55,
    [pad(1, 'flt.on')]: 1, [pad(1, 'flt.type')]: 1, [pad(1, 'flt.cut')]: 300,
    [pad(1, 'fx.reverb.size')]: 0.85, [pad(1, 'fx.reverb.mix')]: 0.55,
    [pad(2, 'oscA.table')]: 3, [pad(2, 'oscA.tune')]: -7,
    [pad(2, 'noise.level')]: 0.55, [pad(2, 'noise.color')]: 0.1, [pad(2, 'aenv.dec')]: 0.16,
    [pad(3, 'oscA.level')]: 0, [pad(3, 'oscB.table')]: 19, [pad(3, 'oscB.tune')]: -3,
    [pad(3, 'oscB.level')]: 0.8, [pad(3, 'noise.level')]: 0.2, [pad(3, 'aenv.dec')]: 0.4,
    [pad(4, 'oscA.level')]: 0, [pad(4, 'oscB.table')]: 20, [pad(4, 'oscB.tune')]: -2,
    [pad(4, 'oscB.level')]: 0.75, [pad(4, 'aenv.dec')]: 0.08,
    [pad(11, 'oscA.level')]: 0, [pad(11, 'oscB.table')]: 4, [pad(11, 'oscB.level')]: 0.7,
    [pad(11, 'aenv.dec')]: 2.2, [pad(11, 'flt.on')]: 1, [pad(11, 'flt.type')]: 3,
    [pad(11, 'flt.cut')]: 5000,
    [pad(12, 'oscA.table')]: 2, [pad(12, 'oscA.tune')]: 7,
    [pad(12, 'ring.freq')]: 3907, [pad(12, 'ring.mix')]: 0.5, [pad(12, 'aenv.dec')]: 0.12,
    [pad(13, 'oscA.level')]: 0, [pad(13, 'oscB.table')]: 28, [pad(13, 'oscB.tune')]: 4,
    [pad(13, 'oscB.level')]: 0.7, [pad(13, 'aenv.dec')]: 0.15, [pad(13, 'pan')]: 0.3,
    // Stab: VOX through a resonant band-pass, pitch jittered per hit.
    [pad(14, 'oscA.table')]: 7, [pad(14, 'oscA.tune')]: -12, [pad(14, 'aenv.dec')]: 0.18,
    [pad(14, 'flt.on')]: 1, [pad(14, 'flt.type')]: 2, [pad(14, 'flt.cut')]: 1200,
    [pad(14, 'flt.res')]: 0.5, [pad(14, 'mod1.src')]: 3, [pad(14, 'mod1.dst')]: 5,
    [pad(14, 'mod1.amt')]: 0.3,
    [pad(15, 'oscA.table')]: 9, [pad(15, 'aenv.dec')]: 0.1,
    [pad(15, 'mod1.src')]: 3, [pad(15, 'mod1.dst')]: 1, [pad(15, 'mod1.amt')]: 0.6,
  });
  // Hats/ride: UZU metals, high-passed thin.
  const metals: Array<[number, number, number, number]> = [
    [5, 21, 0.05, 8000], [6, 22, 0.3, 6000], [7, 23, 0.9, 4500],
  ];
  for (const [padI, sample, dec, cut] of metals) {
    params[pad(padI, 'oscA.level')] = 0;
    params[pad(padI, 'oscB.table')] = sample;
    params[pad(padI, 'oscB.level')] = 0.75;
    params[pad(padI, 'aenv.dec')] = dec;
    params[pad(padI, 'flt.on')] = 1;
    params[pad(padI, 'flt.type')] = 3;
    params[pad(padI, 'flt.cut')] = cut;
    if (padI < 7) params[pad(padI, 'choke')] = 1;
  }
  // Acid blips: 303-ish squelch — resonant LP24 swept hard by the mod env.
  [-17, -12, -5].forEach((tune, i) => {
    params[pad(8 + i, 'oscA.table')] = 3;
    params[pad(8 + i, 'oscA.pos')] = 0.3;
    params[pad(8 + i, 'oscA.tune')] = tune;
    params[pad(8 + i, 'aenv.dec')] = 0.14;
    params[pad(8 + i, 'flt.on')] = 1;
    params[pad(8 + i, 'flt.type')] = 1;
    params[pad(8 + i, 'flt.cut')] = 700;
    params[pad(8 + i, 'flt.res')] = 0.72;
    params[pad(8 + i, 'mod1.src')] = 1;
    params[pad(8 + i, 'mod1.dst')] = 4;
    params[pad(8 + i, 'mod1.amt')] = 0.9;
    params[pad(8 + i, 'modenv.dec')] = 0.12;
  });
  return params;
}

const ACID_CAVE_PATTERNS = buildPatterns(
  {
    0: { on: [0, 4, 8, 12], acc: [0, 4, 8, 12] },
    1: { on: [0] },
    3: { on: [4, 12] },
    5: { on: [0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15] },
    6: { on: [2, 6, 10, 14], acc: [2, 10] },
    8: { on: [3, 11] },
    9: { on: [6, 14] },
    10: { on: [7] },
    15: { on: [15] },
  },
  {
    0: { on: [0, 4, 8, 12], acc: [0, 4, 8, 12] },
    1: { on: [0, 8] },
    2: { on: [12] },
    3: { on: [4] },
    6: { on: [2, 6, 10, 14], acc: [2, 10] },
    7: { on: [0, 2, 4, 6, 8, 10, 12, 14] },
    8: { on: [3, 7, 11] },
    9: { on: [6, 10, 14] },
    10: { on: [7, 15], acc: [15] },
    13: { on: [5, 13] },
    14: { on: [2, 10] },
  },
);

// BOOM BAP — 90 BPM hip-hop. Sample-forward with wavetable glue under the
// kick, every sample capped by a lazy low-pass for dust, RAND modulation for
// MPC-style humanization, and a reversed UZU MOD transition pad.
const BOOM_BAP_PADS = [
  'KICK', 'KICK 808', 'SNARE', 'CLAP', 'RIM', 'CH HAT', 'OH HAT', 'SHAKER',
  'TOM LO', 'TOM MD', 'TOM HI', 'CRASH', 'COWBELL', 'MARACAS', 'VOX', 'REVERSE',
];

function boomBapParams(): Partial<ParamValues> {
  const params: Partial<ParamValues> = {
    'seq.bpm': 90,
    'master.swing': 0.56,
    'fx.comp.on': 1, 'fx.comp.thr': -14,
    'fx.drive.on': 1, 'fx.drive.amt': 0.35, 'fx.drive.mix': 0.2,
    'fx.reverb.on': 1, 'fx.reverb.size': 0.35, 'fx.reverb.mix': 0.1,
  };
  // Sampled backbone: [pad, sample, tune, level, decay].
  const samples: Array<[number, number, number, number, number]> = [
    [0, 16, -2, 0.85, 0.4], [1, 5, -5, 0.85, 0.55], [2, 18, -4, 0.8, 0.3],
    [3, 19, -6, 0.8, 0.5], [4, 6, 0, 0.75, 0.12], [5, 2, -7, 0.75, 0.07],
    [6, 3, -5, 0.75, 0.35], [7, 29, 0, 0.7, 0.14], [8, 13, -3, 0.75, 0.4],
    [9, 14, -3, 0.75, 0.38], [10, 15, -3, 0.75, 0.36], [11, 27, -4, 0.7, 1.8],
    [12, 7, -7, 0.6, 0.2], [13, 8, 0, 0.7, 0.1], [15, 31, 0, 0.75, 0.8],
  ];
  for (const [padI, sample, tune, level, dec] of samples) {
    params[pad(padI, 'oscA.level')] = 0;
    params[pad(padI, 'oscB.table')] = sample;
    params[pad(padI, 'oscB.tune')] = tune;
    params[pad(padI, 'oscB.level')] = level;
    params[pad(padI, 'aenv.dec')] = dec;
    // The dust: cap everything with a dull low-pass like a worn 12-bit sampler.
    params[pad(padI, 'flt.on')] = 1;
    params[pad(padI, 'flt.type')] = 1;
    params[pad(padI, 'flt.cut')] = 8500;
  }
  Object.assign(params, {
    // THUD glue under the sampled kick so the low end hits like a sub.
    [pad(0, 'oscA.tune')]: -24, [pad(0, 'oscA.level')]: 0.5,
    [pad(0, 'penv.amt')]: 18, [pad(0, 'penv.dec')]: 0.04,
    [pad(0, 'lvl')]: 0.9, [pad(0, 'fx.reverb.on')]: 0,
    [pad(1, 'fx.reverb.on')]: 0,
    [pad(2, 'noise.level')]: 0.25, [pad(2, 'noise.color')]: 0.35,
    [pad(2, 'mod1.src')]: 3, [pad(2, 'mod1.dst')]: 2, [pad(2, 'mod1.amt')]: 0.15,
    [pad(5, 'mod1.src')]: 3, [pad(5, 'mod1.dst')]: 7, [pad(5, 'mod1.amt')]: 0.2,
    [pad(5, 'choke')]: 1, [pad(6, 'choke')]: 1,
    [pad(7, 'pan')]: 0.3, [pad(13, 'pan')]: -0.3,
    // Dusty vocal chop on the wavetable side.
    [pad(14, 'oscA.table')]: 7, [pad(14, 'oscA.tune')]: -10, [pad(14, 'aenv.dec')]: 0.5,
    [pad(14, 'flt.on')]: 1, [pad(14, 'flt.type')]: 0, [pad(14, 'flt.cut')]: 3500,
    // Reversed sample sweep into the downbeat.
    [pad(15, 'oscB.phase')]: 1,
  });
  return params;
}

const BOOM_BAP_PATTERNS = buildPatterns(
  {
    0: { on: [0, 7, 10], acc: [0] },
    2: { on: [4, 12], acc: [4, 12] },
    5: { on: [0, 2, 4, 6, 8, 10, 12, 14], acc: [0, 8] },
    7: { on: [3, 11] },
    4: { on: [14] },
  },
  {
    0: { on: [0, 5, 10, 11], acc: [0] },
    1: { on: [8] },
    2: { on: [4, 12, 14], acc: [4, 12] },
    5: { on: [0, 2, 4, 6, 8, 10, 14], acc: [0, 8] },
    6: { on: [12] },
    7: { on: [3, 11] },
    12: { on: [7] },
    14: { on: [6] },
    15: { on: [12] },
  },
);

// PIRATE RADIO — 133 BPM UK garage 2-step. Bright UZU kit swung hard, a THUD
// sub for basslines, a mod-env wobble stab, random-pitch vox chops, and a
// detuned BLOOM organ stab.
const PIRATE_RADIO_PADS = [
  'KICK', 'SUB BASS', 'SNARE', 'CLAP', 'RIM', 'CH HAT', 'OH HAT', 'SHAKER',
  'PERC L', 'PERC M', 'PERC H', 'CRASH', 'WOBBLE', 'TAMB', 'VOX CHOP', 'ORGAN',
];

function pirateRadioParams(): Partial<ParamValues> {
  const params: Partial<ParamValues> = {
    'seq.bpm': 133,
    'master.swing': 0.58,
    'fx.comp.on': 1,
    'fx.chorus.on': 1, 'fx.chorus.rate': 0.5, 'fx.chorus.depth': 0.3, 'fx.chorus.mix': 0.12,
    'fx.delay.on': 1, 'fx.delay.time': 0.34, 'fx.delay.fb': 0.45, 'fx.delay.mix': 0.16,
    'fx.reverb.on': 1, 'fx.reverb.size': 0.5, 'fx.reverb.mix': 0.18,
  };
  Object.assign(params, {
    // Kick: tight UZU BD2 with a THUD knock on top.
    [pad(0, 'oscA.tune')]: -20, [pad(0, 'oscA.level')]: 0.4,
    [pad(0, 'penv.amt')]: 22, [pad(0, 'penv.dec')]: 0.025,
    [pad(0, 'oscB.table')]: 17, [pad(0, 'oscB.level')]: 0.85,
    [pad(0, 'aenv.dec')]: 0.28, [pad(0, 'lvl')]: 0.9, [pad(0, 'fx.reverb.on')]: 0,
    // Sub: long THUD for one-finger basslines between the drums.
    [pad(1, 'oscA.tune')]: -31, [pad(1, 'penv.amt')]: 6, [pad(1, 'penv.dec')]: 0.05,
    [pad(1, 'aenv.dec')]: 1.2, [pad(1, 'lvl')]: 0.9, [pad(1, 'fx.reverb.on')]: 0,
    [pad(2, 'oscA.level')]: 0, [pad(2, 'oscB.table')]: 18, [pad(2, 'oscB.tune')]: 3,
    [pad(2, 'oscB.level')]: 0.8, [pad(2, 'noise.level')]: 0.2, [pad(2, 'noise.color')]: 0.6,
    [pad(2, 'aenv.dec')]: 0.22,
    [pad(3, 'oscA.level')]: 0, [pad(3, 'oscB.table')]: 19, [pad(3, 'oscB.tune')]: 2,
    [pad(3, 'oscB.level')]: 0.8, [pad(3, 'aenv.dec')]: 0.45, [pad(3, 'fx.reverb.mix')]: 0.3,
    // Rim: pitch jitters per hit so the skippy 2-step rims never repeat.
    [pad(4, 'oscA.level')]: 0, [pad(4, 'oscB.table')]: 20, [pad(4, 'oscB.tune')]: 6,
    [pad(4, 'oscB.level')]: 0.8, [pad(4, 'aenv.dec')]: 0.09, [pad(4, 'pan')]: -0.2,
    [pad(4, 'mod1.src')]: 3, [pad(4, 'mod1.dst')]: 5, [pad(4, 'mod1.amt')]: 0.15,
    [pad(7, 'oscA.level')]: 0, [pad(7, 'oscB.table')]: 29, [pad(7, 'oscB.tune')]: 4,
    [pad(7, 'oscB.level')]: 0.75, [pad(7, 'aenv.dec')]: 0.12,
    [pad(11, 'oscA.level')]: 0, [pad(11, 'oscB.table')]: 27, [pad(11, 'oscB.level')]: 0.75,
    [pad(11, 'aenv.dec')]: 1.6,
    // Wobble: GRIT sub stab, resonant LP24 pumped by the mod env.
    [pad(12, 'oscA.table')]: 3, [pad(12, 'oscA.tune')]: -24,
    [pad(12, 'aenv.dec')]: 0.5, [pad(12, 'flt.on')]: 1, [pad(12, 'flt.type')]: 1,
    [pad(12, 'flt.cut')]: 500, [pad(12, 'flt.res')]: 0.6,
    [pad(12, 'mod1.src')]: 1, [pad(12, 'mod1.dst')]: 4, [pad(12, 'mod1.amt')]: 0.7,
    [pad(12, 'modenv.dec')]: 0.3,
    [pad(13, 'oscA.level')]: 0, [pad(13, 'oscB.table')]: 30, [pad(13, 'oscB.tune')]: 3,
    [pad(13, 'oscB.level')]: 0.7, [pad(13, 'aenv.dec')]: 0.15, [pad(13, 'pan')]: 0.3,
    // Vox chop: pitch dives in and lands somewhere new every hit.
    [pad(14, 'oscA.table')]: 7, [pad(14, 'oscA.tune')]: 7,
    [pad(14, 'penv.amt')]: -12, [pad(14, 'penv.dec')]: 0.06, [pad(14, 'aenv.dec')]: 0.22,
    [pad(14, 'fx.chorus.mix')]: 0.25,
    [pad(14, 'mod1.src')]: 3, [pad(14, 'mod1.dst')]: 5, [pad(14, 'mod1.amt')]: 0.4,
    // Organ stab: detuned BLOOM unison, low-passed warm.
    [pad(15, 'oscA.table')]: 5, [pad(15, 'oscA.unison')]: 3, [pad(15, 'oscA.detune')]: 0.3,
    [pad(15, 'aenv.dec')]: 0.3, [pad(15, 'flt.on')]: 1, [pad(15, 'flt.type')]: 0,
    [pad(15, 'flt.cut')]: 4000,
  });
  // Shuffled UZU hats + pitched-up perc toms.
  const hats: Array<[number, number, number, number]> = [[5, 21, 0.05, 8500], [6, 22, 0.35, 6000]];
  for (const [padI, sample, dec, cut] of hats) {
    params[pad(padI, 'oscA.level')] = 0;
    params[pad(padI, 'oscB.table')] = sample;
    params[pad(padI, 'oscB.tune')] = 2;
    params[pad(padI, 'oscB.level')] = 0.75;
    params[pad(padI, 'aenv.dec')] = dec;
    params[pad(padI, 'flt.on')] = 1;
    params[pad(padI, 'flt.type')] = 3;
    params[pad(padI, 'flt.cut')] = cut;
    params[pad(padI, 'choke')] = 1;
    params[pad(padI, 'v2l')] = 0.9;
  }
  [24, 25, 26].forEach((sample, i) => {
    params[pad(8 + i, 'oscA.level')] = 0;
    params[pad(8 + i, 'oscB.table')] = sample;
    params[pad(8 + i, 'oscB.tune')] = 2;
    params[pad(8 + i, 'oscB.level')] = 0.75;
    params[pad(8 + i, 'aenv.dec')] = 0.3;
    params[pad(8 + i, 'pan')] = (i - 1) * 0.35;
  });
  return params;
}

const PIRATE_RADIO_PATTERNS = buildPatterns(
  {
    0: { on: [0, 10], acc: [0] },
    1: { on: [0, 7] },
    2: { on: [4, 12], acc: [12] },
    4: { on: [7, 15] },
    5: { on: [2, 3, 6, 7, 11, 14], acc: [2, 6] },
    6: { on: [10] },
    14: { on: [13] },
    15: { on: [8] },
  },
  {
    0: { on: [0, 7, 10], acc: [0] },
    1: { on: [0, 7, 11] },
    2: { on: [4, 12], acc: [12] },
    3: { on: [12] },
    4: { on: [7, 13, 15] },
    5: { on: [2, 3, 6, 7, 11, 14], acc: [2, 6] },
    6: { on: [10] },
    8: { on: [5] },
    10: { on: [14] },
    12: { on: [6, 14] },
    14: { on: [3, 13] },
    15: { on: [8, 11] },
  },
);

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
  { name: 'NEON GRID', params: neonGridParams(), padNames: [...NEON_GRID_PADS], patterns: NEON_GRID_PATTERNS, chain: [...AB_CHAIN] },
  { name: 'ACID CAVE', params: acidCaveParams(), padNames: [...ACID_CAVE_PADS], patterns: ACID_CAVE_PATTERNS, chain: [...AB_CHAIN] },
  { name: 'BOOM BAP', params: boomBapParams(), padNames: [...BOOM_BAP_PADS], patterns: BOOM_BAP_PATTERNS, chain: [...AB_CHAIN] },
  { name: 'PIRATE RADIO', params: pirateRadioParams(), padNames: [...PIRATE_RADIO_PADS], patterns: PIRATE_RADIO_PATTERNS, chain: [...AB_CHAIN] },
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
