import json, sys, wave, numpy as np
d = sys.argv[1]; step = 0.4
w = wave.open(f'{d}/audio.wav'); sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).mean(1) / 32768
def bands(seg):
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2; f = np.fft.rfftfreq(len(seg), 1 / sr)
    out = []; b = 100.0
    while b < 10000: out.append(10 * np.log10(sp[(f >= b) & (f < b * 1.26)].sum() + 1e-12)); b *= 1.26
    return np.array(out)
ref = bands(x[int(0.4 * sr):int(0.8 * sr)])
vals = json.load(open(f'{d}/values.json')) if len(sys.argv) < 3 else []
for t in np.arange(0, len(x) / sr - step, step):
    seg = x[int(t * sr):int((t + step) * sr)]
    rms = 20 * np.log10(np.sqrt((seg ** 2).mean()) + 1e-9)
    v = [r for r in vals if abs(r['t'] - t * 1000) < 30]
    print(f"{t:4.1f}s dist-from-start {np.sqrt(((bands(seg) - ref) ** 2).mean()):5.1f} dB rms {rms:6.1f}  {v[0] if v else ''}")
