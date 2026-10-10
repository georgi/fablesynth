"""Serum 1 (.fxp) -> WT-1 user preset converter.

Inputs per preset: the s1host parameter dump (display text per Serum parameter),
the raw fxp chunk (mod matrix, table and noise names), and the s1host wavetable
capture (64 single cycles of 2048 samples across WTPos, per enabled oscillator).
Output: one WT-1 Preset JSON ({name, params, tables}) plus a list of losses.

usage: convert.py <dumpDir> <captureDir> <outDir>
"""
import base64, glob, json, math, os, re, struct, sys, zlib

FRAME = 2048
MASTER_TRIM = 1.0  # global loudness calibration against Serum renders
NUM_FACTORY_TABLES = 6  # WT-1 TABLE_NAMES; user tables index from here
VST_NAMES = None        # Serum parameter names by VST index (from the dump)

# ---------------------------------------------------------------- helpers
def num(text):
    m = re.search(r'-?\d+(?:\.\d+)?', text.replace('−', '-'))
    return float(m.group()) if m else 0.0

def db(text):
    if 'oo' in text or '�' in text or '￢' in text or '-∞' in text:
        return None
    m = re.search(r'\((-?[\d.]+) dB\)', text) or re.search(r'(-?[\d.]+) dB', text)
    return float(m.group(1)) if m else None

def amp_of_db(d):
    return 0.0 if d is None else 10 ** (d / 20)

def seconds(text):
    v = num(text)
    if 'ms' in text:
        return v / 1000
    if text.strip().endswith('s') or ' s' in text:
        return v
    return v / 1000  # Serum shows bare numbers in ms below 1 s

def clamp(x, lo, hi):
    return max(lo, min(hi, x))

def pct(text):
    return num(text) / 100

