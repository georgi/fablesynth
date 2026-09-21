// FableSynth DR-1 DSP core — AudioWorklet thread. Self-contained (no imports).
// 16 one-shot pad voices, a per-pad FX rack, five output buses and a
// sample-accurate step sequencer. See worklet.js for the reference
// implementations of the shared primitives (mip playback, SVF, ADAA drive) —
// copied here because worklets can't import.
//
// Fidelity contract (docs/audio-engine-review.md §4): Hermite table reads,
// intra-block ramps for every increment, sample-rate-derived smoothing and
// filter poles, and a seedable RNG — the same choices the JUCE port makes, so
// the two engines stay comparable. The FX rack (review W6) is a port of
// juce/source/drum/dsp/DrumFx.cpp for the same reason: one algorithm, and one
// that an offline test can render.
//
// In:  {t:'init',params} {t:'tables',list} {t:'samples',list} {t:'p',k,v} {t:'trig',pad,v}
//      {t:'pats',data} {t:'chain',list} {t:'play'} {t:'stop'} {t:'sel',pad} {t:'panic'}
//      {t:'seed',v}
// Out: {t:'step',s,pat,hits} per step while playing
//      {t:'viz',a,b,env} every 2048 samples for the selected pad
//      {t:'latency',n} once, at construction
// Outputs: one stereo bus per OUT_NAMES entry (MAIN, AUX 1..4).

const NPADS = 16;
const MAXUNI = 7;
const STEPS = 16;
const NPATTERNS = 4;
const ACCENT_VEL = 1.0;
const PLAIN_VEL = 0.72;
const SWING_MAX = 0.667;
const MOD_LOG_D = 5;
const BASE_NOTE = 60;

// Time constants, not per-sample coefficients: every smoother below used to
// hold a literal tuned at 48 kHz, so a 96 kHz context halved its time. Each
// value here reproduces the old coefficient exactly at 48 kHz.
const POS_TAU = 0.000774; // was posSm += (pos - posSm) * 0.35 per 16 samples
const CUT_TAU = 0.003847; // was cutSm += (fc - cutSm) * 0.5 per 128 samples
const CHOKE_TAU = 0.0003; // ≈2.8 ms to -80 dB; also the retrigger fade (D2)
const DC_R_48 = 0.9998; // DC-blocker pole, quoted at 48 kHz
const NOISE_SR = 48000; // reference rate for the noise one-pole colour
const MAX_STEP = 32; // sample-player playback-rate ceiling (was unbounded)
const EDGE_FADE = 0.0015; // seconds of fade at a sample's start/end/stop
const TYPE_XFADE = 0.003; // seconds of crossfade on a filter-type switch
const E45 = Math.exp(-4.5);
const INV_E45 = 1 / (1 - E45);

// Coefficient of a one-pole smoother advanced by `n` samples toward its target.
const smoothCoef = (n, tauSamples) => 1 - Math.exp(-n / Math.max(1e-9, tauSamples));

// Re-map a one-pole coefficient authored at 48 kHz onto the running rate.
const mapPole = (a48, sr) => 1 - Math.pow(1 - a48, NOISE_SR / sr);

function lcosh(z) {
  const a = Math.abs(z);
  return a + Math.log1p(Math.exp(-2 * a)) - Math.LN2;
}

// 4-point cubic Hermite, the read JUCE uses. Linear reads left 10–20 dB more
// interpolation image below C6 (review §1).
function hermite4(ym1, y0, y1, y2, f) {
  const c1 = 0.5 * (y1 - ym1);
  const c2 = ym1 - 2.5 * y0 + 2 * y1 - 0.5 * y2;
  const c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
  return ((c3 * f + c2) * f + c1) * f + y0;
}

function hermiteTable(d, base, idx, mask, f) {
  return hermite4(
    d[base + ((idx - 1) & mask)],
    d[base + idx],
    d[base + ((idx + 1) & mask)],
    d[base + ((idx + 2) & mask)],
    f,
  );
}

// xorshift32, mirroring JUCE's Rng: seedable, so a noise-bearing render is
// reproducible and comparable between the two engines.
class Rng {
  constructor(seed) { this.s = (seed >>> 0) || 0x9e3779b9; }
  next() {
    let x = this.s;
    x ^= x << 13; x >>>= 0;
    x ^= x >>> 17;
    x ^= x << 5; x >>>= 0;
    this.s = x;
    return x;
  }
  // uniform in [-1, 1)
  uni() { return this.next() * (2 / 4294967296) - 1; }
}

// ---------- FX rack ----------
// Port of juce/source/dsp/Fx.h and juce/source/drum/dsp/DrumFx.cpp. The pad FX
// used to be a graph of native WebAudio nodes on the main thread; both products
// now run this one algorithm, and every stage is testable offline (review W6).
// The native graph keeps only the analysers.
//
// Per pad: OTT -> compressor -> drive -> chorus -> ping-pong delay, then an equal-power
// reverb send. Per bus (DrumBusOut, review D1): master gain -> DC block ->
// lookahead limiter with a hard -1 dBFS ceiling.

const BUSES = 5; // OUT_NAMES.length in ../params.ts
const FX_TAU = 0.02; // wet/dry and gain smoothing, matching setTargetAtTime(.., 0.02)
const HB1_TAPS = 47, HB2_TAPS = 17, HB_BETA = 6;
// 4x drive oversampler group delay in base samples: (47-1)/2 + (17-1)/4.
const DRIVE_LATENCY = (HB1_TAPS - 1) / 2 + (HB2_TAPS - 1) / 4; // 27
const LIM_THR = 0.398, LIM_RATIO = 14;
const LIM_CEILING = 0.8912509381337456; // -1 dBFS


const COMB_TUNE = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617];
const AP_TUNE = [556, 441, 341, 225];
const STEREO_SPREAD = 23;
// FX gate (review D8): a chain bypasses itself once its input AND its own
// output have been below GATE_EPS for this long. Level-driven, so a reverb or
// delay tail is never truncated; the hold-off also covers one full delay round
// trip, after which the line provably holds silence.
const GATE_HOLD = 0.25;
const GATE_EPS = 1e-5;

// Zero-latency overload guard for module boundaries. Floating-point signals
// can exceed unity internally; only excess boundary levels are attenuated.
// Common stereo gain preserves pan, instant attack catches the first peak,
// and an 80 ms release avoids independently flattening successive samples.
// This is sample-peak protection, not an oversampled true-peak limiter.
const StereoPeakGuard = globalThis.FablePeakGuard;

// Compressor dynamics are shared with WT-1 and BL-1 in ott-worklet.js.

const mixGate = (on, amount, wet) => (wet
  ? (on ? Math.sin((amount * Math.PI) / 2) : 0)
  : (on ? Math.cos((amount * Math.PI) / 2) : 1));

// One-pole smoother toward a target (setTargetAtTime equivalent).
class Smooth {
  constructor(sr, tau) {
    this.cur = 0; this.target = 0;
    this.coef = 1 - Math.exp(-1 / (tau * sr));
  }
  next() { this.cur += (this.target - this.cur) * this.coef; return this.cur; }
  // Advance n samples at once (block-rate users: reverb size, bus gain).
  nextN(n) { this.cur += (this.target - this.cur) * (1 - Math.pow(1 - this.coef, n)); return this.cur; }
  snap(v) { this.cur = v; this.target = v; }
  settled() { return this.cur === this.target; }
}

class Biquad {
  constructor() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; }
  lowpass(freq, q, sr) {
    const w0 = (2 * Math.PI * Math.min(freq, sr * 0.49)) / sr;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 - cw) / 2 / a0; this.b1 = (1 - cw) / a0; this.b2 = this.b0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha) / a0;
  }
  highpass(freq, q, sr) {
    const w0 = (2 * Math.PI * Math.min(freq, sr * 0.49)) / sr;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 + cw) / 2 / a0; this.b1 = -(1 + cw) / a0; this.b2 = this.b0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha) / a0;
  }
  process(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
  reset() { this.z1 = 0; this.z2 = 0; }
}

