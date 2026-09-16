import { soft808DubKit } from './dubKit';
// TIDAL MEMORY — authored dub techno, D minor, 118 BPM.
// The helpers only encode the score. No family/energy rules choose its notes.
import { FACTORY_PATCHES, patchToState } from '../../bass/patches';
import { notes, drums, wtPatch } from './score';
import type { SessionPreset } from '../sessionPresets';

// Rootless Dm9; the sub supplies D. The small upper-voice movement to E and
// back to F makes the hook. Bbmaj9/D in REFRACTION changes its colour.
const dm9 = [5, 9, 12, 16], dm7 = [5, 9, 12, 17], bb9 = [5, 9, 10, 12];
const soundings = notes('WT1', 'DISTANT CHORD', [[6, 2, dm9], [30, 1, dm9], [54, 3, dm7]]);
const undertow = notes('WT1', 'CHORD / FIRST TRACE', [[2, 2, dm9, true], [14, 1, dm9], [26, 2, dm7], [38, 1, dm9], [50, 2, dm9], [59, 1, dm7]]);
const pressure = notes('WT1', 'CHORD / TIDAL MOTIF', [[2, 2, dm9, true], [11, 1, dm9], [18, 2, dm7], [30, 1, dm9], [34, 2, dm9, true], [43, 1, dm7], [54, 2, dm9], [61, 1, dm9]]);
const refraction = notes('WT1', 'CHORD / REFRACTED', [[2, 2, bb9, true], [14, 1, bb9], [22, 2, [5, 9, 12, 14]], [35, 2, dm9], [46, 1, dm7], [54, 2, dm9], [62, 1, [4, 9, 12, 16]]]);
const lowWater = notes('WT1', 'CHORD / EXPOSED', [[6, 6, [5, 9, 12, 16]], [38, 8, [4, 9, 12, 16]]]);
const returning = notes('WT1', 'CHORD / WIDER RETURN', [[2, 2, dm9, true], [10, 1, dm7], [18, 2, dm9], [27, 1, dm9], [34, 2, bb9, true], [42, 1, [5, 9, 12, 14]], [51, 2, dm7], [58, 3, dm9]]);
const dissolve = notes('WT1', 'CHORD / LAST REFLECTION', [[2, 3, dm9], [22, 2, dm7], [46, 4, dm9]]);

const bassIn = notes('BL1', 'SUB / FINDING THE POCKET', [[0, 3, -10, true], [10, 2, -10], [18, 4, -10], [30, 1, -3], [32, 3, -10, true], [42, 2, -10], [50, 4, -10], [60, 2, -8]]);
const bassA = notes('BL1', 'SUB / UNDERTOW', [[0, 3, -10, true], [7, 2, -10], [10, 3, -10], [16, 3, -10], [23, 2, -3], [28, 2, -10], [32, 3, -10, true], [39, 2, -10], [42, 3, -10], [48, 3, -10], [55, 2, -5], [60, 2, -8]]);
const bassB = notes('BL1', 'SUB / TURNING CURRENT', [[0, 4, -2, true], [10, 2, -2], [16, 3, -2], [23, 2, -5], [28, 2, -3], [32, 3, -10, true], [39, 2, -10], [44, 2, -3], [48, 4, -10], [58, 2, -8], [62, 2, -11]]);
const bassReturn = notes('BL1', 'SUB / HOME WITH A SCAR', [[0, 3, -10, true], [7, 2, -10], [10, 3, -10], [16, 4, -10], [26, 2, -3], [30, 1, -10], [32, 3, -2, true], [39, 2, -2], [44, 2, -5], [48, 3, -10, true], [55, 2, -3], [60, 3, -10]]);

const signalIn = notes('WT1', 'SIGNAL / BEYOND THE WALL', [[45, 2, 21], [59, 2, 16]]);
const signalA = notes('WT1', 'SIGNAL / TWO LIGHTS', [[13, 1, 21], [29, 2, 16], [45, 1, 21], [60, 3, 19]]);
const signalB = notes('WT1', 'SIGNAL / FALLING IMAGE', [[9, 2, 21], [25, 2, 17], [41, 2, 16], [57, 4, 14]]);
const signalBreak = notes('WT1', 'SIGNAL / WATERLINE', [[3, 6, 21], [19, 4, 19], [35, 6, 16], [55, 6, 14]]);
const signalReturn = notes('WT1', 'SIGNAL / ANSWER', [[13, 2, 16], [29, 2, 21], [45, 2, 17], [59, 4, 14]]);
const signalOut = notes('WT1', 'SIGNAL / RECEDED', [[13, 3, 21], [37, 4, 16]]);

