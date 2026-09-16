// KIE sound generation: explicit submit, resumable polling, no paid retries.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
const kitPrompts=JSON.parse(await readFile(new URL('./drum-kit-prompts.json',import.meta.url),'utf8'));
const variant=process.argv[3] || 'original';
if(!['original','dry-machine','handclap-pocket',...Object.keys(kitPrompts)].includes(variant))throw Error('Unknown variant');
const dir='build/auditions/kie-stems'+(variant==='original'?'':'-'+variant);await mkdir(dir,{recursive:true});
const env=await readFile('.env','utf8');
const match=env.match(/^\s*(?:export\s+)?KIE_API_KEY\s*=\s*(.*?)\s*$/m);
const key=process.env.KIE_API_KEY || match?.[1].replace(/^(['"])(.*)\1$/,'$2');
if(!key)throw Error('KIE_API_KEY is missing');
async function api(route,body){
 const r=await fetch('https://api.kie.ai/api/v1/jobs/'+route,{method:body?'POST':'GET',headers:{Authorization:`Bearer ${key}`,...(body?{'Content-Type':'application/json'}:{})},...(body?{body:JSON.stringify(body)}:{}),signal:AbortSignal.timeout(60000)});
 const j=await r.json();if(!r.ok||j.code!==200)throw Error(`KIE request failed: HTTP ${r.status}, API ${j.code}: ${j.msg}`);return j.data;
}
const file=dir+'/workflow.json';let state={};try{state=JSON.parse(await readFile(file,'utf8'));}catch(e){if(e.code!=='ENOENT')throw e;}
const save=()=>writeFile(file,JSON.stringify(state,null,2));
async function submit(name,body){if(state[name])return;state[name]={status:'submitting',request:body};await save();const d=await api('createTask',body);state[name].taskId=d.taskId;state[name].status='submitted';await save();console.log(name+': submitted');}
async function poll(name){const t=state[name];if(!t?.taskId)return;if(t.status==='success'||t.status==='fail')return;const d=await api('recordInfo?taskId='+encodeURIComponent(t.taskId));t.status=d.state;if(d.state==='success')t.result=typeof d.resultJson==='string'?JSON.parse(d.resultJson):d.resultJson;if(d.state==='fail')t.error=d.failMsg;await save();console.log(name+': '+t.status+(t.error?' '+t.error:''));}
const prompts={
 ...kitPrompts,
 'dry-machine':{title:'Dry Machine Session',duration:40,prompt:'Drum machine instrumental. Solo kick opening. Solo clap middle. Solo closed and open hi hats ending. Sparse stop-start rhythm with silent rests.',style:'Dry studio drum-machine performance, 100 BPM. Deep tight analog kick, compact snappy handclap, silky short 808 hats. Instruments take turns in separate solo sections, rarely overlap. Close direct recording, mono-centered transients, no room sound. Minimal rhythm, empty space, crisp attack and fast decay.',negative_tags:'bassline, synthesizer, keys, pads, vocals, melody, reverb, delay, cymbal wash, fills, distortion, cowbell',style_weight:.95,weirdness_constraint:.05},
 'handclap-pocket':{title:'Muted House Percussion',duration:40,prompt:'Minimal percussion instrumental with breaks. Start with handclap solo, pause, dry hat solo, pause, round kick solo. Spacious alternating hits.',style:'Understated early Chicago house drum-tool, 110 BPM, intimate dry handclaps with short layered flams, soft noisy closed hats, brief open hats, warm rounded kick. Exposed percussion solo breaks and frequent full stops. Each sound gets its own space, strong natural attacks, tiny tails, clean unprocessed studio sound.',negative_tags:'bass guitar, bassline, piano, synth chords, vocals, pitched percussion, cowbell, triangle, reverb, delay, shaker loops, crash cymbal, distortion',style_weight:.9,weirdness_constraint:.1}
};
if(process.argv[2]==='generate')await submit('music',{model:'ai-music-api/generate',input:{model:'V6',custom_mode:true,instrumental:true,title:'Deep Pocket Drum Study',duration:60,prompt:'Instrumental deep house. Exposed drum intro and outro, sparse breakdowns. Kick alone, then clap, then hats. Leave space between drum hits.',style:'111 BPM deep house, dry punchy analog kick with warm sub body, tight soft handclap, crisp restrained 808 closed and open hats. Sparse warm bass and mellow keys. Drum-forward studio mix, minimal drum reverb. Eight bar percussion-only intro and outro with staggered instrument entrances, sparse drum breaks for sampling.',negative_tags:'vocals, cowbell, triangle, metallic bells, distorted drums, busy percussion, washed out reverb',style_weight:.85,weirdness_constraint:.15,...(prompts[variant] || {})}});
else if(process.argv[2]==='poll'){await poll('music');await poll('stems');}
else if(process.argv[2]==='separate'){
 const r=state.music?.result;const tracks=Array.isArray(r?.data)?r.data:r?.data?.data || r?.data?.sunoData;
 if(!tracks?.[0]?.id)throw Error('Inspect music result shape before separation');
 const track=tracks[0];state.selectedAudio=track;await save();
 await submit('stems',{model:'ai-music-api/separate-vocals',input:{task_id:state.music.taskId,audio_id:track.id,type:'split_stem',stem_name:'Drums'}});
}else if(process.argv[2]==='download'){
 const urls=[];
 function walk(o,path=''){if(!o)return;if(typeof o==='string'&&o.startsWith('https://')&&/url/i.test(path)&&!/image|stream/i.test(path))urls.push([path,o]);else if(typeof o==='object')for(const [k,v]of Object.entries(o))walk(v,path+'_'+k);}
 walk(state.stems?.result,'stem');
 if(state.selectedAudio?.audio_url)urls.push(['original',state.selectedAudio.audio_url]);
 for(const [name,url] of urls){const f=dir+'/'+name.replace(/[^a-z0-9_-]/gi,'_')+'.audio';try{await readFile(f);continue;}catch(e){if(e.code!=='ENOENT')throw e;}const r=await fetch(url,{signal:AbortSignal.timeout(120000)});if(!r.ok)throw Error('Download failed');await writeFile(f,Buffer.from(await r.arrayBuffer()));console.log(name+': downloaded');}
}else throw Error('Use generate, poll, separate or download');
