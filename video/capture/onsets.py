import sys, wave, numpy as np
d = sys.argv[1]; t0 = float(sys.argv[2]); t1 = float(sys.argv[3])
w = wave.open(f'{d}/audio.wav'); sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).mean(1) / 32768
h = int(0.005 * sr); env = np.array([np.sqrt((x[i:i + h] ** 2).mean()) for i in range(int(t0 * sr), int(t1 * sr), h)])
db = 20 * np.log10(env + 1e-9); rise = np.diff(db)
idx = np.where(rise > 9)[0]
last = -100; out = []
for i in idx:
    if i - last > 20: out.append(round(t0 + (i + 1) * 0.005, 3)); last = i
print(out)
