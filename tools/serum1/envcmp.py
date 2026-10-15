import sys, wave, math, glob, os
def env(p, win=240):  # 5 ms RMS envelope, mono
    w = wave.open(p); d = w.readframes(w.getnframes())
    x = [int.from_bytes(d[i:i + 3], 'little', signed=True) / 8388608 for i in range(0, len(d), 3)]
    m = [(x[i] + x[i + 1]) * 0.5 for i in range(0, len(x) - 1, 2)]
    return [math.sqrt(sum(v * v for v in m[i:i + win]) / win) for i in range(0, len(m) - win, win)]
def times(e):
    on, off = int((256 / 48000) / 0.005), int((1.0 + 256 / 48000) / 0.005)
    seg = e[on:off]; pk = max(seg) or 1e-9
    t10 = next((i for i, v in enumerate(seg) if v >= 0.1 * pk), 0); t90 = next((i for i, v in enumerate(seg) if v >= 0.9 * pk), 0)
    lvl = max(e[off - 2:off + 1]) or 1e-9
    rel = next((i for i, v in enumerate(e[off:off + 240]) if v < lvl * 10 ** (-30 / 20)), 240)
    hold = sum(e[on + 40:off]) / max(1, off - on - 40) / pk  # mean level after 200 ms vs peak
    return (t90 - t10) * 5, rel * 5, hold
if __name__ == '__main__':
    S = sys.argv[1]
    print('%-28s %14s %16s %14s' % ('preset (first note)', 'attack ms S/W', 'release ms S/W', 'body S/W'))
    for p in sorted(glob.glob(S + '/serum/*.wav')):
        n = os.path.basename(p); a, b = times(env(p)), times(env(S + '/wt1/' + n))
        print('%-28s %6d /%6d %7d /%7d %6.2f /%5.2f' % (n[:28], a[0], b[0], a[1], b[1], a[2], b[2]))