# ---------------------------------------------------------------- fxp chunk
def fxp_chunk(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('>I', d, 56)[0]
    z = zlib.decompressobj()
    return z.decompress(d[60:60 + n])

def cstr(raw, off):
    return raw[off:off + 512].split(b'\0')[0].decode('latin1')

def mod_slots(raw):
    out = []
    for k in range(16):
        m = 32 + 40 * k
        assert raw[m + 2] == k and raw[m + 3] == 0xff
        src = struct.unpack_from('<i', raw, m - 12)[0]
        if not src & 0xffff:
            continue
        out.append({
            'slot': k + 1,
            'src': src & 0xffff,
            'aux': (src >> 16) & 0xffff,
            'amt': struct.unpack_from('<f', raw, m - 28)[0],
            'dest': struct.unpack_from('<H', raw, m - 6)[0],
            'flag': raw[m + 1],
        })
    return out

# ---------------------------------------------------------------- mappings
# Serum source id -> WT-1 MOD_SOURCES index. Guessed ids are flagged.
# Measured with `s1host srcprobe` (each id routed to A Vol in Serum itself).
# Env 1 (2) maps to MOD ENV 2 when Serum's Env 3 is free (see convert()).
SRC_MAP = {1: 13, 2: None, 3: 3, 4: 8, 5: 1, 6: 2, 7: 6, 8: 7, 9: 4, 10: 5, 11: 14,
           12: 15, 13: 15, 15: 15, 16: 15, 17: 9, 18: 10, 19: 11, 20: 12}
SRC_APPROX = {12: 'CHAOS 1 -> RANDOM (per note)', 13: 'CHAOS 2 -> RANDOM (per note)'}
SRC_NAME = {1: 'MOD WHEEL', 2: 'ENV 1', 3: 'ENV 2', 4: 'ENV 3', 5: 'LFO 1', 6: 'LFO 2', 7: 'LFO 3', 8: 'LFO 4',
            9: 'VELO', 10: 'NOTE', 11: 'AFTERTOUCH', 12: 'CHAOS 1', 13: 'CHAOS 2', 15: 'NOTEON RAND 1',
            16: 'NOTEON RAND 2', 17: 'MACRO 1', 18: 'MACRO 2', 19: 'MACRO 3', 20: 'MACRO 4', 21: 'PITCH BEND'}
LFO_SOURCES = {1, 2, 6, 7}  # WT-1 indices; Serum LFOs modulate unipolar by default

CUT_OCT = math.log2(22050 / 8)  # Serum cutoff knob span in octaves
# Serum destination name -> (WT-1 MOD_DESTS index, amount scale)
DEST_MAP = {
    'A WTPos': (1, 1), 'B WTPos': (2, 1), 'Fil Cutoff': (3, CUT_OCT / 5),
    'Mast.Tun': (4, 1 / 12), 'A CoarsePit': (4, 64 / 12), 'B CoarsePit': (4, 64 / 12),
    'Amp.': (5, 1), 'A Vol': (7, 1), 'B Vol': (8, 1), 'A UniDet': (11, 1), 'A Uni LR': (12, 1),
    'A Pan': (13, 1), 'B UniDet': (14, 1), 'B Uni LR': (15, 1), 'B Pan': (16, 1),
    'Fil Reso': (17, 0.9), 'Fil Driv': (18, 1), 'Sub Osc Level': (24, 1), 'Noise Level': (25, 1),
    'A UniBlend': (26, 1), 'B UniBlend': (27, 1),
}
# Per-osc pitch routes collapse onto global PITCH; only safe when the other
# oscillator is off.
PITCH_PER_OSC = {'A CoarsePit': 'B', 'B CoarsePit': 'A'}

FILTER_TYPES = ['LP 12', 'LP 24', 'BP 12', 'HP 12', 'NOTCH', 'COMB', 'VOWEL']

def filter_type(name, var):
    """Serum filter name -> (WT-1 FILTER_TYPES index or None, exact?)."""
    n = name
    if n in ('Low 12', 'LP 12', 'MG Low 12'):
        return 0, True
    if n in ('MG Low 6', 'Low 6'):
        return 0, False
    if n in ('Low 24', 'MG Low 24'):
        return 1, True
    if n in ('Low 18', 'MG Low 18', 'German LP', 'French LP', 'Add Bass'):
        return 1, False
    if n in ('High 12', 'HP 12'):
        return 3, True
    if n.startswith('High'):
        return 3, False
    if n in ('Band 12', 'BP 12'):
        return 2, True
    if n in ('Band 24', 'Band EQ 12', 'Peak 24', 'LH 12', 'HB 12', 'PP 12'):
        return 2, False
    if n.startswith('Notch') or n in ('NN 12', 'BN 12', 'HN 12', 'LN 12'):
        return 4, n.startswith('Notch')
    if n.startswith(('L/B/H', 'L/P/H', 'L/N/H', 'B/P/N')):
        if n.startswith('B/P/N'):
            return (2 if var < 1 / 3 else 2 if var < 2 / 3 else 4), False
        mid = 4 if n.startswith('L/N/H') else 2
        return (0 if var < 1 / 3 else mid if var < 2 / 3 else 3), False
    if n.startswith(('Cmb', 'Combs', 'Flg')):
        return 5, False
    if n.startswith('Formant'):
        return 6, False
    return None, False  # phasers, EQ shapes, reverb, ring mod, S&H

LFO_DIVS = [('1/1', 0.25), ('1/2', 0.5), ('1/4', 1), ('1/4.', 2 / 3), ('1/4T', 1.5), ('1/8', 2),
            ('1/8.', 4 / 3), ('1/8T', 3), ('1/16', 4), ('1/16T', 6), ('1/32', 8)]

def cycles_per_beat(text):
    """Serum tempo-synced rate text -> cycles per quarter note, or None."""
    t = text.strip()
    if t == 'fast':
        return None
    mul = 1.0
    if t.endswith(' t'):
        mul, t = 1.5, t[:-2]
    if t.endswith('.'):
        mul, t = mul * 2 / 3, t[:-1]
    if t.endswith('bar'):
        bars = num(t) if re.search(r'\d', t) else 1
        return mul / (4 * bars)
    m = re.fullmatch(r'1/(\d+)', t)
    return mul * int(m.group(1)) / 4 if m else None

def lfo_params(pre, text, losses, n):
    out = {}
    if text.endswith('Hz'):
        out[pre + '.sync'] = 0
        out[pre + '.rate'] = clamp(num(text), 0.02, 30)
        return out
    if text == 'fast':
        out[pre + '.sync'] = 0; out[pre + '.rate'] = 30
        return out
    cpb = cycles_per_beat(text)
    if cpb is None:
        losses.append(f'LFO {n} rate "{text}" unparsed')
        return out
    if cpb < 0.25:  # slower than one bar: WT-1 divisions stop at 1/1
        out[pre + '.sync'] = 0
        out[pre + '.rate'] = clamp(cpb * 2, 0.02, 30)  # at 120 BPM
        losses.append(f'LFO {n} {text} -> free {out[pre + ".rate"]:.3f} Hz (120 BPM)')
        return out
    best = min(range(len(LFO_DIVS)), key=lambda i: abs(math.log(LFO_DIVS[i][1] / cpb)))
    out[pre + '.sync'] = 1
    out[pre + '.syncrate'] = best
    if abs(math.log(LFO_DIVS[best][1] / cpb)) > 0.01:
        losses.append(f'LFO {n} {text} -> {LFO_DIVS[best][0]}')
    return out

DELAY_DIVS = [('1/4', 1), ('1/4.', 2 / 3), ('1/8', 2), ('1/8.', 4 / 3), ('1/8T', 3), ('1/16', 4)]

# ---------------------------------------------------------------- tables
def table_from_capture(path, name):
    d = open(path, 'rb').read()
    x = struct.unpack('<%df' % (len(d) // 4), d)
    frames = [x[i:i + FRAME] for i in range(0, len(x), FRAME)]
    peak = max(max(abs(v) for v in f) for f in frames) or 1.0
    if all(max(abs(a - b) for a, b in zip(f[::8], frames[0][::8])) < 2e-3 * peak for f in frames):
        frames = frames[:1]  # static table: capture noise only
    frames = [[v / peak for v in f] for f in frames]
    flat = struct.pack('<%df' % (len(frames) * FRAME), *[v for f in frames for v in f])
    return {'v': 1, 'name': name[:24], 'frames': len(frames), 'wave': base64.b64encode(flat).decode()}, peak

def sub_table(shape):
    """Single-cycle frame for a Serum sub shape (peak 1)."""
    f = []
    for i in range(FRAME):
        x = i / FRAME
        if shape == 'Sine': v = math.sin(2 * math.pi * x)
        elif shape == 'Triangle': v = 1 - 4 * abs(x - 0.5) if False else (4 * x if x < 0.25 else 2 - 4 * x if x < 0.75 else 4 * x - 4)
        elif shape == 'Saw': v = 1 - 2 * x
        elif shape == 'Square': v = 1.0 if x < 0.5 else -1.0
        elif shape == 'Pulse': v = 1.0 if x < 0.25 else -1.0
        else: v = math.tanh(4 * math.sin(2 * math.pi * x)) / math.tanh(4)  # RoundRect
        f.append(v)
    flat = struct.pack('<%df' % FRAME, *f)
    return {'v': 1, 'name': 'SUB ' + shape.upper(), 'frames': 1, 'wave': base64.b64encode(flat).decode()}

# ---------------------------------------------------------------- convert
def convert(dump, cap_dir, idx):
    P = {p['name']: p['text'].strip() for p in dump['params']}
    V = {p['name']: p['v'] for p in dump['params']}
    raw = fxp_chunk(dump['file'])
    name = os.path.basename(dump['file'])[:-4]
    losses, out, tables = [], {}, []

    # Oscillators A/B
    for X, pre in (('A', 'oscA'), ('B', 'oscB')):
        on = P[f'Osc {X} On'] == 'on'
        out[pre + '.on'] = 1 if on else 0
        if not on:
            continue
        cap = os.path.join(cap_dir, f'{idx:04d}_{X}.f32')
        if os.path.exists(cap):
            tname = cstr(raw, 15368 if X == 'A' else 15880).split('/')[-1].rsplit('.', 1)[0] or f'{name[:16]} {X}'
            t, _ = table_from_capture(cap, tname)
            out[pre + '.table'] = NUM_FACTORY_TABLES + len(tables)
            tables.append(t)
        else:
            losses.append(f'osc {X}: no capture')
        out[pre + '.pos'] = V[f'{X} WTPos']
        out[pre + '.oct'] = clamp(num(P[f'{X} Octave']), -3, 3)
        out[pre + '.semi'] = clamp(num(P[f'{X} Semi']), -12, 12)
        out[pre + '.fine'] = clamp(num(P[f'{X} Fine']), -100, 100)
        out[pre + '.unison'] = clamp(int(num(P[f'{X} Unison'])), 1, 16)
        out[pre + '.detune'] = clamp(num(P[f'{X} UniDet']), 0, 1)
        out[pre + '.blend'] = clamp(pct(P[f'{X} UniBlend']), 0, 1)
        out[pre + '.spread'] = clamp(pct(P[f'{X} Uni LR']), 0, 1)
        # Serum amplitude = knob^2; keep the default 75% at WT-1's default 0.75.
        out[pre + '.level'] = clamp(0.75 * V[f'{X} Vol'] ** 2 / 0.75 ** 2, 0, 1)
        out[pre + '.pan'] = clamp(num(P[f'{X} Pan']) / 50, -1, 1)
        warp = P.get(f'WarpOsc{X}', 'Off')
        if warp != 'Off':
            losses.append(f'osc {X} warp {warp}: static shape baked, modulation lost')

    # Sub. Serum's sub plays at note + octave (0 = note pitch) with any of six
    # shapes; a free WT-1 oscillator reproduces it exactly, WT-1's own sub
    # (sine/square at -1/-2 oct) is the fallback.
    sub_slot = None
    out['sub.on'] = 0
    if P['Osc S On'] == 'on':
        shape, octv = P['SubOscShape'], int(num(P['SubOscOctave']))
        free = 'B' if not out.get('oscB.on') else ('A' if not out.get('oscA.on') else None)
        if free:
            sub_slot = free; pre = 'osc' + free
            out.update({pre + '.on': 1, pre + '.table': NUM_FACTORY_TABLES + len(tables), pre + '.pos': 0,
                        pre + '.oct': clamp(octv, -3, 3), pre + '.semi': 0, pre + '.fine': 0, pre + '.unison': 1,
                        pre + '.pan': clamp(num(P['Sub Osc Pan']) / 50, -1, 1),
                        # Serum sub RMS is linear in its knob (s1host mixprobe).
                        pre + '.level': clamp(0.879 * V['Sub Osc Level'], 0, 1)})
            tables.append(sub_table(shape))
        else:
            out['sub.on'] = 1
            out['sub.shape'] = 1 if shape in ('Square', 'Pulse', 'Saw') else 0
            out['sub.oct'] = clamp(octv, -2, -1)
            if shape not in ('Sine', 'Square') or octv not in (-1, -2):
                losses.append(f'sub {shape} {octv:+d} oct -> {"SQR" if out["sub.shape"] else "SINE"} {out["sub.oct"]:+d} oct (both oscillators in use)')
        # Measured (s1host mixprobe): Serum sub RMS is linear in its knob, WT-1's
        # sub gain is level^2 * 0.3; matched against the oscillator law.
        out['sub.level'] = clamp(0.968 * math.sqrt(V['Sub Osc Level']), 0, 1)
    out['noise.on'] = 1 if P['Osc N On'] == 'on' else 0
    if out['noise.on']:
        nname = cstr(raw, 16392)
        out['noise.type'] = 1 if 'pink' in nname.lower() else 0
        # Serum noise RMS = 0.086 v^2; WT-1 noise gain is level^2 * 0.35.
        out['noise.level'] = clamp(0.662 * V['Noise Level'], 0, 1)
        losses.append(f'noise sample "{nname}" -> {"PINK" if out["noise.type"] else "WHITE"}')

    # Filter. Serum routes each oscillator to the filter or around it. WT-1
    # SPLIT sends osc A (+ sub, noise) through filter 1 and osc B through
    # filter 2, so a bypassed oscillator rides the path whose filter is off.
    on = {'A': out.get('oscA.on'), 'B': out.get('oscB.on'), 'N': out.get('noise.on'), 'S': out.get('sub.on')}
    fil = {o: P[f'Osc{o}>Fil'] == 'on' for o in 'ABNS'}
    if sub_slot:
        fil[sub_slot] = fil['S']  # the sub now lives in that oscillator
    routed = {o for o in 'ABNS' if on[o] and fil[o]}
    fpre = None
    if P['Filter On'] == 'on' and routed:
        a_path = routed & {'A', 'N', 'S'}
        if 'B' not in routed and on['B']:
            fpre = 'filter'; out['filter.route'] = 2; out['filter2.on'] = 0
        elif 'B' in routed and not a_path:
            fpre = 'filter2'; out['filter.route'] = 2; out['filter.on'] = 0
        else:
            fpre = 'filter'; out['filter.route'] = 0
        path = {'filter': {'A', 'N', 'S'}, 'filter2': {'B'}}[fpre] if out['filter.route'] == 2 else {'A', 'B', 'N', 'S'}
        stray = {o for o in path if on[o]} - routed
        if stray:
            losses.append(f'filter: {"".join(sorted(stray))} bypass not reproducible (filtered too)')
        if out['filter.route'] == 2 and fpre == 'filter2' and routed - {'B'}:
            losses.append('filter: A-path oscillators routed but left dry')
    if fpre:
        out[fpre + '.on'] = 1
        ft, exact = filter_type(P['Fil Type'], pct(P['Fil Var']))
        if ft is None:
            losses.append(f'filter {P["Fil Type"]} not available: filter off')
            out[fpre + '.on'] = 0; ft = 1
        elif not exact:
            losses.append(f'filter {P["Fil Type"]} -> {FILTER_TYPES[ft]}')
        out[fpre + '.type'] = ft
        out[fpre + '.cutoff'] = clamp(num(P['Fil Cutoff']), 20, 20000)
        out[fpre + '.res'] = clamp(pct(P['Fil Reso']) * 0.9, 0, 1)
        out[fpre + '.drive'] = clamp(pct(P['Fil Driv']), 0, 1)
        out[fpre + '.env'] = 0
        out[fpre + '.key'] = 0
    else:
        out['filter.on'] = 0
    # Filter routes follow the filter that stands in for Serum's.
    retarget = {3: 9, 17: 10, 18: 21} if fpre == 'filter2' else {}

    # Envelopes
    def env(n, pre):
        # Serum's time text is ambiguous ("620 s" means 620 ms); the knob law
        # measured with `s1host ranges` is exact: t = 32 s * v^5.
        a, h, d, r = (32 * V[f'Env{n} {k}'] ** 5 for k in ('Atk', 'Hold' if n == 1 else 'Hld', 'Dec', 'Rel'))
        sus = amp_of_db(db(P[f'Env{n} Sus'])) if n == 1 else pct(P[f'Env{n} Sus'])
        # CURVE model: Serum-shaped segments with hold and stored curves.
        out[pre + '.model'] = 1
        out[pre + '.h'] = clamp(h, 0, 8)
        out[pre + '.ac'] = V[f'A curve{n}']
        out[pre + '.dc'] = V[f'D curve{n}']
        out[pre + '.rc'] = V[f'R curve{n}']
        out[pre + '.a'] = clamp(a, 0.001, 8)
        out[pre + '.d'] = clamp(d, 0.005, 10)
        out[pre + '.s'] = clamp(sus, 0, 1)
        out[pre + '.r'] = clamp(r, 0.005, 12)
    env(1, 'env1'); env(2, 'env2'); env(3, 'env3')
    slots = mod_slots(raw)
    env1_src = any(m['src'] == 2 or m['aux'] == 2 for m in slots)
    env3_used = any(m['src'] == 4 or m['aux'] == 4 for m in slots)
    src_map = dict(SRC_MAP)
    if env1_src and not env3_used:
        for k in ('a', 'd', 's', 'r', 'model', 'h', 'ac', 'dc', 'rc'):
            out['env3.' + k] = out['env1.' + k]
        src_map[2] = 8  # Env 1 copied into MOD ENV 2

    # LFOs (shape data is not decoded: Serum's default triangle)
    for n in range(1, 5):
        pre = f'lfo{n}'
        out.update(lfo_params(pre, P[f'LFO{n} Rate'], losses, n))
        out[pre + '.shape'] = 1
        rise = P[f'LFO{n} Rise']
        if rise not in ('Off', '0.0 s'):
            cpb = cycles_per_beat(rise)
            out[pre + '.rise'] = clamp(seconds(rise) if cpb is None else 0.5 / cpb, 0, 5)

    # Mod matrix
    routes = []
    note_offsets = []
    slot = 1
    for m in slots:
        sname = SRC_NAME.get(m['src'], f'src{m["src"]}')
        wsrc = src_map.get(m['src'])
        dname = VST_NAMES[m['dest']] if m['dest'] < len(VST_NAMES) else f'dest{m["dest"]}'
        if wsrc is None:
            losses.append(f'route {sname} -> {dname}: source not available')
            continue
        if dname not in DEST_MAP:
            losses.append(f'route {sname} -> {dname}: destination not available')
            continue
        if dname in PITCH_PER_OSC and out.get('osc' + PITCH_PER_OSC[dname] + '.on'):
            losses.append(f'route {sname} -> {dname}: per-osc pitch applied to both oscillators')
        dst, scale = DEST_MAP[dname]
        if dname == 'Sub Osc Level' and sub_slot:
            dst = 7 if sub_slot == 'A' else 8
        dst = retarget.get(dst, dst)
        outn = num(P['Mod%2d out' % m['slot'] if m['slot'] >= 10 else 'Mod %d out' % m['slot']]) / 100
        amt = m['amt'] * scale * outn
        if m['src'] in SRC_APPROX:
            losses.append(f'route {sname} -> {dname}: {SRC_APPROX[m["src"]]}')
        if m['src'] == 10:
            # Serum NOTE = n/127 (unipolar); WT-1 NOTE = (n - 60)/24. Match the
            # slope, then fold Serum's value at note 60 into the base below.
            note_offsets.append((dst, amt * 60 / 127))
            amt *= 24 / 127
        aux = src_map.get(m['aux']) if m['aux'] else 0
        if m['aux'] and not aux:
            losses.append(f'route {sname} -> {dname}: aux src{m["aux"]} not available')
        routes.append((wsrc, dst, amt, aux or 0, f'{sname} -> {dname}'))
    # NOTE routes: Serum's value at note 60 shifts the destination's base.
    base_of = {1: 'oscA.pos', 2: 'oscB.pos', 3: 'filter.cutoff', 9: 'filter2.cutoff', 7: 'oscA.level', 8: 'oscB.level',
               17: 'filter.res', 10: 'filter2.res', 24: 'sub.level', 25: 'noise.level'}
    for dst, off in note_offsets:
        key = base_of.get(dst)
        if key is None or key not in out:
            continue
        if key.endswith('cutoff'):
            out[key] = clamp(out[key] * 2 ** (off * 5), 20, 20000)
        else:
            out[key] = clamp(out[key] + off, 0, 1)
    # Routes sum, so a depth beyond +-1 spreads over extra slots while free.
    need = [max(1, math.ceil(abs(r[2]) - 1e-9)) for r in routes]
    while sum(need) > 16 and any(n > 1 for n in need):
        need[max(range(len(need)), key=lambda i: need[i])] -= 1
    for (wsrc, dst, amt, aux, label), n in zip(routes, need):
        if abs(amt) > n:
            losses.append(f'route {label}: depth {amt:+.2f} clipped to {n * (1 if amt > 0 else -1):+d}')
        part = clamp(amt / n, -1, 1)
        for _ in range(n):
            if slot > 16:
                losses.append(f'route {label}: no free slot'); break
            out[f'mat{slot}.src'] = wsrc
            out[f'mat{slot}.dst'] = dst
            out[f'mat{slot}.amt'] = part
            out[f'mat{slot}.pol'] = 1 if wsrc in LFO_SOURCES else 0
            out[f'mat{slot}.aux'] = aux
            slot += 1

    # Macros and performance
    for n in range(1, 5):
        out[f'macro{n}'] = clamp(pct(P[f'Macro {n}']), 0, 1)
    out['perf.modwheel'] = clamp(V['Mod Wheel'], 0, 1)
    out['master.glide'] = clamp(8 * V['PortTime'] ** 5, 0, 0.5)  # knob law t = 8 s * v^5
    # Voice mode lives in the chunk's internal float block (validated with
    # `s1host polyprobe`): 14344 MONO, 14348 LEGATO, 14372 voice count (0 = 1
    # voice, which plays like mono with retrigger).
    mono_flag, legato_flag, voices = (struct.unpack_from('<f', raw, o)[0] for o in (14344, 14348, 14372))
    out['master.mono'] = 1 if mono_flag >= 0.5 or voices == 0.0 else 0
    out['master.legato'] = 1 if legato_flag >= 0.5 else 0

    # FX
    fx_lost = [n for n, k in (('flanger', 'Flg'), ('phaser', 'Phs'), ('hyper/dimension', 'Hyp'), ('FX filter', 'FX Fil'))
               if P[f'{k} Enable'] == 'on']
    for n in fx_lost:
        losses.append(f'FX {n} not available')
    if P['Rev Enable'] == 'on':
        out.update({'fx.reverb.on': 1, 'fx.reverb.mix': pct(P['Verb Wet']), 'fx.reverb.size': pct(P['VerbSize'])})
    if P['Dly Enable'] == 'on':
        out.update({'fx.delay.on': 1, 'fx.delay.mix': pct(P['Dly_Wet']), 'fx.delay.fb': clamp(pct(P['Dly_Feed']), 0, 0.92),
                    'fx.delay.mode': 0 if P['Dly_Mode'] == 'Ping-Pong' else 1,
                    'fx.delay.wow': 0, 'fx.delay.flutter': 0, 'fx.delay.sat': 0})
        cpb = cycles_per_beat(P['Dly_TimL'])
        if P['Dly_BPM_Sync'] == 'on' and cpb:
            best = min(range(len(DELAY_DIVS)), key=lambda i: abs(math.log(DELAY_DIVS[i][1] / cpb)))
            out['fx.delay.sync'] = 1; out['fx.delay.div'] = best
        else:
            out['fx.delay.sync'] = 0; out['fx.delay.time'] = clamp(num(P['Dly_TimL']) / 1000, 0.02, 1.5)
    if P['Cho Enable'] == 'on':
        out.update({'fx.chorus.on': 1, 'fx.chorus.rate': clamp(num(P['Cho_Rate']), 0.05, 8),
                    'fx.chorus.depth': clamp(num(P['Cho_Dep']) / 13, 0, 1), 'fx.chorus.mix': pct(P['Cho_Wet'])})
    if P['Dist Enable'] == 'on':
        mode = P['Dist_Mode']
        out.update({'fx.drive.on': 1, 'fx.drive.amt': pct(P['Dist_Drv']), 'fx.drive.mix': pct(P['Dist_Wet']),
                    'fx.drive.type': 2 if ('Hard' in mode or 'Diode' in mode) else 0})
        if mode not in ('Tube', 'SoftClip', 'HardClip'):
            losses.append(f'distortion mode {mode} approximated')
    # WT-1's leveling compressor defaults on; Serum's compressor is off unless enabled.
    out['fx.comp.on'] = 0
    if P['Comp Enable'] == 'on' and P['CmpMBnd'] == 'MultBand':
        out.update({'fx.ott.on': 1, 'fx.ott.depth': clamp(pct(P['Comp_Wet']) * 0.6, 0, 1)})
    elif P['Comp Enable'] == 'on':
        out.update({'fx.comp.on': 1, 'fx.comp.thr': clamp(num(P['Cmp_Thr']), -40, 0),
                    'fx.comp.ratio': 20 if P['Cmp_Rat'] == 'Limit' else clamp(num(P['Cmp_Rat'].split(':')[0]), 1, 20),
                    'fx.comp.att': clamp(num(P['Cmp_Att']) / 1000, 0.0001, 0.1),
                    'fx.comp.rel': clamp(num(P['Cmp_Rel']) / 1000, 0.01, 2),
                    'fx.comp.gain': clamp(num(P['CmpGain']), 0, 12)})
    if P['EQ Enable'] == 'on':
        out.update({'fx.eq.on': 1, 'fx.eq.low': clamp(num(P['EQ VolL']), -15, 15), 'fx.eq.lfreq': clamp(num(P['EQ FrqL']), 20, 20000),
                    'fx.eq.high': clamp(num(P['EQ VolH']), -15, 15), 'fx.eq.hfreq': clamp(num(P['EQ FrqH']), 20, 20000),
                    'fx.eq.ltype': 0 if P['EQ TypL'] == 'Shelf' else 1, 'fx.eq.htype': 2 if P['EQ TypH'] == 'Shelf' else 1})

    # Both masters are knob-squared gains: map the knob, default 70% -> 0.75.
    out['master.volume'] = clamp(V['MasterVol'] * 0.75 / 0.70 * MASTER_TRIM, 0, 1)
    category = os.path.basename(os.path.dirname(dump['file']))
    preset = {'name': name, 'params': {k: round(v, 6) if isinstance(v, float) else v for k, v in out.items()}}
    if tables:
        preset['tables'] = tables
    return category, preset, losses


def main():
    global VST_NAMES
    dump_dir, cap_dir, out_dir = sys.argv[1:4]
    dumps = sorted(glob.glob(dump_dir + '/*.json'))
    only = set(sys.argv[4].split(',')) if len(sys.argv) > 4 else None
    report = {}
    for path in dumps:
        idx = int(os.path.basename(path)[:4])
        dump = json.load(open(path))
        if VST_NAMES is None:
            VST_NAMES = [p['name'] for p in dump['params']]
        if only and not any(o in dump['file'] for o in only):
            continue
        category, preset, losses = convert(dump, cap_dir, idx)
        d = os.path.join(out_dir, category)
        os.makedirs(d, exist_ok=True)
        json.dump(preset, open(os.path.join(d, preset['name'] + '.json'), 'w'))
        report[f'{category}/{preset["name"]}'] = losses
    json.dump(report, open(os.path.join(out_dir, 'losses.json'), 'w'), indent=1)
    print('converted', len(report))


if __name__ == '__main__':
    main()
