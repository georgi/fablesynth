import { soft808DubKit } from './dubKit';
// PHASE RUNNER — F minor, 126 BPM. Bass-led dub with interlocking echoes.
import { FACTORY_PATCHES, patchToState } from '../../bass/patches';
import type { SessionPreset } from '../sessionPresets';
import { notes, drums, wtPatch } from './score';

// The hook leans into beat two, then leaves beat three to the chord's echo.
// F / Eb / C are its fingerprint; the last bar's G is a pickup, not a cadence.
const ignitionBass = notes('BL1', 'MOTOR / TURNOVER', [[2,2,-7,true],[5,2,-7],[10,2,-9],[18,2,-7,true],[21,2,-7],[30,1,-12],[34,2,-7,true],[37,2,-7],[42,2,-9],[50,2,-7,true],[58,2,-12],[62,1,-5]]);
const motorBass = notes('BL1', 'MOTOR / THREE AGAINST FOUR', [[2,2,-7,true],[5,2,-7],[10,2,-9],[14,1,-12],[18,2,-7,true],[21,2,-7],[26,2,-9],[30,1,-5],[34,2,-7,true],[37,2,-7],[42,2,-9],[46,1,-12],[50,2,-7,true],[53,2,-4],[58,2,-12],[62,1,-5]]);
const crossBass = notes('BL1', 'MOTOR / SIDE ROAD', [[2,2,-7,true],[5,2,-7],[11,2,-12],[18,2,-9,true],[21,2,-9],[26,2,-12],[30,1,-5],[34,2,-7,true],[38,2,-4],[43,2,-9],[50,2,-7,true],[54,2,-12],[59,2,-5]]);
const suspendedBass = notes('BL1', 'MOTOR / IDLING', [[2,6,-7],[26,3,-12],[34,6,-7],[58,3,-9]]);
const secondBass = notes('BL1', 'MOTOR / SECOND WIND', [[2,2,-7,true],[5,2,-7],[10,2,-9],[14,1,-12],[18,2,-7,true],[22,2,-4],[26,2,-9],[30,1,-5],[34,2,-7,true],[37,2,-7],[42,2,-12],[46,1,-9],[50,2,-7,true],[55,2,-12],[60,3,-7]]);
const coastBass = notes('BL1', 'MOTOR / COAST', [[2,2,-7],[10,2,-9],[18,3,-7],[30,2,-12],[34,4,-7],[50,5,-7]]);

// Compact, rootless Fm9. Sparse triggers let the 3/16 ping-pong fill the grid.
const fm9 = [0,3,7,8], suspended = [0,3,5,10], eb9 = [1,5,7,10];
const firstChords = notes('WT1', 'ECHO / FIRST SPARK', [[12,1,fm9],[44,1,fm9]]);
const lockChords = notes('WT1', 'ECHO / LOCKSTEP', [[0,1,fm9,true],[22,1,fm9],[32,1,suspended],[54,1,fm9]]);
const crossChords = notes('WT1', 'ECHO / ACROSS THE BAR', [[7,1,fm9],[23,1,eb9],[39,1,suspended],[55,1,fm9]]);
const hangingChords = notes('WT1', 'ECHO / HANGING WIRE', [[0,3,suspended],[32,3,fm9]]);
const secondChords = notes('WT1', 'ECHO / SHIFTED RETURN', [[0,1,fm9,true],[19,1,suspended],[32,1,fm9],[51,1,[0,3,7,12]],[61,1,fm9]]);
const lastChords = notes('WT1', 'ECHO / AFTERIMAGE', [[0,1,fm9],[28,1,suspended],[44,1,fm9]]);

// A low wooden answer, not a bright lead. Its 1/4 echo runs against the chords.
const pulseA = notes('WT1', 'RELAY / ANSWER', [[15,1,0],[31,1,7],[47,1,0],[61,1,3]]);
const pulseB = notes('WT1', 'RELAY / CROSS TALK', [[3,1,7],[19,1,5],[35,1,0],[51,1,3]]);
const pulseBreak = notes('WT1', 'RELAY / ALONE', [[11,2,0],[27,2,3],[43,2,7],[59,2,3]]);
const pulseReturn = notes('WT1', 'RELAY / OPEN CIRCUIT', [[13,1,0],[29,1,7],[45,1,3],[58,1,0]]);
const pulseOut = notes('WT1', 'RELAY / FADING', [[15,1,7],[39,1,0]]);

