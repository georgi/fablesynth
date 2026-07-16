// Factory presets. Each entry overrides defaults from params.ts.
// Table indices: 0 PRIME, 1 BLOOM, 2 PULSE, 3 VOX, 4 CHIME, 5 GLITCH
// Mod sources: 0 —, 1 LFO1, 2 LFO2, 3 MODENV, 4 VELO, 5 NOTE
// Mod dests:   0 —, 1 A POS, 2 B POS, 3 F1 CUT, 4 PITCH, 5 AMP, 6 PAN, 7 A LVL,
//              8 B LVL, 9 F2 CUT, 10 F2 RES

import { defaultParams, type ModConnection, type ParamValues } from './params';
import { MOD_MATRIX_SIZE } from './store/slotHelpers';
import type { SerializedUserTable } from './engine/usertables';

export interface Preset {
  name: string;
  params: Partial<ParamValues>;
  // Modulation routes from older user presets. Routes now live entirely in the
  // `mat{n}.*` params (params-as-truth); factory presets encode them there and
  // current saves carry them in `params`. This optional array is kept only so
  // pre-existing user presets still load — `resolvePresetMods` expands it into
  // slots in order (truncating beyond 16). See `resolvePresetMods` below.
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
    
      'fx.eq.on': 1, 'fx.eq.low': -1.4, 'fx.eq.mid': -1.3, 'fx.eq.mfreq': 405, 'fx.eq.high': 2.7,
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
      'fx.drive.on': 1, 'fx.drive.amt': 0.45, 'fx.drive.mix': 0.2,
      'fx.delay.on': 1, 'fx.delay.time': 0.18, 'fx.delay.fb': 0.3, 'fx.delay.mix': 0.22,
    
