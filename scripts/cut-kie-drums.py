"""Cut sparse generated passages into audition candidates; NumPy + ffmpeg.
No semantic instrument classification is claimed. Review candidates before banking.
"""
from pathlib import Path
import json,subprocess,wave,sys
import numpy as np
root=Path(sys.argv[1] if len(sys.argv)>1 else 'build/auditions/kie-drums');out=root/'cuts';out.mkdir(parents=True,exist_ok=True)
sr=48000;hop=240
manifest=[];audition=[]
for source in sorted(root.glob('*.audio')):
 r=subprocess.run(['ffmpeg','-v','error','-i',str(source),'-f','f32le','-ar',str(sr),'-ac','1','-'],capture_output=True,check=True)
 x=np.frombuffer(r.stdout,np.float32).copy()
 if not len(x) or not np.isfinite(x).all():raise ValueError('Invalid source audio')
 n=len(x)//hop;e=np.sqrt(np.mean(x[:n*hop].reshape(n,hop)**2,axis=1));floor=np.percentile(e,20)
 # Detect energy rises above both background and a relative peak floor.
 threshold=max(float(e.max())*.065,float(floor)*3,1e-5)
 active=e>threshold;starts=np.where(active & ~np.r_[False,active[:-1]])[0]
 accepted=[]
 for frame in starts:
  if accepted and frame-accepted[-1]<30:continue
  accepted.append(int(frame))
 candidates=[]
 for j,frame in enumerate(accepted):
  start=max(0,frame*hop-int(.004*sr));next_start=accepted[j+1]*hop if j+1<len(accepted) else len(x)
  end=min(len(x),next_start-int(.004*sr),start+int(.85*sr))
  if end-start<int(.025*sr):continue
  a=x[start:end].copy();a-=np.mean(a)
  peak=float(np.max(abs(a)))
  if peak<1e-5:continue
  # Find the last appreciable tail; leave 15 ms for a smooth fade.
  last=np.where(abs(a)>peak*.008)[0]
  if len(last):a=a[:min(len(a),int(last[-1]) + int(.015*sr))]
  fade=min(int(.008*sr),len(a)//4);a[-fade:]*=np.linspace(1,0,fade)
  a[:24]*=np.linspace(0,1,24) # 0.5 ms edge fade, retained onset pre-roll
  a*=10**(-3/20)/max(np.max(abs(a)),1e-10)
  # Rank by quiet pre-hit background; a useful heuristic, not sound quality.
  pre=x[max(0,start-int(.08*sr)):start];background=float(np.sqrt(np.mean(pre*pre))) if len(pre) else 0
  candidates.append((background/peak,start,a))
 for i,(bg,start,a) in enumerate(sorted(candidates,key=lambda c:c[0])[:8],1):
  name=f'{source.stem}-{i:02}.wav'
  with wave.open(str(out/name),'wb') as w:
   w.setnchannels(1);w.setsampwidth(2);w.setframerate(sr);w.writeframes((np.clip(a,-1,1)*32767).astype('<i2').tobytes())
  manifest.append({'file':name,'source':source.name,'start_seconds':round(start/sr,5),'duration_seconds':round(len(a)/sr,5),'prehit_to_peak_db':round(20*np.log10(max(bg,1e-8)),1),'status':'unreviewed candidate'})
  audition.extend([a,np.zeros(int(.65*sr),np.float32)])
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
if audition:
 pcm=np.concatenate(audition).astype(np.float32)
 subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(sr),'-ac','1','-i','-','-c:a','libmp3lame','-b:a','192k',str(out/'audition.mp3')],input=pcm.tobytes(),check=True)
print(f'Exported {len(manifest)} candidates; review for overlapping instruments and unwanted tails.')
