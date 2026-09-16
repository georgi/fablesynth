"""Local reference comparison. Requires numpy, Pillow and ffmpeg; no uploads.
Uses stereo RMS for level matching and mono only for envelope/spectrum analysis.
"""
from pathlib import Path
import subprocess, json
import numpy as np
from PIL import Image, ImageDraw, ImageFont
OUT=Path('build/auditions/reference-study'); SR=24000; D=8*240/111
REF='/Users/mg/Downloads/Deep Hypnotic Groove.mp3'
FON='/System/Library/Fonts/Supplemental/Arial.ttf'
def run(args):return subprocess.run(args,check=True,capture_output=True)
def decode(path,start=0,duration=D):
 return np.frombuffer(run(['ffmpeg','-v','error','-ss',str(start),'-i',str(path),'-t',str(duration),'-f','f32le','-ar',str(SR),'-ac','2','-']).stdout,np.float32).reshape(-1,2).copy()
def db(a):return float(20*np.log10(max(float(a),1e-12)))
def band(x,lo,hi):
 # Smooth frequency-domain band edges, avoiding abrupt brick-wall ringing.
 f=np.fft.rfftfreq(len(x),1/SR)
 w=np.clip((f-lo*.75)/(lo*.25),0,1)*np.clip((hi*1.25-f)/(hi*.25),0,1)
 w=.5-.5*np.cos(np.pi*w)
 return np.fft.irfft(np.fft.rfft(x)*w,n=len(x))
def env(x):
 n=len(x)//120
 return np.sqrt(np.mean(x[:n*120].reshape(n,120)**2,axis=1))
def peaks(e,dist=70):
 candidates=np.where((e[1:-1]>e[:-2])&(e[1:-1]>=e[2:]))[0]+1
 chosen=[]
 for i in sorted(candidates,key=lambda i:e[i],reverse=True):
  if e[i]>.25*max(e) and all(abs(i-j)>=dist for j in chosen):chosen.append(i)
 return sorted(chosen)
labels=['Reference','Before','Candidate'];colors=['#d99123','#718096','#008b88'];audio={};metrics={}
for name,path,start in [('Reference',REF,30),('Before',OUT/'before.wav',0),('Candidate',OUT/'after.wav',0)]:
 x=decode(path,start);rms=np.sqrt(np.mean(x*x));gain=10**((-25-db(rms))/20);x*=gain;audio[name]=x
 out=OUT/(name.lower()+'-matched.wav')
 subprocess.run(['ffmpeg','-v','error','-y','-f','f32le','-ar',str(SR),'-ac','2','-i','-','-c:a','pcm_s24le',str(out)],input=x.astype(np.float32).tobytes(),check=True)
 run(['ffmpeg','-v','error','-y','-i',str(out),'-c:a','libmp3lame','-b:a','192k',str(out.with_suffix('.mp3'))])
 m=x.mean(axis=1);metrics[name]={'source_start_seconds':start,'original_rms_dbfs':round(db(rms),2),'match_gain_db':round(db(gain),2),'matched_peak_dbfs':round(db(np.max(abs(x))),2)}
 for lo,hi in [(35,140),(140,450),(450,2500),(3000,10000)]:
  metrics[name][f'{lo}-{hi}_relative_db']=round(db(np.sqrt(np.mean(band(m,lo,hi)**2)))-db(np.sqrt(np.mean(m*m))),2)
 # Same four-bar full-kit duration, sample rate, FFT/window, gain and dB legend.
 run(['ffmpeg','-v','error','-y','-ss',str(D/2),'-i',str(out),'-t',str(D/2),'-lavfi','showspectrumpic=s=1000x300:legend=1:scale=log:fscale=log:color=viridis:win_func=hann:drange=80','-frames:v','1',str(OUT/(name.lower()+'-spectrum.png'))])
