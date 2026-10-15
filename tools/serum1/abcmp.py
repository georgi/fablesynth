import sys, wave, struct, math, glob, os
def load(p):
    w = wave.open(p); n = w.getnframes(); sw = w.getsampwidth(); d = w.readframes(n)
    x = [int.from_bytes(d[i:i + 3], 'little', signed=True) / 8388608 for i in range(0, len(d), 3)]
    return x[0::2], x[1::2]
def feats(L, R, win=2400):
    out = []
    for i in range(0, len(L) - win, win):
        m = [(L[j] + R[j]) * 0.5 for j in range(i, i + win, 2)]
        e = math.sqrt(sum(v * v for v in m) / len(m)) + 1e-9
        dd = math.sqrt(sum((m[k] - m[k - 1]) ** 2 for k in range(1, len(m))) / len(m))
        out.append((20 * math.log10(e), dd / e))
    return out
def corr(a, b):
    ma, mb = sum(a) / len(a), sum(b) / len(b)
    num = sum((x - ma) * (y - mb) for x, y in zip(a, b)); den = math.sqrt(sum((x - ma) ** 2 for x in a) * sum((y - mb) ** 2 for y in b)) or 1
    return num / den
if __name__ == '__main__':
    S = sys.argv[1]
    print('%-34s %7s %7s %6s %6s %6s %5s' % ('preset', 'serumdB', 'wt1dB', 'envCor', 'brtS', 'brtW', 'peakW'))
    for p in sorted(glob.glob(S + '/serum/*.wav')):
        n = os.path.basename(p); q = S + '/wt1/' + n
        a, b = load(p), load(q)
        fa, fb = feats(*a), feats(*b)
        k = min(len(fa), len(fb)); fa, fb = fa[:k], fb[:k]
        act = [i for i in range(k) if fa[i][0] > -60]
        lv = lambda f: 10 * math.log10(sum(10 ** (f[i][0] / 10) for i in act) / max(1, len(act)))
        br = lambda f: sum(f[i][1] for i in act) / max(1, len(act))
        pk = max(max(abs(v) for v in b[0]), max(abs(v) for v in b[1]))
        print('%-34s %7.1f %7.1f %6.2f %6.3f %6.3f %5.2f' % (n[:34], lv(fa), lv(fb), corr([fa[i][0] for i in range(k)], [fb[i][0] for i in range(k)]), br(fa), br(fb), pk))
