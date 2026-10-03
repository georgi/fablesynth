# Per-bar check for LAB takes: how much each bar differs from the dry bar 0.
import sys, wave, numpy as np
d = sys.argv[1]; BAR = 4 * 60 / 126
w = wave.open(f'{d}/audio.wav'); sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2) / 32768
m = x.mean(1); side = (x[:, 0] - x[:, 1]) / 2
def spec(s):
    sp = np.abs(np.fft.rfft(s * np.hanning(len(s)))); return 20 * np.log10(np.convolve(sp, np.ones(64) / 64, 'same')[:len(sp) // 2] + 1e-9)
ref = None
for b in range(int(len(m) / sr / BAR)):
    s = m[int(b * BAR * sr):int((b + 1) * BAR * sr)]
    st = s[:len(s) // 16 * 16].reshape(16, -1); rms = 20 * np.log10(np.sqrt((st ** 2).mean(1)) + 1e-9)
    sp = spec(s); ref = sp if ref is None else ref
    sd = 20 * np.log10(np.sqrt((side[int(b * BAR * sr):int((b + 1) * BAR * sr)] ** 2).mean()) + 1e-9)
    print(f'bar {b}: rms {20*np.log10(np.sqrt((s**2).mean())+1e-9):6.1f}  quietest16th {rms.min():6.1f}  side {sd:6.1f}  specdiff {np.abs(sp-ref).mean():5.1f} dB  steps', ' '.join(f'{v:4.0f}' for v in rms))