# Concatenation uses only constant gain; no limiting or time stretching.
run(['ffmpeg','-v','error','-y','-i',str(OUT/'reference-matched.wav'),'-i',str(OUT/'before-matched.wav'),'-i',str(OUT/'candidate-matched.wav'),'-filter_complex','[0:a][1:a][2:a]concat=n=3:v=0:a=1[out]','-map','[out]','-c:a','libmp3lame','-b:a','192k',str(OUT/'reference-before-candidate.mp3')])
font=lambda n:ImageFont.truetype(FON,n)
# Assemble ffmpeg scientific spectrograms without rescaling their colour ranges.
imgs=[Image.open(OUT/(n.lower()+'-spectrum.png')).convert('RGB') for n in labels]
w=max(i.width for i in imgs);canvas=Image.new('RGB',(w,90+sum(i.height+50 for i in imgs)), 'white');draw=ImageDraw.Draw(canvas)
draw.text((20,12),'Same-scale spectrograms | four-bar excerpts',font=font(26),fill='black');draw.text((20,48),'Stereo RMS matched to -25 dBFS. Reference is a full mix; studies are drums + bass.',font=font(17),fill='#444444');y=90
for name,im in zip(labels,imgs):
 draw.text((20,y),name+(' | reference 38.65–47.30 s' if name=='Reference' else ' | full kit section'),font=font(21),fill='black');canvas.paste(im,(0,y+36));y+=im.height+50
canvas.save(OUT/'spectrograms.png')
# Peak-aligned median envelopes: reference opening has the most regular low pulses.
curves={};widths={}
for name,path in [('Reference',REF),('Before',OUT/'before-drums.wav'),('Candidate',OUT/'after-drums.wav')]:
 x=decode(path,0,D/2).mean(axis=1);e=env(band(x,35,140));p=peaks(e);segments=[]
 for i in p:
  if i>=16 and i+70<len(e):segments.append(e[i-16:i+70]/max(e[i],1e-10))
 curve=np.median(segments,axis=0);curves[name]=curve
 above=np.where(curve>=.5)[0];widths[name]={'median_half_amplitude_width_ms':int((above[-1]-above[0]+1)*5),'pulses':len(segments)}
canvas=Image.new('RGB',(1200,660),'white');dr=ImageDraw.Draw(canvas)
dr.text((35,20),'Low-frequency pulse envelopes | 35–140 Hz',font=font(28),fill='black')
dr.text((35,60),'Median of peak-aligned hits; each hit normalized to its own peak. 5 ms RMS windows.',font=font(18),fill='#444444')
dr.text((35,89),'Reference: opening full mix. Before/candidate: isolated kick. Not a recovered drum stem.',font=font(18),fill='#444444')
x0,y0,w,h=90,145,1060,390
for v in [0,.25,.5,.75,1]:
 yy=y0+h*(1-v);dr.line((x0,yy,x0+w,yy),fill='#dddddd');dr.text((35,yy-10),str(v),font=font(16),fill='#444444')
for t in [-80,0,100,200,300]:
 xx=x0+(t+80)/425*w;dr.line((xx,y0,xx,y0+h),fill='#dddddd');dr.text((xx-15,y0+h+10),str(t),font=font(16),fill='#444444')
for name,color in zip(labels,colors):
 pts=[(x0+j/85*w,y0+h*(1-min(1.05,float(v)))) for j,v in enumerate(curves[name])];dr.line(pts,fill=color,width=4)
for i,(name,col) in enumerate(zip(labels,colors)):
 dr.text((95+i*365,592),f'{name}: width {widths[name]["median_half_amplitude_width_ms"]} ms',font=font(20),fill=col)
dr.text((460,557),'Time from pulse peak (ms)',font=font(18),fill='#444444');canvas.save(OUT/'envelopes.png')
metrics['envelope']=widths
(OUT/'measurements.json').write_text(json.dumps(metrics,indent=2))
print(json.dumps(metrics,indent=2))
