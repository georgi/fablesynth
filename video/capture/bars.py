import json, sys, wave, numpy as np
d = sys.argv[1]; BAR = 4 * 60 / 126
m = json.load(open(f'{d}/take.json')); play = m['marks'].get('play', 0) / 1000
w = wave.open(f'{d}/audio.wav'); sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).mean(1) / 32768
print('marks', m['marks'])
win = 0.06
for b in range(int((len(x) / sr - play) / BAR)):
    t0 = play + b * BAR
    hfs, rms = [], []
    for t in np.arange(t0, t0 + BAR - win, win):
        seg = x[int(t * sr):int((t + win) * sr)]
        sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2; f = np.fft.rfftfreq(len(seg), 1 / sr)
        hfs.append(10 * np.log10(sp[f > 800].sum() / (sp.sum() + 1e-12) + 1e-12)); rms.append(20 * np.log10(np.sqrt((seg ** 2).mean()) + 1e-9))
    rms = np.array(rms)
    print(f"bar {b} ({t0:4.1f}s): HF mean {np.mean(hfs):6.1f} std {np.std(hfs):4.1f} | rms p10 {np.percentile(rms, 10):6.1f} median {np.median(rms):6.1f} max {rms.max():6.1f}")
