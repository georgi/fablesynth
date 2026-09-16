// KIE sound generation: explicit submit, resumable polling, no paid retries.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
const dir='build/auditions/kie-drums';await mkdir(dir,{recursive:true});
const env=await readFile('.env','utf8');
const match=env.match(/^\s*(?:export\s+)?KIE_API_KEY\s*=\s*(.*?)\s*$/m);
const key=process.env.KIE_API_KEY || match?.[1].replace(/^(['"])(.*)\1$/,'$2');
if(!key)throw Error('KIE_API_KEY is missing');
async function api(route,body){
 const r=await fetch('https://api.kie.ai/api/v1/jobs/'+route,{method:body?'POST':'GET',headers:{Authorization:`Bearer ${key}`,...(body?{'Content-Type':'application/json'}:{})},...(body?{body:JSON.stringify(body)}:{}),signal:AbortSignal.timeout(60000)});
 const j=await r.json();if(!r.ok||j.code!==200)throw Error(`KIE request failed: HTTP ${r.status}, API ${j.code}: ${j.msg}`);return j.data;
}
const prompts={
 kick:'Dry isolated deep house kick drum sampling session. Only a single warm punchy analog kick repeated slowly, one hit every two seconds with silence between hits. Solid 55 Hz low body, short firm attack, tight 180 ms decay, subtle analog saturation. Several small timbre variations. Absolutely no bassline, melody, chords, vocals, hi hats, claps, other percussion, reverb or delay. Studio drum sample source recording, not a song.',
 clap:'Dry isolated handclap drum sampling session. Only a tight warm 808 style clap repeated slowly, one clap every two seconds, complete silence between claps. Firm short attack with natural layered clap flams and a soft noise body, 120 ms tail. A few subtle variations. No metallic cowbell, kick, hats, bass, melody, chords, voice, reverb or delay. Clean close studio recording for a deep house drum sample library.',
 hats:'Dry isolated 808 hi hat sampling session. Soft crisp closed hi hats for the first half, short open hi hats for the second half. One isolated hit every two seconds with silence between hits. Tight ticks and short airy noise tails, warm restrained treble. No triangle, pitched bell, cowbell, kick, clap, snare, bass, melody, chords, voice, reverb or delay. Clean studio source recording for cutting individual drum samples.'
};
const manifestPath=dir+'/tasks.json';let tasks={};try{tasks=JSON.parse(await readFile(manifestPath,'utf8'));}catch(e){if(e.code!=='ENOENT')throw e;}
if(process.argv[2]==='submit'){
 for(const [kind,prompt] of Object.entries(prompts)){
  if(tasks[kind])continue;
  if(prompt.length>500)throw Error('Prompt too long');
  // Persist intent before POST: an uncertain response must not be resubmitted.
  tasks[kind]={prompt,status:'submitting'};await writeFile(manifestPath,JSON.stringify(tasks,null,2));
  const data=await api('createTask',{model:'ai-music-api/sounds',input:{prompt,model:'V6',sound_loop:false,sound_tempo:60,grab_lyrics:false}});
  tasks[kind]={...tasks[kind],taskId:data.taskId,status:'submitted'};
  await writeFile(manifestPath,JSON.stringify(tasks,null,2));console.log(`${kind}: submitted ${data.taskId}`);
 }
}else if(process.argv[2]==='poll'){
 for(const [kind,t] of Object.entries(tasks)){
  if(!t.taskId){console.log(`${kind}: uncertain submission; do not retry automatically`);continue;}
  if(t.status==='downloaded')continue;
  const d=await api('recordInfo?taskId='+encodeURIComponent(t.taskId));t.status=d.state;
  if(d.state==='success'){
   const result=typeof d.resultJson==='string'?JSON.parse(d.resultJson):d.resultJson;t.result=result;
   const urls=result?.resultUrls || (Array.isArray(result?.data) ? result.data.map(item=>item.audio_url).filter(Boolean) : []);
   for(let i=0;i<urls.length;i++){
    const u=new URL(urls[i]);if(u.protocol!=='https:')throw Error('Unexpected media URL');
    const r=await fetch(u,{signal:AbortSignal.timeout(120000)});if(!r.ok)throw Error('Media download failed');
    await writeFile(`${dir}/${kind}-${i}.audio`,Buffer.from(await r.arrayBuffer()));
   }
   t.status=urls.length?'downloaded':'success-needs-inspection';
  }else if(d.state==='fail')t.error=d.failMsg;
  console.log(`${kind}: ${t.status}${t.error?' — '+t.error:''}`);
  await writeFile(manifestPath,JSON.stringify(tasks,null,2));
 }
}else throw Error('Use submit or poll');
