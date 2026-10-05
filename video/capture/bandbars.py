# Per-half-bar band energy (dB) of a NEON take on the audible 132 BPM grid.
# usage: python3.11 capture/bandbars.py <take> <first song bar of take bar 0> [from] [to]
import sys, json, wave, numpy as np
n, song0 = sys.argv[1], float(sys.argv[2])
w = wave.open(f'public/takes/{n}/audio.wav'); sr = w.getframerate()
a = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).mean(1) / 32768
m = json.load(open(f'public/takes/{n}/take.json')); off = m['audio'][0]['offsetMs']
BAR = 4 * 60000 / 132; g = m['marks'].get('grid0', m['marks']['bar0'] + 66)
lo, hi = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (0, int((m['dur'] - g) / BAR))
bands = [(20, 150), (150, 1000), (1000, 4000), (4000, 16000)]
print('song bar   ' + '  '.join(f'{a_}-{b_}'.rjust(10) for a_, b_ in bands) + '     rms')
for k in range(lo, hi):
    for h in (0, 0.5):
        s = int((g + (k + h) * BAR - off) / 1000 * sr); x = a[max(0, s):s + int(BAR / 2000 * sr)]
        if len(x) < 2048: continue
        X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / sr)
        e = [10 * np.log10(X[(f >= p) & (f < q)].sum() + 1e-12) for p, q in bands]
        print(f'{k + song0 + h:8.1f}   ' + '  '.join(f'{v:10.1f}' for v in e) + f'  {20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-9):6.1f}')
