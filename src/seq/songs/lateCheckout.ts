// LATE CHECKOUT — deep-house groove draft, 122 BPM, sixteen authored bars.
// Four four-bar cells remain editable in the hosted devices. Not yet a factory song.
import { FACTORY_PATCHES, patchToState } from '../../bass/patches';
import type { SessionDoc } from '../protocol';
import { soft808DubKit } from './dubKit';
import { notes, drums, wtPatch } from './score';

const am9 = [0,4,7,11], fmaj9 = [0,4,7,9], dm9 = [0,4,5,9];
const eSus = [2,7,9,11], e7 = [2,8,11,16], cmaj9 = [2,4,7,11];
const keysA = notes('WT1', 'KEYS / OPEN WINDOW', [[0,5,am9],[10,3,am9],[18,4,am9],[29,2,am9],[32,5,fmaj9],[42,3,fmaj9],[50,4,fmaj9],[61,2,fmaj9]]);
const keysB = notes('WT1', 'KEYS / TURN THE CORNER', [[0,5,dm9],[11,2,dm9],[18,4,dm9],[28,3,dm9],[32,5,eSus],[43,2,eSus],[50,5,e7],[61,2,e7]]);
const keysC = notes('WT1', 'KEYS / SUN ON THE FLOOR', [[0,6,am9],[11,2,am9],[19,4,[0,4,9,11]],[29,2,am9],[32,5,cmaj9],[42,3,cmaj9],[50,4,cmaj9],[60,3,cmaj9]]);
const keysD = notes('WT1', 'KEYS / STAY A LITTLE', [[0,5,dm9],[10,3,dm9],[16,5,fmaj9],[26,3,fmaj9],[32,5,eSus],[42,3,eSus],[48,5,e7],[58,3,[2,8,11]]]);

const bassA = notes('BL1', 'BASS / WALK IN', [[0,3,-3,true],[6,2,-3],[11,2,4],[16,3,-3],[22,2,0],[27,2,-3],[30,1,-5],[32,3,-7,true],[38,2,-7],[43,2,0],[48,3,-7],[54,2,-3],[59,2,0]]);
const bassB = notes('BL1', 'BASS / EASY TURN', [[0,3,-10,true],[6,2,-10],[11,2,-3],[16,3,-10],[23,2,-7],[28,2,-10],[31,1,-9],[32,3,-8,true],[38,2,-8],[43,2,-1],[48,3,-8],[54,2,-4],[59,2,-1],[62,1,-5]]);
const bassC = notes('BL1', 'BASS / HIGHER GROUND', [[0,3,-3,true],[6,2,-3],[11,2,4],[16,3,-3],[22,2,0],[27,2,4],[30,1,-5],[32,3,-5,true],[38,2,-5],[43,2,2],[48,3,-5],[54,2,-1],[59,2,2],[62,1,-8]]);
const bassD = notes('BL1', 'BASS / COME BACK ROUND', [[0,3,-10,true],[6,2,-3],[11,2,-10],[16,3,-7,true],[22,2,0],[27,2,-7],[30,1,-9],[32,3,-8,true],[38,2,-1],[43,2,-8],[48,3,-8],[54,2,-4],[58,2,-1],[62,1,-5]]);

// One recognizable question, an answer, then a looser reprise. Warm upper keys.
const hookA = notes('WT1', 'HOOK / GOOD MORNING', [[5,3,16],[9,2,14],[14,3,12],[22,5,11],[37,3,16],[41,2,19],[46,3,16],[55,5,12]]);
const hookB = notes('WT1', 'HOOK / NO HURRY', [[5,3,17],[10,2,16],[14,3,12],[23,4,9],[37,3,14],[42,2,11],[51,3,8],[58,4,11]]);
const hookC = notes('WT1', 'HOOK / WINDOW LIGHT', [[5,3,16],[9,2,14],[14,3,12],[22,3,11],[27,2,12],[37,4,19],[43,2,16],[50,3,14],[57,5,11]]);
const hookD = notes('WT1', 'HOOK / SEE YOU AGAIN', [[5,3,17],[10,3,16],[21,3,16],[26,3,12],[37,3,14],[42,2,11],[51,3,8],[58,5,11]]);

