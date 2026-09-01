// FableSynth DSP core — runs in the AudioWorklet thread. Self-contained (no imports).
// Protocol (port messages in):
//   {t:'init', params:{id:value,...}}
//   {t:'tables', list:[{frames,mips,size,buf:ArrayBuffer}]}   full pool
//   {t:'table', i, frames, mips, size, buf}   one slot (buf transferred)
//   {t:'tablecount', n}                       resize the slot list
//   {t:'p', k, v}                    single param change (incl. mat{n}.src/.dst/.amt)
//   {t:'on', n, v} {t:'off', n}      note events (n=midi note, v=0..1)
//   {t:'bend', s}                    pitch bend in semitones
//   {t:'panic'}
//   {t:'pats', data:Uint8Array} {t:'chain', list} {t:'play'} {t:'stop'}   note sequencer
//   {t:'host',on} {t:'tempo',bpm,swing,anchor} {t:'clip',data,bars,atFrame}
//   {t:'clipstop',atFrame}                     hosted clip transport (SQ-4)
//
// Modulation is a fixed pool of 16 slots (mat1..mat16), each {src,dst,amt}, read
// straight from `this.p` — no separate routing message. The per-destination
// scaling matches the VST exactly, so the two engines sound identical.
// Out: {t:'viz', a, b, n}            modulated wt positions + active voice count
//      {t:'mod', d}                  live per-destination route sums for the UI's
//                                    knob indicators: d[MOD_DESTS index] = sum x
//                                    (Float32Array, viz cadence, only while a
//                                    routed voice sounds; d=null once when idle)
//      {t:'step', s, pat}            per step while the sequencer plays
//      {t:'clipstart', frame} {t:'clipstop', frame} {t:'pos', step, bar}   hosted

const NVOICES = 8;
const MAXUNI = 16;
// Fixed modulation pool: mat1..mat16, each {src,dst,amt}. Mirrors MOD_MATRIX_SIZE
// in the params/slot helpers and the VST's MOD_MATRIX_SIZE.
const MOD_MATRIX_SIZE = 16;

// Note sequencer constants — hand-copied from noteseq.ts (this module is
// import-free); the parity test asserts they still match.
const SEQ_STEPS = 16;
const SEQ_NPATTERNS = 4;
const SEQ_STRIDE = 3;
const WT_POLY_LANES = 8;
const SEQ_ACCENT_VEL = 1.0;
const SEQ_PLAIN_VEL = 0.72;
const SEQ_SWING_MAX = 0.667;

// Generic modulation log-curve depth: a full route swings a Log param ×2^(x·D),
// i.e. ±D octaves. D=5 reproduces the legacy CUTOFF scaling exactly. Mirrors the
// engine's `std::pow(2, x*5)` and the design contract (D=5).
const MOD_LOG_D = 5;

// Canonical dst index -> target. Mirrors dstTarget() in params.ts / Params.h
// index-for-index. Per-param dests hold the modulated paramId; the three globals
// and "none" hold a sentinel handled directly below. Globals keep their legacy
// math (pitch ±12 semis, amp gain, pan add); per-param dests fold into pm via the
// Lin/Log curve rule. Index 0 (none) is intentionally absent (skipped by src/dst
// guard). Keep in sync with MOD_DESTS.
const DST_PITCH = '\0pitch', DST_AMP = '\0amp', DST_PAN = '\0pan';
const DST_TARGET = [
  null,            // 0 — (unused; guarded out)
  'oscA.pos',      // 1  A POS
  'oscB.pos',      // 2  B POS
  'filter.cutoff', // 3  F1 CUT
  DST_PITCH,       // 4  PITCH (global)
  DST_AMP,         // 5  AMP   (global)
  DST_PAN,         // 6  PAN   (global)
  'oscA.level',    // 7  A LVL
  'oscB.level',    // 8  B LVL
  'filter2.cutoff',// 9  F2 CUT
  'filter2.res',   // 10 F2 RES
  'oscA.detune',   // 11 A DETUNE
  'oscA.spread',   // 12 A SPREAD
  'oscA.pan',      // 13 A PAN
  'oscB.detune',   // 14 B DETUNE
  'oscB.spread',   // 15 B SPREAD
  'oscB.pan',      // 16 B PAN
  'filter.res',    // 17 F1 RES
  'filter.drive',  // 18 F1 DRIVE
  'filter.env',    // 19 F1 ENV
  'filter.key',    // 20 F1 KEY
  'filter2.drive', // 21 F2 DRIVE
  'filter2.env',   // 22 F2 ENV
  'filter2.key',   // 23 F2 KEY
  'sub.level',     // 24 SUB LVL
  'noise.level',   // 25 NOISE LVL
  'oscA.blend',    // 26 A BLEND
  'oscB.blend',    // 27 B BLEND
];

// Per-param curve + range for every modulatable target, mirroring PARAM_DEFS in
// params.ts (curve + min/max). Used to fold a route sum x into the modulated value:
//   Lin: pm = p + x·(hi−lo)   (width-1 lin reproduces POS/LEVEL/RES exactly)
//   Log: pm = p · 2^(x·D)     (D=5 reproduces CUTOFF exactly)
// Kept local because the worklet runs in the render thread with no imports.
const MOD_PARAM_INFO = {
  'oscA.pos':       { curve: 'lin', lo: 0, hi: 1 },
  'oscB.pos':       { curve: 'lin', lo: 0, hi: 1 },
  'oscA.level':     { curve: 'lin', lo: 0, hi: 1 },
  'oscB.level':     { curve: 'lin', lo: 0, hi: 1 },
  'oscA.detune':    { curve: 'lin', lo: 0, hi: 1 },
  'oscB.detune':    { curve: 'lin', lo: 0, hi: 1 },
  'oscA.spread':    { curve: 'lin', lo: 0, hi: 1 },
  'oscB.spread':    { curve: 'lin', lo: 0, hi: 1 },
  'oscA.blend':     { curve: 'lin', lo: 0, hi: 1 },
  'oscB.blend':     { curve: 'lin', lo: 0, hi: 1 },
  'oscA.pan':       { curve: 'lin', lo: -1, hi: 1 },
  'oscB.pan':       { curve: 'lin', lo: -1, hi: 1 },
  'filter.cutoff':  { curve: 'log', lo: 20, hi: 20000 },
  'filter2.cutoff': { curve: 'log', lo: 20, hi: 20000 },
  'filter.res':     { curve: 'lin', lo: 0, hi: 1 },
  'filter2.res':    { curve: 'lin', lo: 0, hi: 1 },
  'filter.drive':   { curve: 'lin', lo: 0, hi: 1 },
  'filter2.drive':  { curve: 'lin', lo: 0, hi: 1 },
  'filter.env':     { curve: 'lin', lo: -1, hi: 1 },
  'filter2.env':    { curve: 'lin', lo: -1, hi: 1 },
  'filter.key':     { curve: 'lin', lo: 0, hi: 1 },
  'filter2.key':    { curve: 'lin', lo: 0, hi: 1 },
  'sub.level':      { curve: 'lin', lo: 0, hi: 1 },
  'noise.level':    { curve: 'lin', lo: 0, hi: 1 },
};
// ---------- flat parameter store (mirrors juce/source/dsp/Params.h) ----------
// The render thread must never build a string key (finding W2): every parameter
// lives in a Float64Array indexed by an integer id, and `{t:'p'}` messages are
// resolved to that index once, in onMsg. PARAM_IDS mirrors PARAM_DEFS in
// params.ts index-for-index; the parity test asserts the two lists still match.
const PARAM_IDS = [];
const addOsc = (pre) => PARAM_IDS.push(
  pre + '.on', pre + '.table', pre + '.pos', pre + '.oct', pre + '.semi', pre + '.fine',
  pre + '.unison', pre + '.detune', pre + '.spread', pre + '.blend', pre + '.level', pre + '.pan');
const addFilter = (pre) => PARAM_IDS.push(
  pre + '.on', pre + '.type', pre + '.cutoff', pre + '.res', pre + '.drive', pre + '.env', pre + '.key');
const addLfo = (pre) => PARAM_IDS.push(
  pre + '.shape', pre + '.rate', pre + '.sync', pre + '.syncrate', pre + '.rise', pre + '.phase', pre + '.retrig');
addOsc('oscA'); addOsc('oscB');
PARAM_IDS.push('sub.on', 'sub.shape', 'sub.oct', 'sub.level');
PARAM_IDS.push('noise.on', 'noise.type', 'noise.level');
addFilter('filter');
PARAM_IDS.push('filter.route');
addFilter('filter2');
PARAM_IDS.push('env1.a', 'env1.d', 'env1.s', 'env1.r');
PARAM_IDS.push('env2.a', 'env2.d', 'env2.s', 'env2.r');
addLfo('lfo1'); addLfo('lfo2');
for (let i = 1; i <= MOD_MATRIX_SIZE; i++) PARAM_IDS.push('mat' + i + '.src', 'mat' + i + '.dst', 'mat' + i + '.amt');
PARAM_IDS.push('fx.eq.on', 'fx.eq.low', 'fx.eq.mid', 'fx.eq.mfreq', 'fx.eq.high');
PARAM_IDS.push('fx.drive.on', 'fx.drive.amt', 'fx.drive.mix');
PARAM_IDS.push('fx.chorus.on', 'fx.chorus.rate', 'fx.chorus.depth', 'fx.chorus.mix');
PARAM_IDS.push('fx.delay.on', 'fx.delay.time', 'fx.delay.fb', 'fx.delay.mix');
PARAM_IDS.push('fx.reverb.on', 'fx.reverb.size', 'fx.reverb.mix');
PARAM_IDS.push('fx.comp.on', 'fx.comp.thr', 'fx.comp.gain');
PARAM_IDS.push('master.volume', 'master.glide', 'master.mono');
PARAM_IDS.push('seq.bpm', 'seq.swing', 'seq.root');

const NUM_PARAMS = PARAM_IDS.length;
const PID = Object.create(null);
for (let i = 0; i < NUM_PARAMS; i++) PID[PARAM_IDS[i]] = i;

// Offsets inside the repeated osc / filter / LFO / mat groups above.
const O_ON = 0, O_TABLE = 1, O_POS = 2, O_OCT = 3, O_SEMI = 4, O_FINE = 5,
      O_UNI = 6, O_DET = 7, O_SPR = 8, O_BLEND = 9, O_LEVEL = 10, O_PAN = 11;
const F_ON = 0, F_TYPE = 1, F_CUT = 2, F_RES = 3, F_DRIVE = 4, F_ENV = 5, F_KEY = 6;
const L_SHAPE = 0, L_RATE = 1, L_SYNC = 2, L_SYNCRATE = 3, L_RISE = 4, L_PHASE = 5, L_RETRIG = 6;
const M_SRC = 0, M_DST = 1, M_AMT = 2, M_STRIDE = 3;

const OSCA = PID['oscA.on'], OSCB = PID['oscB.on'];
const FLT1 = PID['filter.on'], FLT2 = PID['filter2.on'];
const F1_CUT = FLT1 + F_CUT, F2_CUT = FLT2 + F_CUT;
const FILTER_ROUTE = PID['filter.route'];
const LFO1 = PID['lfo1.shape'], LFO2 = PID['lfo2.shape'];
const MAT1 = PID['mat1.src'];
const SUB_ON = PID['sub.on'], SUB_SHAPE = PID['sub.shape'], SUB_OCT = PID['sub.oct'], SUB_LEVEL = PID['sub.level'];
const NOISE_ON = PID['noise.on'], NOISE_TYPE = PID['noise.type'], NOISE_LEVEL = PID['noise.level'];
const ENV1_A = PID['env1.a'], ENV2_A = PID['env2.a'];
const MASTER_GLIDE = PID['master.glide'], MASTER_MONO = PID['master.mono'];
const SEQ_BPM = PID['seq.bpm'], SEQ_SWING = PID['seq.swing'], SEQ_ROOT = PID['seq.root'];
// FX + master param indices (the FX chain moved into the worklet, finding W6).
const FXEQ_ON = PID['fx.eq.on'], FXEQ_LOW = PID['fx.eq.low'], FXEQ_MID = PID['fx.eq.mid'],
      FXEQ_MFREQ = PID['fx.eq.mfreq'], FXEQ_HIGH = PID['fx.eq.high'];
const FXDRIVE_ON = PID['fx.drive.on'], FXDRIVE_AMT = PID['fx.drive.amt'], FXDRIVE_MIX = PID['fx.drive.mix'];
const FXCHORUS_ON = PID['fx.chorus.on'], FXCHORUS_RATE = PID['fx.chorus.rate'],
      FXCHORUS_DEPTH = PID['fx.chorus.depth'], FXCHORUS_MIX = PID['fx.chorus.mix'];
const FXDELAY_ON = PID['fx.delay.on'], FXDELAY_TIME = PID['fx.delay.time'],
      FXDELAY_FB = PID['fx.delay.fb'], FXDELAY_MIX = PID['fx.delay.mix'];
const FXREVERB_ON = PID['fx.reverb.on'], FXREVERB_SIZE = PID['fx.reverb.size'], FXREVERB_MIX = PID['fx.reverb.mix'];
const FXCOMP_ON = PID['fx.comp.on'], FXCOMP_THR = PID['fx.comp.thr'], FXCOMP_GAIN = PID['fx.comp.gain'];
const MASTER_VOLUME = PID['master.volume'];

// dst index -> param index, or one of these sentinels for the three globals.
const D_NONE = -1, D_PITCH = -2, D_AMP = -3, D_PAN = -4;
const DST_PIDX = new Int32Array(DST_TARGET.length).fill(D_NONE);
// param index -> MOD_DESTS index (0 = not a destination). Feeds the live-mod
// telemetry snapshot; globals and slot 0 have no owning knob and stay 0.
const PIDX_DST = new Int32Array(NUM_PARAMS);
for (let i = 1; i < DST_TARGET.length; i++) {
  const t = DST_TARGET[i];
  if (t === DST_PITCH) DST_PIDX[i] = D_PITCH;
  else if (t === DST_AMP) DST_PIDX[i] = D_AMP;
  else if (t === DST_PAN) DST_PIDX[i] = D_PAN;
  else if (typeof t === 'string' && PID[t] !== undefined) { DST_PIDX[i] = PID[t]; PIDX_DST[PID[t]] = i; }
}
// param index -> mod curve (0 none, 1 lin, 2 log) + Lin span (hi-lo).
const MOD_CURVE = new Uint8Array(NUM_PARAMS);
const MOD_SPAN = new Float64Array(NUM_PARAMS);
for (const id in MOD_PARAM_INFO) {
  const inf = MOD_PARAM_INFO[id], i = PID[id];
  MOD_CURVE[i] = inf.curve === 'log' ? 2 : 1;
  MOD_SPAN[i] = inf.hi - inf.lo;
}