class DelayLine {
  constructor(n) { this.buf = new Float32Array(Math.max(4, n | 0)); this.w = 0; }
  reset() { this.buf.fill(0); this.w = 0; }
  write(x) { this.buf[this.w] = x; if (++this.w >= this.buf.length) this.w = 0; }
  // 4-point Catmull-Rom, for the modulated reads (chorus, echo).
  readHermite(d) {
    const sz = this.buf.length, b = this.buf;
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

// Fixed integer delay, block at a time: the drive's dry path needs exactly
// DRIVE_LATENCY samples of alignment and nothing else, so it costs two copies
// per block instead of a fractional read per sample.
class IntDelay {
  constructor(d) { this.d = d; this.h = new Float32Array(d); }
  reset() { this.h.fill(0); }
  block(x, out, n) {
    const d = this.d, h = this.h;
    if (n >= d) {
      for (let i = 0; i < d; i++) out[i] = h[i];
      for (let i = d; i < n; i++) out[i] = x[i - d];
      for (let i = 0; i < d; i++) h[i] = x[n - d + i];
    } else {
      for (let i = 0; i < n; i++) out[i] = h[i];
      h.copyWithin(0, n);
      for (let i = 0; i < n; i++) h[d - n + i] = x[i];
    }
  }
  copyFrom(o) { this.h.set(o.h); }
}

function besselI0(x) {
  let sum = 1, term = 1;
  for (let k = 1; k < 64; k++) {
    term *= (x * x) / (4 * k * k);
    sum += term;
    if (term < 1e-16 * sum) break;
  }
  return sum;
}

// Kaiser-windowed half-band FIR, split into its two polyphase phases. Every tap
// at an even offset from the centre is structurally zero and the centre tap is
// 0.5, so the interpolator never multiplies the zero-stuffed inputs and the
// decimator never computes the phase it throws away (review J4). With the
// symmetric taps folded as well that is 46 MACs per base sample through the 4x
// path where the direct form the C++ started from does 324. Coefficients are
// shared by every instance; only the histories are per-instance.
//
// Verified against the direct form it replaces (scratchpad probe, 47+17 tap
// cascade, 48 kHz): max abs error 2.8e-16 on a 20 Hz->20 kHz swept sine and
// 1.1e-16 on an impulse, whose peak stays on sample 27 — so the group delay,
// and with it the reported chain latency, is unchanged.
function designHalfBand(taps, beta) {
  const h = new Float64Array(taps);
  const M = taps - 1, ib = besselI0(beta), c = (taps - 1) / 2;
  for (let i = 0; i < taps; i++) {
    const m = i - M * 0.5;
    const sinc = m === 0 ? 0.5 : Math.sin((Math.PI * 0.5 * m)) / (Math.PI * m);
    const t = (2 * m) / M;
    h[i] = (sinc * besselI0(beta * Math.sqrt(Math.max(0, 1 - t * t)))) / ib;
  }
  // sin(PI*m/2) at an even integer m is ~1e-16, not 0; force the exact zero so
  // the polyphase split is exact and the skipped multiplies cost nothing.
  for (let i = 0; i < taps; i++) if (i !== c && (i - c) % 2 === 0) h[i] = 0;
  const pe = new Float64Array((taps + 1) >> 1);
  const po = new Float64Array(taps >> 1);
  for (let j = 0; j < pe.length; j++) pe[j] = h[2 * j];
  for (let j = 0; j < po.length; j++) po[j] = h[2 * j + 1];
  // Each phase is itself symmetric, so pair the equal taps and multiply once:
  // 46 MACs per base sample through the 4x path instead of 86 unfolded.
  const fold = (v) => {
    const co = [], j1 = [], j2 = [];
    for (let j = 0; j < v.length - 1 - j; j++) {
      if (v[j] === 0) continue;
      co.push(v[j]); j1.push(j); j2.push(v.length - 1 - j);
    }
    const mid = v.length & 1 ? (v.length - 1) >> 1 : -1;
    return {
      co: new Float64Array(co), j1: new Int32Array(j1), j2: new Int32Array(j2),
      mid: mid >= 0 && v[mid] !== 0 ? mid : -1,
      midCo: mid >= 0 ? v[mid] : 0,
    };
  };
  return { h, fe: fold(pe), fo: fold(po), np: Math.max(pe.length, po.length + 1) };
}

class HalfBandFir {
  constructor(d) {
    this.d = d;
    this.hx = new Float64Array(2 * d.np);
    this.he = new Float64Array(2 * d.np);
    this.ho = new Float64Array(2 * d.np);
    this.px = 0; this.pd = 0;
  }
  reset() { this.hx.fill(0); this.he.fill(0); this.ho.fill(0); this.px = 0; this.pd = 0; }
  // Adopt another instance's history. Used by the mono fast path, where the
  // right channel was skipped because its input was identical to the left's:
  // had it run, its state would be exactly this.
  copyFrom(o) {
    this.hx.set(o.hx); this.he.set(o.he); this.ho.set(o.ho);
    this.px = o.px; this.pd = o.pd;
  }
  // Block forms of the two entry points. A block at a time rather than a
  // sample at a time so each filter's taps are loaded into locals once per
  // block: measured 23 % of the whole drive stage, which is more than the
  // arithmetic the folding above saves.
  interpBlock(x, out, n) {
    const d = this.d, np = d.np, hx = this.hx;
    const fe = d.fe, fo = d.fo;
    const eco = fe.co, ej1 = fe.j1, ej2 = fe.j2, emid = fe.mid, emc = fe.midCo, en = eco.length;
    const oco = fo.co, oj1 = fo.j1, oj2 = fo.j2, omid = fo.mid, omc = fo.midCo, on = oco.length;
    let p = this.px;
    for (let i = 0; i < n; i++) {
      if (--p < 0) p = np - 1;
      const v = x[i];
      hx[p] = v; hx[p + np] = v;
      let a0 = emid >= 0 ? emc * hx[p + emid] : 0;
      for (let k = 0; k < en; k++) a0 += eco[k] * (hx[p + ej1[k]] + hx[p + ej2[k]]);
      let a1 = omid >= 0 ? omc * hx[p + omid] : 0;
      for (let k = 0; k < on; k++) a1 += oco[k] * (hx[p + oj1[k]] + hx[p + oj2[k]]);
      out[2 * i] = 2 * a0; out[2 * i + 1] = 2 * a1;
    }
    this.px = p;
  }
  decimBlock(x, out, n) {
    const d = this.d, np = d.np, he = this.he, ho = this.ho;
    const fe = d.fe, fo = d.fo;
    const eco = fe.co, ej1 = fe.j1, ej2 = fe.j2, emid = fe.mid, emc = fe.midCo, en = eco.length;
    const oco = fo.co, oj1 = fo.j1, oj2 = fo.j2, omid = fo.mid, omc = fo.midCo, on = oco.length;
    let p = this.pd;
    for (let i = 0; i < n; i++) {
      if (--p < 0) p = np - 1;
      const x0 = x[2 * i], x1 = x[2 * i + 1];
      he[p] = x0; he[p + np] = x0;
      ho[p] = x1; ho[p + np] = x1;
      // The two phases are summed separately and added once, so the result is
      // bit-identical to the sample-at-a-time form rather than merely equal to
      // within a rounding step.
      let ae = emid >= 0 ? emc * he[p + emid] : 0;
      for (let k = 0; k < en; k++) ae += eco[k] * (he[p + ej1[k]] + he[p + ej2[k]]);
      const q = p + 1;
      let ao = omid >= 0 ? omc * ho[q + omid] : 0;
      for (let k = 0; k < on; k++) ao += oco[k] * (ho[q + oj1[k]] + ho[q + oj2[k]]);
      out[i] = ae + ao;
    }
    this.pd = p;
  }
}

const HB1 = designHalfBand(HB1_TAPS, HB_BETA);
const HB2 = designHalfBand(HB2_TAPS, HB_BETA);

// Oversampler scratch, shared by every pad: the chains run one after another
// on a single thread, so one set of buffers is enough.
let OS_N = 0, OS_IN = null, OS_X2 = null, OS_X4 = null, OS_Y2 = null;
function osScratch(n) {
  if (n <= OS_N) return;
  OS_N = n;
  OS_IN = new Float64Array(n);
  OS_X2 = new Float64Array(2 * n);
  OS_X4 = new Float64Array(4 * n);
  OS_Y2 = new Float64Array(2 * n);
}

// Lookahead brickwall limiter: fixed makeup gain feeding a delayed signal path,
// linked-stereo sliding-window-minimum gain that fully develops inside the
// ~1.5 ms lookahead, ~200 ms release, hard -1 dBFS sample-peak ceiling.
class LookaheadLimiter {
  constructor(sr, makeup) {
    this.la = Math.max(8, Math.round(0.0015 * sr));
    this.qcap = this.la + 2;
    this.dlL = new Float32Array(this.la);
    this.dlR = new Float32Array(this.la);
    this.qv = new Float64Array(this.qcap);
    this.qi = new Float64Array(this.qcap);
    this.atk = 1 - Math.exp(-4 / this.la);
    this.rel = 1 - Math.exp(-1 / (0.2 * sr));
    this.makeup = makeup;
    this.w = 0; this.qh = 0; this.qt = 0; this.t = 0; this.env = 1;
  }
  reset() {
    this.dlL.fill(0); this.dlR.fill(0); this.qv.fill(1); this.qi.fill(0);
    this.w = 0; this.qh = 0; this.qt = 0; this.t = 0; this.env = 1;
  }
  process(L, R, n) {
    const la = this.la, qcap = this.qcap, qv = this.qv, qi = this.qi;
    const dlL = this.dlL, dlR = this.dlR, mk = this.makeup;
    let w = this.w, qh = this.qh, qt = this.qt, t = this.t, env = this.env;
    for (let i = 0; i < n; i++) {
      const xl = L[i] * mk, xr = R[i] * mk;
      const pk = Math.max(Math.abs(xl), Math.abs(xr));
      const g = pk > LIM_CEILING ? LIM_CEILING / pk : 1;
      // monotonic ring queue: minimum required gain over the last la+1 samples
      for (;;) {
        if (qh === qt) break;
        const prev = qt > 0 ? qt - 1 : qcap - 1;
        if (qv[prev] < g) break;
        qt = prev;
      }
      qv[qt] = g; qi[qt] = t; qt = qt + 1 < qcap ? qt + 1 : 0;
      if (qi[qh] < t - la) qh = qh + 1 < qcap ? qh + 1 : 0;
      const wmin = qv[qh];
      env += (wmin - env) * (wmin < env ? this.atk : this.rel);
      const dl = dlL[w], dr = dlR[w];
      dlL[w] = xl; dlR[w] = xr;
      if (++w >= la) w = 0;
      t++;
      let gg = env;
      const pd = Math.max(Math.abs(dl), Math.abs(dr));
      if (gg * pd > LIM_CEILING) gg = LIM_CEILING / pd; // catch smoothing residue
      L[i] = dl * gg; R[i] = dr * gg;
    }
    this.w = w; this.qh = qh; this.qt = qt; this.t = t; this.env = env;
  }
}

// Freeverb, sample-rate scaled, tuned by SIZE. One instance per output bus,
// shared by every pad on that bus: sixteen independent reverbs cost more than
// the rest of the drum machine put together, exactly as the sixteen convolvers
// this replaces did (review D7/D8). SIZE is continuous — the network's
// feedback and damping move under the running tail instead of a buffer swap.
class Freeverb {
  constructor(sr) {
    const scale = sr / 44100;
    this.cL = []; this.cR = []; this.aL = []; this.aR = [];
    for (let i = 0; i < 8; i++) {
      this.cL.push({ buf: new Float32Array(Math.floor(COMB_TUNE[i] * scale)), idx: 0, filt: 0 });
      this.cR.push({ buf: new Float32Array(Math.floor((COMB_TUNE[i] + STEREO_SPREAD) * scale)), idx: 0, filt: 0 });
    }
    for (let i = 0; i < 4; i++) {
      this.aL.push({ buf: new Float32Array(Math.floor(AP_TUNE[i] * scale)), idx: 0 });
      this.aR.push({ buf: new Float32Array(Math.floor((AP_TUNE[i] + STEREO_SPREAD) * scale)), idx: 0 });
    }
    this.sr = sr;
    this.size = new Smooth(sr, FX_TAU);
    this.size.snap(0.4);
    this.fb = 0.812; this.damp1 = 0.32; this.damp2 = 0.68;
    this.silentFor = 1e9; this.gated = true;
    this.meterEnabled = false; this.meterL = 0; this.meterR = 0; this.meterLR = 0;
  }
  reset() {
    for (const c of this.cL) { c.buf.fill(0); c.idx = 0; c.filt = 0; }
    for (const c of this.cR) { c.buf.fill(0); c.idx = 0; c.filt = 0; }
    for (const a of this.aL) { a.buf.fill(0); a.idx = 0; }
    for (const a of this.aR) { a.buf.fill(0); a.idx = 0; }
    this.meterL = 0; this.meterR = 0; this.meterLR = 0;
  }
  // Coefficients follow the smoothed SIZE once per block: a knob move retunes
  // the running network, it never restarts it.
  update(n) {
    const s = this.size.nextN(n);
    this.fb = 0.7 + s * 0.28;
    this.damp1 = 0.4 - s * 0.2;
    this.damp2 = 1 - this.damp1;
  }
  process(inL, inR, outL, outR, n) {
    let inPk = 0;
    for (let i = 0; i < n; i++) {
      const a = inL[i] < 0 ? -inL[i] : inL[i], b = inR[i] < 0 ? -inR[i] : inR[i];
      if (a > inPk) inPk = a;
      if (b > inPk) inPk = b;
    }
    if (this.gated) {
      // Nothing is being sent and the tail has already decayed below
      // GATE_EPS — the network holds silence, so running it changes nothing.
      if (inPk <= GATE_EPS) return;
      this.gated = false;
      this.silentFor = 0;
    }
    let outPk = 0;
    const fb = this.fb, d1 = this.damp1, d2 = this.damp2;
    for (let i = 0; i < n; i++) {
      const input = (inL[i] + inR[i]) * 0.015;
      let l = 0, r = 0;
      for (let c = 0; c < 8; c++) {
        const cl = this.cL[c], bl = cl.buf;
        const ol = bl[cl.idx];
        cl.filt = ol * d2 + cl.filt * d1;
        bl[cl.idx] = input + cl.filt * fb;
        if (++cl.idx >= bl.length) cl.idx = 0;
        l += ol;
        const cr = this.cR[c], br = cr.buf;
        const or_ = br[cr.idx];
        cr.filt = or_ * d2 + cr.filt * d1;
        br[cr.idx] = input + cr.filt * fb;
        if (++cr.idx >= br.length) cr.idx = 0;
        r += or_;
      }
      for (let a = 0; a < 4; a++) {
        const al = this.aL[a], bl = al.buf;
        const bo = bl[al.idx];
        bl[al.idx] = l + bo * 0.5;
        l = bo - l;
        if (++al.idx >= bl.length) al.idx = 0;
        const ar = this.aR[a], br = ar.buf;
        const bo2 = br[ar.idx];
        br[ar.idx] = r + bo2 * 0.5;
        r = bo2 - r;
        if (++ar.idx >= br.length) ar.idx = 0;
      }
      if (this.meterEnabled) { this.meterL += l * l; this.meterR += r * r; this.meterLR += l * r; }
      outL[i] += l; outR[i] += r;
      const a = l < 0 ? -l : l, b = r < 0 ? -r : r;
      if (a > outPk) outPk = a;
      if (b > outPk) outPk = b;
    }
    if (inPk <= GATE_EPS && outPk <= GATE_EPS) {
      this.silentFor += n;
      if (this.silentFor >= GATE_HOLD * this.sr) this.gated = true;
    } else {
      this.silentFor = 0;
    }
  }
}

// One pad's insert chain: OTT -> compressor -> drive -> chorus -> ping-pong delay.
// The reverb is a send (see DrumProcessor.process) so pads can share one.
class PadFx {
  constructor(sr) {
    this.sr = sr;
    this.headroom = {};
    for (const stage of ['input', 'ott', 'comp', 'drive', 'chorus', 'delay']) {
      this.headroom[stage] = new StereoPeakGuard(sr);
    }
    this.delayFeedbackGuard = new StereoPeakGuard(sr);
    // drive
    this.driveK = 1; this.drivePre = 1; this.driveNorm = 1;
    this.driveWet = new Smooth(sr, FX_TAU); this.driveDry = new Smooth(sr, FX_TAU);
    this.driveDry.snap(1);
    this.u1L = new HalfBandFir(HB1); this.u2L = new HalfBandFir(HB2);
    this.d2L = new HalfBandFir(HB2); this.d1L = new HalfBandFir(HB1);
    this.u1R = new HalfBandFir(HB1); this.u2R = new HalfBandFir(HB2);
    this.d2R = new HalfBandFir(HB2); this.d1R = new HalfBandFir(HB1);
    this.dryL = new IntDelay(DRIVE_LATENCY); this.dryR = new IntDelay(DRIVE_LATENCY);
    this.dbL = new Float32Array(128); this.dbR = new Float32Array(128);
    this.wetL = new Float64Array(128); this.wetR = new Float64Array(128);
    this.driveOff = true; this.driveGated = false;
    // A drum voice is mono until PAN spreads it, so L and R usually hold the
    // same samples. While they do, the drive runs once and the right-hand
    // filters are left alone; they are resynced from the left the moment the
    // two channels differ. Halves the cost of the chain's dominant stage.
    this.monoRun = false;
    this.driveColorL = new globalThis.FableDriveColor(sr);
    this.driveColorR = new globalThis.FableDriveColor(sr);
    this.comp = new globalThis.FableCompressor(sr);
    this.eq = new globalThis.FableParametricEq(sr);
    this.ott = new globalThis.FableOttCompressor(sr);
    // chorus
    this.chPhase = 0; this.chRate = 0.6; this.chDepth = 0.5;
    this.chWet = new Smooth(sr, FX_TAU); this.chDry = new Smooth(sr, FX_TAU);
    this.chDry.snap(1);
    this.chDl1 = new DelayLine(0.05 * sr); this.chDl2 = new DelayLine(0.05 * sr);
    this.chorusOff = true; this.chorusGated = false;
    // delay
    this.dlTime = new Smooth(sr, 0.08); this.dlFb = new Smooth(sr, FX_TAU);
    this.dlWet = new Smooth(sr, FX_TAU); this.dlDry = new Smooth(sr, FX_TAU);
    this.dlDry.snap(1);
    this.dlL = new DelayLine(2 * sr + 4); this.dlR = new DelayLine(2 * sr + 4);
    this.dlDamp = new Biquad(); this.dlDamp.lowpass(4500, 0.707, sr);
    this.delayOff = true; this.delayGated = false;
    // reverb send (equal power, same law as the insert stages)
    this.verbWet = new Smooth(sr, FX_TAU); this.verbDry = new Smooth(sr, FX_TAU);
    this.verbDry.snap(1);
    this.verbSize = 0.4; this.verbOn = true;
    // chain gate (review D8)
    this.silentFor = 1e9; this.gated = true; this.outPk = 0;
    this.bus = 0;
    this.meterSelected = false;
    this.meterInput = 0; this.meterOtt = 0; this.meterCompIn = 0; this.meterCompOut = 0;
    this.meterEchoIn = 0; this.meterEchoL = 0; this.meterEchoR = 0;
  }

  syncRight() {
    this.driveColorR.copyShapeFrom(this.driveColorL);
    this.u1R.copyFrom(this.u1L); this.u2R.copyFrom(this.u2L);
    this.d2R.copyFrom(this.d2L); this.d1R.copyFrom(this.d1L);
    this.dryR.copyFrom(this.dryL);
    this.monoRun = false;
  }

  reset() {
    this.u1L.reset(); this.u2L.reset(); this.d2L.reset(); this.d1L.reset();
    for (const guard of Object.values(this.headroom)) guard.reset();
    this.delayFeedbackGuard.reset();
    this.u1R.reset(); this.u2R.reset(); this.d2R.reset(); this.d1R.reset();
    this.dryL.reset(); this.dryR.reset(); this.monoRun = false;
    this.chDl1.reset(); this.chDl2.reset();
    this.dlL.reset(); this.dlR.reset(); this.dlDamp.reset();
    this.chPhase = 0;
    this.driveColorL.reset(); this.driveColorR.reset(); this.comp.reset();
    this.ott.reset(); this.eq.reset();
    this.meterInput = 0; this.meterOtt = 0; this.meterCompIn = 0; this.meterCompOut = 0;
    this.meterEchoIn = 0; this.meterEchoL = 0; this.meterEchoR = 0;
  }

  setParams(pv, f) {
    this.eq.setParams(k => pv[f[fid(k)]]);
    this.driveColorL.setParams(pv[f[fid('fx.drive.type')]], pv[f[fid('fx.drive.tone')]]);
    this.driveColorR.setParams(pv[f[fid('fx.drive.type')]], pv[f[fid('fx.drive.tone')]]);
    const amt = pv[f[P_FXDRIVE_AMT]];
    if (amt !== this.driveAmt) {
      this.driveAmt = amt;
      this.drivePre = 1 + amt * 2;
      this.driveK = 1 + amt * 12;
      this.driveNorm = 1 / (this.drivePre * Math.tanh(this.driveK));
    }
    const dOn = pv[f[P_FXDRIVE_ON]] > 0.5;
    this.driveOff = !dOn;
    this.driveWet.target = mixGate(dOn, pv[f[P_FXDRIVE_MIX]], true);
    this.driveDry.target = mixGate(dOn, pv[f[P_FXDRIVE_MIX]], false);

    // Legacy makeup is retained in saved/native patches, not applied on web.
    this.comp.setParams(pv[f[P_FXCOMP_ON]] > 0.5, pv[f[P_FXCOMP_THR]], pv[f[fid('fx.comp.att')]], pv[f[fid('fx.comp.rel')]], pv[f[fid('fx.comp.ratio')]]);
    this.ott.setParams(pv[f[P_FXOTT_ON]] > 0.5, pv[f[P_FXOTT_DEPTH]],
      pv[f[P_FXOTT_TIME]], pv[f[P_FXOTT_UP]], pv[f[P_FXOTT_DOWN]]);

    this.chRate = pv[f[P_FXCHORUS_RATE]];
    this.chDepth = pv[f[P_FXCHORUS_DEPTH]];
    const cOn = pv[f[P_FXCHORUS_ON]] > 0.5;
    this.chorusOff = !cOn;
    this.chWet.target = mixGate(cOn, pv[f[P_FXCHORUS_MIX]] * 0.8, true);
    this.chDry.target = mixGate(cOn, pv[f[P_FXCHORUS_MIX]] * 0.8, false);

    this.dlTime.target = pv[f[P_FXDELAY_TIME]];
    this.dlFb.target = pv[f[P_FXDELAY_FB]];
    const delOn = pv[f[P_FXDELAY_ON]] > 0.5;
    this.delayOff = !delOn;
    this.dlWet.target = mixGate(delOn, pv[f[P_FXDELAY_MIX]] * 0.85, true);
    this.dlDry.target = mixGate(delOn, pv[f[P_FXDELAY_MIX]] * 0.85, false);

    const rOn = pv[f[P_FXREVERB_ON]] > 0.5;
    this.verbOn = rOn;
    this.verbSize = Math.min(1, Math.max(0, pv[f[P_FXREVERB_SIZE]]));
    const rAmt = pv[f[P_FXREVERB_MIX]] * 0.9;
    this.verbWet.target = mixGate(rOn, rAmt, true);
    this.verbDry.target = mixGate(rOn, rAmt, false);

    this.bus = Math.max(0, Math.min(BUSES - 1, pv[f[P_OUT]] | 0));
  }

  // Hold-off before the gate may close: one full delay round trip plus
  // GATE_HOLD, so an echo still in flight can never be cut.
  holdSamples() {
    const extra = this.delayOff ? 0 : this.dlTime.target;
    return (GATE_HOLD + extra) * this.sr;
  }

  shape(x) { return Math.tanh(x * this.driveK) * this.driveNorm; }

  // One channel of a block through the 4x oversampled shaper: interpolate to
  // 2x then 4x, shape, decimate back. Stage at a time, so each FIR's taps are
  // loaded once per block rather than once per sample.
  //
  // INVARIANT: driveK/driveNorm/drivePre are block-rate. setParams runs once
  // per block, before any pad renders, so shaping a whole block under one set
  // of gains is exactly what the sample-at-a-time form did (verified: 0.0
  // difference over 76800 samples with AMT stepping every block). Only the
  // wet/dry mix moves per sample, and that is applied after this returns.
  // If a drive coefficient is ever ramped INSIDE a block — a J1-style smoother
  // on the FX amounts would do it — this must be called once per coefficient
  // chunk instead. Ignoring that diverges by ~0.3 full scale, not by an epsilon.
  driveBlock(u1, u2, d2, d1, out, n) {
    u1.interpBlock(OS_IN, OS_X2, n);
    u2.interpBlock(OS_X2, OS_X4, 2 * n);
    const color = u1 === this.u1L ? this.driveColorL : this.driveColorR;
    const K = this.driveK, norm = this.driveNorm, m = 4 * n;
    for (let i = 0; i < m; i++) OS_X4[i] = color.shape(OS_X4[i], K, norm);
    d2.decimBlock(OS_X4, OS_Y2, 2 * n);
    d1.decimBlock(OS_Y2, out, n);
  }

  // Returns false when the chain is gated and contributed nothing. `live` is
  // false when no voice wrote into the buffer this block, so the input is
  // known to be silent without scanning it.
  process(L, R, n, live) {
    let inPk = 0;
    if (live) {
      for (let i = 0; i < n; i++) {
        const a = L[i] < 0 ? -L[i] : L[i], b = R[i] < 0 ? -R[i] : R[i];
        if (a > inPk) inPk = a;
        if (b > inPk) inPk = b;
      }
    }
    if (this.gated) {
      if (inPk <= GATE_EPS) return false;
      this.gated = false;
      this.silentFor = 0;
    }

    // Gate a stage only when it is OFF; mix == 0 while ON must keep its state
    // accumulating, exactly as the C++ chain does.
    const driveGate = this.driveOff && this.driveWet.target === 0 && Math.abs(this.driveWet.cur) < 1e-6;

    const chorusGate = this.chorusOff && this.chWet.target === 0 && Math.abs(this.chWet.cur) < 1e-6;
    const delayGate = this.delayOff && this.dlWet.target === 0 && Math.abs(this.dlWet.cur) < 1e-6;
    if (driveGate && !this.driveGated) {
      this.driveWet.snap(0); this.driveDry.snap(1);
      this.driveColorL.reset(); this.driveColorR.reset();
      this.u1L.reset(); this.u2L.reset(); this.d2L.reset(); this.d1L.reset();
      this.u1R.reset(); this.u2R.reset(); this.d2R.reset(); this.d1R.reset();
    }

    if (chorusGate && !this.chorusGated) { this.chWet.snap(0); this.chDry.snap(1); this.chDl1.reset(); this.chDl2.reset(); }
    if (delayGate && !this.delayGated) { this.dlWet.snap(0); this.dlDry.snap(1); this.dlL.reset(); this.dlR.reset(); this.dlDamp.reset(); this.delayFeedbackGuard.reset(); }
    this.driveGated = driveGate;
    this.chorusGated = chorusGate; this.delayGated = delayGate;
    if (this.meterSelected) for (let i = 0; i < n; i++) this.meterInput += 0.5 * (L[i] * L[i] + R[i] * R[i]);
    this.headroom.input.process(L, R, n);
    this.eq.process(L, R, n);

    // ---- OTT -> compressor (automatic level matching) ----
    this.ott.process(L, R, n);
    if (this.meterSelected) for (let i = 0; i < n; i++) this.meterOtt += 0.5 * (L[i] * L[i] + R[i] * R[i]);
    this.headroom.ott.process(L, R, n);
    if (this.meterSelected) for (let i = 0; i < n; i++) this.meterCompIn += 0.5 * (L[i] * L[i] + R[i] * R[i]);
    this.comp.process(L, R, n);
    if (this.meterSelected) for (let i = 0; i < n; i++) this.meterCompOut += 0.5 * (L[i] * L[i] + R[i] * R[i]);
    this.headroom.comp.process(L, R, n);

    // ---- drive (4x oversampled tanh waveshaper) ----
    // The dry path always runs through the DRIVE_LATENCY delay, so the mix
    // stays time-aligned with the FIR group delay and the reported chain
    // latency is constant whether the drive is active or gated.
    let mono = true;
    for (let i = 0; i < n; i++) if (L[i] !== R[i]) { mono = false; break; }
    if (!mono && this.monoRun) this.syncRight();
    this.monoRun = mono;
    if (this.dbL.length < n) { this.dbL = new Float32Array(n); this.dbR = new Float32Array(n); }
    const dbL = this.dbL, dbR = this.dbR;
    this.dryL.block(L, dbL, n);
    if (!mono) this.dryR.block(R, dbR, n);
    if (this.driveGated) {
      for (let i = 0; i < n; i++) L[i] = dbL[i];
      if (mono) { for (let i = 0; i < n; i++) R[i] = dbL[i]; } else { for (let i = 0; i < n; i++) R[i] = dbR[i]; }
    } else {
      const pre = this.drivePre;
      osScratch(n);
      if (this.wetL.length < n) { this.wetL = new Float64Array(n); this.wetR = new Float64Array(n); }
      for (let i = 0; i < n; i++) OS_IN[i] = pre * L[i];
      this.driveBlock(this.u1L, this.u2L, this.d2L, this.d1L, this.wetL, n);
      if (!mono) {
        for (let i = 0; i < n; i++) OS_IN[i] = pre * R[i];
        this.driveBlock(this.u1R, this.u2R, this.d2R, this.d1R, this.wetR, n);
      }
      for (let i = 0; i < n; i++) {
        const wet = this.driveWet.next(), dry = this.driveDry.next();
        const dl = dry * dbL[i] + wet * this.driveColorL.processTone(this.wetL[i]);
        L[i] = dl;
        R[i] = dry * (mono ? dbL[i] : dbR[i]) + wet * this.driveColorR.processTone(mono ? this.wetL[i] : this.wetR[i]);
      }
    }

    this.headroom.drive.process(L, R, n);

    // ---- chorus (two modulated taps, stereo) ----
    if (!this.chorusGated) {
      const sr = this.sr, inc = this.chRate / sr;
      const depth = 0.0008 + this.chDepth * 0.0045;
      let ph = this.chPhase;
      for (let i = 0; i < n; i++) {
        ph += inc;
        if (ph >= 1) ph -= 1;
        const lfo = Math.sin(2 * Math.PI * ph);
        const mono = 0.5 * (L[i] + R[i]);
        this.chDl1.write(mono); this.chDl2.write(mono);
        const c1 = this.chDl1.readHermite((0.012 + depth * lfo) * sr);
        const c2 = this.chDl2.readHermite((0.017 - depth * 0.8 * lfo) * sr);
        const wet = this.chWet.next(), dry = this.chDry.next();
        L[i] = dry * L[i] + wet * c1;
        R[i] = dry * R[i] + wet * c2;
      }
      this.chPhase = ph;
    }

    this.headroom.chorus.process(L, R, n);

    // ---- ping-pong delay ----
    if (!this.delayGated) {
      const sr = this.sr;
      for (let i = 0; i < n; i++) {
        const dt = this.dlTime.next() * sr;
        const fb = this.dlFb.next();
        const dL = this.dlL.readHermite(dt);
        const dR = this.dlR.readHermite(dt);
        if (this.meterSelected) {
          this.meterEchoIn += 0.5 * (L[i] * L[i] + R[i] * R[i]);
        }
        const mono = 0.5 * (L[i] + R[i]);
        const feedbackL = mono + fb * dR;
        const feedbackR = this.dlDamp.process(fb * dL);
        const feedbackGain = this.delayFeedbackGuard.gainFor(feedbackL, feedbackR);
        this.dlL.write(feedbackL * feedbackGain);
        this.dlR.write(feedbackR * feedbackGain);
        const wet = this.dlWet.next(), dry = this.dlDry.next();
        if (this.meterSelected) {
          this.meterEchoL += (wet * dL) * (wet * dL); this.meterEchoR += (wet * dR) * (wet * dR);
        }
        L[i] = dry * L[i] + wet * dL;
        R[i] = dry * R[i] + wet * dR;
      }
    }

    this.headroom.delay.process(L, R, n);
    let outPk = 0;
    for (let i = 0; i < n; i++) {
      const a = L[i] < 0 ? -L[i] : L[i], b = R[i] < 0 ? -R[i] : R[i];
      if (a > outPk) outPk = a;
      if (b > outPk) outPk = b;
    }
    this.outPk = outPk;
    if (inPk <= GATE_EPS && outPk <= GATE_EPS) {
      this.silentFor += n;
      if (this.silentFor >= this.holdSamples()) this.gated = true;
    } else {
      this.silentFor = 0;
    }
    return true;
  }
}

// Per-bus output stage (review D1): master gain -> DC block -> lookahead
// limiter, once on the summed bus. MAIN therefore has a real -1 dBFS ceiling.
class BusOut {
  constructor(sr) {
    this.sr = sr;
    this.inputGuard = new StereoPeakGuard(sr);
    this.gain = new Smooth(sr, FX_TAU);
    this.dcL = new Biquad(); this.dcL.highpass(8, 0.707, sr);
    this.dcR = new Biquad(); this.dcR.highpass(8, 0.707, sr);
    // WebAudio's DynamicsCompressor applies a spec-defined makeup gain
    // ((1/c(1))^0.6). The web limiter WAS that node, so keep its ~4.5 dB ahead
    // of the lookahead limiter or the port sits under the old loudness.
    const c1 = Math.pow(1 / LIM_THR, 1 / LIM_RATIO - 1);
    this.lim = new LookaheadLimiter(sr, Math.pow(1 / c1, 0.6));
    this.silentFor = 1e9; this.gated = true;
  }
  latencySamples() { return this.lim.la; }
  process(L, R, n, inPk) {
    if (this.gated) {
      if (inPk <= GATE_EPS) return false;
      this.gated = false;
      this.silentFor = 0;
    }
    this.inputGuard.process(L, R, n);
    const g0 = this.gain.cur, g1 = this.gain.nextN(n);
    const dg = n > 0 ? (g1 - g0) / n : 0;
    for (let i = 0; i < n; i++) {
      const g = g0 + dg * i;
      L[i] = this.dcL.process(L[i] * g);
      R[i] = this.dcR.process(R[i] * g);
    }
    this.lim.process(L, R, n);
    let outPk = 0;
    for (let i = 0; i < n; i++) {
      const a = L[i] < 0 ? -L[i] : L[i], b = R[i] < 0 ? -R[i] : R[i];
      if (a > outPk) outPk = a;
      if (b > outPk) outPk = b;
    }
    if (inPk <= GATE_EPS && outPk <= GATE_EPS) {
      this.silentFor += n;
      if (this.silentFor >= GATE_HOLD * this.sr) this.gated = true;
    } else {
      this.silentFor = 0;
    }
    return true;
  }
}

// ---------- flat parameter store ----------
// `pad<i>.<field>` resolves to `i * NF + field` once, in the message handler.
// The render loop never builds a key string and never hashes one (review D7).
const OSC_FIELDS = ['table', 'pos', 'tune', 'fine', 'phase', 'unison', 'detune', 'level'];
const FIELDS = [
  ...OSC_FIELDS.map((f) => 'oscA.' + f),
  ...OSC_FIELDS.map((f) => 'oscB.' + f),
  'noise.color', 'noise.level', 'ring.freq', 'ring.mix',
  'penv.amt', 'penv.dec', 'aenv.att', 'aenv.hold', 'aenv.dec', 'aenv.curve',
  'flt.on', 'flt.type', 'flt.cut', 'flt.res', 'flt.drive',
  'mod1.src', 'mod1.dst', 'mod1.amt', 'mod2.src', 'mod2.dst', 'mod2.amt',
  'mod3.src', 'mod3.dst', 'mod3.amt', 'mod4.src', 'mod4.dst', 'mod4.amt',
  'modenv.dec', 'lvl', 'pan', 'v2l', 'v2m', 'choke', 'out',
  // FX rack — these used to live in native WebAudio nodes on the main thread
  // and never reached the worklet at all (review W6).
  'fx.drive.on', 'fx.drive.amt', 'fx.drive.mix',
  'fx.comp.on', 'fx.comp.thr', 'fx.comp.gain',
  'fx.chorus.on', 'fx.chorus.rate', 'fx.chorus.depth', 'fx.chorus.mix',
  'fx.delay.on', 'fx.delay.time', 'fx.delay.fb', 'fx.delay.mix',
  'fx.reverb.on', 'fx.reverb.size', 'fx.reverb.mix',
  'fx.ott.on', 'fx.ott.depth', 'fx.ott.time', 'fx.ott.up', 'fx.ott.down', 'fx.ott.gain',
  ...globalThis.FableEqFields,
  'fx.comp.att', 'fx.comp.rel', 'fx.comp.ratio', 'fx.drive.tone', 'fx.drive.type',
];
const NF = FIELDS.length;
const fid = (name) => FIELDS.indexOf(name);

// Offsets inside an oscillator group (oscA and the sample layer share it).
const A_TABLE = 0, A_POS = 1, A_TUNE = 2, A_FINE = 3;
const A_PHASE = 4, A_UNISON = 5, A_DETUNE = 6, A_LEVEL = 7;
const OSC_A = 0, OSC_B = 8;

const P_NOISE_COLOR = fid('noise.color'), P_NOISE_LEVEL = fid('noise.level');
const P_RING_FREQ = fid('ring.freq'), P_RING_MIX = fid('ring.mix');
const P_PENV_AMT = fid('penv.amt'), P_PENV_DEC = fid('penv.dec');
const P_AENV_ATT = fid('aenv.att'), P_AENV_HOLD = fid('aenv.hold');
const P_AENV_DEC = fid('aenv.dec'), P_AENV_CURVE = fid('aenv.curve');
const P_FLT_ON = fid('flt.on'), P_FLT_TYPE = fid('flt.type');
const P_FLT_CUT = fid('flt.cut'), P_FLT_RES = fid('flt.res'), P_FLT_DRIVE = fid('flt.drive');
const P_MOD1 = fid('mod1.src'); // src, dst, amt triplets, 3 apart
const P_MODENV_DEC = fid('modenv.dec');
const P_LVL = fid('lvl'), P_PAN = fid('pan');
const P_V2L = fid('v2l'), P_V2M = fid('v2m'), P_CHOKE = fid('choke'), P_OUT = fid('out');
const P_FXDRIVE_ON = fid('fx.drive.on'), P_FXDRIVE_AMT = fid('fx.drive.amt'), P_FXDRIVE_MIX = fid('fx.drive.mix');
const P_FXCOMP_ON = fid('fx.comp.on'), P_FXCOMP_THR = fid('fx.comp.thr');
const P_FXOTT_ON = fid('fx.ott.on'), P_FXOTT_DEPTH = fid('fx.ott.depth'), P_FXOTT_TIME = fid('fx.ott.time');
const P_FXOTT_UP = fid('fx.ott.up'), P_FXOTT_DOWN = fid('fx.ott.down');
const P_FXCHORUS_ON = fid('fx.chorus.on'), P_FXCHORUS_RATE = fid('fx.chorus.rate');
const P_FXCHORUS_DEPTH = fid('fx.chorus.depth'), P_FXCHORUS_MIX = fid('fx.chorus.mix');
const P_FXDELAY_ON = fid('fx.delay.on'), P_FXDELAY_TIME = fid('fx.delay.time');
const P_FXDELAY_FB = fid('fx.delay.fb'), P_FXDELAY_MIX = fid('fx.delay.mix');
const P_FXREVERB_ON = fid('fx.reverb.on'), P_FXREVERB_SIZE = fid('fx.reverb.size'), P_FXREVERB_MIX = fid('fx.reverb.mix');

// Global FX controls are a second, post-mix chain. Their field order matches
// the pad insert suffixes, letting PadFx be reused without sharing state.
const GROUP_FX_FIELDS = FIELDS.slice(P_FXDRIVE_ON);
const GLOBALS = ['seq.bpm', 'master.swing', 'master.volume', ...GROUP_FX_FIELDS];
const G_BPM = NPADS * NF, G_SWING = G_BPM + 1, G_VOL = G_BPM + 2;
const G_FX = G_VOL + 1;
const PSIZE = NPADS * NF + GLOBALS.length;

const PARAM_INDEX = new Map();
for (let i = 0; i < NPADS; i++) {
  for (let f = 0; f < NF; f++) PARAM_INDEX.set('pad' + i + '.' + FIELDS[f], i * NF + f);
}
GLOBALS.forEach((g, i) => PARAM_INDEX.set(g, NPADS * NF + i));

// Modulation destinations, in `dst` order (1..9).
const M_POSA = 0, M_POSB = 1, M_LEVEL = 2, M_CUT = 3, M_PITCH = 4;
const M_FINEA = 5, M_FINEB = 6, M_NOISE = 7, M_RES = 8;
const M_SCALE = [1, 1, 1, 1, 24, 200, 200, 1, 1];
const NMOD = 9;

function makeOscState() {
  return {
    phases: new Float64Array(MAXUNI),
    incs: new Float64Array(MAXUNI),
    incsEnd: new Float64Array(MAXUNI),
    gl: new Float32Array(MAXUNI),
    gr: new Float32Array(MAXUNI),
    uni: 1, off0: 0, off1: 0, off0b: 0, off1b: 0,
    blend: 0, blendEnd: 0,
    ft: 0, ftEnd: 0,
    gain: 0, mask: 0, size: 0, data: null, posSm: -1,
  };
}

function makeSampleState() {
  return {
    pos: -1, index: -1, done: false,
    // anti-alias pre-filter cursor: `fi` is the newest source index already
    // filtered into h0..h3 (h3 newest), walked in the read direction.
    primed: false, pre: false, fi: 0, z1: 0, z2: 0,
    h0: 0, h1: 0, h2: 0, h3: 0,
  };
}

function makeFilterState() {
  return {
    svf: new Float64Array(8),
    // The outgoing filter type keeps running on a copy of the state for the
    // length of the crossfade, so a type switch is not a step (JUCE parity).
    svfOld: new Float64Array(8),
    ftypeOld: 0, twoPoleOld: false,
    xfLeft: 0, xfLen: 1,
    cutSm: 0,
    satXL: 0, satXR: 0,
    ftype: 0, twoPole: false,
    a1: 0, a2: 0, a3: 0, k1: 0,
  };
}

class PadVoice {
  constructor() {
    this.active = false;
    this.vel = 1; this.rand = 0;
    this.t = 0;
    this.ampLevel = 0; this.choking = false;
    this.oA = makeOscState(); this.sample = makeSampleState();
    this.f = makeFilterState();
    this.noiseY = 0;
    this.ringPhase = 0.25;
    this.dcxL = 0; this.dcxR = 0; this.dcyL = 0; this.dcyR = 0;
  }

  trigger(v, rand) {
    this.active = true; this.choking = false;
    this.vel = v; this.rand = rand;
    this.t = 0; this.ampLevel = 0;
    this.oA.posSm = -1;
    this.sample.pos = -1; this.sample.index = -1; this.sample.done = false;
    this.sample.primed = false;
    this.f.svf.fill(0); this.f.cutSm = 0; this.f.satXL = 0; this.f.satXR = 0;
    this.f.xfLeft = 0;
    this.noiseY = 0;
    this.ringPhase = 0.25;
    this.dcxL = this.dcxR = this.dcyL = this.dcyR = 0;
  }

  choke() { if (this.active) this.choking = true; }
  kill() { this.active = false; this.choking = false; this.ampLevel = 0; }
}

class DrumProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.pv = new Float64Array(PSIZE);
    this.tables = [];
    this.samples = [];
    // padIds[i][field] — the flat store index of every field of pad i.
    this.padIds = [];
    for (let i = 0; i < NPADS; i++) {
      const ids = new Int32Array(NF);
      for (let f = 0; f < NF; f++) ids[f] = i * NF + f;
      this.padIds.push(ids);
    }
    this.voices = [];
    // One fading tail slot per pad: a retrigger moves the sounding hit here
    // and fades it, instead of cutting it to zero in one sample (review D2).
    this.tails = [];
    for (let i = 0; i < NPADS; i++) { this.voices.push(new PadVoice()); this.tails.push(new PadVoice()); }
    this.pats = new Uint8Array(NPATTERNS * NPADS * STEPS);
    this.chain = [0]; this.chainPos = 0;
    this.playing = false;
    this.step = -1;
    this.samplesToNext = 0;
    this.sel = 0;
    this.rng = new Rng(0x1d3c5b7f);
    this.dcR = Math.pow(DC_R_48, NOISE_SR / sampleRate);
    this.m0 = new Float64Array(NMOD);
    this.m1 = new Float64Array(NMOD);
    // ---- hosted clip transport (SQ-4, docs/sq4-clips.md §6) ----
    this.hosted = false;
    this.hostBpm = 120;
    this.hostSwing = 0;
    this.hostAnchor = 0; // songStartFrame — the shared timebase's beat zero
    this.clip = null; // { data: Uint8Array, bars } — byte per pad-step, bar-major
    this.clipPend = null; // { data, bars, at }
    this.clipStopAt = -1;
    this.clipStep = -1; // absolute step within the clip
    this.clipToNext = 0;
    this.vizCount = 0;
    // Pad inserts feed their routed buses; a second, same-control group chain
    // follows each physical output sum. Those instances form one logical DR-1
    // group strip while preserving separate MAIN/AUX output streams.
    this.padFx = [];
    for (let i = 0; i < NPADS; i++) this.padFx.push(new PadFx(sampleRate));
    this.groupFx = []; this.groupFxIds = new Int32Array(NF);
    for (let f = P_FXDRIVE_ON; f < NF; f++) this.groupFxIds[f] = G_FX + f - P_FXDRIVE_ON;
    this.busOut = []; this.verbs = []; this.verbInputGuards = [];
    this.groupVerbs = []; this.groupVerbInputGuards = [];
    for (let b = 0; b < BUSES; b++) {
      this.busOut.push(new BusOut(sampleRate)); this.verbs.push(new Freeverb(sampleRate));
      this.verbInputGuards.push(new StereoPeakGuard(sampleRate));
      this.groupFx.push(new PadFx(sampleRate)); this.groupVerbs.push(new Freeverb(sampleRate));
      this.groupVerbInputGuards.push(new StereoPeakGuard(sampleRate));
    }
    this.sizeAcc = new Float64Array(BUSES);
    this.sizeW = new Float64Array(BUSES);
    this.metering = false; this.meterSamples = 0; this.meterEnergy = new Float64Array(4); this.meterReduction = 0;
    this.echoMetering = false; this.echoSamples = 0; this.echoEnergy = new Float64Array(3);
    this.reverbMetering = false; this.reverbSamples = 0; this.reverbEnergy = new Float64Array(3);
    this.meterPad = 0; this.meterBus = 0;
    this.fxDirty = true;
    // Reported the way the plugin does: drive FIR group delay + limiter
    // lookahead. Constant whether a stage is active, bypassed or gated.
    this.latency = DRIVE_LATENCY * 2 + this.busOut[0].latencySamples();
    this.cap = 0;
    this.padL = []; this.padR = [];
    this.busL = []; this.busR = []; this.verbInL = []; this.verbInR = [];
    this.ensureCap(128);
    this.port.onmessage = (e) => this.onMsg(e.data);
    this.port.postMessage({ t: 'latency', n: this.latency });
  }

