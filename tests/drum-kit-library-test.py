import importlib.util,tempfile,wave
from pathlib import Path
import numpy as np
s=importlib.util.spec_from_file_location('c','scripts/drum-kit-library.py');c=importlib.util.module_from_spec(s);s.loader.exec_module(c)
x=np.zeros(48000*2,np.float32)
for t in [.2,.7,1.2]:
 n=6000;y=np.sin(2*np.pi*60*np.arange(n)/48000)*np.exp(-np.arange(n)/1200);x[int(t*48000):int(t*48000)+n]+=y
assert len(c.detect(x))==3
assert c.detect(np.zeros(48000))==[]
for h in c.candidates(x):
 assert np.isfinite(h['audio']).all() and np.max(abs(h['audio']))<.71
 assert h['label']=='LOW'
with tempfile.TemporaryDirectory() as d:
 root=Path(d);src=root/'source';src.mkdir()
 with wave.open(str(src/'drums.audio'),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(48000);w.writeframes((x*30000).astype('<i2').tobytes())
 out=root/'kit/Samples';c.cut(src,out,'test',20);files=list(out.glob('*.wav'));assert files;files[0].unlink();c.cut(src,out,'test',20);assert not files[0].exists();c.index(root)
print('PASS: silence, isolated hits, peak limit, labels, dedup, deletion-safe rerun')