// LP24 resonance taper (finding B3). The cascade used to run two stages at the
// SAME damping k, so the peak at fc was (1/k)^2 — two coincident resonances —
// and with k = 2 - 1.93*res bottoming out at 0.071 the filter only reached
// Q ~= 14 and never rang. The resonance now lives in stage 1 alone and stage 2
// stays critically damped:
//   resT = res + 0.0035*res^4
//   k1   = max(LP24_K_MIN, 0.5*(2 - 1.93*resT)^2),  k2 = 2
// 1/(k1*k2) reproduces the old (1/k)^2 magnitude at fc to within 1.3 dB across
// the whole knob and to within 0.03 dB up to res 0.9, so presets keep their
// timbre, while the single resonant stage's Q climbs from 14 to ~470 at the top
// — a filter that sings instead of merely peaking. Identical to Engine.cpp.
const LP24_K_MIN = 0.002; // Q = 500 damping floor

// Longest tuned-comb delay. 4096 samples covers cutoffs down to ~11 Hz at 48 kHz,
// so the full 20 Hz..20 kHz CUTOFF range maps to a valid comb pitch.
const COMB_MAX = 4096;

// LFO note-division factors (cycles per beat, beat = quarter note). Index maps
// to params.ts LFO_DIVS.
const LFO_DIV_F = [0.25, 0.5, 1, 2 / 3, 1.5, 2, 4 / 3, 3, 4, 6, 8];

// Vowel formants (A-E-I-O-U), reused from the VOX wavetable voicing. The VOWEL
// filter is a 3-band bandpass bank tuned to these, morphed by the CUTOFF knob.
const VOWELS = [
  [730, 1090, 2440],
  [530, 1840, 2480],
  [390, 1990, 2550],
  [570, 840, 2410],
  [440, 1020, 2240],
];
const F_AMPS = [1, 0.55, 0.32];

// Sample-rate-invariant smoothing (finding W1). A coefficient is derived from a
// time constant instead of being a fixed per-block/per-sample number, so the
// smoothing TIME is the same at 44.1, 48 and 96 kHz. The taus below are chosen
// to reproduce the legacy constants exactly at the 48 kHz reference:
//   POS   0.35 per 128 samples, CUT 0.5 per 128 samples, steal fade 0.12/sample.
// Mirrors Engine.cpp's smoothCoef/POS_TAU/CUT_TAU.
function smoothCoef(n, tauSr) { return 1 - Math.exp(-n / tauSr); }
const POS_TAU = 128 / (48000 * 0.4307829160924542); // -ln(0.65)
const CUT_TAU = 128 / (48000 * Math.LN2);
const STEAL_TAU = 1 / (48000 * 0.1278333715098849); // -ln(0.88)
const STEAL_C = smoothCoef(1, STEAL_TAU * sampleRate);

// Fast deterministic RNG (xorshift32) — replaces Math.random() for noise, unison
// start phases and S&H (finding W5). Mirrors `Rng` in Engine.h, so a seeded
// render is reproducible and comparable across the two engines.
class Rng {
  constructor(seed) { this.s = (seed >>> 0) || 0x9e3779b9; }
  next() {
    let s = this.s;
    s = (s ^ (s << 13)) >>> 0;
    s = (s ^ (s >>> 17)) >>> 0;
    s = (s ^ (s << 5)) >>> 0;
    this.s = s;
    return (s >>> 8) * (1 / 16777216);
  }
}

class Env {
  constructor() {
    this.state = 0; this.level = 0; this.s = 0.8;
    this.ca = 0.01; this.cd = 0.001; this.cr = 0.001;
    // Numeric cache key (finding W2): building `a+'|'+d+'|'+r` allocated a
    // cons-string per voice per block on the render thread.
    this._a = NaN; this._d = NaN; this._r = NaN;
  }
  // decay/release use tau = t/4.5 so the audible tail roughly matches the label
  set(a, d, s, r) {
    this.s = s;
    if (a !== this._a || d !== this._d || r !== this._r) {
      this._a = a; this._d = d; this._r = r;
      this.ca = 1 - Math.exp(-1 / (Math.max(0.0008, a) * sampleRate));
      this.cd = 1 - Math.exp(-1 / (Math.max(0.002, d / 4.5) * sampleRate));
      this.cr = 1 - Math.exp(-1 / (Math.max(0.002, r / 4.5) * sampleRate));
    }
  }
  trigger() { this.state = 1; }
  release() { if (this.state !== 0) this.state = 4; }
  kill() { this.state = 0; this.level = 0; }
  process() {
    switch (this.state) {
      case 1: {
        this.level += (1.45 - this.level) * this.ca;
        if (this.level >= 1) { this.level = 1; this.state = 2; }
        break;
      }
      case 2: {
        this.level += (this.s - this.level) * this.cd;
        if (this.level - this.s < 0.0005) this.state = 3;
        break;
      }
      case 3: this.level = this.s; break;
      case 4: {
        this.level -= this.level * this.cr;
        if (this.level < 1e-4) { this.level = 0; this.state = 0; }
        break;
      }
      case 5: {
        // steal fade: ~2 ms to silence at any sample rate, then the voice is
        // free for its pending note
        this.level -= this.level * STEAL_C;
        if (this.level < 1e-4) { this.level = 0; this.state = 0; }
        break;
      }
    }
    return this.level;
  }
  processBlock(n) { for (let i = 0; i < n; i++) this.process(); return this.level; }
}

class LFO {
  constructor() { this.phase = 0; this.hold = 0; this.elapsed = 0; }
  reset(rng) { this.phase = 0; this.hold = rng.next() * 2 - 1; this.elapsed = 0; }
  // Read the shape at a wrapped phase offset (for the start-phase control).
  valueOff(shape, off) {
    let p = this.phase + off; p -= Math.floor(p);
    switch (shape | 0) {
      case 0: return Math.sin(2 * Math.PI * p);
      case 1: return 1 - 4 * Math.abs(p - 0.5);
      case 2: return 1 - 2 * p;
      case 3: return p < 0.5 ? 1 : -1;
      default: return this.hold;
    }
  }
  // Fade-in gain, per-voice, keyed off note-on (samples since reset).
  riseGain(riseSec) { return riseSec <= 0 ? 1 : Math.min(1, this.elapsed / (riseSec * sampleRate)); }
  advance(rate, n, rng) {
    this.elapsed += n;
    // NaN guard: a non-finite rate (e.g. params not yet initialised when the
    // free-running global LFO advances) would latch phase to a sticky NaN.
    const d = (rate * n) / sampleRate;
    if (Number.isFinite(d)) this.phase += d;
    if (this.phase >= 1) { this.phase -= Math.floor(this.phase); this.hold = rng.next() * 2 - 1; }
  }
}

// Per-oscillator runtime state inside a voice.
// DC_R and the Kellet pink-noise poles below are 48 kHz reference values; the
// processor remaps them to the context rate in its constructor (finding W1), so
// the DC corner and the pink tilt are the same filter at any sample rate.
const DC_R = 0.9998; // ~3.5 Hz highpass — removes DC without touching bass
const PINK_P = [0.99886, 0.99332, 0.969, 0.8665, 0.55, 0.7616];
const PINK_G = [0.0555179, 0.0750759, 0.153852, 0.3104856, 0.5329522, 0.016898];

// Oscillator phase normally advances by less than one table length per sample,
// but defensive wrapping keeps a malformed/modulated increment from turning the
// table index into an out-of-range read. Mirrors Engine.cpp's wrapOscPhase.
function wrapOscPhase(phase, size) {
  if (!Number.isFinite(phase)) return 0;
  if (phase < 0 || phase >= size) {
    phase = phase % size;
    if (phase < 0) phase += size;
  }
  return phase;
}

// Numerically stable ln(cosh(z)) — the antiderivative of tanh, used by the
// anti-aliased (ADAA) saturator below. cosh overflows for |z| > ~710, so we
// fold large arguments to |z| - ln2 + log1p(e^-2|z|). Exact for small z too
// (ln cosh 0 = 0), so it is safe across the whole drive range.
function lcosh(z) {
  const a = Math.abs(z);
  return a + Math.log1p(Math.exp(-2 * a)) - Math.LN2;
}

// Finding W1: 4-point cubic Hermite (Catmull-Rom) table read, replacing the
// linear read. Indices are pre-wrapped by the caller (branchless & mask), `off`
// selects the frame/mip. Mirrors Engine.cpp's rdH — the interpolation images
// this removes were 10-20 dB above the JUCE engine's below C6.
function rdH(d, off, im1, i0, i1, i2, f) {
  const ym1 = d[off + im1], y0 = d[off + i0], y1 = d[off + i1], y2 = d[off + i2];
  const c1 = 0.5 * (y1 - ym1);
  const c2 = ym1 - 2.5 * y0 + 2 * y1 - 0.5 * y2;
  const c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
  return ((c3 * f + c2) * f + c1) * f + y0;
}

// Per-voice runtime state for one filter (both the persistent DSP state and the
// block-rate coefficients). Kept allocation-free so coef updates never trigger GC.
function makeFilterState() {
  return {
    svf: new Float64Array(8),   // SVF: 2 stages x 2 ch x (ic1, ic2)
    fmt: new Float64Array(12),  // formant: 2 ch x 3 bands x (s1, s2)
    combL: new Float32Array(COMB_MAX),
    combR: new Float32Array(COMB_MAX),
    combW: 0,
    cutSm: 0,
    satXL: 0, satXR: 0,         // ADAA drive: previous input per channel
    ftype: 0, twoPole: false,
    k1: 0,                      // SVF damping, stage 1 (a1..a3 ramp per sub-block)
    k2: 0,                      // SVF damping, stage 2 (LP24 only; = k1 elsewhere)
    cutTarget: 0, cutPrev: 0,   // runFilter ramps cutPrev -> cutTarget
    combLen: 1, combLenPrev: 0, combFb: 0, // comb coefs
    fc: new Float64Array(9),    // formant biquad coefs: 3 bands x (b0, a1, a2)
    famp: new Float64Array(3),
  };
}

function resetFilterState(fs) {
  fs.svf.fill(0); fs.fmt.fill(0);
  fs.combL.fill(0); fs.combR.fill(0); fs.combW = 0;
  fs.cutSm = 0; fs.satXL = 0; fs.satXR = 0;
  fs.cutPrev = 0; fs.combLenPrev = 0;
}

function makeOscState() {
  return {
    phases: new Float64Array(MAXUNI),
    incs: new Float64Array(MAXUNI),
    gl: new Float32Array(MAXUNI),
    gr: new Float32Array(MAXUNI),
    uni: 1, off0: 0, off1: 0, off0b: 0, off1b: 0, mipBlend: 0,
    ft: 0, gain: 0, mask: 0, size: 0, data: null, posSm: -1,
    // Previous chunk's targets — renderOsc ramps to this chunk's across n
    // samples so block-rate modulation has no staircase (finding W1).
    pIncs: new Float64Array(MAXUNI),
    pGl: new Float64Array(MAXUNI),
    pGr: new Float64Array(MAXUNI),
    pFt: 0, pOff0: -1, pUni: 0, havePrev: false,
  };
}

class Voice {
  constructor() {
    this.note = 60; this.vel = 1; this.gate = false; this.age = 0;
    this.pitch = 60; this.velGain = 0;
    this.pending = null; // {n, vel, start} queued behind a steal fade (Env state 5)
    this.ampEnv = new Env(); this.modEnv = new Env();
    this.lfo1 = new LFO(); this.lfo2 = new LFO();
    this.oA = makeOscState(); this.oB = makeOscState();
    this.subPhase = 0; this.subIncPrev = -1; this.ampFacPrev = -1;
    this.pb = [0, 0, 0, 0, 0, 0, 0]; // pink noise filter state
    this.f1 = makeFilterState(); this.f2 = makeFilterState();
    this.dcxL = 0; this.dcxR = 0; this.dcyL = 0; this.dcyR = 0;
  }
  get active() { return this.ampEnv.state !== 0; }

  noteOn(note, vel, startPitch, age, rng, phaseRandA, phaseRandB) {
    this.note = note; this.vel = vel; this.gate = true; this.age = age;
    this.pitch = startPitch;
    this.velGain = 0.25 + 0.75 * vel * vel;
    this.ampEnv.trigger(); this.modEnv.trigger();
    this.lfo1.reset(rng); this.lfo2.reset(rng);
    for (let i = 0; i < MAXUNI; i++) {
      // Start phase is in SAMPLES (all tables are 2048 wide): scale the random
      // draw to a full cycle so unison voices (and osc A vs B) decorrelate.
      this.oA.phases[i] = phaseRandA ? rng.next() * 2048 : 0;
      this.oB.phases[i] = phaseRandB ? rng.next() * 2048 : 0;
    }
    this.oA.posSm = -1; this.oB.posSm = -1;
    // A fresh note has no previous chunk to ramp from.
    this.oA.havePrev = false; this.oB.havePrev = false;
    this.subPhase = 0; this.subIncPrev = -1; this.ampFacPrev = -1;
    resetFilterState(this.f1); resetFilterState(this.f2);
    this.dcxL = this.dcxR = this.dcyL = this.dcyR = 0;
  }
  noteOff() { this.gate = false; this.ampEnv.release(); this.modEnv.release(); }
  kill() { this.gate = false; this.pending = null; this.ampEnv.kill(); this.modEnv.kill(); }
}

// ---------- FX chain (finding W6) ----------
// A line-for-line port of juce/source/dsp/Fx.cpp / Fx.h into the worklet, so the
// web app and the plugin run ONE algorithm: EQ -> drive -> chorus -> ping-pong
// delay -> reverb -> leveling compressor -> master gain -> DC block -> lookahead
// limiter. The web build used to assemble this from native WebAudio nodes
// (WaveShaper / Delay / Convolver / DynamicsCompressor), which differed from the
// plugin in every stage and could not be tested offline. synth.ts now keeps the
// native graph only for the scope and spectrum analysers.

// Safety-limiter static curve: threshold -8 dB, ratio 14 — the WebAudio
// DynamicsCompressor settings the old master limiter used. Only its spec makeup
// gain survives here; the ceiling is the lookahead limiter's hard -1 dBFS.
const LIM_THR = 0.398, LIM_RATIO = 14.0;
// Leveling compressor: WebAudio DynamicsCompressor defaults (ratio 4, knee 9 dB,
// attack 10 ms, release 200 ms) with THRESH/MAKEUP params.
const COMP_RATIO = 4.0, COMP_KNEE = 9.0;

// WebAudio DynamicsCompressor static curve, in dB of gain reduction (<= 0).
function compGainDb(xDb, thrDb) {
  const over = xDb - thrDb;
  if (over <= 0) return 0;
  if (over < COMP_KNEE) return ((1 / COMP_RATIO) - 1) * over * over / (2 * COMP_KNEE);
  return ((1 / COMP_RATIO) - 1) * (over - COMP_KNEE * 0.5);
}

// One-pole smoother toward a target (setTargetAtTime equivalent).
class Smooth {
  constructor() { this.cur = 0; this.target = 0; this.coef = 0.01; }
  setTime(tau) { this.coef = 1 - Math.exp(-1 / (tau * sampleRate)); }
  next() { this.cur += (this.target - this.cur) * this.coef; return this.cur; }
  snap(v) { this.cur = this.target = v; }
}

