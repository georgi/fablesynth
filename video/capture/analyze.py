import json, sys, wave, numpy as np
d = sys.argv[1]
m = json.load(open(f'{d}/take.json'))
w = wave.open(f'{d}/audio.wav'); sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).mean(1) / 32768
print('marks', m['marks'], 'frames', len(m['times']))
step = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
for t in np.arange(0, m['dur'] / 1000 - step, step):
    seg = x[int(t * sr):int((t + step) * sr)]
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))); f = np.fft.rfftfreq(len(seg), 1 / sr)
    c = (sp * f).sum() / max(sp.sum(), 1e-9); rms = 20 * np.log10(np.sqrt((seg ** 2).mean()) + 1e-9)
    dn = any(p['d'] for p in m['cursor'] if t * 1000 <= p['t'] < (t + step) * 1000)
    print(f"{t:5.2f}s centroid {c:6.0f} Hz  rms {rms:6.1f} dB {'DRAG' if dn else ''}")
