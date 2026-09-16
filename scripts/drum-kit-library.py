"""Cut separated drum stems into a deletion-friendly WAV library.
Requires NumPy and ffmpeg. Labels describe spectrum, not verified instruments.
"""
import argparse,csv,json,subprocess,wave
from pathlib import Path
import numpy as np
SR=48000;HOP=240

def decode(path):
 r=subprocess.run(['ffmpeg','-v','error','-i',str(path),'-f','f32le','-ar',str(SR),'-ac','1','-'],capture_output=True,check=True)
 x=np.frombuffer(r.stdout,np.float32).copy()
 if not np.isfinite(x).all():raise ValueError('Nonfinite audio')
 return x

def detect(x):
 n=len(x)//HOP
 if n<4:return []
 e=np.sqrt(np.mean(x[:n*HOP].reshape(n,HOP)**2,axis=1))
 threshold=max(float(np.max(e))*.055,float(np.percentile(e,15))*2.5,1e-5)
 active=e>threshold
 rises=np.where(active & ~np.r_[False,active[:-1]])[0]
 # Also detect new hits inside a decaying tail. 80 ms lockout preserves clap flams.
 prev=np.maximum(np.r_[0,e[:-1]],1e-8)
 sharp=np.where((e>threshold)&(e>prev*2.8))[0]
 hits=[]
 for i in sorted(set(rises.tolist()+sharp.tolist())):
  if not hits or i-hits[-1]>=16:hits.append(i)
 return hits

def candidates(x):
 hits=detect(x);result=[]
 for j,i in enumerate(hits):
  start=max(0,i*HOP-192);next_hit=hits[j+1]*HOP-192 if j+1<len(hits) else len(x)
  end=min(len(x),next_hit,start+int(.7*SR))
  if end-start<1200:continue
  a=x[start:end].copy();a-=a.mean();peak=float(np.max(abs(a)))
  if peak<.0001:continue
  last=np.flatnonzero(abs(a)>peak*.008)
  if len(last):a=a[:min(len(a),int(last[-1])+720)]
  if len(a)<1200:continue
  a[:24]*=np.linspace(0,1,24);fade=min(384,len(a)//4);a[-fade:]*=np.linspace(1,0,fade)
  a*=10**(-3/20)/max(float(np.max(abs(a))),1e-10)
  freq=np.fft.rfftfreq(len(a),1/SR);power=abs(np.fft.rfft(a))**2;total=max(float(power.sum()),1e-12)
  low=float(power[freq<200].sum()/total);high=float(power[freq>3000].sum()/total)
  label='LOW' if low>.65 else 'HIGH' if high>.35 else 'MID' if low<.2 else 'MIXED'
  pre=x[max(0,start-3840):start];background=float(np.sqrt(np.mean(pre*pre))) if len(pre) else 0
  score=20*np.log10(max(background/peak,1e-8))
  result.append(dict(start=start,audio=a,label=label,background_db=round(float(score),1),low_ratio=round(low,3),high_ratio=round(high,3)))
 return result

def cut(source,out,kit,limit):
 out.mkdir(parents=True,exist_ok=True);manifest=out/'manifest.json'
 if manifest.exists():
  print(f'{kit}: already cut; existing/deleted selections preserved');return
 rows=[]
 for path in sorted([*source.glob('*.audio'), *source.glob('Drums.mp3'), *source.glob('Percussion.mp3')]):
  all_hits=candidates(decode(path));kept=[]
  # Favor quiet starts, but keep multiple spectral groups and timbres.
  groups={}
  for hit in sorted(all_hits,key=lambda h:h['background_db']):groups.setdefault(hit['label'],[]).append(hit)
  while groups and len(kept)<limit:
   for label in list(groups):
    hit=groups[label].pop(0)
    duplicate=False
    for other in kept:
     if other['label']!=hit['label']:continue
     n=min(len(hit['audio']),len(other['audio']),9600)
     a=hit['audio'][:n];b=other['audio'][:n]
     corr=abs(float(np.dot(a,b)))/(float(np.linalg.norm(a)*np.linalg.norm(b))+1e-12)
     if corr>.97:duplicate=True;break
    if not duplicate:kept.append(hit)
    if not groups[label]:del groups[label]
    if len(kept)>=limit:break
  for h in kept:
   ms=round(h['start']/SR*1000);name=f'{kit}__{h["label"]}__{path.stem}__{ms:06}ms.wav'
   with wave.open(str(out/name),'wb') as w:
    w.setnchannels(1);w.setsampwidth(2);w.setframerate(SR);w.writeframes((np.clip(h['audio'],-1,1)*32767).astype('<i2').tobytes())
   rows.append(dict(file=name,source=path.name,start_seconds=round(h['start']/SR,5),duration_ms=round(len(h['audio'])/SR*1000),spectral_group=h['label'],background_db=h['background_db'],low_ratio=h['low_ratio'],high_ratio=h['high_ratio']))
 manifest.write_text(json.dumps(rows,indent=2));print(f'{kit}: {len(rows)} cuts')

def index(root):
 rows=[]
 for manifest in sorted(root.glob('*/Samples/manifest.json')):
  kit=manifest.parent.parent;clips=[]
  for row in sorted(json.loads(manifest.read_text()),key=lambda r:r['file']):
   p=manifest.parent/row['file']
   if not p.exists():continue
   rows.append({'kit':kit.name,**row});a=decode(p);clips.extend([a,np.zeros(24000,np.float32)])
  preview=kit/'Audition.mp3'
  if clips:
   subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(SR),'-ac','1','-i','-','-c:a','libmp3lame','-b:a','192k',str(preview)],input=np.concatenate(clips).astype(np.float32).tobytes(),check=True)
  elif preview.exists():preview.unlink()
 if rows:
  with (root/'Sample-Index.csv').open('w') as f:
   writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
 else:(root/'Sample-Index.csv').write_text('kit,file\n')
 print(f'Indexed {len(rows)} surviving samples')

if __name__=='__main__':
 p=argparse.ArgumentParser();sub=p.add_subparsers(dest='command',required=True)
 c=sub.add_parser('cut');c.add_argument('source',type=Path);c.add_argument('output',type=Path);c.add_argument('--kit',required=True);c.add_argument('--limit',type=int,default=20)
 i=sub.add_parser('index');i.add_argument('root',type=Path)
 args=p.parse_args()
 if args.command=='cut':cut(args.source,args.output,args.kit,args.limit)
 else:index(args.root)