const drumsA = drums('808 / IN THE POCKET', {
  0:'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  3:'....x.......X...|....x.......X...|....x.......X...|....x.......X...',
  5:'..x...X...x...X.|..x..xX...x...X.|..x...X...x...X.|..x...X..xx.....',
  6:'..............x.|..........x.....|..............x.|..........x.....',
});
const drumsB = drums('808 / LITTLE PUSH', {
  0:'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  3:'....x.......X...|....x.......X...|....x.......X...|....x......xX...',
  5:'..x...X...x..x..|..x...X...x...X.|..x...X...x..x..|..x...X...x.....',
  6:'..............x.|..........x.....|..............x.|..........x.....',
});
const drumsC = drums('808 / LOOSE SHOULDERS', {
  0:'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  3:'....x.......X...|....x.......X...|....x.......X...|....x.......X...',
  5:'..x...X..xx...x.|..x...X...x...X.|..x..xX...x...x.|..x...X...x..x..',
  6:'..............x.|..........x.....|..............x.|..........x.....',
});
const drumsD = drums('808 / TURNAROUND', {
  0:'X...x...x...x...|x...x...X...x...|X...x...x...x...|x...x...X...x...',
  3:'....x.......X...|....x.......X...|....x.......X...|....x.......X...',
  5:'..x...X...x...x.|..x...X..xx...x.|..x...X...x...x.|..x...X...x.....',
  6:'..............x.|..........x.....|..............x.|..........x.....',
});

export const LATE_CHECKOUT: SessionDoc = {
  v:1, name:'LATE CHECKOUT — GROOVE DRAFT', bpm:122, swing:0.16, quant:'1 BAR',
  tracks: [
    { machine:'DR1', name:'POCKET', color:'#4de8ff', gain:0.56, patch:soft808DubKit() },
    { machine:'BL1', name:'ROUND BASS', color:'#4dff9e', gain:0.48, patch:{kind:'inline',base:16,data:{params:{
      ...patchToState(FACTORY_PATCHES[16]).params,
      'osc.pos':0.12, 'osc.level':0.45, 'sub.level':0.72, 'flt.cut':430,
      'flt.res':0.10, 'flt.env':0.22, 'flt.drive':0.12,
      'aenv.att':0.007, 'aenv.dec':0.25, 'aenv.sus':0.32, 'aenv.rel':0.085,
      'acc.amt':0.35, 'fx.drive.on':0,
    }}} },
    { machine:'WT1', name:'WARM KEYS', color:'#ffa14d', gain:0.50, patch:wtPatch(20, {
      'oscB.oct':0, 'oscB.level':0.10, 'filter.type':1, 'filter.cutoff':1550,
      'filter.env':0.16, 'filter.key':0.16, 'filter.res':0.08,
      'env1.a':0.006, 'env1.d':1.1, 'env1.s':0.3, 'env1.r':0.28,
      'mat1.amt':0.12, 'mat2.amt':0.08, 'mat3.amt':0.12,
      'fx.chorus.mix':0.22, 'fx.delay.on':1, 'fx.delay.time':45/122,
      'fx.delay.fb':0.24, 'fx.delay.mix':0.18,
      'fx.reverb.size':0.45, 'fx.reverb.mix':0.22,
      'fx.eq.low':-6, 'fx.eq.mid':-2, 'fx.eq.high':-4,
    }) },
    { machine:'WT1', name:'TOP LINE', color:'#b18cff', gain:0.58, patch:wtPatch(20, {
      'oscA.level':0.65, 'oscB.oct':0, 'oscB.level':0.08,
      'filter.type':1, 'filter.cutoff':1500, 'filter.env':0.12, 'filter.key':0.12,
      'env1.a':0.008, 'env1.d':0.8, 'env1.s':0.1, 'env1.r':0.30,
      'mat1.amt':0.1, 'mat2.amt':0.06, 'mat3.amt':0.1,
      'fx.chorus.mix':0.18, 'fx.delay.on':1, 'fx.delay.time':30/122,
      'fx.delay.fb':0.28, 'fx.delay.mix':0.20,
      'fx.reverb.size':0.48, 'fx.reverb.mix':0.24,
      'fx.eq.low':-7, 'fx.eq.mid':-1, 'fx.eq.high':-4,
    }) },
  ],
  scenes: [
    { name:'01 / QUESTION', clips:[drumsA,bassA,keysA,hookA] },
    { name:'02 / ANSWER', clips:[drumsB,bassB,keysB,hookB] },
    { name:'03 / REPRISE', clips:[drumsC,bassC,keysC,hookC] },
    { name:'04 / TURNAROUND', clips:[drumsD,bassD,keysD,hookD] },
  ],
};
