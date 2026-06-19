// Factory presets. Each entry overrides defaults from params.ts.
// Table indices: 0 PRIME, 1 BLOOM, 2 PULSE, 3 VOX, 4 CHIME, 5 GLITCH
// Mod sources: 0 —, 1 LFO1, 2 LFO2, 3 MODENV, 4 VELO, 5 NOTE
// Mod dests:   0 —, 1 A POS, 2 B POS, 3 CUTOFF, 4 PITCH, 5 AMP, 6 PAN, 7 A LVL, 8 B LVL

import type { ModConnection, ParamValues } from './params';
import type { SerializedUserTable } from './engine/usertables';

export interface Preset {
  name: string;
  params: Partial<ParamValues>;
  // Modulation routes. Factory presets omit this and encode routes in `mat*`
  // params (migrated on load); user presets saved by the current build store
  // them here explicitly. See store.ts `resolvePresetMods`.
  mods?: ModConnection[];
  // Optional embedded user wavetables. When present they define the table pool
  // the preset's `*.table` indices (>= TABLE_NAMES.length) address. Factory
  // presets omit this and only use the 6 procedural tables. See
  // engine/usertables.ts for the serialization format.
  tables?: SerializedUserTable[];
}

export const FACTORY_PRESETS: Preset[] = [
  { name: 'INIT', params: {} },

  {
    name: 'VELVET PAD',
    params: {
      'oscA.table': 1, 'oscA.pos': 0.34, 'oscA.unison': 5, 'oscA.detune': 0.28, 'oscA.spread': 0.85, 'oscA.level': 0.7,
      'oscB.on': 1, 'oscB.table': 3, 'oscB.pos': 0.22, 'oscB.level': 0.42, 'oscB.unison': 3, 'oscB.detune': 0.2, 'oscB.fine': 6,
      'filter.type': 1, 'filter.cutoff': 1400, 'filter.res': 0.12, 'filter.env': 0.35, 'filter.key': 0.3,
      'env1.a': 0.9, 'env1.d': 1.2, 'env1.s': 0.82, 'env1.r': 1.8,
      'env2.a': 1.6, 'env2.d': 2.5, 'env2.s': 0.55, 'env2.r': 2.2,
      'lfo1.rate': 0.13,
      'mat1.src': 1, 'mat1.dst': 1, 'mat1.amt': 0.3,
      'mat2.src': 2, 'mat2.dst': 6, 'mat2.amt': 0.25, 'lfo2.rate': 0.21,
      'fx.chorus.on': 1, 'fx.chorus.mix': 0.55,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.72, 'fx.reverb.mix': 0.42,
    },
  },

  {
    name: 'ACID LINE',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.level': 0.85,
      'sub.on': 1, 'sub.level': 0.4, 'sub.oct': -1,
      'filter.type': 1, 'filter.cutoff': 240, 'filter.res': 0.78, 'filter.env': 0.85, 'filter.drive': 0.35,
      'env1.a': 0.002, 'env1.d': 0.3, 'env1.s': 0.45, 'env1.r': 0.08,
      'env2.a': 0.001, 'env2.d': 0.19, 'env2.s': 0, 'env2.r': 0.12,
      'master.glide': 0.06,
      'fx.drive.on': 1, 'fx.drive.amt': 0.45, 'fx.drive.mix': 0.8,
      'fx.delay.on': 1, 'fx.delay.time': 0.18, 'fx.delay.fb': 0.3, 'fx.delay.mix': 0.22,
    },
  },

  {
    name: 'CRYSTAL PLUCK',
    params: {
      'oscA.table': 4, 'oscA.pos': 0.72, 'oscA.level': 0.8, 'oscA.unison': 2, 'oscA.detune': 0.1, 'oscA.spread': 0.5,
      'filter.type': 0, 'filter.cutoff': 3200, 'filter.res': 0.2, 'filter.env': 0.6, 'filter.key': 0.6,
      'env1.a': 0.001, 'env1.d': 0.5, 'env1.s': 0, 'env1.r': 0.6,
      'env2.a': 0.001, 'env2.d': 0.32, 'env2.s': 0, 'env2.r': 0.3,
      'mat1.src': 4, 'mat1.dst': 3, 'mat1.amt': 0.35,
      'mat2.src': 3, 'mat2.dst': 1, 'mat2.amt': -0.4,
      'fx.delay.on': 1, 'fx.delay.time': 0.42, 'fx.delay.fb': 0.42, 'fx.delay.mix': 0.3,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.55, 'fx.reverb.mix': 0.35,
    },
  },

  {
    name: 'HYPER SAW',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.unison': 7, 'oscA.detune': 0.42, 'oscA.spread': 1, 'oscA.level': 0.8,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0.66, 'oscB.oct': 1, 'oscB.unison': 5, 'oscB.detune': 0.35, 'oscB.spread': 0.9, 'oscB.level': 0.45,
      'sub.on': 1, 'sub.level': 0.45,
      'filter.type': 0, 'filter.cutoff': 12000, 'filter.res': 0.05,
      'env1.a': 0.01, 'env1.d': 0.4, 'env1.s': 0.9, 'env1.r': 0.5,
      'fx.chorus.on': 1, 'fx.chorus.rate': 0.4, 'fx.chorus.mix': 0.4,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.4, 'fx.reverb.mix': 0.22,
    },
  },

  {
    name: 'VOWEL TALK',
    params: {
      'oscA.table': 3, 'oscA.pos': 0.1, 'oscA.level': 0.85, 'oscA.unison': 3, 'oscA.detune': 0.12, 'oscA.spread': 0.6,
      'sub.on': 1, 'sub.level': 0.5, 'sub.oct': -1,
      'filter.type': 0, 'filter.cutoff': 4500, 'filter.res': 0.25,
      'env1.a': 0.01, 'env1.d': 0.3, 'env1.s': 0.85, 'env1.r': 0.25,
      'lfo1.shape': 1, 'lfo1.rate': 0.6,
      'mat1.src': 1, 'mat1.dst': 1, 'mat1.amt': 0.55,
      'mat2.src': 5, 'mat2.dst': 1, 'mat2.amt': 0.3,
      'fx.drive.on': 1, 'fx.drive.amt': 0.22, 'fx.drive.mix': 0.6,
    },
  },

  {
    name: 'CATHEDRAL BELL',
    params: {
      'oscA.table': 4, 'oscA.pos': 1, 'oscA.level': 0.75,
      'oscB.on': 1, 'oscB.table': 4, 'oscB.pos': 0.5, 'oscB.oct': 1, 'oscB.fine': 9, 'oscB.level': 0.3,
      'filter.type': 0, 'filter.cutoff': 9000, 'filter.res': 0.05, 'filter.key': 0.5,
      'env1.a': 0.001, 'env1.d': 2.8, 'env1.s': 0.12, 'env1.r': 3.5,
      'env2.a': 0.001, 'env2.d': 1.8, 'env2.s': 0, 'env2.r': 1.5,
      'mat1.src': 3, 'mat1.dst': 1, 'mat1.amt': -0.5,
      'lfo2.rate': 4.6, 'mat2.src': 2, 'mat2.dst': 4, 'mat2.amt': 0.015,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.9, 'fx.reverb.mix': 0.5,
      'fx.delay.on': 1, 'fx.delay.time': 0.6, 'fx.delay.fb': 0.35, 'fx.delay.mix': 0.18,
    },
  },

  {
    name: 'NEURO WOBBLE',
    params: {
      'oscA.table': 5, 'oscA.pos': 0.3, 'oscA.level': 0.8,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 1, 'oscB.oct': -1, 'oscB.level': 0.55,
      'sub.on': 1, 'sub.level': 0.55, 'sub.oct': -1,
      'filter.type': 1, 'filter.cutoff': 700, 'filter.res': 0.45, 'filter.drive': 0.4,
      'env1.a': 0.003, 'env1.d': 0.3, 'env1.s': 0.9, 'env1.r': 0.15,
      'lfo1.shape': 0, 'lfo1.rate': 2.2,
      'mat1.src': 1, 'mat1.dst': 3, 'mat1.amt': 0.55,
      'mat2.src': 1, 'mat2.dst': 1, 'mat2.amt': 0.5,
      'mat3.src': 1, 'mat3.dst': 2, 'mat3.amt': -0.35,
      'fx.drive.on': 1, 'fx.drive.amt': 0.5, 'fx.drive.mix': 0.7,
    },
  },

  // ---- Iconic Serum-style sounds ----------------------------------------

  {
    // Detuned twin-saw Reese: the staple drum'n'bass / neuro foundation.
    name: 'REESE BASS',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.unison': 4, 'oscA.detune': 0.5, 'oscA.spread': 0.35, 'oscA.level': 0.85,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0.66, 'oscB.semi': -12, 'oscB.unison': 2, 'oscB.detune': 0.28, 'oscB.level': 0.5,
      'sub.on': 1, 'sub.level': 0.5, 'sub.oct': -1,
      'filter.type': 1, 'filter.cutoff': 520, 'filter.res': 0.26, 'filter.drive': 0.3, 'filter.key': 0.2,
      'env1.a': 0.005, 'env1.d': 0.4, 'env1.s': 0.85, 'env1.r': 0.18,
      'lfo1.shape': 0, 'lfo1.rate': 0.16,
      'mat1.src': 1, 'mat1.dst': 3, 'mat1.amt': 0.18,
      'mat2.src': 1, 'mat2.dst': 1, 'mat2.amt': 0.12,
      'fx.drive.on': 1, 'fx.drive.amt': 0.35, 'fx.drive.mix': 0.7,
    },
  },

  {
    // The legendary stacked-fifths saw lead — second osc a fifth up.
    name: 'POWER FIFTHS',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.unison': 3, 'oscA.detune': 0.22, 'oscA.spread': 0.7, 'oscA.level': 0.7,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0.66, 'oscB.semi': 7, 'oscB.unison': 3, 'oscB.detune': 0.22, 'oscB.spread': 0.7, 'oscB.level': 0.55,
      'sub.on': 1, 'sub.level': 0.35,
      'filter.type': 0, 'filter.cutoff': 6500, 'filter.res': 0.12, 'filter.env': 0.3, 'filter.key': 0.3,
      'env1.a': 0.005, 'env1.d': 0.6, 'env1.s': 0.8, 'env1.r': 0.35,
      'env2.a': 0.005, 'env2.d': 0.3, 'env2.s': 0, 'env2.r': 0.2,
      'fx.chorus.on': 1, 'fx.chorus.mix': 0.35,
      'fx.delay.on': 1, 'fx.delay.time': 0.33, 'fx.delay.fb': 0.32, 'fx.delay.mix': 0.22,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.5, 'fx.reverb.mix': 0.26,
    },
  },

  {
    // Talking dubstep growl: LFO sweeps the vowel table + cutoff into drive.
    name: 'GROWL BASS',
    params: {
      'oscA.table': 3, 'oscA.pos': 0.15, 'oscA.unison': 2, 'oscA.detune': 0.14, 'oscA.spread': 0.5, 'oscA.level': 0.85,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0.66, 'oscB.oct': -1, 'oscB.level': 0.5,
      'sub.on': 1, 'sub.level': 0.55, 'sub.oct': -1,
      'filter.type': 1, 'filter.cutoff': 650, 'filter.res': 0.45, 'filter.drive': 0.5,
      'env1.a': 0.004, 'env1.d': 0.3, 'env1.s': 0.9, 'env1.r': 0.12,
      'lfo1.shape': 1, 'lfo1.rate': 5.5,
      'mat1.src': 1, 'mat1.dst': 1, 'mat1.amt': 0.7,
      'mat2.src': 1, 'mat2.dst': 3, 'mat2.amt': 0.4,
      'fx.drive.on': 1, 'fx.drive.amt': 0.55, 'fx.drive.mix': 0.85,
    },
  },

  {
    // Wide future-bass chord stab — 7-voice supersaw under a bloom layer.
    name: 'FUTURE CHORD',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.unison': 7, 'oscA.detune': 0.32, 'oscA.spread': 1, 'oscA.level': 0.72,
      'oscB.on': 1, 'oscB.table': 1, 'oscB.pos': 0.4, 'oscB.unison': 3, 'oscB.detune': 0.24, 'oscB.spread': 0.8, 'oscB.level': 0.4,
      'filter.type': 0, 'filter.cutoff': 8000, 'filter.res': 0.08, 'filter.env': 0.25, 'filter.key': 0.2,
      'env1.a': 0.02, 'env1.d': 0.5, 'env1.s': 0.85, 'env1.r': 0.6,
      'lfo1.shape': 0, 'lfo1.rate': 0.5,
      'mat1.src': 1, 'mat1.dst': 1, 'mat1.amt': 0.3,
      'mat2.src': 1, 'mat2.dst': 3, 'mat2.amt': 0.2,
      'fx.chorus.on': 1, 'fx.chorus.rate': 0.5, 'fx.chorus.mix': 0.5,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.68, 'fx.reverb.mix': 0.42,
    },
  },

  {
    // Aggressive riddim screech — square-wave hook clamped by a fast LFO + drive.
    name: 'SCREECH LEAD',
    params: {
      'oscA.table': 2, 'oscA.pos': 0.7, 'oscA.unison': 3, 'oscA.detune': 0.3, 'oscA.spread': 0.7, 'oscA.level': 0.78,
      'oscB.on': 1, 'oscB.table': 5, 'oscB.pos': 0.4, 'oscB.level': 0.4,
      'filter.type': 2, 'filter.cutoff': 1800, 'filter.res': 0.6, 'filter.drive': 0.4,
      'env1.a': 0.004, 'env1.d': 0.4, 'env1.s': 0.85, 'env1.r': 0.15,
      'lfo1.shape': 3, 'lfo1.rate': 8,
      'mat1.src': 1, 'mat1.dst': 3, 'mat1.amt': 0.5,
      'mat2.src': 1, 'mat2.dst': 2, 'mat2.amt': 0.4,
      'mat3.src': 4, 'mat3.dst': 3, 'mat3.amt': 0.3,
      'fx.drive.on': 1, 'fx.drive.amt': 0.6, 'fx.drive.mix': 0.8,
      'fx.delay.on': 1, 'fx.delay.time': 0.25, 'fx.delay.fb': 0.3, 'fx.delay.mix': 0.18,
    },
  },

  {
    // Hardstyle "donk" stab: hollow pulse with a mod-env pitch blip on attack.
    name: 'DONK STAB',
    params: {
      'oscA.table': 2, 'oscA.pos': 0.25, 'oscA.level': 0.85,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 1, 'oscB.oct': 1, 'oscB.level': 0.28,
      'sub.on': 1, 'sub.level': 0.4,
      'filter.type': 0, 'filter.cutoff': 3500, 'filter.res': 0.3, 'filter.env': 0.5, 'filter.key': 0.3,
      'env1.a': 0.002, 'env1.d': 0.16, 'env1.s': 0, 'env1.r': 0.1,
      'env2.a': 0.002, 'env2.d': 0.05, 'env2.s': 0, 'env2.r': 0.05,
      'mat1.src': 3, 'mat1.dst': 4, 'mat1.amt': 0.25,
      'fx.drive.on': 1, 'fx.drive.amt': 0.3, 'fx.drive.mix': 0.6,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.4, 'fx.reverb.mix': 0.2,
    },
  },

  {
    // Progressive-house pluck: snappy saw, key-tracked filter, ping-pong delay.
    name: 'HOUSE PLUCK',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.unison': 3, 'oscA.detune': 0.18, 'oscA.spread': 0.6, 'oscA.level': 0.78,
      'oscB.on': 1, 'oscB.table': 2, 'oscB.pos': 0.3, 'oscB.semi': 12, 'oscB.level': 0.3,
      'filter.type': 1, 'filter.cutoff': 2200, 'filter.res': 0.2, 'filter.env': 0.55, 'filter.key': 0.5,
      'env1.a': 0.003, 'env1.d': 0.28, 'env1.s': 0, 'env1.r': 0.2,
      'env2.a': 0.003, 'env2.d': 0.18, 'env2.s': 0, 'env2.r': 0.15,
      'fx.chorus.on': 1, 'fx.chorus.mix': 0.3,
      'fx.delay.on': 1, 'fx.delay.time': 0.38, 'fx.delay.fb': 0.4, 'fx.delay.mix': 0.3,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.5, 'fx.reverb.mix': 0.25,
    },
  },

  {
    // Bright trap bell melody: detuned chimes an octave apart, long tail, echo.
    name: 'TRAP BELL',
    params: {
      'oscA.table': 4, 'oscA.pos': 0.85, 'oscA.unison': 2, 'oscA.detune': 0.12, 'oscA.spread': 0.5, 'oscA.level': 0.8,
      'oscB.on': 1, 'oscB.table': 4, 'oscB.pos': 0.6, 'oscB.semi': 12, 'oscB.fine': 4, 'oscB.level': 0.32,
      'filter.type': 0, 'filter.cutoff': 7000, 'filter.res': 0.1, 'filter.key': 0.5,
      'env1.a': 0.001, 'env1.d': 0.9, 'env1.s': 0.1, 'env1.r': 0.8,
      'env2.a': 0.001, 'env2.d': 0.4, 'env2.s': 0, 'env2.r': 0.4,
      'mat1.src': 3, 'mat1.dst': 1, 'mat1.amt': -0.3,
      'fx.delay.on': 1, 'fx.delay.time': 0.3, 'fx.delay.fb': 0.45, 'fx.delay.mix': 0.32,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.7, 'fx.reverb.mix': 0.4,
    },
  },

  {
    // Chiptune square lead: raw (filter off), PWM sweep + a touch of vibrato.
    name: '8-BIT LEAD',
    params: {
      'oscA.table': 2, 'oscA.pos': 0, 'oscA.level': 0.8,
      'filter.on': 0,
      'env1.a': 0.001, 'env1.d': 0.1, 'env1.s': 0.7, 'env1.r': 0.05,
      'lfo1.shape': 0, 'lfo1.rate': 6,
      'mat1.src': 1, 'mat1.dst': 4, 'mat1.amt': 0.01,
      'mat2.src': 1, 'mat2.dst': 1, 'mat2.amt': 0.2,
      'fx.delay.on': 1, 'fx.delay.time': 0.25, 'fx.delay.fb': 0.25, 'fx.delay.mix': 0.2,
    },
  },

  {
    // Slow evolving atmosphere: dual LFOs crawl across cutoff, position and pan.
    name: 'DARK DRONE',
    params: {
      'oscA.table': 1, 'oscA.pos': 0.2, 'oscA.unison': 4, 'oscA.detune': 0.3, 'oscA.spread': 0.9, 'oscA.level': 0.7,
      'oscB.on': 1, 'oscB.table': 3, 'oscB.pos': 0.5, 'oscB.oct': -1, 'oscB.unison': 2, 'oscB.detune': 0.2, 'oscB.level': 0.45,
      'sub.on': 1, 'sub.level': 0.35, 'sub.oct': -2,
      'filter.type': 1, 'filter.cutoff': 900, 'filter.res': 0.15,
      'env1.a': 2.5, 'env1.d': 3, 'env1.s': 0.7, 'env1.r': 4,
      'lfo1.shape': 0, 'lfo1.rate': 0.08,
      'lfo2.shape': 0, 'lfo2.rate': 0.05,
      'mat1.src': 1, 'mat1.dst': 3, 'mat1.amt': 0.4,
      'mat2.src': 2, 'mat2.dst': 1, 'mat2.amt': 0.5,
      'mat3.src': 2, 'mat3.dst': 6, 'mat3.amt': 0.4,
      'fx.chorus.on': 1, 'fx.chorus.mix': 0.5,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.9, 'fx.reverb.mix': 0.5,
    },
  },

  {
    // Classic dubstep wobble: square LFO throws the lowpass open and shut.
    name: 'WUB BASS',
    params: {
      'oscA.table': 0, 'oscA.pos': 1, 'oscA.unison': 2, 'oscA.detune': 0.1, 'oscA.level': 0.8,
      'oscB.on': 1, 'oscB.table': 2, 'oscB.pos': 0.5, 'oscB.oct': -1, 'oscB.level': 0.5,
      'sub.on': 1, 'sub.level': 0.6, 'sub.oct': -1,
      'filter.type': 1, 'filter.cutoff': 400, 'filter.res': 0.55, 'filter.drive': 0.45,
      'env1.a': 0.004, 'env1.d': 0.3, 'env1.s': 0.95, 'env1.r': 0.12,
      'lfo1.shape': 0, 'lfo1.rate': 4,
      'mat1.src': 1, 'mat1.dst': 3, 'mat1.amt': 0.7,
      'fx.drive.on': 1, 'fx.drive.amt': 0.4, 'fx.drive.mix': 0.75,
    },
  },

  {
    // Portamento mono-style lead: glide between notes, squelchy resonant filter.
    name: 'GLIDE LEAD',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.unison': 2, 'oscA.detune': 0.12, 'oscA.spread': 0.4, 'oscA.level': 0.82,
      'oscB.on': 1, 'oscB.table': 2, 'oscB.pos': 0.4, 'oscB.semi': -12, 'oscB.level': 0.4,
      'filter.type': 1, 'filter.cutoff': 1600, 'filter.res': 0.4, 'filter.env': 0.5, 'filter.key': 0.4,
      'env1.a': 0.005, 'env1.d': 0.5, 'env1.s': 0.6, 'env1.r': 0.25,
      'env2.a': 0.005, 'env2.d': 0.3, 'env2.s': 0.2, 'env2.r': 0.2,
      'master.glide': 0.12,
      'lfo1.shape': 0, 'lfo1.rate': 5,
      'mat1.src': 1, 'mat1.dst': 4, 'mat1.amt': 0.008,
      'fx.delay.on': 1, 'fx.delay.time': 0.3, 'fx.delay.fb': 0.35, 'fx.delay.mix': 0.25,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.45, 'fx.reverb.mix': 0.22,
    },
  },
];

const LS_KEY = 'fablesynth.userPresets';

export function loadUserPresets(): Preset[] {
  try {
    return JSON.parse(localStorage.getItem(LS_KEY) as string) || [];
  } catch {
    return [];
  }
}

export function saveUserPreset(name: string, params: ParamValues, mods: ModConnection[] = [], tables: SerializedUserTable[] = []): Preset[] {
  const list = loadUserPresets().filter((p) => p.name !== name);
  const preset: Preset = { name, params: { ...params } };
  if (mods.length) preset.mods = mods.map((m) => ({ ...m }));
  if (tables.length) preset.tables = tables;
  list.push(preset);
  localStorage.setItem(LS_KEY, JSON.stringify(list));
  return list;
}