      'fx.eq.on': 1, 'fx.eq.low': -6, 'fx.eq.mid': -6, 'fx.eq.mfreq': 263, 'fx.eq.high': 6,
    },
  },

  {
    name: 'CRYSTAL PLUCK',
    params: {
      'oscA.table': 4, 'oscA.pos': 0.72, 'oscA.oct': 1, 'oscA.level': 0.8, 'oscA.unison': 2, 'oscA.detune': 0.1, 'oscA.spread': 0.5,
      'filter.type': 0, 'filter.cutoff': 3200, 'filter.res': 0.2, 'filter.env': 0.6, 'filter.key': 0.6,
      'env1.a': 0.001, 'env1.d': 0.5, 'env1.s': 0, 'env1.r': 0.6,
      'env2.a': 0.001, 'env2.d': 0.32, 'env2.s': 0, 'env2.r': 0.3,
      'mat1.src': 4, 'mat1.dst': 3, 'mat1.amt': 0.35,
      'mat2.src': 3, 'mat2.dst': 1, 'mat2.amt': -0.4,
      'fx.delay.on': 1, 'fx.delay.time': 0.42, 'fx.delay.fb': 0.42, 'fx.delay.mix': 0.3,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.55, 'fx.reverb.mix': 0.35,
    
      'fx.eq.on': 1, 'fx.eq.low': 6, 'fx.eq.mid': 4.5, 'fx.eq.mfreq': 709, 'fx.eq.high': -6,
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
    
      'fx.eq.on': 1, 'fx.eq.low': 5.6, 'fx.eq.mid': 6, 'fx.eq.mfreq': 598, 'fx.eq.high': -6,
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
      'fx.drive.on': 1, 'fx.drive.amt': 0.22, 'fx.drive.mix': 0.2,
    
      'fx.eq.on': 1, 'fx.eq.low': 5.1, 'fx.eq.mid': 5, 'fx.eq.mfreq': 916, 'fx.eq.high': -6,
    },
  },

  {
    name: 'CATHEDRAL BELL',
    params: {
      'oscA.table': 4, 'oscA.pos': 1, 'oscA.oct': 1, 'oscA.level': 0.75,
      'oscB.on': 1, 'oscB.table': 4, 'oscB.pos': 0.5, 'oscB.oct': 2, 'oscB.fine': 9, 'oscB.level': 0.3,
      'filter.type': 0, 'filter.cutoff': 9000, 'filter.res': 0.05, 'filter.key': 0.5,
      'env1.a': 0.001, 'env1.d': 2.8, 'env1.s': 0.12, 'env1.r': 3.5,
      'env2.a': 0.001, 'env2.d': 1.8, 'env2.s': 0, 'env2.r': 1.5,
      'mat1.src': 3, 'mat1.dst': 1, 'mat1.amt': -0.5,
      'lfo2.rate': 4.6, 'mat2.src': 2, 'mat2.dst': 4, 'mat2.amt': 0.015,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.9, 'fx.reverb.mix': 0.5,
      'fx.delay.on': 1, 'fx.delay.time': 0.6, 'fx.delay.fb': 0.35, 'fx.delay.mix': 0.18,
    
      'fx.eq.on': 1, 'fx.eq.low': 6, 'fx.eq.mid': 5, 'fx.eq.mfreq': 693, 'fx.eq.high': -6,
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
      'fx.drive.on': 1, 'fx.drive.amt': 0.5, 'fx.drive.mix': 0.2,
    
      'fx.eq.on': 1, 'fx.eq.low': -3.2, 'fx.eq.mid': 2.8, 'fx.eq.mfreq': 356, 'fx.eq.high': 0.5,
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
      'fx.drive.on': 1, 'fx.drive.amt': 0.35, 'fx.drive.mix': 0.2,
    
      'fx.eq.on': 1, 'fx.eq.low': -6, 'fx.eq.mid': -6, 'fx.eq.mfreq': 320, 'fx.eq.high': 6,
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
    
      'fx.eq.on': 1, 'fx.eq.low': 5.9, 'fx.eq.mid': 6, 'fx.eq.mfreq': 601, 'fx.eq.high': -6,
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
      'fx.drive.on': 1, 'fx.drive.amt': 0.55, 'fx.drive.mix': 0.2,
    
      'fx.eq.on': 1, 'fx.eq.low': -6, 'fx.eq.mid': -5, 'fx.eq.mfreq': 620, 'fx.eq.high': 6,
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
    
      'fx.eq.on': 1, 'fx.eq.low': 6, 'fx.eq.mid': 6, 'fx.eq.mfreq': 431, 'fx.eq.high': -6,
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
      'fx.drive.on': 1, 'fx.drive.amt': 0.6, 'fx.drive.mix': 0.2,
      'fx.delay.on': 1, 'fx.delay.time': 0.25, 'fx.delay.fb': 0.3, 'fx.delay.mix': 0.18,
    
      'fx.eq.on': 1, 'fx.eq.low': 6, 'fx.eq.mid': 2.3, 'fx.eq.mfreq': 734, 'fx.eq.high': -6,
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
      'fx.drive.on': 1, 'fx.drive.amt': 0.3, 'fx.drive.mix': 0.2,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.4, 'fx.reverb.mix': 0.2,
    
      'fx.eq.on': 1, 'fx.eq.low': 6, 'fx.eq.mid': 6, 'fx.eq.mfreq': 900, 'fx.eq.high': -6,
    },
  },

  {
    // Progressive-house pluck: snappy saw, key-tracked filter, ping-pong delay.
    name: 'HOUSE PLUCK',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.oct': 1, 'oscA.unison': 3, 'oscA.detune': 0.18, 'oscA.spread': 0.6, 'oscA.level': 0.78,
      'oscB.on': 1, 'oscB.table': 2, 'oscB.pos': 0.3, 'oscB.oct': 1, 'oscB.semi': 12, 'oscB.level': 0.3,
      'filter.type': 1, 'filter.cutoff': 2200, 'filter.res': 0.2, 'filter.env': 0.55, 'filter.key': 0.5,
      'env1.a': 0.003, 'env1.d': 0.28, 'env1.s': 0, 'env1.r': 0.2,
      'env2.a': 0.003, 'env2.d': 0.18, 'env2.s': 0, 'env2.r': 0.15,
      'fx.chorus.on': 1, 'fx.chorus.mix': 0.3,
      'fx.delay.on': 1, 'fx.delay.time': 0.38, 'fx.delay.fb': 0.4, 'fx.delay.mix': 0.3,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.5, 'fx.reverb.mix': 0.25,
    
      'fx.eq.on': 1, 'fx.eq.low': 4.6, 'fx.eq.mid': -6, 'fx.eq.mfreq': 504, 'fx.eq.high': 1.4,
    },
  },

  {
    // Bright trap bell melody: detuned chimes an octave apart, long tail, echo.
    name: 'TRAP BELL',
    params: {
      'oscA.table': 4, 'oscA.pos': 0.85, 'oscA.oct': 1, 'oscA.unison': 2, 'oscA.detune': 0.12, 'oscA.spread': 0.5, 'oscA.level': 0.8,
      'oscB.on': 1, 'oscB.table': 4, 'oscB.pos': 0.6, 'oscB.oct': 1, 'oscB.semi': 12, 'oscB.fine': 4, 'oscB.level': 0.32,
      'filter.type': 0, 'filter.cutoff': 7000, 'filter.res': 0.1, 'filter.key': 0.5,
      'env1.a': 0.001, 'env1.d': 0.9, 'env1.s': 0.1, 'env1.r': 0.8,
      'env2.a': 0.001, 'env2.d': 0.4, 'env2.s': 0, 'env2.r': 0.4,
      'mat1.src': 3, 'mat1.dst': 1, 'mat1.amt': -0.3,
      'fx.delay.on': 1, 'fx.delay.time': 0.3, 'fx.delay.fb': 0.45, 'fx.delay.mix': 0.32,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.7, 'fx.reverb.mix': 0.4,
    
      'fx.eq.on': 1, 'fx.eq.low': 6, 'fx.eq.mid': 4, 'fx.eq.mfreq': 680, 'fx.eq.high': -6,
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
    
      'fx.eq.on': 1, 'fx.eq.low': 6, 'fx.eq.mid': 6, 'fx.eq.mfreq': 507, 'fx.eq.high': -6,
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
    
      'fx.eq.on': 1, 'fx.eq.low': -5.9, 'fx.eq.mid': -4.4, 'fx.eq.mfreq': 361, 'fx.eq.high': 6,
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
      'fx.drive.on': 1, 'fx.drive.amt': 0.4, 'fx.drive.mix': 0.2,
    
      'fx.eq.on': 1, 'fx.eq.low': 0.1, 'fx.eq.mid': 5.5, 'fx.eq.mfreq': 493, 'fx.eq.high': -5.6,
    },
  },

  {
    // Portamento mono-style lead: glide between notes, squelchy resonant filter.
    name: 'GLIDE LEAD',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.oct': 1, 'oscA.unison': 2, 'oscA.detune': 0.12, 'oscA.spread': 0.4, 'oscA.level': 0.82,
      'oscB.on': 1, 'oscB.table': 2, 'oscB.pos': 0.4, 'oscB.oct': 1, 'oscB.semi': -12, 'oscB.level': 0.4,
      'filter.type': 1, 'filter.cutoff': 1600, 'filter.res': 0.4, 'filter.env': 0.5, 'filter.key': 0.4,
      'env1.a': 0.005, 'env1.d': 0.5, 'env1.s': 0.6, 'env1.r': 0.25,
      'env2.a': 0.005, 'env2.d': 0.3, 'env2.s': 0.2, 'env2.r': 0.2,
      'master.glide': 0.12,
      'lfo1.shape': 0, 'lfo1.rate': 5,
      'mat1.src': 1, 'mat1.dst': 4, 'mat1.amt': 0.008,
      'fx.delay.on': 1, 'fx.delay.time': 0.3, 'fx.delay.fb': 0.35, 'fx.delay.mix': 0.25,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.45, 'fx.reverb.mix': 0.22,
    
      'fx.eq.on': 1, 'fx.eq.low': 1.9, 'fx.eq.mid': -4.7, 'fx.eq.mfreq': 546, 'fx.eq.high': 2.8,
    },
  },

  // ---- Acoustic & classic keys ------------------------------------------

  {
    // Tine e-piano: near-sine body + a quiet CHIME "tine" layer an octave up.
    // Velocity opens the filter and brings the tine forward; slow pan tremolo.
    name: 'MELLOW RHODES',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.08, 'oscA.level': 0.8,
      'oscB.on': 1, 'oscB.table': 4, 'oscB.pos': 0.12, 'oscB.oct': 1, 'oscB.level': 0.18,
      'filter.type': 0, 'filter.cutoff': 1200, 'filter.res': 0.1, 'filter.env': 0.45, 'filter.key': 0.4,
      'env1.a': 0.002, 'env1.d': 1.4, 'env1.s': 0.42, 'env1.r': 0.5,
      'env2.a': 0.001, 'env2.d': 0.35, 'env2.s': 0, 'env2.r': 0.3,
      'lfo1.rate': 0.9,
      'mat1.src': 4, 'mat1.dst': 3, 'mat1.amt': 0.4,
      'mat2.src': 4, 'mat2.dst': 8, 'mat2.amt': 0.35,
      'mat3.src': 1, 'mat3.dst': 6, 'mat3.amt': 0.3,
      'fx.chorus.on': 1, 'fx.chorus.mix': 0.35,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.35, 'fx.reverb.mix': 0.25,
    
      'fx.eq.on': 1, 'fx.eq.low': -6, 'fx.eq.mid': -6, 'fx.eq.mfreq': 302, 'fx.eq.high': 6,
    },
  },

  {
    // Bright FM-style "Dyno" e-piano: sine body, glassy bell partials two
    // octaves up, deep chorus. Harder velocity = brighter and more bell.
    name: 'DYNO EPIANO',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.05, 'oscA.level': 0.75,
      'oscB.on': 1, 'oscB.table': 4, 'oscB.pos': 0.3, 'oscB.oct': 2, 'oscB.level': 0.15,
      'filter.type': 0, 'filter.cutoff': 2500, 'filter.res': 0.08, 'filter.env': 0.3, 'filter.key': 0.5,
      'env1.a': 0.001, 'env1.d': 1.8, 'env1.s': 0.3, 'env1.r': 0.6,
      'env2.a': 0.001, 'env2.d': 0.5, 'env2.s': 0, 'env2.r': 0.4,
      'mat1.src': 4, 'mat1.dst': 3, 'mat1.amt': 0.45,
      'mat2.src': 4, 'mat2.dst': 8, 'mat2.amt': 0.4,
      'fx.chorus.on': 1, 'fx.chorus.rate': 0.7, 'fx.chorus.mix': 0.5,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.4, 'fx.reverb.mix': 0.24,
    
      'fx.eq.on': 1, 'fx.eq.low': 1.9, 'fx.eq.mid': 2.1, 'fx.eq.mfreq': 443, 'fx.eq.high': -4,
    },
  },

  {
    // Clean drawbar organ: sine 8' + 4' (osc B up an octave) + sine sub as the
    // 16' drawbar. Gated envelope, rotary-style vibrato + pan from LFO 1.
    name: 'DRAWBAR ORGAN',
    params: {
      'oscA.table': 0, 'oscA.pos': 0, 'oscA.level': 0.7,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0, 'oscB.oct': 1, 'oscB.level': 0.5,
      'sub.on': 1, 'sub.level': 0.6, 'sub.oct': -1,
      'filter.on': 0,
      'env1.a': 0.003, 'env1.d': 0.1, 'env1.s': 1, 'env1.r': 0.05,
      'lfo1.rate': 6.5,
      'mat1.src': 1, 'mat1.dst': 4, 'mat1.amt': 0.004,
      'mat2.src': 1, 'mat2.dst': 6, 'mat2.amt': 0.25,
      'fx.chorus.on': 1, 'fx.chorus.rate': 0.8, 'fx.chorus.mix': 0.4,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.35, 'fx.reverb.mix': 0.18,
    
      'fx.eq.on': 1, 'fx.eq.low': -6, 'fx.eq.mid': -6, 'fx.eq.mfreq': 309, 'fx.eq.high': 6,
    },
  },

  {
    // Overdriven rock organ: square stack through drive with a faster
    // Leslie-style wobble panning the whole thing.
    name: 'ROCK ORGAN',
    params: {
      'oscA.table': 2, 'oscA.pos': 0, 'oscA.unison': 2, 'oscA.detune': 0.08, 'oscA.level': 0.75,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0, 'oscB.oct': 1, 'oscB.level': 0.45,
      'sub.on': 1, 'sub.level': 0.5, 'sub.oct': -1,
      'filter.type': 1, 'filter.cutoff': 5000, 'filter.res': 0.1,
      'env1.a': 0.002, 'env1.d': 0.1, 'env1.s': 1, 'env1.r': 0.06,
      'lfo1.rate': 5.5,
      'mat1.src': 1, 'mat1.dst': 4, 'mat1.amt': 0.005,
      'mat2.src': 1, 'mat2.dst': 6, 'mat2.amt': 0.35,
      'fx.drive.on': 1, 'fx.drive.amt': 0.5, 'fx.drive.mix': 0.2,
      'fx.chorus.on': 1, 'fx.chorus.rate': 1.2, 'fx.chorus.mix': 0.45,
    
      'fx.eq.on': 1, 'fx.eq.low': -2.8, 'fx.eq.mid': 3.1, 'fx.eq.mfreq': 410, 'fx.eq.high': -0.3,
    },
  },

  {
    // Classic analog string machine: wide detuned saws, slow bow-in, heavy
    // chorus. LFO 1 drifts the table position for ensemble motion.
    name: 'ANALOG STRINGS',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.62, 'oscA.unison': 7, 'oscA.detune': 0.3, 'oscA.spread': 1, 'oscA.level': 0.65,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0.66, 'oscB.oct': 1, 'oscB.unison': 3, 'oscB.detune': 0.22, 'oscB.spread': 0.8, 'oscB.level': 0.3,
      'filter.type': 0, 'filter.cutoff': 2800, 'filter.res': 0.08, 'filter.key': 0.2,
      'env1.a': 0.5, 'env1.d': 1, 'env1.s': 0.85, 'env1.r': 1.2,
      'lfo1.rate': 0.25, 'lfo2.rate': 0.11,
      'mat1.src': 1, 'mat1.dst': 1, 'mat1.amt': 0.12,
      'mat2.src': 2, 'mat2.dst': 6, 'mat2.amt': 0.2,
      'fx.chorus.on': 1, 'fx.chorus.rate': 0.5, 'fx.chorus.mix': 0.55,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.7, 'fx.reverb.mix': 0.35,
    
      'fx.eq.on': 1, 'fx.eq.low': 3.2, 'fx.eq.mid': 2.7, 'fx.eq.mfreq': 467, 'fx.eq.high': -5.9,
    },
  },

  {
    // Dark cinematic ensemble: BLOOM stack over a low saw, mod env slowly
    // blooms the table open while the section swells in a big hall.
    name: 'CINEMA STRINGS',
    params: {
      'oscA.table': 1, 'oscA.pos': 0.45, 'oscA.unison': 5, 'oscA.detune': 0.25, 'oscA.spread': 0.9, 'oscA.level': 0.68,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0.6, 'oscB.oct': -1, 'oscB.level': 0.4,
      'filter.type': 0, 'filter.cutoff': 1500, 'filter.res': 0.1, 'filter.env': 0.3, 'filter.key': 0.2,
      'env1.a': 1.2, 'env1.d': 2, 'env1.s': 0.75, 'env1.r': 2.5,
      'env2.a': 2.5, 'env2.d': 3, 'env2.s': 0.4, 'env2.r': 2,
      'mat1.src': 3, 'mat1.dst': 1, 'mat1.amt': 0.3,
      'fx.chorus.on': 1, 'fx.chorus.mix': 0.4,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.85, 'fx.reverb.mix': 0.45,
    
      'fx.eq.on': 1, 'fx.eq.low': -0.1, 'fx.eq.mid': 1, 'fx.eq.mfreq': 392, 'fx.eq.high': -0.9,
    },
  },

  {
    // Punchy brass section: tight detuned saws, the filter "blats" open on a
    // slightly-lagged mod env, subtle vibrato, a touch of drive.
    name: 'BRASS SECTION',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.unison': 3, 'oscA.detune': 0.15, 'oscA.spread': 0.5, 'oscA.level': 0.8,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0.66, 'oscB.fine': 8, 'oscB.level': 0.5,
      'sub.on': 1, 'sub.level': 0.3,
      'filter.type': 0, 'filter.cutoff': 900, 'filter.res': 0.15, 'filter.env': 0.7, 'filter.key': 0.3,
      'env1.a': 0.03, 'env1.d': 0.3, 'env1.s': 0.85, 'env1.r': 0.2,
      'env2.a': 0.06, 'env2.d': 0.35, 'env2.s': 0.55, 'env2.r': 0.2,
      'lfo1.rate': 5,
      'mat1.src': 4, 'mat1.dst': 3, 'mat1.amt': 0.3,
      'mat2.src': 1, 'mat2.dst': 4, 'mat2.amt': 0.003,
      'fx.drive.on': 1, 'fx.drive.amt': 0.25, 'fx.drive.mix': 0.2,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.4, 'fx.reverb.mix': 0.22,
    
      'fx.eq.on': 1, 'fx.eq.low': 6, 'fx.eq.mid': 6, 'fx.eq.mfreq': 547, 'fx.eq.high': -6,
    },
  },

  {
    // Soft 80s synth brass: wide saws over a low pulse, slower filter swell,
    // chorused into a warm pad-adjacent brass bed.
    name: 'SOFT BRASS',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.66, 'oscA.unison': 5, 'oscA.detune': 0.28, 'oscA.spread': 0.9, 'oscA.level': 0.7,
      'oscB.on': 1, 'oscB.table': 2, 'oscB.pos': 0.15, 'oscB.oct': -1, 'oscB.level': 0.45,
      'filter.type': 0, 'filter.cutoff': 1200, 'filter.res': 0.1, 'filter.env': 0.5, 'filter.key': 0.25,
      'env1.a': 0.12, 'env1.d': 0.6, 'env1.s': 0.8, 'env1.r': 0.8,
      'env2.a': 0.15, 'env2.d': 0.8, 'env2.s': 0.4, 'env2.r': 0.6,
      'fx.chorus.on': 1, 'fx.chorus.mix': 0.45,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.55, 'fx.reverb.mix': 0.3,
    
      'fx.eq.on': 1, 'fx.eq.low': 1.7, 'fx.eq.mid': 1.8, 'fx.eq.mfreq': 443, 'fx.eq.high': -3.5,
    },
  },

  {
    // Rounded nylon-string pluck: tri-saw body with an octave sine, velocity
    // brightness and a mod-env flick of table position on the attack.
    name: 'NYLON PLUCK',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.3, 'oscA.level': 0.8,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0.05, 'oscB.oct': 1, 'oscB.level': 0.25,
      'filter.type': 0, 'filter.cutoff': 1800, 'filter.res': 0.12, 'filter.env': 0.5, 'filter.key': 0.6,
      'env1.a': 0.001, 'env1.d': 0.7, 'env1.s': 0, 'env1.r': 0.35,
      'env2.a': 0.001, 'env2.d': 0.25, 'env2.s': 0, 'env2.r': 0.2,
      'mat1.src': 4, 'mat1.dst': 3, 'mat1.amt': 0.35,
      'mat2.src': 3, 'mat2.dst': 1, 'mat2.amt': 0.25,
      'fx.delay.on': 1, 'fx.delay.time': 0.32, 'fx.delay.fb': 0.28, 'fx.delay.mix': 0.18,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.45, 'fx.reverb.mix': 0.25,
    
      'fx.eq.on': 1, 'fx.eq.low': -5, 'fx.eq.mid': -2.7, 'fx.eq.mfreq': 276, 'fx.eq.high': 6,
    },
  },

  {
    // Kalimba-style thumb pluck: sparse CHIME partials with a short strike
    // that flicks the table bright then settles, echoing gently.
    name: 'KALIMBA PLUCK',
    params: {
      'oscA.table': 4, 'oscA.pos': 0.15, 'oscA.level': 0.85,
      'oscB.on': 1, 'oscB.table': 0, 'oscB.pos': 0, 'oscB.oct': 1, 'oscB.level': 0.2,
      'filter.type': 0, 'filter.cutoff': 2600, 'filter.res': 0.1, 'filter.env': 0.4, 'filter.key': 0.7,
      'env1.a': 0.001, 'env1.d': 0.45, 'env1.s': 0, 'env1.r': 0.4,
      'env2.a': 0.001, 'env2.d': 0.12, 'env2.s': 0, 'env2.r': 0.1,
      'mat1.src': 3, 'mat1.dst': 1, 'mat1.amt': 0.35,
      'mat2.src': 4, 'mat2.dst': 3, 'mat2.amt': 0.3,
      'fx.delay.on': 1, 'fx.delay.time': 0.28, 'fx.delay.fb': 0.3, 'fx.delay.mix': 0.22,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.5, 'fx.reverb.mix': 0.28,
    
      'fx.eq.on': 1, 'fx.eq.low': -6, 'fx.eq.mid': -6, 'fx.eq.mfreq': 277, 'fx.eq.high': 6,
    },
  },

  {
    // Concert harp: warm triangle-ish string with a faint chime sparkle, long
    // natural decay into a wide hall — glissando-ready.
    name: 'CELTIC HARP',
    params: {
      'oscA.table': 0, 'oscA.pos': 0.22, 'oscA.unison': 2, 'oscA.detune': 0.06, 'oscA.spread': 0.4, 'oscA.level': 0.8,
      'oscB.on': 1, 'oscB.table': 4, 'oscB.pos': 0.08, 'oscB.oct': 1, 'oscB.level': 0.15,
      'filter.type': 0, 'filter.cutoff': 2200, 'filter.res': 0.1, 'filter.env': 0.45, 'filter.key': 0.65,
      'env1.a': 0.001, 'env1.d': 1.3, 'env1.s': 0, 'env1.r': 1.1,
      'env2.a': 0.001, 'env2.d': 0.3, 'env2.s': 0, 'env2.r': 0.25,
      'mat1.src': 4, 'mat1.dst': 3, 'mat1.amt': 0.35,
      'mat2.src': 3, 'mat2.dst': 1, 'mat2.amt': 0.2,
      'fx.delay.on': 1, 'fx.delay.time': 0.36, 'fx.delay.fb': 0.3, 'fx.delay.mix': 0.15,
      'fx.reverb.on': 1, 'fx.reverb.size': 0.75, 'fx.reverb.mix': 0.38,
    
      'fx.eq.on': 1, 'fx.eq.low': -6, 'fx.eq.mid': -0.9, 'fx.eq.mfreq': 273, 'fx.eq.high': 6,
    },
  },
];