// RBJ cookbook biquad, transposed direct form II. 0 dB gain on the shelving /
// peaking designs yields exact unity, so "EQ off" is a true bypass.
class Biquad {
  constructor() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; }
  reset() { this.z1 = 0; this.z2 = 0; }
  process(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
  lowpass(freq, q) {
    const w0 = 2 * Math.PI * Math.min(freq, sampleRate * 0.49) / sampleRate;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 - cw) / 2 / a0; this.b1 = (1 - cw) / a0; this.b2 = this.b0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha) / a0;
  }
  highpass(freq, q) {
    const w0 = 2 * Math.PI * Math.min(freq, sampleRate * 0.49) / sampleRate;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 + cw) / 2 / a0; this.b1 = -(1 + cw) / a0; this.b2 = this.b0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha) / a0;
  }
  lowShelf(freq, gainDb) {
    const A = Math.pow(10, gainDb / 40);
    const w0 = 2 * Math.PI * Math.min(freq, sampleRate * 0.49) / sampleRate;
    const cw = Math.cos(w0), sw = Math.sin(w0);
    const alpha = sw / 2 * Math.SQRT2; // shelf slope S = 1
    const tsa = 2 * Math.sqrt(A) * alpha;
    const a0 = (A + 1) + (A - 1) * cw + tsa;
    this.b0 = A * ((A + 1) - (A - 1) * cw + tsa) / a0;
    this.b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
    this.b2 = A * ((A + 1) - (A - 1) * cw - tsa) / a0;
    this.a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
    this.a2 = ((A + 1) + (A - 1) * cw - tsa) / a0;
  }
  highShelf(freq, gainDb) {
    const A = Math.pow(10, gainDb / 40);
    const w0 = 2 * Math.PI * Math.min(freq, sampleRate * 0.49) / sampleRate;
    const cw = Math.cos(w0), sw = Math.sin(w0);
    const alpha = sw / 2 * Math.SQRT2;
    const tsa = 2 * Math.sqrt(A) * alpha;
    const a0 = (A + 1) - (A - 1) * cw + tsa;
    this.b0 = A * ((A + 1) + (A - 1) * cw + tsa) / a0;
    this.b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
    this.b2 = A * ((A + 1) + (A - 1) * cw - tsa) / a0;
    this.a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
    this.a2 = ((A + 1) - (A - 1) * cw - tsa) / a0;
  }
  peaking(freq, q, gainDb) {
    const A = Math.pow(10, gainDb / 40);
    const w0 = 2 * Math.PI * Math.min(freq, sampleRate * 0.49) / sampleRate;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q);
    const a0 = 1 + alpha / A;
    this.b0 = (1 + alpha * A) / a0; this.b1 = (-2 * cw) / a0; this.b2 = (1 - alpha * A) / a0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha / A) / a0;
  }
}

// Fractional-read delay line. Float32 storage mirrors the plugin's `float` buffers
// so a feedback path quantizes identically.
class DelayLine {
  constructor(n) { this.buf = new Float32Array(n); this.w = 0; }
  reset() { this.buf.fill(0); this.w = 0; }
  write(x) { this.buf[this.w] = x; if (++this.w >= this.buf.length) this.w = 0; }
  read(d) {
    const sz = this.buf.length;
    let rd = this.w - d;
    while (rd < 0) rd += sz;
    const i0 = rd | 0, frac = rd - i0;
    const i1 = i0 + 1 < sz ? i0 + 1 : 0;
    return this.buf[i0] + frac * (this.buf[i1] - this.buf[i0]);
  }
  // 4-point Catmull-Rom, for the modulated reads (chorus, echo). Chromium's
  // DelayNode interpolated linearly; this is what the plugin does.
  // Adopt another line's contents and write position (mono fast path).
  copyFrom(o) { this.buf.set(o.buf); this.w = o.w; }
  // Block form of write-then-read at a fixed delay (the drive's dry path).
  blockRead(src, dst, n, d) {
    for (let i = 0; i < n; i++) { this.write(src[i]); dst[i] = this.read(d); }
  }
  readHermite(d) {
    const b = this.buf, sz = b.length;
    let rd = this.w - d;
    while (rd < 0) rd += sz;
    const i1 = rd | 0, t = rd - i1;
    const i0 = i1 > 0 ? i1 - 1 : sz - 1;
    const i2 = i1 + 1 < sz ? i1 + 1 : 0;
    const i3 = i2 + 1 < sz ? i2 + 1 : 0;
    const y0 = b[i0], y1 = b[i1], y2 = b[i2], y3 = b[i3];
    const c1 = 0.5 * (y2 - y0);
    const c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3;
    const c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
  }
}

// Hoisted out of the per-sample chorus LFO. Exactly the double 2*Math.PI.
const TWO_PI = 6.283185307179586;

function besselI0(x) {
  let sum = 1, term = 1;
  for (let k = 1; k < 64; k++) {
    term *= x * x / (4 * k * k);
    sum += term;
    if (term < 1e-16 * sum) break;
  }
  return sum;
}

// Odd-length Kaiser-windowed half-band FIR (cutoff = rate/4), stored POLYPHASE
// (finding J4). Every tap an even distance from the centre is structurally zero
// and the centre tap is exactly 0.5, so the direct form spends most of its
// multiplies on zeros; on top of that the interpolator's input is half
// zero-stuffed samples and the decimator throws away half its outputs. Split
// into phases, one branch is therefore a BARE DELAY (the lone centre tap) and
// the other a dense FIR of ceil(taps/2) taps — about a quarter of the direct
// form's MACs, 86 per base sample through the 4x drive path instead of 324.
// The dense loop is unrolled by 4. Same decomposition as HalfBandFir in
// juce/source/dsp/Fx.cpp and HalfBand in src/bass/engine/worklet-bass.js.
//
// One instance is driven in ONE mode: interpolate() or decimate(), never both —
// each keeps its own history.
class HalfBandFir {
  constructor(taps, beta) {
    const h = new Float64Array(taps);
    const M = taps - 1, ib = besselI0(beta);
    for (let i = 0; i < taps; i++) {
      const m = i - M * 0.5;
      const sinc = m === 0 ? 0.5 : Math.sin(Math.PI * 0.5 * m) / (Math.PI * m);
      const t = 2 * m / M;
      h[i] = sinc * besselI0(beta * Math.sqrt(Math.max(0, 1 - t * t))) / ib;
    }
    // sin(PI*m/2) for an even integer m evaluates to ~1e-16, not 0. Forcing
    // those taps to zero makes the polyphase split exact.
    const c = (taps - 1) >> 1;
    for (let i = 0; i < taps; i++) if (i !== c && ((i - c) % 2) === 0) h[i] = 0;
    this.h = h;

    const ne = (taps + 1) >> 1, no = taps >> 1;
    this.cEven = (c & 1) === 0; // which phase the centre tap lands in
    const branch = new Float64Array(this.cEven ? no : ne);
    for (let j = 0; j < branch.length; j++) branch[j] = h[this.cEven ? 2 * j + 1 : 2 * j];
    this.g = branch;        // the dense phase
    this.dly = c >> 1;      // the delay phase's single tap, in phase samples
    this.np = Math.max(ne, no + 1);
    // Mirror-written histories: writing each sample at p and p+np lets a branch
    // read a contiguous window with no wrap test.
    this.hx = new Float64Array(2 * this.np);
    this.he = new Float64Array(2 * this.np);
    this.ho = new Float64Array(2 * this.np);
    this.px = 0; this.pd = 0;
    this.y0 = 0; this.y1 = 0;
  }

  reset() {
    this.hx.fill(0); this.he.fill(0); this.ho.fill(0);
    this.px = 0; this.pd = 0; this.y0 = 0; this.y1 = 0;
  }

  // Adopt another instance's history. Used by the mono fast path, where the
  // right channel was skipped because its input was identical to the left's:
  // had it run, its state would be exactly this.
  copyFrom(o) {
    this.hx.set(o.hx); this.he.set(o.he); this.ho.set(o.ho);
    this.px = o.px; this.pd = o.pd;
  }

  // Block forms of the two entry points. A block at a time rather than a
  // sample at a time, so each filter's taps and pointers land in locals once
  // per block instead of once per sample — measured at 23 % of the whole drive
  // stage. Same arithmetic and the same state evolution, sample for sample.

  // 2x interpolate: n base-rate samples in, 2n upsampled samples out.
  // Equivalent to the direct form fed 2*x then 0 for each input.
  interpBlock(x, out, n) {
    const np = this.np, hx = this.hx, g = this.g, nb = g.length;
    const dly = this.dly, cEven = this.cEven;
    let p = this.px;
    for (let i = 0; i < n; i++) {
      if (--p < 0) p = np - 1;
      const v = x[i];
      hx[p] = v; hx[p + np] = v;
      let a = 0, j = 0;
      for (; j + 3 < nb; j += 4) {
        a += g[j] * hx[p + j] + g[j + 1] * hx[p + j + 1]
           + g[j + 2] * hx[p + j + 2] + g[j + 3] * hx[p + j + 3];
      }
      for (; j < nb; j++) a += g[j] * hx[p + j];
      a += a;                     // the zero-stuff gain of 2
      const d = hx[p + dly];      // delay branch: 2 * 0.5 * x[n - dly]
      const o = i + i;
      if (cEven) { out[o] = d; out[o + 1] = a; } else { out[o] = a; out[o + 1] = d; }
    }
    this.px = p;
  }

  // 2x decimate: 2n high-rate samples in, n outputs at the kept (first) phase.
  // Equivalent to the direct form fed both samples, keeping the first result.
  decimBlock(x, out, n) {
    const np = this.np, he = this.he, ho = this.ho, g = this.g, nb = g.length;
    const dly = this.dly, cEven = this.cEven;
    let p = this.pd;
    for (let i = 0; i < n; i++) {
      if (--p < 0) p = np - 1;
      const o = i + i;
      const x0 = x[o], x1 = x[o + 1];
      he[p] = x0; he[p + np] = x0;
      ho[p] = x1; ho[p + np] = x1;
      // The dense branch reads the phase the centre tap does not.
      const b = cEven ? ho : he;
      const q = cEven ? p + 1 : p;
      let a = 0, j = 0;
      for (; j + 3 < nb; j += 4) {
        a += g[j] * b[q + j] + g[j + 1] * b[q + j + 1]
           + g[j + 2] * b[q + j + 2] + g[j + 3] * b[q + j + 3];
      }
      for (; j < nb; j++) a += g[j] * b[q + j];
      out[i] = a + 0.5 * (cEven ? he[p + dly] : ho[p + dly + 1]);
    }
    this.pd = p;
  }

}

// Oversampler and drive scratch. Module-level and grown on demand: the render
// thread must never allocate. OS_X4 holds 4x the block length.
let OS_IN = new Float64Array(128), OS_X2 = new Float64Array(256);
let OS_X4 = new Float64Array(512), OS_Y2 = new Float64Array(256);
let FX_EQL = new Float64Array(128), FX_EQR = new Float64Array(128);
let FX_DRYL = new Float64Array(128), FX_DRYR = new Float64Array(128);
let FX_WETL = new Float64Array(128), FX_WETR = new Float64Array(128);
function fxScratch(n) {
  if (OS_IN.length >= n) return;
  OS_IN = new Float64Array(n); OS_X2 = new Float64Array(2 * n);
  OS_X4 = new Float64Array(4 * n); OS_Y2 = new Float64Array(2 * n);
  FX_EQL = new Float64Array(n); FX_EQR = new Float64Array(n);
  FX_DRYL = new Float64Array(n); FX_DRYR = new Float64Array(n);
  FX_WETL = new Float64Array(n); FX_WETR = new Float64Array(n);
}

// 4x drive oversampler stages: 47-tap first half-band (2x), 17-tap second (4x).
// Total up+shape+down group delay is an exact integer in base samples.
const HB1_TAPS = 47, HB2_TAPS = 17;
const DRIVE_LATENCY = ((HB1_TAPS - 1) / 2 + (HB2_TAPS - 1) / 4) | 0; // 27

// Lookahead brickwall limiter: fixed makeup gain feeding a delayed signal path,
// linked-stereo sliding-window-minimum gain that fully develops inside the
// ~1.5 ms lookahead, ~200 ms release, hard -1 dBFS sample-peak ceiling. The
// WebAudio DynamicsCompressor this replaces had no ceiling at all.
const LIM_CEILING = 0.8912509381337456; // -1 dBFS

class LookaheadLimiter {
  constructor(makeup) {
    this.la = Math.max(8, Math.round(0.0015 * sampleRate));
    this.qcap = this.la + 2;
    this.dlL = new Float32Array(this.la);
    this.dlR = new Float32Array(this.la);
    this.qv = new Float64Array(this.qcap);
    this.qi = new Float64Array(this.qcap);
    this.atk = 1 - Math.exp(-4 / this.la);                 // develops inside the window
    this.rel = 1 - Math.exp(-1 / (0.2 * sampleRate));      // ~200 ms release
    this.makeup = makeup;
    this.reset();
  }
  reset() {
    this.dlL.fill(0); this.dlR.fill(0); this.qv.fill(1); this.qi.fill(0);
    this.qh = 0; this.qt = 0; this.w = 0; this.t = 0; this.env = 1;
    this.outL = 0; this.outR = 0;
  }
  process(l, r) {
    const cap = this.qcap;
    const xl = l * this.makeup, xr = r * this.makeup;
    const pk = Math.abs(xl) > Math.abs(xr) ? Math.abs(xl) : Math.abs(xr);
    const g = pk > LIM_CEILING ? LIM_CEILING / pk : 1;
    // monotonic ring queue: minimum required gain over the last la+1 samples
    const qv = this.qv, qi = this.qi;
    while (this.qh !== this.qt) {
      const pv = this.qt > 0 ? this.qt - 1 : cap - 1;
      if (qv[pv] < g) break;
      this.qt = pv;
    }
    qv[this.qt] = g; qi[this.qt] = this.t;
    this.qt = this.qt + 1 < cap ? this.qt + 1 : 0;
    if (qi[this.qh] < this.t - this.la) this.qh = this.qh + 1 < cap ? this.qh + 1 : 0;
    const wmin = qv[this.qh];
    this.env += (wmin - this.env) * (wmin < this.env ? this.atk : this.rel);
    const dl = this.dlL[this.w], dr = this.dlR[this.w];
    this.dlL[this.w] = xl; this.dlR[this.w] = xr;
    if (++this.w >= this.la) this.w = 0;
    this.t++;
    let gg = this.env;
    const ad = Math.abs(dl), bd = Math.abs(dr);
    const pd = ad > bd ? ad : bd;
    if (gg * pd > LIM_CEILING) gg = LIM_CEILING / pd; // catch smoothing residue
    this.outL = dl * gg; this.outR = dr * gg;
  }
}

// Freeverb building blocks.
class FvComb {
  constructor(n) { this.buf = new Float32Array(n); this.idx = 0; this.filt = 0; this.damp1 = 0.2; this.damp2 = 0.8; this.feedback = 0.84; }
  reset() { this.buf.fill(0); this.filt = 0; }
  process(x) {
    const out = this.buf[this.idx];
    this.filt = out * this.damp2 + this.filt * this.damp1;
    this.buf[this.idx] = x + this.filt * this.feedback;
    if (++this.idx >= this.buf.length) this.idx = 0;
    return out;
  }
}
class FvAllpass {
  constructor(n) { this.buf = new Float32Array(n); this.idx = 0; this.feedback = 0.5; }
  reset() { this.buf.fill(0); }
  process(x) {
    const bo = this.buf[this.idx];
    const out = -x + bo;
    this.buf[this.idx] = x + bo * this.feedback;
    if (++this.idx >= this.buf.length) this.idx = 0;
    return out;
  }
}

