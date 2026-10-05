# Finds the audible 132 BPM grid of a NEON take: spectral-flux onsets, then the
# circular mean of their phase on the 16th grid near the UI 'bar0' tick.
# Writes marks.grid0 (take ms of audible song bar 0 of the take) into take.json.
# --kick uses the downbeat kicks instead (for takes whose hats sit off the 16th grid).
# usage: python3.11 capture/grid.py [--kick] neon-intro [neon-build ...]
import sys, json, wave, numpy as np
STEP = 60000 / 132 / 4
KICK = '--kick' in sys.argv
def kick_lag(a, sr, m):
    X = np.fft.rfft(a); f = np.fft.rfftfreq(len(a), 1 / sr); X[f > 120] = 0
    env = np.convolve(np.fft.irfft(X, len(a)) ** 2, np.ones(64) / 64, 'same')
    errs = []
    for k in range(int((m['dur'] - m['marks']['bar0']) / (4 * STEP))):
        exp = m['marks']['bar0'] + k * 4 * STEP; s = int((exp - m['audio'][0]['offsetMs'] - 20) / 1000 * sr); x = env[s:s + int(.15 * sr)]
        if len(x) and x.max() > env.max() * 0.15: errs.append((s + np.argmax(x > x.max() * 0.2)) / sr * 1000 + m['audio'][0]['offsetMs'] - exp)
    errs = np.array(errs); med = np.median(errs); errs = errs[abs(errs - med) < 15]
    return float(np.median(errs)), len(errs), float(np.ptp(errs))
for n in [x for x in sys.argv[1:] if not x.startswith('--')]:
    p = f'public/takes/{n}/take.json'; m = json.load(open(p))
    w = wave.open(f'public/takes/{n}/audio.wav'); sr = w.getframerate()
    a = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).mean(1) / 32768
    if KICK:
        lag, cnt, spread = kick_lag(a, sr, m)
        m['marks']['grid0'] = round(m['marks']['bar0'] + lag, 1); json.dump(m, open(p, 'w'))
        print(f'{n}: kick lag {lag:.1f} ms, {cnt} beats, spread {spread:.1f} ms'); continue
    hop, N = 128, 1024
    frames = np.lib.stride_tricks.sliding_window_view(a, N)[::hop] * np.hanning(N)
    S = np.log1p(np.abs(np.fft.rfft(frames, axis=1)) * 10)[:, int(1500 / sr * N):]  # transients only
    flux = np.maximum(0, np.diff(S, axis=0)).sum(1)
    t = (np.arange(len(flux)) * hop + N / 2) / sr * 1000 + m['audio'][0]['offsetMs']
    thr = np.percentile(flux, 95)
    peaks = [i for i in range(1, len(flux) - 1) if flux[i] > thr and flux[i] >= flux[i - 1] and flux[i] >= flux[i + 1]]
    on = t[peaks]; wt = flux[peaks]
    ph = np.angle((wt * np.exp(2j * np.pi * (on - m['marks']['bar0']) / STEP)).sum()) / (2 * np.pi) * STEP
    lag = ph if ph > -STEP / 4 else ph + STEP  # audio follows the UI tick
    resid = ((on - m['marks']['bar0'] - lag + STEP / 2) % STEP) - STEP / 2
    m['marks']['grid0'] = round(m['marks']['bar0'] + lag, 1)
    json.dump(m, open(p, 'w'))
    print(f'{n}: lag {lag:.1f} ms, {len(on)} onsets, |residual| median {np.median(abs(resid)):.1f} ms')