  // Scratch buffers for one render quantum. 128 in every browser today; grown
  // once if a host ever asks for more, never inside the render loop.
  ensureCap(n) {
    if (n <= this.cap) return;
    this.cap = n;
    for (let i = 0; i < NPADS; i++) { this.padL[i] = new Float32Array(n); this.padR[i] = new Float32Array(n); }
    for (let b = 0; b < BUSES; b++) {
      this.busL[b] = new Float32Array(n); this.busR[b] = new Float32Array(n);
      this.verbInL[b] = new Float32Array(n); this.verbInR[b] = new Float32Array(n);
    }
    this.tmpL = new Float32Array(n); this.tmpR = new Float32Array(n);
    this.fL = new Float32Array(n); this.fR = new Float32Array(n);
    this.xL = new Float32Array(n); this.xR = new Float32Array(n);
  }

  setParam(k, v) {
    const i = PARAM_INDEX.get(k);
    if (i !== undefined) { this.pv[i] = v; this.fxDirty = true; }
  }

  onMsg(d) {
    switch (d.t) {
      case 'dynamics':
        this.metering = !!d.on; this.meterEnergy.fill(0); this.meterSamples = 0; this.meterReduction = 0;
        break;
      case 'echo':
        this.echoMetering = !!d.on; this.echoEnergy.fill(0); this.echoSamples = 0;
        break;
      case 'reverb':
        this.reverbMetering = !!d.on; this.reverbEnergy.fill(0); this.reverbSamples = 0;
        break;
      case 'meterPad':
        this.meterPad = Math.max(0, Math.min(NPADS - 1, d.pad | 0));
        this.meterEnergy.fill(0); this.echoEnergy.fill(0); this.reverbEnergy.fill(0);
        this.meterSamples = this.echoSamples = this.reverbSamples = 0;
        this.meterReduction = 0;
        for (const fx of this.padFx) {
          fx.meterInput = fx.meterOtt = fx.meterCompIn = fx.meterCompOut = 0;
          fx.meterEchoIn = fx.meterEchoL = fx.meterEchoR = 0;
        }
        break;
      case 'init':
        for (const k in d.params) {
          const v = d.params[k];
          if (Number.isFinite(v)) this.setParam(k, v);
        }
        break;
      case 'p': if (Number.isFinite(d.v)) this.setParam(d.k, d.v); break;
      case 'seed': this.rng = new Rng(d.v | 0); break;
      case 'tables':
        this.tables = d.list.map((x) => ({
          frames: x.frames, mips: x.mips, size: x.size, mask: x.size - 1,
          data: new Float32Array(x.buf),
        }));
        break;
      case 'samples':
        this.samples = d.list.map((x) => ({
          sampleRate: x.sampleRate,
          data: new Float32Array(x.buf),
        }));
        break;
      case 'trig': this.trigger(d.pad | 0, d.v); break;
      case 'pats': this.pats = new Uint8Array(d.data.slice(0)); break;
      case 'chain':
        if (Array.isArray(d.list) && d.list.length) {
          // Clamp like JUCE does: an out-of-range entry used to select a
          // pattern slot that does not exist and play a silent bar (D6).
          this.chain = d.list.map((x) => Math.max(0, Math.min(NPATTERNS - 1, x | 0)));
          this.chainPos = Math.min(this.chainPos, this.chain.length - 1);
        }
        break;
      case 'play':
        if (this.hosted) break; // conductor owns the transport
        this.playing = true; this.step = -1; this.chainPos = 0; this.samplesToNext = 0;
        break;
      case 'stop': this.playing = false; this.step = -1; break;
      case 'sel': this.sel = Math.max(0, Math.min(NPADS - 1, d.pad | 0)); break;
      case 'panic':
        this.meterEnergy.fill(0); this.meterSamples = 0; this.meterReduction = 0;
        this.echoEnergy.fill(0); this.echoSamples = 0;
        this.reverbEnergy.fill(0); this.reverbSamples = 0;
        for (const v of this.voices) v.kill();
        for (const v of this.tails) v.kill();
        if (!d.preserveTransport) {
          this.clip = null; this.clipPend = null; this.clipStopAt = -1; this.clipStep = -1;
        }
        break;
      case 'host': this.hosted = !!d.on; break;
      case 'tempo':
        if (Number.isFinite(d.bpm)) this.hostBpm = d.bpm;
        if (Number.isFinite(d.swing)) this.hostSwing = d.swing;
        if (Number.isFinite(d.anchor)) this.hostAnchor = d.anchor;
        break;
      case 'clip':
        this.clipPend = { data: new Uint8Array(d.data), bars: Math.max(1, d.bars | 0), at: +d.atFrame || 0 };
        this.clipStopAt = -1;
        break;
      case 'clipstop':
        this.clipPend = null;
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

  // ---------- hosted clip transport ----------
  hostTick(n) {
    const end = currentFrame + n;
    if (this.clipStopAt >= 0 && this.clipStopAt < end) {
      this.clipStopAt = -1;
      if (this.clip) {
        this.clip = null;
        this.clipStep = -1;
        // sounding pads ring out (design: DR-1 stop lets voices decay)
      }
      // ack even when nothing was playing — the stop may have targeted a
      // pending-only launch and the conductor clears its STOP marker on this
      this.port.postMessage({ t: 'clipstop', frame: currentFrame });
    }
    if (this.clipPend && this.clipPend.at < end) {
      this.clip = this.clipPend;
      this.clipPend = null;
      // Phase-lock to the shared timebase: enter at the global song position
      // modulo the clip length, so a (re)launch can never desync devices —
      // position is derived from the anchor, never restarted at step 0.
      this.clipStep = this.clipPhase(Math.round) - 1;
      this.clipToNext = 0;
      this.port.postMessage({ t: 'clipstart', frame: currentFrame });
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
    const total = this.clip.bars * STEPS;
    const idx = quantize(Math.max(0, currentFrame - this.hostAnchor) / dur);
    return ((idx % total) + total) % total;
  }

  clipFire() {
    const bpm = Math.max(60, Math.min(200, this.hostBpm || 120));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = Math.min(1, Math.max(0, this.hostSwing || 0));
    const total = this.clip.bars * STEPS;
    const abs = (this.clipStep + 1) % total;
    const s = abs % STEPS;
    const bar = (abs / STEPS) | 0;
    const hits = [];
    for (let i = 0; i < NPADS; i++) {
      const val = this.clip.data[(bar * NPADS + i) * STEPS + s];
      if (val) {
        this.trigger(i, val === 2 ? ACCENT_VEL : PLAIN_VEL);
        hits.push(i);
      }
    }
    this.clipStep = abs;
    const offNow = s % 2 === 1 ? swing * SWING_MAX * dur : 0;
    const sNext = (s + 1) % STEPS;
    const offNext = sNext % 2 === 1 ? swing * SWING_MAX * dur : 0;
    // Schedule the next step at its absolute anchor-grid time. A free-running
    // countdown (dur - offNow + offNext) drops the block-quantization residue
    // each fire and drifts late without bound against the shared timebase.
    const idx = Math.round((currentFrame - this.hostAnchor - offNow) / dur);
    this.clipToNext = this.hostAnchor + (idx + 1) * dur + offNext - currentFrame;
    this.port.postMessage({ t: 'pos', step: s, bar, hits });
  }

  trigger(padI, vel) {
    if (padI < 0 || padI >= NPADS) return;
    const pv = this.pv;
    const g = pv[this.padIds[padI][P_CHOKE]] | 0;
    if (g > 0) {
      for (let j = 0; j < NPADS; j++) {
        if (j !== padI && (pv[this.padIds[j][P_CHOKE]] | 0) === g) {
          this.voices[j].choke();
          this.tails[j].choke();
        }
      }
    }
    // Retrigger: park the sounding hit on the tail slot with a choke fade and
    // start the new one on the freed slot. A retrigger faster than the fade
    // (<3 ms) drops the older tail — bounded, and below the fade's own level.
    const cur = this.voices[padI];
    if (cur.active && cur.ampLevel > 1e-4) {
      const tail = this.tails[padI];
      tail.kill();
      this.tails[padI] = cur;
      cur.choking = true;
      this.voices[padI] = tail;
    }
    const v = this.voices[padI];
    v.trigger(Math.max(0, Math.min(1, Number.isFinite(vel) ? vel : 1)), this.rng.uni());
    const f = this.padIds[padI];
    const phaseA = (Math.max(0, Math.min(1, pv[f[OSC_A + A_PHASE]])) * 2048) % 2048;
    for (let i = 0; i < MAXUNI; i++) v.oA.phases[i] = phaseA;
  }

  // Modulation matrix evaluated at voice time `t`, written into `out` (no
  // allocation — the render path calls this twice per block to get a ramp).
  padMod(f, v, t, out) {
    const pv = this.pv;
    const dec = Math.max(0.002, pv[f[P_MODENV_DEC]] / 4.5);
    const env = Math.exp(-t / (dec * sampleRate));
    const velSrc = v.vel * pv[f[P_V2M]];
    for (let i = 0; i < NMOD; i++) out[i] = 0;
    for (let n = 0; n < 4; n++) {
      const b = P_MOD1 + n * 3;
      const src = pv[f[b]] | 0;
      const dst = pv[f[b + 1]] | 0;
      if (!src || !dst || dst > NMOD) continue;
      const s = src === 1 ? env : src === 2 ? velSrc : src === 3 ? v.rand : 0;
      out[dst - 1] += s * (pv[f[b + 2]] || 0) * M_SCALE[dst - 1];
    }
  }

  // Modulation value at block fraction `u`, between the two evaluations the
  // render path made at the block's edges.
  mlerp(i, u) {
    const a = this.m0[i];
    return a + (this.m1[i] - a) * u;
  }

  // `pitch0/pitch1` are absolute MIDI note numbers at the first and last
  // sample of the sub-block; everything derived from them ramps per sample.
  setupOsc(o, f, base, pitch0, pitch1, pos0, pos1, count) {
    const pv = this.pv;
    const table = this.tables[pv[f[base + A_TABLE]] | 0];
    if (!table) return false;

    const freq0 = 440 * Math.pow(2, (pitch0 - 69) / 12);
    const freq1 = 440 * Math.pow(2, (pitch1 - 69) / 12);
    const nyq = sampleRate * 0.45;
    if (!(freq0 > 0 && freq0 <= nyq)) return false;
    const fEnd = Math.min(nyq, Math.max(1e-4, freq1));

    let level = Math.min(1.2, Math.max(0, pv[f[base + A_LEVEL]]));
    level *= level;
    if (!(level >= 1e-5)) return false;

    const uni = Math.max(1, Math.min(MAXUNI, pv[f[base + A_UNISON]] | 0));
    const det = pv[f[base + A_DETUNE]];
    const spr = 0.6;

    const posT = Math.min(1, Math.max(0, pos1));
    const prev = o.posSm;
    if (prev < 0) o.posSm = Math.min(1, Math.max(0, pos0));
    else o.posSm += (posT - o.posSm) * smoothCoef(count, POS_TAU * sampleRate);
    const posPrev = prev < 0 ? o.posSm : prev;

    const spanF = table.frames - 1;
    const posF0 = posPrev * spanF;
    const posF1 = o.posSm * spanF;
    const f0 = posF1 | 0;
    const f1 = Math.min(spanF, f0 + 1);
    // Ramp the morph fraction across the sub-block, but snap when the frame
    // pair changes — the two reads then address different frames (JUCE does
    // the same, DrumEngine.cpp:334).
    o.ftEnd = posF1 - f0;
    o.ft = (posF0 | 0) === f0 ? posF0 - f0 : o.ftEnd;

    const cps0 = freq0 / sampleRate;
    const cps1 = fEnd / sampleRate;
    const maxRatio = Math.pow(2, (Math.abs(det) * 50) / 1200);
    const k = (maxRatio * 1024) / 0.475;
    const mipF0 = Math.log2(cps0 * k);
    const mipF1 = Math.log2(cps1 * k);
    // Full trilinear: blend by the mip fraction always. The old 0.07-octave
    // window hard-switched mips inside a drum pitch envelope (review D5).
    const mipFmax = Math.max(mipF0, mipF1);
    let mip = 0;
    if (mipFmax > 0) mip = Math.min(table.mips - 1, Math.ceil(mipFmax));
    const fineMip = mip > 0 ? mip - 1 : 0;
    if (mip > 0) {
      o.blend = Math.min(1, Math.max(0, 1 - (mipF0 - (mip - 1))));
      o.blendEnd = Math.min(1, Math.max(0, 1 - (mipF1 - (mip - 1))));
    } else {
      o.blend = 0; o.blendEnd = 0;
    }

    o.off0 = (f0 * table.mips + mip) * table.size;
    o.off1 = (f1 * table.mips + mip) * table.size;
    o.off0b = (f0 * table.mips + fineMip) * table.size;
    o.off1b = (f1 * table.mips + fineMip) * table.size;
    o.data = table.data;
    o.mask = table.mask;
    o.size = table.size;
    o.uni = uni;

    for (let u = 0; u < uni; u++) {
      const sprd = uni > 1 ? (u / (uni - 1)) * 2 - 1 : 0;
      const cents = sprd * det * 50;
      const ratio = Math.pow(2, cents / 1200);
      o.incs[u] = cps0 * ratio * table.size;
      o.incsEnd[u] = cps1 * ratio * table.size;
      const pan = Math.max(-1, Math.min(1, sprd * spr));
      const a = ((pan + 1) * Math.PI) / 4;
      o.gl[u] = Math.cos(a);
      o.gr[u] = Math.sin(a);
    }
    o.gain = (level * 0.32) / Math.sqrt(uni);
    return true;
  }

  renderOsc(o, tmpL, tmpR, off, n) {
    const data = o.data, mask = o.mask, size = o.size, g = o.gain;
    const off0 = o.off0, off1 = o.off1, off0b = o.off0b, off1b = o.off1b;
    const invN = n > 0 ? 1 / n : 0;
    const ft0 = o.ft, dFt = (o.ftEnd - ft0) * invN;
    const b0 = o.blend, dB = (o.blendEnd - b0) * invN;
    const blended = b0 > 0.001 || o.blendEnd > 0.001;
    for (let u = 0; u < o.uni; u++) {
      let ph = o.phases[u];
      const inc0 = o.incs[u], dInc = (o.incsEnd[u] - inc0) * invN;
      const gl = o.gl[u] * g, gr = o.gr[u] * g;
      for (let i = 0; i < n; i++) {
        const idx = ph | 0;
        const frac = ph - idx;
        const ft = ft0 + dFt * i;
        const s0 = hermiteTable(data, off0, idx, mask, frac);
        const s1 = hermiteTable(data, off1, idx, mask, frac);
        let s = s0 + ft * (s1 - s0);
        if (blended) {
          const t0 = hermiteTable(data, off0b, idx, mask, frac);
          const t1 = hermiteTable(data, off1b, idx, mask, frac);
          s += (b0 + dB * i) * (t0 + ft * (t1 - t0) - s);
        }
        tmpL[off + i] += s * gl;
        tmpR[off + i] += s * gr;
        ph += inc0 + dInc * i;
        if (ph >= size) ph -= size;
        else if (ph < 0) ph += size;
      }
      o.phases[u] = ph;
    }
  }

  // One-shot sample layer: Hermite read, a two-pole pre-filter whenever the
  // playback rate exceeds 1 (so pitching up no longer aliases), a clamped
  // rate, per-sample rate ramp, and short edge fades (review D4).
  renderSample(st, f, base, pitch0, pitch1, start0, tmpL, tmpR, off, n) {
    if (st.done) return false;
    const pv = this.pv;
    const index = Math.max(0, Math.min(this.samples.length - 1, pv[f[base + A_TABLE]] | 0));
    const sample = this.samples[index];
    if (!sample || sample.data.length < 2) return false;

    const level = Math.max(0, Math.min(1.2, pv[f[base + A_LEVEL]]));
    const gain = level * level * 0.75;
    if (gain <= 1e-6) return false; // early-out: no loop at level 0 (D7)

    const data = sample.data;
    const last = data.length - 1;
    const start = Math.max(0, Math.min(0.999, start0));
    const end = Math.max(start + 1 / data.length, Math.min(1, pv[f[base + A_DETUNE]]));
    const reverse = pv[f[base + A_PHASE]] >= 0.5;
    const lo = start * last;
    const hi = end * last;
    if (st.pos < 0 || st.index !== index) {
      st.index = index;
      st.pos = reverse ? hi : lo;
      st.primed = false;
    }

    const dir = reverse ? -1 : 1;
    const rate = sample.sampleRate / sampleRate;
    const raw0 = rate * Math.pow(2, pitch0 / 12) * dir;
    const raw1 = rate * Math.pow(2, pitch1 / 12) * dir;
    const step0 = Math.max(-MAX_STEP, Math.min(MAX_STEP, raw0));
    const step1 = Math.max(-MAX_STEP, Math.min(MAX_STEP, raw1));
    const dStep = n > 0 ? (step1 - step0) / n : 0;
    const fade = Math.max(1, EDGE_FADE * sample.sampleRate);

    const speed = Math.max(Math.abs(step0), Math.abs(step1));
    const pre = speed > 1;
    // Two one-poles at 0.45/speed of the source Nyquist: the band that would
    // otherwise fold when the read decimates.
    const a = pre ? 1 - Math.exp(-2 * Math.PI * (0.45 / speed)) : 0;
    if (pre && (!st.primed || st.pre !== pre)) {
      const seedI = Math.max(0, Math.min(last, Math.round(st.pos)));
      const y = data[seedI];
      st.fi = seedI - 4 * dir;
      st.z1 = y; st.z2 = y;
      st.h0 = y; st.h1 = y; st.h2 = y; st.h3 = y;
    }
    st.primed = true;
    st.pre = pre;

    let pos = st.pos;
    for (let i = 0; i < n; i++) {
      if ((!reverse && pos >= hi) || (reverse && pos <= lo)) {
        st.done = true;
        break;
      }
      const i0 = Math.floor(pos);
      const frac = pos - i0;
      let value;
      if (pre) {
        // Walk the pre-filter to the newest index the Hermite read needs.
        const target = dir > 0 ? i0 + 2 : i0 - 1;
        while (dir > 0 ? st.fi < target : st.fi > target) {
          st.fi += dir;
          const x = data[st.fi < 0 ? 0 : st.fi > last ? last : st.fi];
          st.z1 += (x - st.z1) * a;
          st.z2 += (st.z1 - st.z2) * a;
          st.h0 = st.h1; st.h1 = st.h2; st.h2 = st.h3; st.h3 = st.z2;
        }
        value = dir > 0
          ? hermite4(st.h0, st.h1, st.h2, st.h3, frac)
          : hermite4(st.h3, st.h2, st.h1, st.h0, frac);
      } else {
        const im1 = i0 > 0 ? i0 - 1 : 0;
        const ip1 = i0 < last ? i0 + 1 : last;
        const ip2 = i0 + 2 < last ? i0 + 2 : last;
        const ic = i0 < 0 ? 0 : i0 > last ? last : i0;
        value = hermite4(data[im1], data[ic], data[ip1], data[ip2], frac);
      }
      // Edge fades replace the hard cuts at START/END and the reverse stop.
      const dist = Math.min(pos - lo, hi - pos);
      const eg = dist < fade ? Math.max(0, dist / fade) : 1;
      const s = value * gain * eg;
      tmpL[off + i] += s;
      tmpR[off + i] += s;
      pos += step0 + dStep * i;
    }
    st.pos = pos;
    return true;
  }

  setupFilter(fs, f, mCut, mRes, count) {
    const pv = this.pv;
    const ftype = pv[f[P_FLT_TYPE]] | 0;
    if (ftype !== fs.ftype && fs.cutSm > 0) {
      // Hand the outgoing type its own copy of the state and fade it out over
      // TYPE_XFADE; the switch used to step the output in one sample.
      fs.svfOld.set(fs.svf);
      fs.ftypeOld = fs.ftype;
      fs.twoPoleOld = fs.twoPole;
      fs.xfLen = Math.max(1, TYPE_XFADE * sampleRate);
      fs.xfLeft = fs.xfLen;
    }
    fs.ftype = ftype;
    let fc = pv[f[P_FLT_CUT]] * Math.pow(2, mCut * MOD_LOG_D);
    if (!Number.isFinite(fc)) fc = 20;
    fc = Math.min(sampleRate * 0.45, Math.max(20, fc));
    if (fs.cutSm <= 0) fs.cutSm = fc;
    fs.cutSm += (fc - fs.cutSm) * smoothCoef(count, CUT_TAU * sampleRate);
    const cut = fs.cutSm;
    const res = Math.min(0.999, Math.max(0, pv[f[P_FLT_RES]] + mRes));

    fs.twoPole = ftype === 1;
    const g = Math.tan((Math.PI * cut) / sampleRate);
    const k = 2 - 1.93 * res;
    fs.k1 = k;
    fs.a1 = 1 / (1 + g * (g + k));
    fs.a2 = g * fs.a1;
    fs.a3 = g * fs.a2;
  }

  runFilter(fs, inL, inR, outL, outR, drive, off, n) {
    if (drive > 0.005) {
      const dg = 1 + drive * 7;
      const dcomp = 1 / Math.pow(dg, 0.55);
      const kF = dcomp / dg;
      let xpL = fs.satXL, xpR = fs.satXR;
      let FpL = kF * lcosh(dg * xpL), FpR = kF * lcosh(dg * xpR);
      for (let i = off; i < off + n; i++) {
        const aL = inL[i], aR = inR[i];
        const dxL = aL - xpL;
        const FL = kF * lcosh(dg * aL);
        outL[i] = dxL > 1e-5 || dxL < -1e-5 ? (FL - FpL) / dxL : dcomp * Math.tanh(dg * 0.5 * (aL + xpL));
        xpL = aL; FpL = FL;
        const dxR = aR - xpR;
        const FR = kF * lcosh(dg * aR);
        outR[i] = dxR > 1e-5 || dxR < -1e-5 ? (FR - FpR) / dxR : dcomp * Math.tanh(dg * 0.5 * (aR + xpR));
        xpR = aR; FpR = FR;
      }
      fs.satXL = xpL; fs.satXR = xpR;
    } else {
      for (let i = off; i < off + n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
      if (n > 0) { fs.satXL = inL[off + n - 1]; fs.satXR = inR[off + n - 1]; }
    }

    // While a type switch is fading, the outgoing type runs on `svfOld` over a
    // copy of the same input and the two are mixed sample by sample.
    const fading = fs.xfLeft > 0;
    if (fading) {
      this.xL.set(outL.subarray(off, off + n), off);
      this.xR.set(outR.subarray(off, off + n), off);
    }

    this.svfStage(fs, fs.svf, fs.ftype, fs.twoPole, outL, outR, off, n);

    if (fading) {
      this.svfStage(fs, fs.svfOld, fs.ftypeOld, fs.twoPoleOld, this.xL, this.xR, off, n);
      const step = 1 / fs.xfLen;
      let g = fs.xfLeft * step; // weight of the outgoing type, 1 → 0
      for (let i = off; i < off + n; i++) {
        if (g <= 0) { g = 0; break; }
        outL[i] += g * (this.xL[i] - outL[i]);
        outR[i] += g * (this.xR[i] - outR[i]);
        g -= step;
      }
      fs.xfLeft = Math.max(0, fs.xfLeft - n);
    }
  }

  // One SVF pass (plus the second pole for LP24) over `bufL/bufR`, using the
  // state array `F` and the coefficients currently on `fs`.
  svfStage(fs, F, ftype, twoPole, bufL, bufR, off, n) {
    const a1 = fs.a1, a2 = fs.a2, a3 = fs.a3, k1 = fs.k1;
    for (let ch = 0; ch < 2; ch++) {
      const buf = ch === 0 ? bufL : bufR;
      const o1 = ch * 2;
      let ic1 = F[o1], ic2 = F[o1 + 1];
      for (let i = off; i < off + n; i++) {
        const x = buf[i];
        const v3 = x - ic2;
        const v1 = a1 * ic1 + a2 * v3;
        const v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        switch (ftype) {
          case 0: case 1: buf[i] = v2; break;
          case 2: buf[i] = k1 * v1; break;
          case 3: buf[i] = x - k1 * v1 - v2; break;
          default: buf[i] = x - k1 * v1; break;
        }
      }
      F[o1] = ic1; F[o1 + 1] = ic2;
    }
    if (twoPole) {
      for (let ch = 0; ch < 2; ch++) {
        const buf = ch === 0 ? bufL : bufR;
        const o1 = 4 + ch * 2;
        let ic1 = F[o1], ic2 = F[o1 + 1];
        for (let i = off; i < off + n; i++) {
          const x = buf[i];
          const v3 = x - ic2;
          const v1 = a1 * ic1 + a2 * v3;
          const v2 = ic2 + a2 * ic1 + a3 * v3;
          ic1 = 2 * v1 - ic1;
          ic2 = 2 * v2 - ic2;
          buf[i] = v2;
        }
        F[o1] = ic1; F[o1 + 1] = ic2;
      }
    }
  }

  renderPad(v, padI, L, R, off, n) {
    const pv = this.pv, f = this.padIds[padI];
    const m0 = this.m0, m1 = this.m1;
    this.padMod(f, v, v.t, m0);
    this.padMod(f, v, v.t + n, m1);
    const tmpL = this.tmpL, tmpR = this.tmpR;
    tmpL.fill(0, 0, n); tmpR.fill(0, 0, n);
    const invN = n > 0 ? 1 / n : 0;

    const pDec = Math.max(0.002, pv[f[P_PENV_DEC]]);
    const pAmt = pv[f[P_PENV_AMT]];
    const peK = -4.5 / (pDec * sampleRate);
    const tuneA = pv[f[OSC_A + A_TUNE]], fineA = pv[f[OSC_A + A_FINE]];
    const tuneB = pv[f[OSC_B + A_TUNE]], fineB = pv[f[OSC_B + A_FINE]];
    const posA = pv[f[OSC_A + A_POS]], posB = pv[f[OSC_B + A_POS]];
    for (let at = 0; at < n; at += 16) {
      const count = Math.min(16, n - at);
      const u0 = at * invN, u1 = (at + count) * invN;
      const pe0 = pAmt * Math.exp(peK * (v.t + at));
      const pe1 = pAmt * Math.exp(peK * (v.t + at + count));
      const pitch0 = this.mlerp(M_PITCH, u0), pitch1 = this.mlerp(M_PITCH, u1);
      const aP0 = BASE_NOTE + tuneA + (fineA + this.mlerp(M_FINEA, u0)) / 100 + pe0 + pitch0;
      const aP1 = BASE_NOTE + tuneA + (fineA + this.mlerp(M_FINEA, u1)) / 100 + pe1 + pitch1;
      const aOn = this.setupOsc(v.oA, f, OSC_A, aP0, aP1,
        posA + this.mlerp(M_POSA, u0), posA + this.mlerp(M_POSA, u1), count);
      if (aOn) this.renderOsc(v.oA, tmpL, tmpR, at, count);
      const bP0 = tuneB + (fineB + this.mlerp(M_FINEB, u0)) / 100 + pe0 + pitch0;
      const bP1 = tuneB + (fineB + this.mlerp(M_FINEB, u1)) / 100 + pe1 + pitch1;
      this.renderSample(v.sample, f, OSC_B, bP0, bP1, posB + this.mlerp(M_POSB, u0),
        tmpL, tmpR, at, count);
    }

    const noiseLevel = Math.min(1, Math.max(0, pv[f[P_NOISE_LEVEL]] + m0[M_NOISE]));
    const noiseGain = noiseLevel * noiseLevel * 0.35;
    if (noiseGain > 1e-6) {
      const color = Math.min(1, Math.max(-1, pv[f[P_NOISE_COLOR]]));
      const a = mapPole(0.02 + (color + 1) * 0.49, sampleRate);
      const rng = this.rng;
      let y = v.noiseY;
      for (let i = 0; i < n; i++) {
        y += (rng.uni() - y) * a;
        const s = y * noiseGain;
        tmpL[i] += s; tmpR[i] += s;
      }
      v.noiseY = y;
    }

    // Sine ring modulator. A fixed-Hz carrier deliberately breaks the
    // oscillator's harmonic series into inharmonic sidebands—the useful bit
    // for bells, struck metal and synthetic cymbals. MIX=0 is a true bypass.
    const ringMix = Math.min(1, Math.max(0, pv[f[P_RING_MIX]]));
    if (ringMix > 1e-6) {
      const ringFreq = Math.min(sampleRate * 0.45, Math.max(20, pv[f[P_RING_FREQ]]));
      const ringInc = ringFreq / sampleRate;
      let phase = v.ringPhase;
      for (let i = 0; i < n; i++) {
        const carrier = Math.sin(phase * Math.PI * 2) * Math.SQRT2;
        const gain = 1 + ringMix * (carrier - 1);
        tmpL[i] *= gain;
        tmpR[i] *= gain;
        phase += ringInc;
        if (phase >= 1) phase -= 1;
      }
      v.ringPhase = phase;
    }

    let srcL = tmpL, srcR = tmpR;
    if (pv[f[P_FLT_ON]]) {
      // 32-sample coefficient chunks on a smoothed cutoff, like JUCE: a
      // cutoff sweep no longer steps once per block.
      const drive = pv[f[P_FLT_DRIVE]];
      for (let at = 0; at < n; at += 32) {
        const count = Math.min(32, n - at);
        const u = (at + count) * invN;
        this.setupFilter(v.f, f, this.mlerp(M_CUT, u), this.mlerp(M_RES, u), count);
        this.runFilter(v.f, tmpL, tmpR, this.fL, this.fR, drive, at, count);
      }
      srcL = this.fL; srcR = this.fR;
    }

    const velGain = 1 - pv[f[P_V2L]] * (1 - v.vel);
    const lv0 = Math.min(1, Math.max(0, pv[f[P_LVL]] + m0[M_LEVEL]));
    const lv1 = Math.min(1, Math.max(0, pv[f[P_LVL]] + m1[M_LEVEL]));
    const lg0 = lv0 * lv0, dLg = (lv1 * lv1 - lg0) * invN;
    const pan = Math.min(1, Math.max(-1, pv[f[P_PAN]]));
    const panA = ((pan + 1) * Math.PI) / 4;
    const panL = Math.cos(panA), panR = Math.sin(panA);
    const dcR = this.dcR;
    const chokeC = smoothCoef(1, CHOKE_TAU * sampleRate);

    // Amp envelope constants hoisted out of the sample loop; the exponential
    // segment advances by a recursive multiply instead of a Math.exp per
    // sample (review D7).
    const att = Math.max(1, pv[f[P_AENV_ATT]] * sampleRate);
    const hold = pv[f[P_AENV_HOLD]] * sampleRate;
    const dec = Math.max(1, pv[f[P_AENV_DEC]] * sampleRate);
    const curve = pv[f[P_AENV_CURVE]];
    const decStart = att + hold;
    const decEnd = decStart + dec;
    const invDec = 1 / dec;
    const rExp = Math.exp(-4.5 * invDec);
    let ex = -1; // running e^(-4.5·td/dec), primed on entry to the decay

    for (let i = 0; i < n; i++) {
      const sl = srcL[i], sr = srcR[i];
      const yL = sl - v.dcxL + dcR * v.dcyL;
      const yR = sr - v.dcxR + dcR * v.dcyR;
      v.dcxL = sl; v.dcyL = yL;
      v.dcxR = sr; v.dcyR = yR;

      if (v.choking) {
        v.ampLevel *= 1 - chokeC;
        if (v.ampLevel < 1e-4) {
          v.kill();
          break;
        }
      } else {
        const t = v.t + i;
        if (t < att) {
          v.ampLevel = t / att;
        } else if (t < decStart) {
          v.ampLevel = 1;
        } else if (t < decEnd) {
          const td = t - decStart;
          if (ex < 0) ex = Math.exp(-4.5 * td * invDec);
          else ex *= rExp;
          // Normalised so the decay reaches 0 instead of stepping down from
          // 0.011·curve at the boundary (review D3).
          const lin = 1 - td * invDec;
          v.ampLevel = lin + ((ex - E45) * INV_E45 - lin) * curve;
        } else {
          v.ampLevel = 0;
        }
      }
      const amp = v.ampLevel * velGain * (lg0 + dLg * i);
      L[off + i] += yL * amp * panL;
      R[off + i] += yR * amp * panR;
    }

    v.t += n;
    if (v.active && !v.choking && v.t >= decEnd && v.ampLevel < 1e-4) v.kill();
  }

  process(_inputs, outputs) {
    // One stereo worklet output per OUTPUT BUS. Each pad renders into its own
    // buffer, runs its own FX chain, then sums into the bus its `out` param
    // selects; the main thread only meters what comes back. Missing outputs are
    // tolerated by the lightweight test harness and older hosts.
    for (const out of outputs) {
      for (const channel of out) channel.fill(0);
    }
    const n = outputs[0]?.[0]?.length || 128;
    this.ensureCap(n);

    const padL = this.padL, padR = this.padR;
    for (let i = 0; i < NPADS; i++) { padL[i].fill(0, 0, n); padR[i].fill(0, 0, n); }

    if (this.hosted) this.hostTick(n);
    const standalone = this.playing && !this.hosted;

    let pos = 0;
    while (pos < n) {
      let run = n - pos;
      if (standalone) {
        if (this.samplesToNext <= 0) {
          this.fireStep();
        }
        run = Math.min(run, Math.ceil(this.samplesToNext));
      }
      for (let i = 0; i < NPADS; i++) {
        const v = this.voices[i];
        if (v.active) this.renderPad(v, i, padL[i], padR[i], pos, run);
        const tail = this.tails[i];
        if (tail.active) this.renderPad(tail, i, padL[i], padR[i], pos, run);
      }
      if (standalone) this.samplesToNext -= run;
      pos += run;
    }

    this.renderFx(outputs, n);

    if (this.reverbMetering && this.reverbSamples >= sampleRate / 30) {
      const e = this.reverbEnergy, rms = i => Math.max(-90, 10 * Math.log10(Math.max(1e-9, e[i] / this.reverbSamples)));
      const product = Math.sqrt(e[0] * e[1]);
      this.port.postMessage({ t: 'reverb', pad: this.meterPad, left: rms(0), right: rms(1), correlation: product > this.reverbSamples * 1e-9 ? Math.max(-1, Math.min(1, e[2] / product)) : 0, bus: this.meterBus, shared: true });
      e.fill(0); this.reverbSamples = 0;
    }
    if (this.echoMetering && this.echoSamples >= sampleRate / 30) {
      const e = this.echoEnergy, rms = i => Math.max(-90, 10 * Math.log10(Math.max(1e-9, e[i] / this.echoSamples)));
      const fx = this.padFx[this.meterPad];
      this.port.postMessage({ t: 'echo', pad: this.meterPad, input: rms(0), left: rms(1), right: rms(2), time: fx.dlTime.cur, driftL: 0, driftR: 0 });
      e.fill(0); this.echoSamples = 0;
    }
    if (this.metering && this.meterSamples >= sampleRate / 30) {
      const e = this.meterEnergy, fx = this.padFx[this.meterPad], ott = fx.ott, comp = fx.comp;
      const db = value => Math.max(-90, Math.min(60, 20 * Math.log10(Math.max(1e-9, value))));
      const rms = i => db(Math.sqrt(e[i] / this.meterSamples));
      const active = ott.depthTarget > 0;
      this.port.postMessage({ t: 'dynamics', pad: this.meterPad,
        ott: { input: rms(0), output: rms(1), levels: Array.from(ott.env, v => active ? db(v) : -90), gains: Array.from(ott.gain, (v, i) => active && ott.env[i] > 1e-7 ? db(v) : 0), makeup: active ? db(ott.autoGain.gain) : 0 },
        comp: { input: rms(2), output: rms(3), reduction: this.meterReduction, makeup: comp.wetTarget ? db(comp.autoGain.gain) : 0 },
      });
      e.fill(0); this.meterSamples = 0; this.meterReduction = 0;
    }

    this.vizCount += n;
    if (this.vizCount >= 2048) {
      this.vizCount = 0;
      const v = this.voices[this.sel];
      this.port.postMessage({
        t: 'viz',
        a: v.active ? v.oA.posSm : -1,
        b: v.active && this.samples[v.sample.index] && v.sample.pos >= 0
          ? Math.max(0, Math.min(1,
            v.sample.pos / Math.max(1, this.samples[v.sample.index].data.length - 1)))
          : -1,
        env: v.active ? v.ampLevel : 0,
      });
    }
    return true;
  }

  // Pad FX chains -> bus sums (+ the shared reverb send) -> bus output stage.
  renderFx(outputs, n) {
    if (this.fxDirty) {
      this.fxDirty = false;
      for (let i = 0; i < NPADS; i++) this.padFx[i].setParams(this.pv, this.padIds[i]);
      for (let b = 0; b < BUSES; b++) this.groupFx[b].setParams(this.pv, this.groupFxIds);
      const vol = this.pv[G_VOL];
      for (const b of this.busOut) b.gain.target = vol * vol * 1.6;
    }
    const busL = this.busL, busR = this.busR, vinL = this.verbInL, vinR = this.verbInR;
    for (let b = 0; b < BUSES; b++) {
      busL[b].fill(0, 0, n); busR[b].fill(0, 0, n);
      vinL[b].fill(0, 0, n); vinR[b].fill(0, 0, n);
    }
    this.sizeAcc.fill(0); this.sizeW.fill(0);
    // Bus identity comes from the selected pad's routed parameters, even when
    // that pad is silent/gated. This keeps the visualizer on AUX tails during
    // a quiet selected hit instead of falling back to MAIN.
    const selectedBus = this.padFx[this.meterPad].bus;
    if (this.meterBus !== selectedBus) {
      this.reverbEnergy.fill(0); this.reverbSamples = 0;
    }
    this.meterBus = selectedBus;
    for (let b = 0; b < BUSES; b++) this.verbs[b].meterEnabled = this.reverbMetering && b === selectedBus;

    for (let i = 0; i < NPADS; i++) {
      const fx = this.padFx[i];
      fx.meterSelected = this.metering || this.echoMetering ? i === this.meterPad : false;
      const live = this.voices[i].active || this.tails[i].active;
      if (!fx.process(this.padL[i], this.padR[i], n, live)) continue;
      if (i === this.meterPad) {
        this.meterBus = fx.bus;
        if (this.metering) {
          this.meterEnergy[0] += fx.meterInput; this.meterEnergy[1] += fx.meterOtt;
          this.meterEnergy[2] += fx.meterCompIn; this.meterEnergy[3] += fx.meterCompOut;
          this.meterReduction = Math.max(this.meterReduction, Math.max(0, -20 * Math.log10(Math.max(1e-9, fx.comp.gain))));
          fx.meterInput = fx.meterOtt = fx.meterCompIn = fx.meterCompOut = 0;
        }
        if (this.echoMetering) {
          this.echoEnergy[0] += fx.meterEchoIn; this.echoEnergy[1] += fx.meterEchoL; this.echoEnergy[2] += fx.meterEchoR;
          fx.meterEchoIn = fx.meterEchoL = fx.meterEchoR = 0;
        }
      }
      const b = fx.bus;
      const l = this.padL[i], r = this.padR[i];
      const bl = busL[b], br = busR[b], vl = vinL[b], vr = vinR[b];
      if (fx.verbWet.settled() && fx.verbDry.settled()) {
        const w = fx.verbWet.cur, d = fx.verbDry.cur;
        if (w === 0) {
          for (let k = 0; k < n; k++) { bl[k] += l[k] * d; br[k] += r[k] * d; }
        } else {
          for (let k = 0; k < n; k++) {
            bl[k] += l[k] * d; br[k] += r[k] * d;
            vl[k] += l[k] * w; vr[k] += r[k] * w;
          }
        }
      } else {
        for (let k = 0; k < n; k++) {
          const w = fx.verbWet.next(), d = fx.verbDry.next();
          bl[k] += l[k] * d; br[k] += r[k] * d;
          vl[k] += l[k] * w; vr[k] += r[k] * w;
        }
      }
      if (fx.verbOn && fx.verbWet.target > 0) {
        // One Freeverb per bus, shared by the pads sending into it. Its SIZE is
        // the send-weighted mean of theirs — continuous, and smoothed into the
        // running tail rather than swapped under it.
        this.sizeAcc[b] += fx.verbWet.target * fx.verbSize;
        this.sizeW[b] += fx.verbWet.target;
      }
    }

    for (let b = 0; b < BUSES; b++) {
      const verb = this.verbs[b];
      if (this.sizeW[b] > 0) verb.size.target = this.sizeAcc[b] / this.sizeW[b];
      verb.update(n);
      this.verbInputGuards[b].process(vinL[b], vinR[b], n);
      verb.meterL = verb.meterR = verb.meterLR = 0;
      verb.process(vinL[b], vinR[b], busL[b], busR[b], n);
      if (this.reverbMetering && b === this.meterBus) {
        this.reverbEnergy[0] += verb.meterL; this.reverbEnergy[1] += verb.meterR; this.reverbEnergy[2] += verb.meterLR;
      }
      // The group strip is a true post-mix insert. PadFx keeps reverb as a
      // send, so give this second layer its own final group reverb network.
      const group = this.groupFx[b];
      const bl = busL[b], br = busR[b];
      group.process(bl, br, n, true);
      const gvinL = vinL[b], gvinR = vinR[b];
      gvinL.fill(0, 0, n); gvinR.fill(0, 0, n);
      if (group.verbWet.settled() && group.verbDry.settled()) {
        const w = group.verbWet.cur, d = group.verbDry.cur;
        for (let k = 0; k < n; k++) {
          const l = bl[k], r = br[k];
          bl[k] = l * d; br[k] = r * d; gvinL[k] = l * w; gvinR[k] = r * w;
        }
      } else {
        for (let k = 0; k < n; k++) {
          const w = group.verbWet.next(), d = group.verbDry.next(), l = bl[k], r = br[k];
          bl[k] = l * d; br[k] = r * d; gvinL[k] = l * w; gvinR[k] = r * w;
        }
      }
      const groupVerb = this.groupVerbs[b];
      groupVerb.size.target = group.verbSize;
      groupVerb.update(n);
      this.groupVerbInputGuards[b].process(gvinL, gvinR, n);
      groupVerb.process(gvinL, gvinR, bl, br, n);
      let pk = 0;
      for (let k = 0; k < n; k++) {
        const a = bl[k] < 0 ? -bl[k] : bl[k], c = br[k] < 0 ? -br[k] : br[k];
        if (a > pk) pk = a;
        if (c > pk) pk = c;
      }
      if (!this.busOut[b].process(bl, br, n, pk)) continue;
      const out = outputs[b];
      if (!out) continue;
      out[0].set(bl.subarray(0, n));
      if (out.length > 1) out[1].set(br.subarray(0, n));
    }
    if (this.metering) this.meterSamples += n;
    if (this.echoMetering) this.echoSamples += n;
    if (this.reverbMetering) this.reverbSamples += n;
  }

  fireStep() {
    const bpm = Math.max(60, Math.min(200, this.pv[G_BPM] || 126));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = this.pv[G_SWING] || 0;
    const next = this.step + 1;
    if (next >= STEPS) {
      this.step = -1;
      this.chainPos = (this.chainPos + 1) % this.chain.length;
    }
    const s = (this.step + 1) % STEPS;
    const pat = this.chain[this.chainPos] | 0;
    const hits = [];
    for (let i = 0; i < NPADS; i++) {
      const val = this.pats[pat * NPADS * STEPS + i * STEPS + s];
      if (val) {
        this.trigger(i, val === 2 ? ACCENT_VEL : PLAIN_VEL);
        hits.push(i);
      }
    }
    this.step = s;
    const offNow = s % 2 === 1 ? swing * SWING_MAX * dur : 0;
    const sNext = (s + 1) % STEPS;
    const offNext = sNext % 2 === 1 ? swing * SWING_MAX * dur : 0;
    this.samplesToNext = dur - offNow + offNext;
    this.port.postMessage({ t: 'step', s, pat, hits });
  }
}

registerProcessor('fable-dr', DrumProcessor);