// Classic Freeverb tuning, scaled to the device sample rate. The web build used
// a ConvolverNode fed a random-noise impulse that was RE-RENDERED on every SIZE
// change, and the buffer swap cut the tail dead; SIZE here only moves the comb
// feedback and damping, so the tail bends instead of dropping out.
const FV_COMB_TUNE = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617];
const FV_AP_TUNE = [556, 441, 341, 225];
const FV_SPREAD = 23;

function mixGate(on, amount, wet) {
  if (wet) return on ? Math.sin(amount * Math.PI / 2) : 0;
  return on ? Math.cos(amount * Math.PI / 2) : 1;
}

class Fx {
  constructor() {
    const scale = sampleRate / 44100;
    this.combL = []; this.combR = []; this.apL = []; this.apR = [];
    for (let i = 0; i < 8; i++) {
      this.combL.push(new FvComb((FV_COMB_TUNE[i] * scale) | 0));
      this.combR.push(new FvComb(((FV_COMB_TUNE[i] + FV_SPREAD) * scale) | 0));
    }
    for (let i = 0; i < 4; i++) {
      this.apL.push(new FvAllpass((FV_AP_TUNE[i] * scale) | 0));
      this.apR.push(new FvAllpass(((FV_AP_TUNE[i] + FV_SPREAD) * scale) | 0));
    }

    this.eqLoL = new Biquad(); this.eqLoR = new Biquad();
    this.eqMidL = new Biquad(); this.eqMidR = new Biquad();
    this.eqHiL = new Biquad(); this.eqHiR = new Biquad();

    this.driveK = 1; this.drivePre = 1; this.driveNorm = 1;
    this.driveWet = new Smooth(); this.driveDry = new Smooth();
    this.up1L = new HalfBandFir(HB1_TAPS, 6); this.up2L = new HalfBandFir(HB2_TAPS, 6);
    this.dn2L = new HalfBandFir(HB2_TAPS, 6); this.dn1L = new HalfBandFir(HB1_TAPS, 6);
    this.up1R = new HalfBandFir(HB1_TAPS, 6); this.up2R = new HalfBandFir(HB2_TAPS, 6);
    this.dn2R = new HalfBandFir(HB2_TAPS, 6); this.dn1R = new HalfBandFir(HB1_TAPS, 6);
    this.dryL = new DelayLine(DRIVE_LATENCY + 4); this.dryR = new DelayLine(DRIVE_LATENCY + 4);
    this.driveSilenced = false;
    this.monoRun = false;

    this.chPhase = 0; this.chRate = 0.6; this.chDepth = 0.5;
    this.chWet = new Smooth(); this.chDry = new Smooth();
    this.chDl1 = new DelayLine((0.05 * sampleRate) | 0);
    this.chDl2 = new DelayLine((0.05 * sampleRate) | 0);
    this.chorusOff = false; this.chorusGated = false;

    this.dlTime = new Smooth(); this.dlFb = new Smooth();
    this.dlWet = new Smooth(); this.dlDry = new Smooth();
    this.dlL = new DelayLine(((2 * sampleRate) | 0) + 4);
    this.dlR = new DelayLine(((2 * sampleRate) | 0) + 4);
    this.dlDamp = new Biquad();
    this.delayOff = false; this.delayGated = false;

    this.verbWet = new Smooth(); this.verbDry = new Smooth();
    this.roomSize = 0.84; this.verbOff = false; this.verbGated = false;

    this.compThrDb = new Smooth(); this.compMakeup = new Smooth();
    this.compWet = new Smooth(); this.compDry = new Smooth();
    this.compEnv = 0;
    this.compAtk = 1 - Math.exp(-1 / (0.010 * sampleRate));
    this.compRel = 1 - Math.exp(-1 / (0.200 * sampleRate));
    this.compOff = false; this.compGated = false;

    this.masterGain = new Smooth();
    this.dcL = new Biquad(); this.dcR = new Biquad();

    for (const s of [this.driveWet, this.driveDry, this.chWet, this.chDry, this.dlFb,
      this.dlWet, this.dlDry, this.verbWet, this.verbDry, this.compThrDb,
      this.compMakeup, this.compWet, this.compDry, this.masterGain]) s.setTime(0.02);
    this.dlTime.setTime(0.08);
    this.driveDry.snap(1); this.chDry.snap(1); this.dlDry.snap(1); this.verbDry.snap(1); this.compDry.snap(1);

    this.dcL.highpass(8, 0.707); this.dcR.highpass(8, 0.707);
    this.dlDamp.lowpass(4500, 0.707);

    // WebAudio's DynamicsCompressor applies a spec-defined makeup gain
    // ((1/c(1))^0.6, c = the static curve at 0 dBFS). The old web master limiter
    // WAS that node, so its ~4.5 dB makeup stays ahead of the lookahead limiter
    // or every patch drops in level.
    const c1 = Math.pow(1 / LIM_THR, 1 / LIM_RATIO - 1);
    this.lim = new LookaheadLimiter(Math.pow(1 / c1, 0.6));
  }

  latencySamples() { return DRIVE_LATENCY + this.lim.la; }

  reset() {
    this.chDl1.reset(); this.chDl2.reset(); this.dlL.reset(); this.dlR.reset();
    this.dryL.reset(); this.dryR.reset();
    for (const c of this.combL) c.reset();
    for (const c of this.combR) c.reset();
    for (const a of this.apL) a.reset();
    for (const a of this.apR) a.reset();
    this.dcL.reset(); this.dcR.reset(); this.dlDamp.reset();
    this.eqLoL.reset(); this.eqLoR.reset(); this.eqMidL.reset();
    this.eqMidR.reset(); this.eqHiL.reset(); this.eqHiR.reset();
    this.up1L.reset(); this.up2L.reset(); this.dn2L.reset(); this.dn1L.reset();
    this.up1R.reset(); this.up2R.reset(); this.dn2R.reset(); this.dn1R.reset();
    this.compEnv = 0;
    this.lim.reset();
    this.chPhase = 0;
    this.monoRun = false;
    this.driveSilenced = this.chorusGated = this.delayGated = this.verbGated = this.compGated = false;
  }

  setParams(p) {
    // 3-band tone EQ (first FX). Gains apply only when on; off forces 0 dB, an
    // exact unity bypass. Shelves at fixed corners, mid bell sweepable at Q 0.9.
    const eqOn = p[FXEQ_ON] > 0.5;
    const loDb = eqOn ? p[FXEQ_LOW] : 0;
    const midDb = eqOn ? p[FXEQ_MID] : 0;
    const hiDb = eqOn ? p[FXEQ_HIGH] : 0;
    const mFreq = p[FXEQ_MFREQ];
    this.eqLoL.lowShelf(120, loDb); this.eqLoR.lowShelf(120, loDb);
    this.eqMidL.peaking(mFreq, 0.9, midDb); this.eqMidR.peaking(mFreq, 0.9, midDb);
    this.eqHiL.highShelf(6000, hiDb); this.eqHiR.highShelf(6000, hiDb);

    const amt = p[FXDRIVE_AMT];
    this.drivePre = 1 + amt * 2;
    this.driveK = 1 + amt * 12;
    this.driveNorm = 1 / (this.drivePre * Math.tanh(this.driveK));
    // No separate OFF flag: mixGate gives wet 0 for OFF and for MIX 0 alike, so
    // `driveWet.target === 0` covers both and drives the one silence gate below.
    const dOn = p[FXDRIVE_ON] > 0.5;
    this.driveWet.target = mixGate(dOn, p[FXDRIVE_MIX], true);
    this.driveDry.target = mixGate(dOn, p[FXDRIVE_MIX], false);

    this.chRate = p[FXCHORUS_RATE];
    this.chDepth = p[FXCHORUS_DEPTH];
    const cOn = p[FXCHORUS_ON] > 0.5;
    this.chorusOff = !cOn;
    this.chWet.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8, true);
    this.chDry.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8, false);

    this.dlTime.target = p[FXDELAY_TIME];
    this.dlFb.target = p[FXDELAY_FB];
    const delOn = p[FXDELAY_ON] > 0.5;
    this.delayOff = !delOn;
    this.dlWet.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85, true);
    this.dlDry.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85, false);

    // SIZE maps to roomsize/decay — a longer, brighter tail with size. No buffer
    // is rebuilt, so a SIZE sweep never cuts the tail.
    const size = p[FXREVERB_SIZE];
    this.roomSize = 0.7 + size * 0.28;
    const damp = 0.4 - size * 0.2;
    for (let i = 0; i < 8; i++) {
      this.combL[i].feedback = this.combR[i].feedback = this.roomSize;
      this.combL[i].damp1 = this.combR[i].damp1 = damp;
      this.combL[i].damp2 = this.combR[i].damp2 = 1 - damp;
    }
    const rOn = p[FXREVERB_ON] > 0.5;
    this.verbOff = !rOn;
    this.verbWet.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9, true);
    this.verbDry.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9, false);

    // Leveling compressor — implicit spec makeup (the static curve at 0 dBFS)
    // times the user MAKEUP, so quiet patches lift while the 4:1 curve tames
    // loud ones.
    const thrDb = p[FXCOMP_THR];
    this.compThrDb.target = thrDb;
    const implicit = Math.pow(10, -0.6 * compGainDb(0, thrDb) / 20);
    this.compMakeup.target = implicit * Math.pow(10, p[FXCOMP_GAIN] / 20);
    const kOn = p[FXCOMP_ON] > 0.5;
    this.compOff = !kOn;
    this.compWet.target = mixGate(kOn, 1, true);
    this.compDry.target = mixGate(kOn, 1, false);

    const vol = p[MASTER_VOLUME];
    this.masterGain.target = vol * vol * 1.6;
  }

  // One channel of a block through the 4x oversampled shaper: interpolate to
  // 2x then 4x, shape all 4n samples in one flat loop, then decimate back.
  // Stage at a time rather than six filter calls per sample; the decimators
  // keep the phase aligned with the integer DRIVE_LATENCY group delay.
  driveBlock(u1, u2, d2, d1, out, n) {
    u1.interpBlock(OS_IN, OS_X2, n);
    u2.interpBlock(OS_X2, OS_X4, 2 * n);
    // tanh is bounded — no pre-clamp (a hard clamp is its own nonsmooth
    // nonlinearity). The old web path was a 513-point WaveShaper table read at
    // 2x with linear interpolation between entries.
    const K = this.driveK, norm = this.driveNorm, m = 4 * n;
    for (let i = 0; i < m; i++) OS_X4[i] = Math.tanh(OS_X4[i] * K) * norm;
    d2.decimBlock(OS_X4, OS_Y2, 2 * n);
    d1.decimBlock(OS_Y2, out, n);
  }

  // Resync the right-hand drive state from the left. The mono fast path skips
  // the right channel while its input is identical to the left's, so had those
  // filters run, their state would be exactly this.
  syncRight() {
    this.up1R.copyFrom(this.up1L); this.up2R.copyFrom(this.up2L);
    this.dn2R.copyFrom(this.dn2L); this.dn1R.copyFrom(this.dn1L);
    this.dryR.copyFrom(this.dryL);
    this.monoRun = false;
  }

  process(L, R, n) {
    // Gate only when OFF; mix == 0 while ON must keep state accumulation alive.
    // The drive's wet gain being zero — MIX 0 while ON, as well as OFF — means
    // the oversampler's output is multiplied by zero, so it is not run at all.
    // The plugin still runs it whenever the stage is ON; the only difference is
    // that here the FIR history restarts from zero when MIX leaves 0, under a
    // wet gain below 1e-6 that then ramps in over 20 ms. Every other stage has
    // a tail or a feedback loop that MUST keep accumulating at MIX 0, so this
    // shortcut applies to the drive alone.
    const driveSilent = this.driveWet.target === 0 && Math.abs(this.driveWet.cur) < 1e-6;
    const chorusGate = this.chorusOff && this.chWet.target === 0 && Math.abs(this.chWet.cur) < 1e-6;
    const delayGate = this.delayOff && this.dlWet.target === 0 && Math.abs(this.dlWet.cur) < 1e-6;
    const verbGate = this.verbOff && this.verbWet.target === 0 && Math.abs(this.verbWet.cur) < 1e-6;
    const compGate = this.compOff && this.compWet.target === 0 && Math.abs(this.compWet.cur) < 1e-6;

    if (driveSilent && !this.driveSilenced) {
      // Both targets are exact here: mixGate gives wet 0 / dry 1 for OFF and
      // for MIX 0 alike, so snapping is snapping to the target and the skipped
      // path is exactly `1 * dry`, not `(1 - eps) * dry`.
      this.driveWet.snap(0); this.driveDry.snap(1);
      this.up1L.reset(); this.up2L.reset(); this.dn2L.reset(); this.dn1L.reset();
      this.up1R.reset(); this.up2R.reset(); this.dn2R.reset(); this.dn1R.reset();
    }
    this.driveSilenced = driveSilent;
    if (chorusGate && !this.chorusGated) { this.chWet.snap(0); this.chDry.snap(1); this.chDl1.reset(); this.chDl2.reset(); }
    if (delayGate && !this.delayGated) { this.dlWet.snap(0); this.dlDry.snap(1); this.dlL.reset(); this.dlR.reset(); this.dlDamp.reset(); }
    if (verbGate && !this.verbGated) {
      this.verbWet.snap(0); this.verbDry.snap(1);
      for (const c of this.combL) c.reset();
      for (const c of this.combR) c.reset();
      for (const a of this.apL) a.reset();
      for (const a of this.apR) a.reset();
    }
    if (compGate && !this.compGated) { this.compWet.snap(0); this.compDry.snap(1); this.compEnv = 0; }

    this.chorusGated = chorusGate;
    this.delayGated = delayGate; this.verbGated = verbGate; this.compGated = compGate;

    const combL = this.combL, combR = this.combR, apL = this.apL, apR = this.apR;
    fxScratch(n);

    // ---- 3-band tone EQ (first FX; 0 dB coeffs = transparent) ----
    // Into double scratch, not back into L/R: the whole chain stays in double
    // precision until the final write, as the per-sample loop it replaces did.
    for (let i = 0; i < n; i++) {
      FX_EQL[i] = this.eqHiL.process(this.eqMidL.process(this.eqLoL.process(L[i])));
      FX_EQR[i] = this.eqHiR.process(this.eqMidR.process(this.eqLoR.process(R[i])));
    }

    // ---- drive (4x oversampled tanh waveshaper), a block at a time ----
    // The dry/bypass path always runs through a DRIVE_LATENCY delay so the
    // dry/wet mix stays time-aligned with the shaper's FIR group delay and the
    // chain latency is constant whether drive is active or gated.
    //
    // Mono fast path: while L and R carry identical samples the drive runs once
    // and the right-hand filters are left alone; they are resynced from the left
    // the moment the channels differ, which costs nothing because their state
    // would have been exactly the left's. WT-1 sits here whenever unison is 1
    // and SPREAD and PAN are 0.
    let mono = true;
    for (let i = 0; i < n; i++) if (FX_EQL[i] !== FX_EQR[i]) { mono = false; break; }
    if (!mono && this.monoRun) this.syncRight();
    this.monoRun = mono;

    this.dryL.blockRead(FX_EQL, FX_DRYL, n, DRIVE_LATENCY + 1);
    if (!mono) this.dryR.blockRead(FX_EQR, FX_DRYR, n, DRIVE_LATENCY + 1);
    if (driveSilent) {
      for (let i = 0; i < n; i++) FX_EQL[i] = FX_DRYL[i];
      if (mono) { for (let i = 0; i < n; i++) FX_EQR[i] = FX_DRYL[i]; }
      else { for (let i = 0; i < n; i++) FX_EQR[i] = FX_DRYR[i]; }
    } else {
      const pre = this.drivePre;
      for (let i = 0; i < n; i++) OS_IN[i] = pre * FX_EQL[i];
      this.driveBlock(this.up1L, this.up2L, this.dn2L, this.dn1L, FX_WETL, n);
      if (!mono) {
        for (let i = 0; i < n; i++) OS_IN[i] = pre * FX_EQR[i];
        this.driveBlock(this.up1R, this.up2R, this.dn2R, this.dn1R, FX_WETR, n);
      }
      for (let i = 0; i < n; i++) {
        const wet = this.driveWet.next(), dry = this.driveDry.next();
        const dl = dry * FX_DRYL[i] + wet * FX_WETL[i];
        FX_EQL[i] = dl;
        FX_EQR[i] = mono ? dl : dry * FX_DRYR[i] + wet * FX_WETR[i];
      }
    }

    for (let i = 0; i < n; i++) {
      let l = FX_EQL[i], r = FX_EQR[i];

      // ---- chorus (two modulated taps, stereo) ----
      if (!chorusGate) {
        this.chPhase += this.chRate / sampleRate;
        if (this.chPhase >= 1) this.chPhase -= 1;
        const lfo = Math.sin(TWO_PI * this.chPhase);
        const depth = 0.0008 + this.chDepth * 0.0045;
        const mono = 0.5 * (l + r);
        this.chDl1.write(mono); this.chDl2.write(mono);
        const c1 = this.chDl1.readHermite((0.012 + depth * lfo) * sampleRate);
        const c2 = this.chDl2.readHermite((0.017 - depth * 0.8 * lfo) * sampleRate);
        const wet = this.chWet.next(), dry = this.chDry.next();
        l = dry * l + wet * c1;
        r = dry * r + wet * c2;
      }

      // ---- ping-pong delay ----
      if (!delayGate) {
        const dt = this.dlTime.next() * sampleRate;
        const fb = this.dlFb.next();
        const dL = this.dlL.readHermite(dt);
        const dR = this.dlR.readHermite(dt);
        const mono = 0.5 * (l + r);
        this.dlL.write(mono + fb * dR);
        this.dlR.write(this.dlDamp.process(fb * dL));
        const wet = this.dlWet.next(), dry = this.dlDry.next();
        l = dry * l + wet * dL;
        r = dry * r + wet * dR;
      }

      // ---- reverb (Freeverb) ----
      if (!verbGate) {
        const input = (l + r) * 0.015; // fixed input gain (Freeverb convention)
        let outL = 0, outR = 0;
        for (let c = 0; c < 8; c++) { outL += combL[c].process(input); outR += combR[c].process(input); }
        for (let a = 0; a < 4; a++) { outL = apL[a].process(outL); outR = apR[a].process(outR); }
        const wet = this.verbWet.next(), dry = this.verbDry.next();
        l = dry * l + wet * outL;
        r = dry * r + wet * outR;
      }

      // ---- leveling compressor (WebAudio DynamicsCompressor semantics) ----
      if (!compGate) {
        const al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
        const pk = al > ar ? al : ar;
        this.compEnv += (pk - this.compEnv) * (pk > this.compEnv ? this.compAtk : this.compRel);
        const thrDb = this.compThrDb.next();
        let cg = 1;
        if (this.compEnv > 1e-6) cg = Math.pow(10, compGainDb(20 * Math.log10(this.compEnv), thrDb) / 20);
        cg *= this.compMakeup.next();
        const wet = this.compWet.next(), dry = this.compDry.next();
        l = dry * l + wet * (l * cg);
        r = dry * r + wet * (r * cg);
      }

      // ---- master gain ----
      const g = this.masterGain.next();
      l *= g; r *= g;

      // ---- DC block ----
      l = this.dcL.process(l);
      r = this.dcR.process(r);

      // ---- lookahead safety limiter (makeup inside, -1 dBFS ceiling) ----
      this.lim.process(l, r);
      L[i] = this.lim.outL; R[i] = this.lim.outR;
    }
  }
}

class FableProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    // Flat parameter store (finding W2): p[index], never p['some.string'].
    this.p = new Float64Array(NUM_PARAMS);
    this.rng = new Rng(0x9e3779b9);
    // Sample-rate-mapped one-pole coefficients (finding W1): the same analog
    // pole at any rate is p' = p^(48k/sr); the pink filter's input gains are
    // rescaled to hold each stage's low-frequency gain, so the pink spectrum
    // matches the 48 kHz reference. Mirrors Engine::prepare.
    const rr = 48000 / sampleRate;
    this.dcR = Math.pow(DC_R, rr);
    this.pinkP = new Float64Array(6);
    this.pinkG = new Float64Array(6);
    for (let i = 0; i < 6; i++) {
      this.pinkP[i] = Math.pow(PINK_P[i], rr);
      this.pinkG[i] = i === 5
        ? PINK_G[5] * (1 + this.pinkP[5]) / (1 + PINK_P[5])
        : PINK_G[i] * (1 - this.pinkP[i]) / (1 - PINK_P[i]);
    }
    this.tables = [];
    this.voices = [];
    for (let i = 0; i < NVOICES; i++) this.voices.push(new Voice());
    this.bend = 0;
    this.bpm = 120;
    // Virtual transport: beats (quarter notes) since audio start. Standalone has
    // no host transport, so synced LFOs phase-lock to this clock (downbeat = t0).
    // Accumulated (not samples*bpm) so a tempo change doesn't rescale the past.
    this.transportBeats = 0;
    this.gLfo1 = new LFO(); this.gLfo1.reset(this.rng);
    this.gLfo2 = new LFO(); this.gLfo2.reset(this.rng);
    this.lastPitch = 60;
    this.held = []; // press-order stack of held note numbers, newest last, tracked mode-independently
    this.clock = 0;
    // ---- note sequencer state ----
    this.seqPlaying = false;
    this.seqStep = -1;
    this.seqPats = new Uint8Array(SEQ_NPATTERNS * SEQ_STEPS * SEQ_STRIDE);
    for (let i = 2; i < this.seqPats.length; i += SEQ_STRIDE) this.seqPats[i] = 1; // oct byte: 1 = oct 0
    this.seqChain = [0];
    this.seqChainPos = 0;
    this.seqToNext = 0; // samples until the next step fires
    this.seqOffQueue = []; // { note, remaining } per sounding seq note (poly)
    this.seqLastNote = -1; // most recent fired note (-1 = none), for viz
    // ---- hosted clip transport (SQ-4) ----
    this.hosted = false;
    this.hostBpm = 120;
    this.hostSwing = 0;
    this.hostAnchor = 0; // songStartFrame — the shared timebase's beat zero
    this.clip = null; // { data: Uint8Array, bars }
    this.clipPend = null; // { data, bars, at } — waiting for its atFrame
    this.clipStopAt = -1;
    this.clipStep = -1; // absolute step within the clip (0 .. bars*16-1)
    this.clipToNext = 0;
    // Absolute frame at the start of the chunk being rendered. `currentFrame`
    // only advances per host block; the split loop (finding W3) needs the
    // chunk's own position for clip scheduling.
    this.frameNow = 0;
    this.vizCount = 0;
    this.tmpL = new Float32Array(128);
    this.tmpR = new Float32Array(128);
    // filter routing scratch: osc-B split source + each filter's output
    this.bL = new Float32Array(128);
    this.bR = new Float32Array(128);
    this.f1L = new Float32Array(128);
    this.f1R = new Float32Array(128);
    this.f2L = new Float32Array(128);
    this.f2R = new Float32Array(128);
    // Per-voice modulation scratch (reused per render — no per-call allocation).
    // _modAccum[i]: route sum for param i. _pm: a full copy of `p` with the
    // modulated params overwritten, so every read is a plain array index.
    this._modAccum = new Float64Array(NUM_PARAMS);
    this._pm = new Float64Array(NUM_PARAMS);
    this._srcs = new Float64Array(6);
    // Live-mod telemetry: the viz voice's route sums per MOD_DESTS index,
    // re-snapshotted every block, sent at the viz cadence. modIdleSent starts
    // true — the UI's default state is already "idle", no terminator needed.
    this.modViz = new Float32Array(DST_TARGET.length);
    this.modVizAny = false;
    this.modIdleSent = true;
    // The FX chain now runs here rather than as native WebAudio nodes
    // (finding W6): same algorithm as juce/source/dsp/Fx.cpp, testable offline.
    this.fx = new Fx();
    this.fxLatency = this.fx.latencySamples();
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  onMsg(d) {
    switch (d.t) {
      // Non-finite param values are dropped at this single choke point: a NaN
      // that reached `p` would latch into phases / env levels and stick there.
      case 'init':
        for (const k in d.params) {
          const i = PID[k], v = d.params[k];
          if (i !== undefined && Number.isFinite(v)) this.p[i] = v;
        }
        this.bpm = Math.min(1000, Math.max(1, this.p[SEQ_BPM] || 120));
        // The FX drive FIR + limiter lookahead add a fixed latency the main
        // thread needs for scope/transport alignment; the plugin reports the
        // same number through setLatencySamples.
        this.port.postMessage({ t: 'latency', n: this.fxLatency });
        break;
      case 'p': {
        // The string id is resolved to its index here, once per message —
        // never on the render thread (finding W2).
        const i = PID[d.k];
        if (i !== undefined && Number.isFinite(d.v)) {
          this.p[i] = d.v;
          // The web build has no host transport: while the sequencer is the
          // tempo authority, synced LFOs follow it.
          if (i === SEQ_BPM) this.bpm = Math.min(1000, Math.max(1, d.v));
        }
        break;
      }
      case 'tables':
        this.tables = d.list.map((x) => this.makeTable(x));
        break;
      // Incremental table publication (finding W4): the pool is 8.6 MB+, so a
      // user-table edit sends only the changed slot, with its buffer
      // transferred rather than structured-clone-copied.
      case 'table': {
        const i = d.i | 0;
        if (i >= 0 && i < 256) this.tables[i] = this.makeTable(d);
        break;
      }
      case 'tablecount':
        this.tables.length = Math.max(0, Math.min(256, d.n | 0));
        break;
      case 'on': this.noteOn(d.n, d.v); break;
      case 'off': this.noteOff(d.n); break;
      case 'bend': this.bend = d.s; break;
      case 'bpm': this.bpm = d.v > 1 ? Math.min(d.v, 1000) : 120; break;
      case 'pats': this.seqPats = new Uint8Array(d.data); break;
      case 'chain':
        if (Array.isArray(d.list) && d.list.length) {
          this.seqChain = d.list.map((x) => x | 0);
          this.seqChainPos = Math.min(this.seqChainPos, this.seqChain.length - 1);
        }
        break;
      case 'play':
        if (this.hosted) break; // conductor owns the transport
        this.seqGateOff(); // restarting must not orphan an old gate
        this.seqPlaying = true;
        this.seqStep = -1;
        this.seqChainPos = 0;
        this.seqToNext = 0;
        break;
      case 'stop':
        this.seqPlaying = false;
        this.seqStep = -1;
        this.seqGateOff();
        break;
      case 'panic':
        for (const v of this.voices) v.kill();
        this.seqOffQueue.length = 0;
        this.held.length = 0;
        this.seqLastNote = -1;
        this.clip = null;
        this.clipPend = null;
        this.clipStopAt = -1;
        this.clipStep = -1;
        this.fx.reset();
        break;
      case 'host': this.hosted = !!d.on; break;
      case 'tempo':
        if (Number.isFinite(d.bpm)) { this.hostBpm = d.bpm; this.bpm = Math.min(1000, Math.max(1, d.bpm)); }
        if (Number.isFinite(d.swing)) this.hostSwing = d.swing;
        if (Number.isFinite(d.anchor)) this.hostAnchor = d.anchor;
        break;
      case 'clip':
        this.clipPend = { data: new Uint8Array(d.data), bars: Math.max(1, d.bars | 0), at: +d.atFrame || 0 };
        this.clipStopAt = -1; // a new launch supersedes a pending stop
        break;
      case 'clipstop':
        this.clipPend = null; // a stop cancels a pending launch
        this.clipStopAt = +d.atFrame || 0;
        break;
      case 'clipupdate': {
        // Hosted hot-swap (SQ-4): replace pattern bytes in place. Position is
        // derived arithmetic, so a live swap never moves the playhead.
        const data = new Uint8Array(d.data);
        const bars = Math.max(1, d.bars | 0);
        if (this.clipPend) {
          this.clipPend = { data, bars, at: this.clipPend.at };
        } else if (this.clip) {
          const resized = bars !== this.clip.bars;
          this.clip = { data, bars };
          // Re-derive the phase only on a bar-count change (plain modulo can
          // land a grown clip half a cycle off). Same-length edits — every
          // sequencer click — are a pure data swap: touching the phase inside
          // a swing/quantization window would skip a step and desync devices.
          if (resized && this.clipStep >= 0) this.clipStep = this.clipPhase(Math.floor);
        }
        break;
      }
    }
  }

  // Wrap a published table description. `buf` is either transferred (single
  // slot) or structured-cloned (full list); either way the Float32Array is a
  // view onto it, never a copy.
  makeTable(x) {
    return {
      frames: x.frames, mips: x.mips, size: x.size, mask: x.size - 1,
      data: new Float32Array(x.buf),
    };
  }

  // ---------- note sequencer ----------
  seqRead(pat, s) {
    const o = (pat * SEQ_STEPS + s) * SEQ_STRIDE;
    const flags = this.seqPats[o];
    return {
      on: (flags & 1) !== 0,
      acc: (flags & 2) !== 0,
      duration: Math.max(1, Math.min(63, (flags >> 2) & 0x3f)),
      semi: Math.min(11, this.seqPats[o + 1]) + 12 * (Math.min(2, this.seqPats[o + 2]) - 1),
    };
  }

  // Gate off every sounding sequencer note (stop / clip swap / clip stop).
  seqGateOff() {
    for (const e of this.seqOffQueue) this.noteOff(e.note);
    this.seqOffQueue.length = 0;
    this.seqLastNote = -1;
  }

  // Schedule an independent note-off for `note` at `remaining` samples. A
  // retriggered pitch supersedes any pending off for it (one voice per note),
  // so overlapping DIFFERENT notes ring concurrently while a repeated pitch
  // takes the new duration.
  seqScheduleOff(note, remaining) {
    // Compact in place: the audio thread must not allocate an array for each
    // sequencer event.
    let w = 0;
    for (let i = 0; i < this.seqOffQueue.length; i++) {
      const e = this.seqOffQueue[i];
      if (e.note !== note) this.seqOffQueue[w++] = e;
    }
    this.seqOffQueue.length = w;
    this.seqOffQueue.push({ note, remaining });
    this.seqLastNote = note;
  }

  // ---------- hosted clip transport ----------
  // A clip is bars*16 steps of the same 3-byte layout, bar-major. Pending
  // commands execute in the render quantum containing their atFrame — every
  // device on the shared context resolves the same frame to the same block.
  clipRead(abs, lane = 0) {
    const o = (abs * WT_POLY_LANES + lane) * SEQ_STRIDE;
    const flags = this.clip.data[o];
    return {
      on: (flags & 1) !== 0,
      acc: (flags & 2) !== 0,
      duration: Math.max(1, Math.min(63, (flags >> 2) & 0x3f)),
      semi: Math.min(11, this.clip.data[o + 1]) + 12 * (Math.min(2, this.clip.data[o + 2]) - 1),
    };
  }

  hostTick(n) {
    const end = this.frameNow + n;
    if (this.clipStopAt >= 0 && this.clipStopAt < end) {
      this.clipStopAt = -1;
      if (this.clip) {
        this.clip = null;
        this.clipStep = -1;
        this.seqGateOff();
      }
      // ack even when nothing was playing — the stop may have targeted a
      // pending-only launch and the conductor clears its STOP marker on this
      this.port.postMessage({ t: 'clipstop', frame: this.frameNow });
    }
    if (this.clipPend && this.clipPend.at < end) {
      this.clip = this.clipPend;
      this.clipPend = null;
      // Phase-lock to the shared timebase: enter at the global song position
      // modulo the clip length, so a (re)launch can never desync devices —
      // position is derived from the anchor, never restarted at step 0.
      this.clipStep = this.clipPhase(Math.round) - 1;
      this.clipToNext = 0;
      this.seqGateOff(); // the old clip's tail note ends where the new clip starts
      this.port.postMessage({ t: 'clipstart', frame: this.frameNow });
    }
    if (this.clip) {
      if (this.clipToNext <= 0) this.clipFire();
      this.clipToNext -= n;
    }
  }

  // Global step index (mod clip length) at the current frame. Activation
  // rounds (atFrame sits at a boundary, block-quantized slightly early);
  // mid-flight resizes floor (the last fired step).
  clipPhase(quantize) {
    const bpm = Math.max(60, Math.min(200, this.hostBpm || 120));
    const dur = (60 / bpm / 4) * sampleRate;
    const total = this.clip.bars * SEQ_STEPS;
    const idx = quantize(Math.max(0, this.frameNow - this.hostAnchor) / dur);
    return ((idx % total) + total) % total;
  }

  clipFire() {
    const bpm = Math.max(60, Math.min(200, this.hostBpm || 120));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = Math.min(1, Math.max(0, this.hostSwing || 0));
    const total = this.clip.bars * SEQ_STEPS;
    const abs = (this.clipStep + 1) % total;
    const s = abs % SEQ_STEPS;
    const chord = Array.from({ length: WT_POLY_LANES }, (_, lane) => this.clipRead(abs, lane)).filter((st) => st.on);
    // Mono: the melody lives in the first active lane; the rest of a chord
    // step would just steal the line note for note.
    if (this.p[MASTER_MONO] && chord.length > 1) chord.length = 1;

    if (chord.length) {
      const root = (this.p[SEQ_ROOT] | 0) || 48;
      for (const st of chord) {
        const note = root + st.semi;
        this.noteOn(note, st.acc ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL);
        this.seqScheduleOff(note, st.duration * dur);
      }
    }

    this.clipStep = abs;
    const offNow = s % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    const sNext = (s + 1) % SEQ_STEPS;
    const offNext = sNext % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    // Schedule the next step at its absolute anchor-grid time. A free-running
    // countdown (dur - offNow + offNext) drops the block-quantization residue
    // each fire and drifts late without bound against the shared timebase.
    const idx = Math.round((this.frameNow - this.hostAnchor - offNow) / dur);
    this.clipToNext = this.hostAnchor + (idx + 1) * dur + offNext - this.frameNow;
    this.port.postMessage({ t: 'pos', step: s, bar: (abs / SEQ_STEPS) | 0 });
  }

  seqFire() {
    const bpm = Math.max(60, Math.min(200, this.p[SEQ_BPM] || 120));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = Math.min(1, Math.max(0, this.p[SEQ_SWING] || 0));
    if (this.seqStep + 1 >= SEQ_STEPS) {
      this.seqStep = -1;
      this.seqChainPos = (this.seqChainPos + 1) % this.seqChain.length;
    }
    const s = this.seqStep + 1;
    const pat = this.seqChain[this.seqChainPos] | 0;
    const st = this.seqRead(pat, s);

    if (st.on) {
      const root = (this.p[SEQ_ROOT] | 0) || 48;
      const n = root + st.semi;
      const vel = st.acc ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL;
      this.noteOn(n, vel);
      this.seqScheduleOff(n, st.duration * dur);
    }

    this.seqStep = s;
    const offNow = s % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    const sNext = (s + 1) % SEQ_STEPS;
    const offNext = sNext % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    this.seqToNext = dur - offNow + offNext;
    this.port.postMessage({ t: 'step', s, pat });
  }

  noteOn(n, vel) {
    // Track held keys mode-independently (in place compaction, no extra
    // allocation beyond one small per-event push, same profile as
    // seqOffQueue) so a mono<->poly flip can't strand a key that was only
    // ever tracked in the other mode. Re-press of a held pitch moves it to
    // the top of the stack.
    let w = 0;
    for (let i = 0; i < this.held.length; i++) if (this.held[i] !== n) this.held[w++] = this.held[i];
    this.held.length = w;
    this.held.push(n);

    if (this.p[MASTER_MONO]) this.monoNoteOn(n, vel);
    else this.polyNoteOn(n, vel);
  }

  monoNoteOn(n, vel) {
    // The newest gated voice carries the mono line; release any others (they
    // can only exist right after a poly -> mono flip).
    let voice = null;
    for (const v of this.voices) {
      if (!v.gate) continue;
      if (!voice) { voice = v; continue; }
      if (v.age > voice.age) { voice.noteOff(); voice = v; } else v.noteOff();
    }
    if (voice) {
      // Legato: retune only — renderVoice glides v.pitch toward v.note, and
      // not calling Voice.noteOn is what keeps the envelopes running.
      voice.note = n;
      this.lastPitch = n;
      return;
    }
    this.polyNoteOn(n, vel); // from silence: normal trigger (glide-from-lastPitch, steal fade)
  }

  monoNoteOff(n) {
    for (const v of this.voices) {
      if (v.pending && v.pending.n === n) v.pending = null;
      if (v.gate && v.note === n) {
        const back = this.held[this.held.length - 1];
        if (back !== undefined) { v.note = back; this.lastPitch = back; } // fall back to the held note, no retrigger
        else v.noteOff();
      }
    }
  }

  polyNoteOn(n, vel) {
    let voice = this.voices.find((v) => v.gate && v.note === n);
    if (!voice) voice = this.voices.find((v) => !v.active);
    if (!voice) {
      // steal: prefer released voices, else oldest
      let best = null;
      for (const v of this.voices) {
        if (!best) { best = v; continue; }
        const vRel = v.gate ? 1 : 0, bRel = best.gate ? 1 : 0;
        if (vRel < bRel || (vRel === bRel && v.age < best.age)) best = v;
      }
      voice = best;
    }
    const glide = this.p[MASTER_GLIDE] || 0;
    const start = glide > 0.001 ? this.lastPitch : n;
    this.lastPitch = n;
    if (voice.ampEnv.state !== 0 && voice.ampEnv.level > 1e-3) {
      // Steal / same-note retrigger of an audible voice: hard-resetting phases
      // and filter state under a hot envelope clicks. Fade out instead (Env
      // state 5) and start the note once the voice reaches silence.
      voice.pending = { n, vel, start };
      voice.gate = false;
      voice.ampEnv.state = 5;
    } else {
      voice.noteOn(n, vel, start, this.clock++, this.rng, 1, 1);
    }
  }

  noteOff(n) {
    let w = 0;
    for (let i = 0; i < this.held.length; i++) if (this.held[i] !== n) this.held[w++] = this.held[i];
    this.held.length = w;

    if (this.p[MASTER_MONO]) { this.monoNoteOff(n); return; }
    for (const v of this.voices) {
      // A note released before its steal fade finished must not start at all.
      if (v.pending && v.pending.n === n) v.pending = null;
      if (v.gate && v.note === n) v.noteOff();
    }
  }

  // Configure one oscillator's per-block render state. Returns true if audible.
  // Reads the modulated snapshot `pm` for the per-param dests (pos/level/pan/detune/
  // spread); pitch and the pan global offset stay as direct additive terms.
  setupOsc(o, base, voice, pm, mPitch, mPan, n) {
    const p = this.p;
    if (!p[base + O_ON]) return false;
    const table = this.tables[p[base + O_TABLE] | 0];
    if (!table) return false;

    const basePitch = voice.pitch + this.bend + p[base + O_OCT] * 12 + p[base + O_SEMI] + p[base + O_FINE] / 100 + mPitch * 12;
    const freq = 440 * Math.pow(2, (basePitch - 69) / 12);
    if (!(freq > 0 && freq <= sampleRate * 0.45)) return false; // inverted: NaN-safe

    let level = Math.min(1.2, Math.max(0, pm[base + O_LEVEL]));
    level *= level;
    if (!(level >= 1e-5)) return false; // inverted: NaN-safe

    const uni = Math.max(1, Math.min(MAXUNI, p[base + O_UNI] | 0));
    const det = pm[base + O_DET];
    const spr = pm[base + O_SPR];
    const blend = Math.min(1, Math.max(0, pm[base + O_BLEND])); // clamp matches JUCE
    const basePan = Math.max(-1, Math.min(1, pm[base + O_PAN] + mPan));

    // position smoothing (avoids zipper on fast morph modulation)
    let pos = Math.min(1, Math.max(0, pm[base + O_POS]));
    if (o.posSm < 0) o.posSm = pos;
    o.posSm += (pos - o.posSm) * smoothCoef(n, POS_TAU * sampleRate);
    const posF = o.posSm * (table.frames - 1);
    const f0 = posF | 0;
    const f1 = Math.min(table.frames - 1, f0 + 1);
    o.ft = posF - f0;

    const cps = freq / sampleRate;
    // |det|: modulation can push detune negative; the widest unison ratio is
    // 2^(|det|*50/1200) regardless of sign, and underestimating it would pick
    // a mip one notch too fine (the alias headroom is only 0.074 oct).
    const maxRatio = Math.pow(2, (Math.abs(det) * 50) / 1200);

    // Continuous mip selection. mipF is the exact (real-valued) mip the pitch
    // calls for; ceil(mipF) is the alias-free choice. For the first W octaves
    // above a mip boundary we crossfade from the finer mip — which is still
    // alias-free there, because mips are built against 0.475*sr while Nyquist
    // is 0.5*sr (0.5/0.475 = 2^0.074 of headroom). Result: glides and bends
    // never step in brightness, and static pitches never fold.
    const W = 0.07;
    const mipF = Math.log2((cps * maxRatio * 1024) / 0.475);
    let mip = 0, mipBlend = 0;
    if (mipF > 0) {
      mip = Math.min(table.mips - 1, Math.ceil(mipF));
      const over = mipF - (mip - 1); // octaves above the previous boundary
      if (over < W) mipBlend = 1 - over / W;
    }
    const fineMip = mip > 0 ? mip - 1 : 0;

    o.off0 = (f0 * table.mips + mip) * table.size;
    o.off1 = (f1 * table.mips + mip) * table.size;
    o.off0b = (f0 * table.mips + fineMip) * table.size;
    o.off1b = (f1 * table.mips + fineMip) * table.size;
    o.mipBlend = mipBlend;
    o.data = table.data;
    o.mask = table.mask;
    o.size = table.size;
    o.uni = uni;

    // BLEND: weight outer (most-detuned) voices vs. the center. weight_u =
    // 1-(1-blend)*|sprd_u|. blend=1 => all weights 1 (identical to legacy).
    // Loudness held ~constant by normalising on sqrt of the sum of squared
    // weights instead of the raw voice count.
    let sumW2 = 0;
    for (let u = 0; u < uni; u++) {
      const sprd = uni > 1 ? (u / (uni - 1)) * 2 - 1 : 0;
      const cents = sprd * det * 50;
      const ratio = Math.pow(2, cents / 1200);
      o.incs[u] = cps * ratio * table.size;
      const weight = 1 - (1 - blend) * Math.abs(sprd);
      sumW2 += weight * weight;
      const pan = Math.max(-1, Math.min(1, sprd * spr + basePan));
      const a = ((pan + 1) * Math.PI) / 4;
      o.gl[u] = Math.cos(a) * weight;
      o.gr[u] = Math.sin(a) * weight;
    }
    // `|| 1` guards uni=2,blend=0 (both endpoints -> sumW2=0). Matches the JUCE
    // `sumW2 > 0 ? sumW2 : 1` predicate exactly, so the engines stay in lockstep.
    o.gain = (level * 0.32) / Math.sqrt(sumW2 || 1);
    return true;
  }

  // Finding W1: phase increments, the morph fraction and the pan/level gain
  // products ramp from the previous chunk's targets to this chunk's across n
  // samples, so block-rate modulation (LFO->pitch, glide, POS, pan, level) has
  // no staircase. The ramp is suppressed on the first chunk after note-on, on a
  // unison-count change, and (for the morph fraction) when the frame pair
  // switched — ft is a fraction WITHIN a pair. Table reads are cubic Hermite.
  // Mirrors Engine::renderOsc.
  renderOsc(o, tmpL, tmpR, n) {
    const data = o.data, mask = o.mask, size = o.size, g = o.gain;
    const invN = 1 / n;
    const rp = o.havePrev && o.pUni === o.uni;
    const ft1 = o.ft;
    const ft0 = rp && o.pOff0 === o.off0 ? o.pFt : ft1;
    const dFt = (ft1 - ft0) * invN;
    const off0 = o.off0, off1 = o.off1;
    const blend = o.mipBlend;
    for (let u = 0; u < o.uni; u++) {
      let ph = wrapOscPhase(o.phases[u], size);
      const inc1 = o.incs[u];
      const inc0 = rp ? o.pIncs[u] : inc1;
      const dInc = (inc1 - inc0) * invN;
      const gl1 = o.gl[u] * g, gr1 = o.gr[u] * g;
      const gl0 = rp ? o.pGl[u] : gl1, gr0 = rp ? o.pGr[u] : gr1;
      const dGl = (gl1 - gl0) * invN, dGr = (gr1 - gr0) * invN;
      if (blend < 0.001) {
        // fast path — single mip, no crossfade
        for (let i = 0; i < n; i++) {
          const idx = ph | 0;
          const f = ph - idx;
          const im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
          const s0 = rdH(data, off0, im1, idx, i2, i3, f);
          const s1 = rdH(data, off1, im1, idx, i2, i3, f);
          const s = s0 + (ft0 + dFt * i) * (s1 - s0);
          tmpL[i] += s * (gl0 + dGl * i);
          tmpR[i] += s * (gr0 + dGr * i);
          // |inc| < size is guaranteed by the 0.45*sr pitch guard, so one
          // conditional subtract wraps; the loop exit re-wraps defensively.
          ph += inc0 + dInc * i;
          if (ph >= size) ph -= size;
        }
      } else {
        // crossfade path — blend coarse mip with finer mip near mip boundary
        const off0b = o.off0b, off1b = o.off1b;
        for (let i = 0; i < n; i++) {
          const idx = ph | 0;
          const f = ph - idx;
          const im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
          const ftN = ft0 + dFt * i;
          // coarse mip
          const sc0 = rdH(data, off0, im1, idx, i2, i3, f);
          const sc1 = rdH(data, off1, im1, idx, i2, i3, f);
          const sc = sc0 + ftN * (sc1 - sc0);
          // fine mip (richer, may alias slightly near the boundary)
          const sf0 = rdH(data, off0b, im1, idx, i2, i3, f);
          const sf1 = rdH(data, off1b, im1, idx, i2, i3, f);
          const sf = sf0 + ftN * (sf1 - sf0);
          const s = sc + blend * (sf - sc);
          tmpL[i] += s * (gl0 + dGl * i);
          tmpR[i] += s * (gr0 + dGr * i);
          ph += inc0 + dInc * i;
          if (ph >= size) ph -= size;
        }
      }
      o.phases[u] = wrapOscPhase(ph, size);
      o.pIncs[u] = inc1;
      o.pGl[u] = gl1; o.pGr[u] = gr1;
    }
    o.pFt = ft1; o.pOff0 = o.off0; o.pUni = o.uni;
    o.havePrev = true;
  }

  // Compute one filter's block-rate coefficients. CUTOFF is shared across all
  // types: it sets corner frequency (SVF), comb pitch (COMB) or vowel morph
  // position (VOWEL). RES sets resonance / feedback / formant sharpness.
  setupFilter(fs, base, v, e2, mCut, pm, n) {
    const p = this.p;
    const ftype = p[base + F_TYPE] | 0;
    fs.ftype = ftype;

    // The cutoff Log route is kept OUT of pm and passed as mCut here so the whole
    // exponent stays in a single Math.pow — bit-identical to the legacy
    // p[cutoff] × 2^(env·4·e2 + key·(note-60)/12 + x·5). env/key are still read from
    // pm so THEY remain modulatable; the base cutoff is read straight from p.
    let fc = p[base + F_CUT] *
      Math.pow(2, pm[base + F_ENV] * 4 * e2 + (pm[base + F_KEY] * (v.note - 60)) / 12 + mCut * MOD_LOG_D);
    if (!Number.isFinite(fc)) fc = 20; // JUCE's std::max(20.0, NaN) also yields 20
    fc = Math.min(sampleRate * 0.45, Math.max(20, fc));
    if (fs.cutSm <= 0) fs.cutSm = fc;
    fs.cutSm += (fc - fs.cutSm) * smoothCoef(n, CUT_TAU * sampleRate);
    const cut = fs.cutSm;
    fs.cutTarget = cut;              // runFilter ramps cutPrev -> cutTarget
    const res = Math.min(0.999, Math.max(0, pm[base + F_RES]));

    if (ftype <= 4) {
      // Cytomic SVF. The g-dependent coefficients are recomputed per <=32-sample
      // sub-block in runFilter from the ramped cutoff (finding W1); only the
      // damping term is fixed for the chunk.
      fs.twoPole = ftype === 1; // LP24 = two cascaded stages
      if (fs.twoPole) {
        // Finding B3: all the resonance in stage 1, stage 2 critically damped.
        const r2 = res * res;
        const resT = res + 0.0035 * r2 * r2;
        const kk = 2 - 1.93 * resT;
        fs.k1 = Math.max(LP24_K_MIN, 0.5 * kk * kk);
        fs.k2 = 2;
      } else {
        fs.k1 = 2 - 1.93 * res;
        fs.k2 = fs.k1;
      }
    } else if (ftype === 5) {
      // tuned feedback comb: delay length tracks cutoff pitch, RES -> feedback
      let len = sampleRate / cut;
      len = Math.min(COMB_MAX - 2, Math.max(1, len));
      fs.combLen = len;
      fs.combFb = res * 0.97; // < 1 keeps the resonator stable
    } else {
      // VOWEL: 3-band bandpass bank, CUTOFF morphs A-E-I-O-U on a log axis
      const norm = Math.min(0.999, Math.max(0, Math.log(cut / 20) / Math.log(1000)));
      const pos = norm * 4;
      const vi = Math.min(3, pos | 0);
      const fr = pos - vi;
      const q = 2 + res * 22; // higher RES -> narrower, more vocal formants
      for (let j = 0; j < 3; j++) {
        const f0 = Math.min(sampleRate * 0.45, VOWELS[vi][j] + (VOWELS[vi + 1][j] - VOWELS[vi][j]) * fr);
        const w0 = (2 * Math.PI * f0) / sampleRate;
        const alpha = Math.sin(w0) / (2 * q);
        const a0 = 1 + alpha;
        fs.fc[j * 3] = alpha / a0;             // b0 (b1 = 0, b2 = -b0): 0 dB peak BPF
        fs.fc[j * 3 + 1] = (-2 * Math.cos(w0)) / a0; // a1
        fs.fc[j * 3 + 2] = (1 - alpha) / a0;   // a2
        fs.famp[j] = F_AMPS[j];
      }
    }
  }

  // Apply one filter (optional ADAA drive + the selected type) to a stereo block,
  // reading in*, writing out*. Stage 1 saturates in -> out, stage 2 filters in place.
  runFilter(fs, inL, inR, outL, outR, drive, n) {
    // -- drive (anti-aliased tanh via ADAA), or a plain copy when disabled --
    if (drive > 0.005) {
      const dg = 1 + drive * 7;
      const dcomp = 1 / Math.pow(dg, 0.55);
      const kF = dcomp / dg;
      let xpL = fs.satXL, xpR = fs.satXR;
      let FpL = kF * lcosh(dg * xpL), FpR = kF * lcosh(dg * xpR);
      for (let i = 0; i < n; i++) {
        const aL = inL[i], aR = inR[i];
        const dxL = aL - xpL;
        const FL = kF * lcosh(dg * aL);
        // |Δx| tiny → ADAA is numerically unstable; fall back to midpoint tanh.
        outL[i] = dxL > 1e-5 || dxL < -1e-5 ? (FL - FpL) / dxL : dcomp * Math.tanh(dg * 0.5 * (aL + xpL));
        xpL = aL; FpL = FL;
        const dxR = aR - xpR;
        const FR = kF * lcosh(dg * aR);
        outR[i] = dxR > 1e-5 || dxR < -1e-5 ? (FR - FpR) / dxR : dcomp * Math.tanh(dg * 0.5 * (aR + xpR));
        xpR = aR; FpR = FR;
      }
      fs.satXL = xpL; fs.satXR = xpR;
    } else {
      for (let i = 0; i < n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
      if (n > 0) { fs.satXL = inL[n - 1]; fs.satXR = inR[n - 1]; }
    }

    const ftype = fs.ftype;
    if (ftype <= 4) {
      // Finding W1: the cutoff ramps from the previous chunk's value across the
      // chunk and the SVF coefficients are recomputed per <=32-sample
      // sub-block, so an automated or modulated cutoff never steps.
      const k1 = fs.k1, k2 = fs.k2;
      const c1c = fs.cutTarget;
      const c0c = fs.cutPrev > 0 ? fs.cutPrev : c1c;
      const F = fs.svf;
      for (let at = 0; at < n; at += 32) {
        const m = Math.min(32, n - at);
        const cut = c0c + (c1c - c0c) * ((at + m) / n);
        const gC = Math.tan((Math.PI * cut) / sampleRate);
        const a1 = 1 / (1 + gC * (gC + k1));
        const a2 = gC * a1, a3 = gC * a2;
        // Stage 2 (LP24) runs its own damping, so it needs its own coefficients.
        const a1b = 1 / (1 + gC * (gC + k2));
        const a2b = gC * a1b, a3b = gC * a2b;
        for (let ch = 0; ch < 2; ch++) {
          const buf = ch === 0 ? outL : outR;
          const o1 = ch * 2;
          let ic1 = F[o1], ic2 = F[o1 + 1];
          for (let i = at; i < at + m; i++) {
            const x = buf[i];
            const v3 = x - ic2;
            const v1 = a1 * ic1 + a2 * v3;
            const v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            switch (ftype) {
              case 0: case 1: buf[i] = v2; break;       // LP
              case 2: buf[i] = k1 * v1; break;          // BP (unity peak-ish)
              case 3: buf[i] = x - k1 * v1 - v2; break; // HP
              default: buf[i] = x - k1 * v1; break;     // notch
            }
          }
          F[o1] = ic1; F[o1 + 1] = ic2;
        }
        if (fs.twoPole) {
          for (let ch = 0; ch < 2; ch++) {
            const buf = ch === 0 ? outL : outR;
            const o1 = 4 + ch * 2;
            let ic1 = F[o1], ic2 = F[o1 + 1];
            for (let i = at; i < at + m; i++) {
              const x = buf[i];
              const v3 = x - ic2;
              const v1 = a1b * ic1 + a2b * v3;
              const v2 = ic2 + a2b * ic1 + a3b * v3;
              ic1 = 2 * v1 - ic1;
              ic2 = 2 * v2 - ic2;
              buf[i] = v2;
            }
            F[o1] = ic1; F[o1 + 1] = ic2;
          }
        }
      }
      fs.cutPrev = c1c;
    } else if (ftype === 5) {
      // resonant comb: y = (1-fb)·x + fb·y[n-len], fractional read for tuning.
      // The delay length ramps across the chunk (finding W1); the fractional
      // read already supports a per-sample length.
      const len1 = fs.combLen;
      const len0 = fs.combLenPrev > 0 ? fs.combLenPrev : len1;
      const dLen = (len1 - len0) / n;
      const fb = fs.combFb, g0 = 1 - fb;
      const cl = fs.combL, cr = fs.combR;
      let w = fs.combW;
      for (let i = 0; i < n; i++) {
        const len = len0 + dLen * (i + 1);
        let rd = w - len;
        rd = ((rd % COMB_MAX) + COMB_MAX) % COMB_MAX;
        const i0 = rd | 0;
        const frac = rd - i0;
        const i1 = i0 + 1 < COMB_MAX ? i0 + 1 : 0;
        const yL = g0 * outL[i] + fb * (cl[i0] + frac * (cl[i1] - cl[i0]));
        const yR = g0 * outR[i] + fb * (cr[i0] + frac * (cr[i1] - cr[i0]));
        cl[w] = yL; cr[w] = yR;
        outL[i] = yL; outR[i] = yR;
        w = w + 1 < COMB_MAX ? w + 1 : 0;
      }
      fs.combW = w;
      fs.combLenPrev = len1;
    } else {
      // VOWEL: parallel bank of 3 bandpass biquads (transposed direct form II)
      const fc = fs.fc, fa = fs.famp, z = fs.fmt;
      for (let ch = 0; ch < 2; ch++) {
        const buf = ch === 0 ? outL : outR;
        const zb = ch * 6;
        for (let i = 0; i < n; i++) {
          const x = buf[i];
          let acc = 0.04 * x; // slight broadband floor, matching the VOX voicing
          for (let j = 0; j < 3; j++) {
            const b0 = fc[j * 3], ca1 = fc[j * 3 + 1], ca2 = fc[j * 3 + 2];
            const zi = zb + j * 2;
            const y = b0 * x + z[zi];
            z[zi] = z[zi + 1] - ca1 * y; // s1 (b1 = 0)
            z[zi + 1] = -b0 * x - ca2 * y; // s2 (b2 = -b0)
            acc += fa[j] * y;
          }
          buf[i] = acc * 0.8;
        }
      }
    }
  }

  lfoHz(base) {
    if (this.p[base + L_SYNC]) {
      const i = Math.min(LFO_DIV_F.length - 1, Math.max(0, this.p[base + L_SYNCRATE] | 0));
      return (this.bpm / 60) * LFO_DIV_F[i];
    }
    return this.p[base + L_RATE];
  }

  // Free-running global LFO phase, updated once per block. When synced, the
  // phase is derived from the transport position (ppq, in quarter notes) so a
  // synced LFO cycle starts on the downbeat. Unsynced LFOs free-run at their Hz.
  // (Retrig LFOs are per-voice and note-aligned, so they bypass this.)
  updateGlobalLfo(g, base, ppq, n) {
    if (this.p[base + L_SYNC]) {
      const i = Math.min(LFO_DIV_F.length - 1, Math.max(0, this.p[base + L_SYNCRATE] | 0));
      let ph = ppq * LFO_DIV_F[i];
      ph -= Math.floor(ph);
      if (ph < g.phase) g.hold = this.rng.next() * 2 - 1; // grid wrap -> new S&H value
      g.phase = ph;
    } else {
      g.advance(this.p[base + L_RATE], n, this.rng);
    }
  }

  // Render one voice into L/R starting at sample `lo`, for `n` samples. `n` is
  // at most one 128-sample chunk, so every block-rate quantity below ramps
  // across a chunk rather than a whole host block (finding W1/W3).
  renderVoice(v, L, R, lo, n) {
    const p = this.p;

    v.ampEnv.set(p[ENV1_A], p[ENV1_A + 1], p[ENV1_A + 2], p[ENV1_A + 3]);
    v.modEnv.set(p[ENV2_A], p[ENV2_A + 1], p[ENV2_A + 2], p[ENV2_A + 3]);

    // glide
    const gl = p[MASTER_GLIDE] || 0;
    if (gl > 0.001) {
      const c = 1 - Math.exp(-n / (gl * 0.3 * sampleRate + 1));
      v.pitch += (v.note - v.pitch) * c;
    } else v.pitch = v.note;

    // mod sources (chunk rate)
    const rt1 = !!p[LFO1 + L_RETRIG], rt2 = !!p[LFO2 + L_RETRIG];
    const l1 = (rt1 ? v.lfo1 : this.gLfo1).valueOff(p[LFO1 + L_SHAPE], p[LFO1 + L_PHASE]) * v.lfo1.riseGain(p[LFO1 + L_RISE]);
    const l2 = (rt2 ? v.lfo2 : this.gLfo2).valueOff(p[LFO2 + L_SHAPE], p[LFO2 + L_PHASE]) * v.lfo2.riseGain(p[LFO2 + L_RISE]);
    const e2 = v.modEnv.level;
    const srcs = this._srcs;
    srcs[1] = l1; srcs[2] = l2; srcs[3] = e2; srcs[4] = v.vel; srcs[5] = (v.note - 60) / 24;

    // modulation destinations — sum every active slot assigned to each target.
    // The 16 fixed slots live in the flat store at MAT1 + (s-1)*M_STRIDE.
    // Globals (pitch/amp/pan) keep their legacy additive math; per-param dests
    // accumulate a route sum indexed by param id, then fold into a per-voice
    // modulated snapshot `pm` via the Lin/Log curve rule. This mirrors the VST
    // engine exactly, so both engines sound identical (existing dests included).
    let mPitch = 0, mAmp = 0, mPan = 0;
    const accum = this._modAccum;
    accum.fill(0);
    let anyRoute = false;
    for (let s = 0, b = MAT1; s < MOD_MATRIX_SIZE; s++, b += M_STRIDE) {
      const src = p[b + M_SRC] | 0;
      const dst = p[b + M_DST] | 0;
      if (!src || !dst || dst >= DST_PIDX.length) continue;
      const x = srcs[src] * (p[b + M_AMT] || 0);
      const target = DST_PIDX[dst];
      if (target === D_PITCH) mPitch += x;
      else if (target === D_AMP) mAmp += x;
      else if (target === D_PAN) mPan += x;
      else if (target >= 0) { accum[target] += x; anyRoute = true; }
    }

    // Build the per-voice modulated snapshot: copy p then apply each targeted
    // param's curve rule. Lin: pm = p + x·(hi−lo); Log: pm = p · 2^(x·D), D=5.
    // Params that are never destinations pass through untouched, so every read
    // below can go straight to pm. Matches the engine's pm_ build.
    const pm = this._pm;
    pm.set(p);
    if (anyRoute) {
      for (let i = 0; i < NUM_PARAMS; i++) {
        const x = accum[i];
        if (x === 0) continue;
        // The filter cutoff routes are NOT folded into pm: they are applied as
        // the single-exponent mCut term inside setupFilter so the result is
        // bit-identical to the legacy single Math.pow. All other dests fold here.
        if (i === F1_CUT || i === F2_CUT) continue;
        const c = MOD_CURVE[i];
        if (c === 2) pm[i] = p[i] * Math.pow(2, x * MOD_LOG_D);
        else if (c === 1) pm[i] = p[i] + x * MOD_SPAN[i];
      }
    }

    // SPLIT routing sends osc A through filter 1 and osc B through filter 2, so
    // they need separate source buffers; every other routing sums into one path.
    const route = p[FILTER_ROUTE] | 0;
    const split = route === 2;

    const tmpL = this.tmpL, tmpR = this.tmpR;
    tmpL.fill(0, 0, n); tmpR.fill(0, 0, n);
    const bL = this.bL, bR = this.bR;
    if (split) { bL.fill(0, 0, n); bR.fill(0, 0, n); }

    const aOn = this.setupOsc(v.oA, OSCA, v, pm, mPitch, mPan, n);
    const bOn = this.setupOsc(v.oB, OSCB, v, pm, mPitch, mPan, n);
    if (aOn) this.renderOsc(v.oA, tmpL, tmpR, n); else v.oA.havePrev = false;
    if (bOn) this.renderOsc(v.oB, split ? bL : tmpL, split ? bR : tmpR, n); else v.oB.havePrev = false;

    // sub oscillator (polyblep square or sine)
    if (p[SUB_ON]) {
      const subLvl = pm[SUB_LEVEL];
      const lvl = subLvl * subLvl * 0.3;
      if (lvl > 1e-6) {
        const sf = 440 * Math.pow(2, (v.pitch + this.bend + p[SUB_OCT] * 12 + mPitch * 12 - 69) / 12);
        const inc1 = sf / sampleRate;
        if (inc1 > 0 && inc1 < 0.45) {
          // Finding W1: ramp the sub increment across the chunk so glide /
          // pitch modulation is staircase-free on the sub too.
          const inc0 = v.subIncPrev > 0 && v.subIncPrev < 0.45 ? v.subIncPrev : inc1;
          const dInc = (inc1 - inc0) / n;
          let ph = v.subPhase;
          const square = (p[SUB_SHAPE] | 0) === 1;
          for (let i = 0; i < n; i++) {
            const inc = inc0 + dInc * i;
            let s;
            if (square) {
              s = ph < 0.5 ? 1 : -1;
              // polyblep at 0 and 0.5
              if (ph < inc) { const t = ph / inc; s += -(t * t) + 2 * t - 1; }
              else if (ph > 1 - inc) { const t = (ph - 1) / inc; s += t * t + 2 * t + 1; }
              const h = ph - 0.5;
              if (h >= 0 && h < inc) { const t = h / inc; s -= -(t * t) + 2 * t - 1; }
              else if (h < 0 && h > -inc) { const t = h / inc; s -= t * t + 2 * t + 1; }
              s *= 0.7;
            } else {
              s = Math.sin(2 * Math.PI * ph);
            }
            const o = s * lvl;
            tmpL[i] += o; tmpR[i] += o;
            ph += inc; if (ph >= 1) ph -= 1;
          }
          v.subPhase = ph;
          v.subIncPrev = inc1;
        } else v.subIncPrev = -1;
      }
    }

    // noise
    if (p[NOISE_ON]) {
      const noiseLvl = pm[NOISE_LEVEL];
      const lvl = noiseLvl * noiseLvl * 0.35;
      if (lvl > 1e-6) {
        const rng = this.rng;
        if ((p[NOISE_TYPE] | 0) === 1) {
          // Kellet pink filter with sample-rate-mapped poles/gains (finding W1;
          // identical to the fixed literals at the 48 kHz reference).
          const b = v.pb, pp = this.pinkP, pg = this.pinkG;
          for (let i = 0; i < n; i++) {
            const w = rng.next() * 2 - 1;
            b[0] = pp[0] * b[0] + w * pg[0];
            b[1] = pp[1] * b[1] + w * pg[1];
            b[2] = pp[2] * b[2] + w * pg[2];
            b[3] = pp[3] * b[3] + w * pg[3];
            b[4] = pp[4] * b[4] + w * pg[4];
            b[5] = -pp[5] * b[5] - w * pg[5];
            const pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362) * 0.11;
            b[6] = w * 0.115926;
            const o = pink * lvl;
            tmpL[i] += o; tmpR[i] += o;
          }
        } else {
          for (let i = 0; i < n; i++) {
            const o = (rng.next() * 2 - 1) * lvl;
            tmpL[i] += o; tmpR[i] += o;
          }
        }
      }
    }

    // ---- per-voice filters with routing ----
    const f1on = !!p[FLT1 + F_ON];
    const f2on = !!p[FLT2 + F_ON];
    if (f1on) this.setupFilter(v.f1, FLT1, v, e2, accum[F1_CUT], pm, n);
    if (f2on) this.setupFilter(v.f2, FLT2, v, e2, accum[F2_CUT], pm, n);

    const f1L = this.f1L, f1R = this.f1R, f2L = this.f2L, f2R = this.f2R;
    const dr1 = pm[FLT1 + F_DRIVE], dr2 = pm[FLT2 + F_DRIVE];
    let oL, oR; // routed output buffers feeding the DC blocker + amp

    if (split) {
      // osc A (+ sub/noise) -> F1, osc B -> F2, summed. Bypassed filters pass dry.
      if (f1on) this.runFilter(v.f1, tmpL, tmpR, f1L, f1R, dr1, n);
      if (f2on) this.runFilter(v.f2, bL, bR, f2L, f2R, dr2, n);
      const aL = f1on ? f1L : tmpL, aR = f1on ? f1R : tmpR;
      const sL = f2on ? f2L : bL, sR = f2on ? f2R : bR;
      for (let i = 0; i < n; i++) { f1L[i] = aL[i] + sL[i]; f1R[i] = aR[i] + sR[i]; }
      oL = f1L; oR = f1R;
    } else if (route === 1) {
      // parallel: both filters see the same signal, outputs summed
      if (f1on) this.runFilter(v.f1, tmpL, tmpR, f1L, f1R, dr1, n);
      if (f2on) this.runFilter(v.f2, tmpL, tmpR, f2L, f2R, dr2, n);
      if (f1on && f2on) {
        for (let i = 0; i < n; i++) { f1L[i] += f2L[i]; f1R[i] += f2R[i]; }
        oL = f1L; oR = f1R;
      } else if (f1on) { oL = f1L; oR = f1R; }
      else if (f2on) { oL = f2L; oR = f2R; }
      else { oL = tmpL; oR = tmpR; } // both bypassed -> dry
    } else {
      // serial: F1 -> F2, each bypassed when off
      let cL = tmpL, cR = tmpR;
      if (f1on) { this.runFilter(v.f1, cL, cR, f1L, f1R, dr1, n); cL = f1L; cR = f1R; }
      if (f2on) { this.runFilter(v.f2, cL, cR, f2L, f2R, dr2, n); cL = f2L; cR = f2R; }
      oL = cL; oR = cR;
    }

    // The AMP-mod factor ramps from the previous chunk's value (finding W1);
    // the DC blocker pole is sample-rate-derived.
    const ampFactor = Math.min(2, Math.max(0, 1 + mAmp));
    const af0 = v.ampFacPrev >= 0 ? v.ampFacPrev : ampFactor;
    const dAf = (ampFactor - af0) / n;
    const dcR = this.dcR;
    for (let i = 0; i < n; i++) {
      const sl = oL[i], sr = oR[i];
      // Per-voice DC blocker (1-pole highpass, ~3.5 Hz) — removes DC before
      // it reaches the FX chain's saturator where it would cause asymmetric clipping.
      const yL = sl - v.dcxL + dcR * v.dcyL;
      const yR = sr - v.dcxR + dcR * v.dcyR;
      v.dcxL = sl; v.dcyL = yL;
      v.dcxR = sr; v.dcyR = yR;
      const amp = v.ampEnv.process() * v.velGain * (af0 + dAf * i);
      L[lo + i] += yL * amp;
      R[lo + i] += yR * amp;
    }
    v.ampFacPrev = ampFactor;

    // advance chunk-rate modulators
    v.lfo1.advance(this.lfoHz(LFO1), n, this.rng);
    v.lfo2.advance(this.lfoHz(LFO2), n, this.rng);
    v.modEnv.processBlock(n);

    this._modAny = anyRoute;
    return { posA: v.oA.posSm, posB: v.oB.posSm };
  }

  // Copy the just-rendered voice's per-destination route sums (this._modAccum,
  // valid right after renderVoice) into the telemetry snapshot. A NONZERO route
  // is what makes telemetry flow; `_modAny` records whether this voice had one,
  // so a route sitting exactly at a zero crossing still counts.
  snapshotModViz() {
    const mv = this.modViz;
    mv.fill(0);
    const accum = this._modAccum;
    for (let i = 0; i < NUM_PARAMS; i++) {
      const di = PIDX_DST[i];
      if (di) mv[di] = accum[i];
    }
    this.modVizAny = this._modAny;
  }

  // Finding W3: split the host block at every sequencer event so a step, its
  // gate-off and the render quantum land on their exact sample instead of the
  // next block start (up to 2.9 ms of jitter at 44.1 kHz, and a whole 4096-
  // sample host block on the extremes). Mirrors Engine::render's chunk loop:
  // every chunk is at most 128 samples, so the engine also behaves identically
  // at any host block size.
  process(_inputs, outputs) {
    const out = outputs[0];
    const L = out[0];
    const R = out.length > 1 ? out[1] : out[0];
    L.fill(0);
    if (R !== L) R.fill(0);
    const n = L.length;
    const hosted = this.hosted;

    let off = 0;
    while (off < n) {
      let run = Math.min(128, n - off);
      this.frameNow = currentFrame + off;

      // Fire the step that is due at this sample, then shorten the chunk so the
      // next one starts exactly where the following step does.
      if (!hosted && this.seqPlaying) {
        if (this.seqToNext <= 0) this.seqFire();
        run = Math.min(run, Math.max(1, Math.ceil(this.seqToNext)));
      }
      // Cut the chunk at the earliest pending sequencer note-off, so each off
      // lands on its own sample (gate-off before the next step's trigger).
      const eo = this.earliestSeqOff();
      if (eo >= 0) run = Math.min(run, Math.max(1, Math.ceil(eo)));
      // The hosted clip transport resolves its commands per chunk; at the usual
      // 128-sample quantum that is exactly the pre-split behaviour.
      if (hosted) this.hostTick(run);

      const ppq = hosted
        ? Math.max(0, this.frameNow - this.hostAnchor) * (this.bpm / 60) / sampleRate
        : this.transportBeats;
      this.renderChunk(L, R, off, run, ppq);
      this.transportBeats += (run / sampleRate) * (this.bpm / 60);
      if (!hosted && this.seqPlaying) this.seqToNext -= run;

      // Drain the per-note off queue: decrement every pending off, fire noteOff
      // for those now due, compact the survivors (no allocation).
      if (this.seqOffQueue.length) {
        let w = 0;
        for (let i = 0; i < this.seqOffQueue.length; i++) {
          const e = this.seqOffQueue[i];
          e.remaining -= run;
          if (e.remaining <= 0) this.noteOff(e.note);
          else this.seqOffQueue[w++] = e;
        }
        this.seqOffQueue.length = w;
        this.seqLastNote = w ? this.seqOffQueue[w - 1].note : -1;
      }
      off += run;
    }
    // FX are block-rate parameterised (as in the plugin) and run over the whole
    // host block, after every sequencer-split chunk has been rendered.
    this.fx.setParams(this.p);
    this.fx.process(L, R, n);
    return true;
  }

  // Samples until the soonest pending sequencer note-off, or -1 when none.
  earliestSeqOff() {
    const q = this.seqOffQueue;
    let best = -1;
    for (let i = 0; i < q.length; i++) {
      const r = q[i].remaining;
      if (best < 0 || r < best) best = r;
    }
    return best;
  }

  // Render one <=128-sample chunk of every sounding voice into L/R at `off`.
  renderChunk(L, R, off, n, ppq) {
    // Update the global (free-run/transport-locked) LFOs before voices read
    // them. ppq = beats since audio start (chunk-start position). Hosted
    // (SQ-4), the conductor's anchor is beat zero of the shared timebase, so
    // every device's synced LFO lands on the same downbeat regardless of when
    // it joined the song.
    this.updateGlobalLfo(this.gLfo1, LFO1, ppq, n);
    this.updateGlobalLfo(this.gLfo2, LFO2, ppq, n);

    let act = 0;
    let viz = null;
    this.modVizAny = false;
    for (const v of this.voices) {
      if (!v.active && v.pending) {
        const pd = v.pending;
        v.pending = null;
        v.noteOn(pd.n, pd.vel, pd.start, this.clock++, this.rng, 1, 1);
      }
      if (!v.active) continue;
      const r = this.renderVoice(v, L, R, off, n);
      // Voice to visualize: the same one the wavetable viz follows — the last
      // gated (still-held) voice in pool order, falling back to any releasing
      // voice. _modAccum still holds exactly this voice's route sums here.
      if (v.gate || !viz) { viz = r; this.snapshotModViz(); }
      act++;
    }

    this.vizCount += n;
    if (this.vizCount >= 2048) {
      this.vizCount = 0;
      this.port.postMessage({
        t: 'viz',
        a: viz ? viz.posA : -1,
        b: viz ? viz.posB : -1,
        n: act,
      });
      // Live-mod telemetry rides the same throttle. slice(): a fresh buffer per
      // send (tiny, ~23 Hz) — postMessage would clone anyway, and the in-process
      // test harness must not see later mutations of the reused snapshot.
      if (this.modVizAny) {
        this.port.postMessage({ t: 'mod', d: this.modViz.slice() });
        this.modIdleSent = false;
      } else if (!this.modIdleSent) {
        this.modIdleSent = true;
        this.port.postMessage({ t: 'mod', d: null });
      }
    }
  }
}

registerProcessor('fable-wt', FableProcessor);
