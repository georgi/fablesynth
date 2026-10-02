# Per-video-frame log-band spectra (30 fps) of each normalized take, for the visualizer.
import json, os, wave, numpy as np
root = os.path.join(os.path.dirname(__file__), '..', 'public', 'takes')
FPS, NB = 30, 48
edges = np.geomspace(35, 16000, NB + 1)
for n in sorted(os.listdir(root)):
    p = os.path.join(root, n, 'norm.wav')
    if not os.path.exists(p): continue
    w = wave.open(p); sr = w.getframerate(); ch = w.getnchannels(); sw = w.getsampwidth()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16 if sw == 2 else np.int32).reshape(-1, ch).mean(1)
    x = x / (32768 if sw == 2 else 2 ** 31)
    N = 4096; win = np.hanning(N); f = np.fft.rfftfreq(N, 1 / sr)
    bins = [np.where((f >= edges[i]) & (f < edges[i + 1]))[0] for i in range(NB)]
    frames, rms = [], []
    for k in range(int(len(x) / sr * FPS)):
        c = int(k / FPS * sr); seg = x[max(0, c - N // 2):c + N // 2]
        if len(seg) < N: seg = np.pad(seg, (0, N - len(seg)))
        sp = np.abs(np.fft.rfft(seg * win)) / (N / 4)
        db = [20 * np.log10(sp[b].max() + 1e-9) if len(b) else -120 for b in bins]
        frames.append([int(max(0, min(100, (v + 80) * 1.25))) for v in db])
        r = x[max(0, c - 735):c + 735]
        rms.append(round(float(20 * np.log10(np.sqrt((r ** 2).mean()) + 1e-9)), 1))
    json.dump({'fps': FPS, 'bands': frames, 'rms': rms}, open(os.path.join(root, n, 'spec.json'), 'w'), separators=(',', ':'))
    print(n, len(frames))
