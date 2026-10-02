import json, sys, wave, numpy as np
d = sys.argv[1]; step = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
m = json.load(open(f'{d}/take.json'))
w = wave.open(f'{d}/audio.wav'); sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).mean(1) / 32768
print('marks', m['marks'])
for t in np.arange(0, len(x) / sr - step, step):
    seg = x[int(t * sr):int((t + step) * sr)]
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2; f = np.fft.rfftfreq(len(seg), 1 / sr)
    tot = sp.sum() + 1e-12
    hf = 10 * np.log10(sp[f > 1000].sum() / tot + 1e-12)
    rms = 20 * np.log10(np.sqrt((seg ** 2).mean()) + 1e-9)
    dn = any(p['d'] for p in m['cursor'] if t * 1000 <= p['t'] < (t + step) * 1000)
    print(f"{t:5.2f}s  >1k {hf:6.1f} dB  rms {rms:6.1f} {'DRAG' if dn else ''}")