const ignition = drums('CHASSIS / START', {
  0:'X...x...x...x...|X...x...x...x...|X...x...x...x...|X...x...x...x...',
  5:'..x.......x.....|..x.......x.....|..x...x...x.....|..x...x...x.....',
  3:'................|............x...|................|............x...',
});
const lock = drums('CHASSIS / LOCKSTEP', {
  0:'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  5:'..X...x...X...x.|..X...x..xX...x.|..X...x...X...x.|..X...x...X..x..',
  3:'....x.......X...|....x.......X...|....x.......X...|....x......xX...',
  6:'......x.........|..............x.|......x.........|..............x.',
  8:'.............x..|................|.............x..|.........x......',
});
const cross = drums('CHASSIS / LATERAL', {
  0:'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  5:'..X...x...X..x..|..X...x...X...x.|..X..xx...X.....|..X...x...X.....',
  3:'....x.......X...|....x.......X...|....x.......X...|....x...........',
  6:'......x.........|..............x.|......x.........|..........x.....',
  8:'.............x..|.........x......|.............x..|...............x',
});
const suspension = drums('CHASSIS / SUSPENDED', {
  5:'..x.......x.....|......x.......x.|..x.......x.....|......x.........',
  3:'............x...|................|............x...|................',
  8:'................|.............x..|................|.............x..',
});
const secondWind = drums('CHASSIS / SECOND WIND', {
  0:'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  5:'..X...x...X...x.|..X..xx...X...x.|..X...x...X..x..|..X...x...X...x.',
  3:'....x.......X...|....x.......X...|....x.......X...|....x......xX...',
  6:'......x.........|..............x.|......x.........|..............x.',
  8:'.............x..|................|.........x......|...............x',
});
const coast = drums('CHASSIS / FREEWHEEL', {
  0:'X...x...x...x...|x...x...x...x...|x.......x.......|x...............',
  5:'..x...x...x...x.|..x...x...x.....|..x.......x.....|......x.........',
  3:'....x.......x...|............x...|................|................',
});

export const PHASE_RUNNER: SessionPreset = {
  name:'PHASE RUNNER', family:'DUB TECHNO', variation:'AUTHORED', energy:4,
  tags:['authored','driving','bass-led','dub techno'],
  session: {
    v:1, name:'PHASE RUNNER', bpm:126, swing:0.06, quant:'1 BAR',
    tracks: [
      { machine:'DR1', name:'CHASSIS', color:'#4de8ff', gain:0.57, patch:soft808DubKit() },
      { machine:'BL1', name:'MOTOR', color:'#4dff9e', gain:0.44, patch:{kind:'inline',base:21,data:{params:{
        ...patchToState(FACTORY_PATCHES[21]).params,
        'osc.pos':0.12, 'osc.level':0.48, 'sub.level':0.75,
        'flt.cut':420, 'flt.res':0.16, 'flt.env':0.28, 'flt.drive':0.2,
        'fenv.dec':0.14, 'aenv.att':0.006, 'aenv.dec':0.22, 'aenv.sus':0.28,
        'aenv.rel':0.075, 'acc.amt':0.5, 'fx.drive.amt':0.12,
      }}} },
      { machine:'WT1', name:'ECHO', color:'#ffa14d', gain:0.58, patch:wtPatch(56, {
        'oscB.oct':0, 'oscB.level':0.22, 'filter.type':1, 'filter.cutoff':980,
        'filter.res':0.12, 'filter.env':0.08, 'filter.key':0.1,
        'env1.a':0.012, 'env1.d':0.20, 'env1.s':0, 'env1.r':0.28,
        'lfo1.sync':0, 'lfo1.rate':0.11, 'lfo1.retrig':0,
        'mat2.src':1, 'mat2.dst':3, 'mat2.amt':0.09,
        'mat3.src':4, 'mat3.dst':3, 'mat3.amt':0.04,
        'fx.delay.time':45/126, 'fx.delay.fb':0.79, 'fx.delay.mix':0.78,
        'fx.reverb.size':0.82, 'fx.reverb.mix':0.44,
        'fx.eq.low':-7, 'fx.eq.high':-7,
      }) },
      { machine:'WT1', name:'RELAY', color:'#b18cff', gain:0.63, patch:wtPatch(59, {
        'oscA.pos':0.04, 'oscA.level':0.7, 'oscB.level':0.18,
        'filter.type':1, 'filter.cutoff':820, 'filter.res':0.1, 'filter.env':0.1,
        'env1.a':0.014, 'env1.d':0.18, 'env1.r':0.35,
        'mat1.amt':0.08, 'fx.delay.time':60/126, 'fx.delay.fb':0.74, 'fx.delay.mix':0.75,
        'fx.reverb.size':0.76, 'fx.reverb.mix':0.42, 'fx.eq.low':-6, 'fx.eq.high':-8,
      }) },
    ],
    scenes: [
      { name:'IGNITION', clips:[ignition,ignitionBass,firstChords,null] },
      { name:'LOCKSTEP', clips:[lock,motorBass,lockChords,pulseA] },
      { name:'CROSSCURRENT', clips:[cross,crossBass,crossChords,pulseB] },
      { name:'SUSPENSION', clips:[suspension,suspendedBass,hangingChords,pulseBreak] },
      { name:'SECOND WIND', clips:[secondWind,secondBass,secondChords,pulseReturn] },
      { name:'COAST', clips:[coast,coastBass,lastChords,pulseOut] },
    ],
  },
};
