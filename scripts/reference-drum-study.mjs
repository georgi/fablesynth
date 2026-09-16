// Reproducible, isolated audition; does not alter the factory bank.
import { build } from 'esbuild';
import { mkdir, writeFile } from 'node:fs/promises';
const r = await build({stdin:{contents:'export { LATE_CHECKOUT } from "./src/seq/songs/lateCheckout"; export { notes, drums } from "./src/seq/songs/score"; export { exportSessionJson } from "./src/seq/sessionLibrary"; export { validateSession } from "./src/seq/protocol";',resolveDir:process.cwd()},bundle:true,platform:'node',format:'esm',write:false});
const {LATE_CHECKOUT,notes,drums,exportSessionJson,validateSession}=await import('data:text/javascript;base64,'+Buffer.from(r.outputFiles[0].text).toString('base64'));
const dir='build/auditions/reference-study'; await mkdir(dir,{recursive:true});
const kick='X...x...x...x...|X...x...x...x...|X...x...x...x...|X...x...x...x...';
const pocket=drums('REFERENCE / POCKET',{0:kick,3:'....x.......x...|....x.......x...|....x.......x...|....x.......x...',5:'..x...x...x...x.|..x...x...x...x.|..x...x...x...x.|..x...x...x..xx.',6:'..............x.|..............x.|..............x.|..........x.....'});
const low=drums('REFERENCE / FOUNDATION',{0:kick});
const bass=notes('BL1','REFERENCE / SPACE',[[2,1,-3],[6,1,-3],[10,1,4],[14,1,-3],[18,1,-3],[22,1,0],[26,1,-3],[30,1,4],[34,1,-3],[38,1,-3],[42,1,4],[46,1,-3],[50,1,-3],[54,1,0],[58,1,-3],[62,1,-5]]);
for(const variant of ['before','after']){
 const s=structuredClone(LATE_CHECKOUT);s.name=`REFERENCE STUDY / ${variant.toUpperCase()}`;s.bpm=111;s.swing=.06;
 s.tracks[2].gain=s.tracks[3].gain=0;
 s.scenes=[{name:'FOUNDATION',clips:[low,bass,null,null]},{name:'FULL KIT',clips:[pocket,bass,null,null]}];
 if(variant==='after'){
  Object.assign(s.tracks[0].patch.data.params,{
   'pad0.oscA.tune':-27,'pad0.oscA.level':.9,
   'pad0.aenv.att':.0005,'pad0.aenv.hold':.003,'pad0.aenv.dec':.14,'pad0.aenv.curve':.88,
   'pad0.penv.amt':22,'pad0.penv.dec':.045,
   'pad0.fx.drive.on':1,'pad0.fx.drive.amt':.22,'pad0.fx.drive.mix':.3,
   'pad0.fx.comp.on':1,'pad0.fx.comp.thr':-8,'pad0.fx.comp.gain':2,
   'pad0.lvl':.95,
   'pad3.lvl':.75,'pad5.lvl':.9,'pad6.lvl':.8,
  });
 }
 const error=validateSession(s);if(error)throw Error(error);
 await writeFile(`${dir}/${variant}.json`,exportSessionJson(s));
 const isolated=structuredClone(s);isolated.tracks[1].gain=0;
 await writeFile(`${dir}/${variant}-drums.json`,exportSessionJson(isolated));
}
console.log('Exported before/after studies and isolated drums at 111 BPM.');