// Resolve a preset into a clean, complete param map (params-as-truth). Modulation
// lives entirely in the 16 fixed `mat{n}.*` slots now. Routes ride in `params`:
// factory presets author them in `mat*` and current user saves persist all 16
// slots there, so the `{ ...defaultParams(), ...presetParams }` spread already
// yields the authored slots plus 0 for every unauthored one — no extra zeroing.
// User presets saved by OLDER builds carry an explicit `mods[]` array instead; we
// expand it into slots in order (truncating anything beyond 16) and zero the rest.
// The per-route scaling is unchanged, so the sound is identical.
export function resolvePresetMods(
  presetParams: Partial<ParamValues>,
  explicit?: ModConnection[],
): ParamValues {
  const merged = { ...defaultParams(), ...presetParams } as ParamValues;
  if (explicit) {
    for (let s = 1; s <= MOD_MATRIX_SIZE; s++) {
      const m = explicit[s - 1];
      merged[`mat${s}.src`] = m ? m.src | 0 : 0;
      merged[`mat${s}.dst`] = m ? m.dst | 0 : 0;
      merged[`mat${s}.amt`] = m ? m.amt || 0 : 0;
    }
  }
  return merged;
}

const LS_KEY = 'fablesynth.userPresets';

export function loadUserPresets(): Preset[] {
  try {
    return JSON.parse(localStorage.getItem(LS_KEY) as string) || [];
  } catch {
    return [];
  }
}

export function saveUserPreset(name: string, params: ParamValues, tables: SerializedUserTable[] = []): Preset[] {
  const list = loadUserPresets().filter((p) => p.name !== name);
  // Routes live in the `mat*` params now, so the full param map already carries
  // them — no separate `mods` array to embed.
  const preset: Preset = { name, params: { ...params } };
  if (tables.length) preset.tables = tables;
  list.push(preset);
  localStorage.setItem(LS_KEY, JSON.stringify(list));
  return list;
}