const entryDrums = drums('ROOM / SOUNDINGS', {
  0: 'X.......x.......|x...............|X.......x.......|x...............',
  5: '......x.......x.|......x.........|......x.......x.|......x.........',
  3: '................|.............x..|................|.........x......',
});
const grooveIn = drums('ROOM / LOCK IN', {
  0: 'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  5: '..x...X...x...X.|..x...X...x...X.|..x...X...x...X.|..x...X...x.....',
  3: '............x...|....x...........|............x...|....x........x..',
  6: '................|..........x.....|................|..........x.....',
});
const grooveA = drums('ROOM / PRESSURE', {
  0: 'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  5: '..x...X...x..x..|..x...X...x...X.|..x...X...x..x..|..x...X...x.....',
  3: '....x.......X...|....x......xX...|....x.......X...|....x.......X...',
  6: '..............x.|..........x.....|..............x.|..........x.....',
  8: '................|...............x|................|.............x..',
});
const grooveB = drums('ROOM / REFRACTION', {
  0: 'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X.......',
  5: '..x...X..xx.....|..x...X...x..x..|..x...X..xx.....|..x...X.........',
  3: '....x.......X...|....x.......X...|....x......xX...|....x...........',
  6: '..............x.|..............x.|..............x.|..........x.....',
  8: '...........x....|................|...........x....|................',
});
const breakDrums = drums('ROOM / NO FLOOR', {
  3: '............x...|................|............x...|.............x..',
  5: '................|......x.........|................|......x.........',
});
const returnDrums = drums('ROOM / RETURN', {
  0: 'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  5: '..x...X...x..x..|..x...X..xx.....|..x...X...x..x..|..x...X...x...x.',
  3: '....x.......X...|....x.......X...|....x......xX...|....x.......X...',
  6: '..............x.|..........x.....|..............x.|..........x.....',
  8: '................|...............x|.......x........|................',
});
const tailDrums = drums('ROOM / SHORELINE', {
  0: 'X...x...x...x...|x.......x.......|x...............|................',
  5: '..x...x...x.....|......x.........|..........x.....|................',
  3: '............x...|................|.............x..|................',
});

export const TIDAL_MEMORY: SessionPreset = {
  name: 'TIDAL MEMORY', family: 'DUB TECHNO', variation: 'AUTHORED', energy: 3,
  tags: ['authored', 'hypnotic', 'deep', 'dub techno'],
  session: {
    v: 1, name: 'TIDAL MEMORY', bpm: 118, swing: 0.08, quant: '1 BAR',
    tracks: [
      { machine: 'DR1', name: 'ROOM', color: '#4de8ff', gain: 0.60, patch: soft808DubKit() },
      { machine: 'BL1', name: 'UNDERTOW', color: '#4dff9e', gain: 0.46, patch: {
        kind: 'inline', base: 21, data: { params: {
          ...patchToState(FACTORY_PATCHES[21]).params,
          'osc.level': 0.35, 'sub.level': 0.85, 'flt.cut': 310, 'flt.env': 0.22,
          'aenv.dec': 0.28, 'aenv.sus': 0.35, 'aenv.rel': 0.1, 'acc.amt': 0.45,
        } },
      } },
      { machine: 'WT1', name: 'REFLECTION', color: '#ffa14d', gain: 0.58, patch: wtPatch(56, {
        'oscB.oct': 0, 'oscB.level': 0.28,
        'filter.type': 1, 'filter.cutoff': 1050, 'filter.res': 0.12, 'filter.key': 0.1,
        'filter.env': 0.06, 'env1.a': 0.014, 'env1.d': 0.34, 'env1.r': 0.65,
        'lfo1.sync': 0, 'lfo1.retrig': 0, 'lfo1.rate': 0.08,
        'mat2.src': 1, 'mat2.dst': 3, 'mat2.amt': 0.07,
        'mat3.src': 4, 'mat3.dst': 3, 'mat3.amt': 0.04,
        // The dotted-eighth repeats carry the rhythm between the dry stabs.
        'fx.delay.time': 45 / 118, 'fx.delay.fb': 0.58, 'fx.delay.mix': 0.64,
        'fx.reverb.size': 0.84, 'fx.reverb.mix': 0.48, 'fx.eq.low': -6, 'fx.eq.high': -6,
      }) },
      { machine: 'WT1', name: 'SIGNAL', color: '#b18cff', gain: 0.80, patch: wtPatch(45, {
        // Warm fundamental in the written octave, with just a trace of bell.
        // Remove the original octave-up strike and its sharp mod-envelope lift.
        'oscA.table': 0, 'oscA.pos': 0.06, 'oscA.oct': 0, 'oscA.level': 0.55,
        'oscB.table': 4, 'oscB.pos': 0.08, 'oscB.oct': 0, 'oscB.level': 0.12,
        'oscB.unison': 1, 'noise.on': 0, 'noise.level': 0,
        'mat1.amt': 0.08,
        'filter.type': 1, 'filter.cutoff': 1150, 'filter.res': 0.08,
        'filter.key': 0.12, 'filter.env': 0.04,
        'env1.a': 0.025, 'env1.d': 0.55, 'env1.s': 0, 'env1.r': 1.1,
        'fx.delay.on': 1, 'fx.delay.time': 45 / 118,
        'fx.delay.fb': 0.6, 'fx.delay.mix': 0.66,
        'fx.reverb.on': 1, 'fx.reverb.size': 0.86, 'fx.reverb.mix': 0.52,
        'fx.eq.on': 1, 'fx.eq.low': -9, 'fx.eq.mid': -2, 'fx.eq.mfreq': 1800, 'fx.eq.high': -9,
      }) },
    ],
    scenes: [
      { name: 'SOUNDINGS', clips: [entryDrums, null, soundings, signalIn] },
      { name: 'UNDERTOW', clips: [grooveIn, bassIn, undertow, null] },
      { name: 'PRESSURE', clips: [grooveA, bassA, pressure, signalA] },
      { name: 'REFRACTION', clips: [grooveB, bassB, refraction, signalB] },
      { name: 'LOW WATER', clips: [breakDrums, null, lowWater, signalBreak] },
      { name: 'RETURN', clips: [returnDrums, bassReturn, returning, signalReturn] },
      { name: 'DISSOLVE', clips: [tailDrums, null, dissolve, signalOut] },
    ],
  },
};
