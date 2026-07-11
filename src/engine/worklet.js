// FableSynth DSP core — runs in the AudioWorklet thread. Self-contained (no imports).
// Protocol (port messages in):
//   {t:'init', params:{id:value,...}}
//   {t:'tables', list:[{frames,mips,size,buf:ArrayBuffer}]}
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

class Env {
  constructor() {
    this.state = 0; this.level = 0; this.s = 0.8;
    this.ca = 0.01; this.cd = 0.001; this.cr = 0.001;
    this._key = '';
  }
  // decay/release use tau = t/4.5 so the audible tail roughly matches the label
  set(a, d, s, r) {
    this.s = s;
    const key = a + '|' + d + '|' + r;
    if (key !== this._key) {
      this._key = key;
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
        // steal fade: ~2 ms to silence, then the voice is free for its pending note
        this.level -= this.level * 0.12;
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
  reset() { this.phase = 0; this.hold = Math.random() * 2 - 1; this.elapsed = 0; }
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
  advance(rate, n) {
    this.elapsed += n;
    // NaN guard: a non-finite rate (e.g. params not yet initialised when the
    // free-running global LFO advances) would latch phase to a sticky NaN.
    const d = (rate * n) / sampleRate;
    if (Number.isFinite(d)) this.phase += d;
    if (this.phase >= 1) { this.phase -= Math.floor(this.phase); this.hold = Math.random() * 2 - 1; }
  }
}

// Per-oscillator runtime state inside a voice
const DC_R = 0.9998; // ~3.5 Hz highpass — removes DC without touching bass

// Numerically stable ln(cosh(z)) — the antiderivative of tanh, used by the
// anti-aliased (ADAA) saturator below. cosh overflows for |z| > ~710, so we
// fold large arguments to |z| - ln2 + log1p(e^-2|z|). Exact for small z too
// (ln cosh 0 = 0), so it is safe across the whole drive range.
function lcosh(z) {
  const a = Math.abs(z);
  return a + Math.log1p(Math.exp(-2 * a)) - Math.LN2;
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
    a1: 0, a2: 0, a3: 0, k1: 0, // SVF coefs
    combLen: 1, combFb: 0,      // comb coefs
    fc: new Float64Array(9),    // formant biquad coefs: 3 bands x (b0, a1, a2)
    famp: new Float64Array(3),
  };
}

function resetFilterState(fs) {
  fs.svf.fill(0); fs.fmt.fill(0);
  fs.combL.fill(0); fs.combR.fill(0); fs.combW = 0;
  fs.cutSm = 0; fs.satXL = 0; fs.satXR = 0;
}

function makeOscState() {
  return {
    phases: new Float64Array(MAXUNI),
    incs: new Float64Array(MAXUNI),
    gl: new Float32Array(MAXUNI),
    gr: new Float32Array(MAXUNI),
    uni: 1, off0: 0, off1: 0, off0b: 0, off1b: 0, mipBlend: 0,
    ft: 0, gain: 0, mask: 0, size: 0, data: null, posSm: -1,
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
    this.subPhase = 0;
    this.pb = [0, 0, 0, 0, 0, 0, 0]; // pink noise filter state
    this.f1 = makeFilterState(); this.f2 = makeFilterState();
    this.dcxL = 0; this.dcxR = 0; this.dcyL = 0; this.dcyR = 0;
  }
  get active() { return this.ampEnv.state !== 0; }

  noteOn(note, vel, startPitch, age, phaseRandA, phaseRandB) {
    this.note = note; this.vel = vel; this.gate = true; this.age = age;
    this.pitch = startPitch;
    this.velGain = 0.25 + 0.75 * vel * vel;
    this.ampEnv.trigger(); this.modEnv.trigger();
    this.lfo1.reset(); this.lfo2.reset();
    for (let i = 0; i < MAXUNI; i++) {
      // Start phase is in SAMPLES (all tables are 2048 wide): scale the random
      // draw to a full cycle so unison voices (and osc A vs B) decorrelate.
      this.oA.phases[i] = phaseRandA ? Math.random() * 2048 : 0;
      this.oB.phases[i] = phaseRandB ? Math.random() * 2048 : 0;
    }
    this.oA.posSm = -1; this.oB.posSm = -1;
    this.subPhase = 0;
    resetFilterState(this.f1); resetFilterState(this.f2);
    this.dcxL = this.dcxR = this.dcyL = this.dcyR = 0;
  }
  noteOff() { this.gate = false; this.ampEnv.release(); this.modEnv.release(); }
  kill() { this.gate = false; this.pending = null; this.ampEnv.kill(); this.modEnv.kill(); }
}

class FableProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.p = Object.create(null);
    this.tables = [];
    this.voices = [];
    for (let i = 0; i < NVOICES; i++) this.voices.push(new Voice());
    this.bend = 0;
    this.bpm = 120;
    // Virtual transport: beats (quarter notes) since audio start. Standalone has
    // no host transport, so synced LFOs phase-lock to this clock (downbeat = t0).
    // Accumulated (not samples*bpm) so a tempo change doesn't rescale the past.
    this.transportBeats = 0;
    this.gLfo1 = new LFO(); this.gLfo1.reset();
    this.gLfo2 = new LFO(); this.gLfo2.reset();
    this.lastPitch = 60;
    this.clock = 0;
    // ---- note sequencer state ----
    this.seqPlaying = false;
    this.seqStep = -1;
    this.seqPats = new Uint8Array(SEQ_NPATTERNS * SEQ_STEPS * SEQ_STRIDE);
    for (let i = 2; i < this.seqPats.length; i += SEQ_STRIDE) this.seqPats[i] = 1; // oct byte: 1 = oct 0
    this.seqChain = [0];
    this.seqChainPos = 0;
    this.seqToNext = 0; // samples until the next step fires
    this.seqToGateOff = -1; // samples until the current step's gate closes (-1 = none/tied)
    this.seqNote = -1; // midi note the sequencer is currently sounding (-1 = none)
    // ---- hosted clip transport (SQ-4) ----
    this.hosted = false;
    this.hostBpm = 120;
    this.hostSwing = 0;
    this.clip = null; // { data: Uint8Array, bars }
    this.clipPend = null; // { data, bars, at } — waiting for its atFrame
    this.clipStopAt = -1;
    this.clipStep = -1; // absolute step within the clip (0 .. bars*16-1)
    this.clipToNext = 0;
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
    // _modAccum: route sum keyed by targeted paramId. _pm: the modulated snapshot,
    // holding only overridden paramIds; read via pmv(pre+'.field') with `p` fallback.
    this._modAccum = Object.create(null);
    this._pm = Object.create(null);
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  onMsg(d) {
    switch (d.t) {
      // Non-finite param values are dropped at this single choke point: a NaN
      // that reached `p` would latch into phases / env levels and stick there.
      case 'init':
        for (const k in d.params) { const v = d.params[k]; if (Number.isFinite(v)) this.p[k] = v; }
        if (Number.isFinite(this.p['seq.bpm'])) this.bpm = Math.min(1000, Math.max(1, this.p['seq.bpm']));
        break;
      case 'p':
        if (Number.isFinite(d.v)) {
          this.p[d.k] = d.v;
          // The web build has no host transport: while the sequencer is the
          // tempo authority, synced LFOs follow it.
          if (d.k === 'seq.bpm') this.bpm = Math.min(1000, Math.max(1, d.v));
        }
        break;
      case 'tables':
        this.tables = d.list.map((x) => ({
          frames: x.frames, mips: x.mips, size: x.size, mask: x.size - 1,
          data: new Float32Array(x.buf),
        }));
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
        this.seqPlaying = true;
        this.seqStep = -1;
        this.seqChainPos = 0;
        this.seqToNext = 0;
        this.seqToGateOff = -1;
        this.seqNote = -1;
        break;
      case 'stop':
        this.seqPlaying = false;
        this.seqStep = -1;
        this.seqToGateOff = -1;
        this.seqGateOff();
        break;
      case 'panic':
        for (const v of this.voices) v.kill();
        this.seqNote = -1;
        this.seqToGateOff = -1;
        this.clip = null;
        this.clipPend = null;
        this.clipStopAt = -1;
        this.clipStep = -1;
        break;
      case 'host': this.hosted = !!d.on; break;
      case 'tempo':
        if (Number.isFinite(d.bpm)) { this.hostBpm = d.bpm; this.bpm = Math.min(1000, Math.max(1, d.bpm)); }
        if (Number.isFinite(d.swing)) this.hostSwing = d.swing;
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
          this.clip = { data, bars };
          if (this.clipStep >= 0) this.clipStep %= bars * SEQ_STEPS;
        }
        break;
      }
    }
  }

  // ---------- note sequencer ----------
  seqRead(pat, s) {
    const o = (pat * SEQ_STEPS + s) * SEQ_STRIDE;
    const flags = this.seqPats[o];
    return {
      on: (flags & 1) !== 0,
      acc: (flags & 2) !== 0,
      tie: (flags & 4) !== 0,
      semi: Math.min(11, this.seqPats[o + 1]) + 12 * (Math.min(2, this.seqPats[o + 2]) - 1),
    };
  }

  seqGateOff() {
    if (this.seqNote >= 0) {
      this.noteOff(this.seqNote);
      this.seqNote = -1;
    }
  }

  // Legato retune of the sounding sequencer voice: no envelope retrigger; the
  // renderVoice glide slew takes v.pitch to the new note (instant at GLIDE 0).
  seqTie(n, vel) {
    let voice = null;
    for (const v of this.voices) {
      if (v.gate && v.note === this.seqNote) { voice = v; break; }
    }
    if (!voice) { this.noteOn(n, vel); return; } // voice got stolen — retrigger
    voice.note = n;
    this.lastPitch = n;
  }

  // ---------- hosted clip transport ----------
  // A clip is bars*16 steps of the same 3-byte layout, bar-major. Pending
  // commands execute in the render quantum containing their atFrame — every
  // device on the shared context resolves the same frame to the same block.
  clipRead(abs) {
    const o = abs * SEQ_STRIDE;
    const flags = this.clip.data[o];
    return {
      on: (flags & 1) !== 0,
      acc: (flags & 2) !== 0,
      tie: (flags & 4) !== 0,
      semi: Math.min(11, this.clip.data[o + 1]) + 12 * (Math.min(2, this.clip.data[o + 2]) - 1),
    };
  }

  hostTick(n) {
    const end = currentFrame + n;
    if (this.clipStopAt >= 0 && this.clipStopAt < end) {
      this.clipStopAt = -1;
      if (this.clip) {
        this.clip = null;
        this.clipStep = -1;
        this.seqGateOff();
      }
      // ack even when nothing was playing — the stop may have targeted a
      // pending-only launch and the conductor clears its STOP marker on this
      this.port.postMessage({ t: 'clipstop', frame: currentFrame });
    }
    if (this.clipPend && this.clipPend.at < end) {
      this.clip = this.clipPend;
      this.clipPend = null;
      this.clipStep = -1;
      this.clipToNext = 0;
      this.seqGateOff(); // the old clip's tail note ends where the new clip starts
      this.port.postMessage({ t: 'clipstart', frame: currentFrame });
    }
    if (this.clip) {
      if (this.clipToNext <= 0) this.clipFire();
      this.clipToNext -= n;
    }
  }

  clipFire() {
    const bpm = Math.max(60, Math.min(200, this.hostBpm || 120));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = Math.min(1, Math.max(0, this.hostSwing || 0));
    const total = this.clip.bars * SEQ_STEPS;
    const abs = (this.clipStep + 1) % total;
    const s = abs % SEQ_STEPS;
    const st = this.clipRead(abs);

    if (st.on) {
      const root = (this.p['seq.root'] | 0) || 48;
      const note = root + st.semi;
      const vel = st.acc ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL;
      if (st.tie && this.seqNote >= 0) this.seqTie(note, vel);
      else { this.seqGateOff(); this.noteOn(note, vel); }
      this.seqNote = note;
      const stN = this.clipRead((abs + 1) % total);
      const gate = Math.min(0.98, Math.max(0.1, this.p['seq.gate'] || 0.55));
      this.seqToGateOff = stN.on && stN.tie ? -1 : gate * dur;
    }

    this.clipStep = abs;
    const offNow = s % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    const sNext = (s + 1) % SEQ_STEPS;
    const offNext = sNext % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    this.clipToNext = dur - offNow + offNext;
    this.port.postMessage({ t: 'pos', step: s, bar: (abs / SEQ_STEPS) | 0 });
  }

  seqFire() {
    const bpm = Math.max(60, Math.min(200, this.p['seq.bpm'] || 120));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = Math.min(1, Math.max(0, this.p['seq.swing'] || 0));
    if (this.seqStep + 1 >= SEQ_STEPS) {
      this.seqStep = -1;
      this.seqChainPos = (this.seqChainPos + 1) % this.seqChain.length;
    }
    const s = this.seqStep + 1;
    const pat = this.seqChain[this.seqChainPos] | 0;
    const st = this.seqRead(pat, s);

    if (st.on) {
      const root = (this.p['seq.root'] | 0) || 48;
      const n = root + st.semi;
      const vel = st.acc ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL;
      if (st.tie && this.seqNote >= 0) this.seqTie(n, vel);
      else { this.seqGateOff(); this.noteOn(n, vel); }
      this.seqNote = n;
      // hold through the step when the NEXT step ties in
      const sN = (s + 1) % SEQ_STEPS;
      const patN = sN === 0 ? this.seqChain[(this.seqChainPos + 1) % this.seqChain.length] | 0 : pat;
      const stN = this.seqRead(patN, sN);
      const gate = Math.min(0.98, Math.max(0.1, this.p['seq.gate'] || 0.55));
      this.seqToGateOff = stN.on && stN.tie ? -1 : gate * dur;
    }

    this.seqStep = s;
    const offNow = s % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    const sNext = (s + 1) % SEQ_STEPS;
    const offNext = sNext % 2 === 1 ? swing * SEQ_SWING_MAX * dur : 0;
    this.seqToNext = dur - offNow + offNext;
    this.port.postMessage({ t: 'step', s, pat });
  }

  noteOn(n, vel) {
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
    const glide = this.p['master.glide'] || 0;
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
      voice.noteOn(n, vel, start, this.clock++, 1, 1);
    }
  }

  noteOff(n) {
    for (const v of this.voices) {
      // A note released before its steal fade finished must not start at all.
      if (v.pending && v.pending.n === n) v.pending = null;
      if (v.gate && v.note === n) v.noteOff();
    }
  }

  // Configure one oscillator's per-block render state. Returns true if audible.
  // Reads the modulated snapshot `pm` for the per-param dests (pos/level/pan/detune/
  // spread); pitch and the pan global offset stay as direct additive terms.
  setupOsc(o, pre, voice, pm, mPitch, mPan) {
    const p = this.p;
    if (!p[pre + '.on']) return false;
    const table = this.tables[p[pre + '.table'] | 0];
    if (!table) return false;

    const basePitch = voice.pitch + this.bend + p[pre + '.oct'] * 12 + p[pre + '.semi'] + p[pre + '.fine'] / 100 + mPitch * 12;
    const freq = 440 * Math.pow(2, (basePitch - 69) / 12);
    if (!(freq > 0 && freq <= sampleRate * 0.45)) return false; // inverted: NaN-safe

    const k = pre + '.';
    let level = Math.min(1.2, Math.max(0, pm[k + 'level'] ?? p[k + 'level']));
    level *= level;
    if (!(level >= 1e-5)) return false; // inverted: NaN-safe

    const uni = Math.max(1, Math.min(MAXUNI, p[pre + '.unison'] | 0));
    const det = pm[k + 'detune'] ?? p[k + 'detune'];
    const spr = pm[k + 'spread'] ?? p[k + 'spread'];
    const blend = Math.min(1, Math.max(0, pm[k + 'blend'] ?? p[k + 'blend'])); // clamp matches JUCE
    const basePan = Math.max(-1, Math.min(1, (pm[k + 'pan'] ?? p[k + 'pan']) + mPan));

    // position smoothing (avoids zipper on fast morph modulation)
    let pos = Math.min(1, Math.max(0, pm[k + 'pos'] ?? p[k + 'pos']));
    if (o.posSm < 0) o.posSm = pos;
    o.posSm += (pos - o.posSm) * 0.35;
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

  renderOsc(o, tmpL, tmpR, n) {
    const data = o.data, mask = o.mask, size = o.size, ft = o.ft, g = o.gain;
    const off0 = o.off0, off1 = o.off1;
    const blend = o.mipBlend;
    if (blend < 0.001) {
      // fast path — single mip, no crossfade
      for (let u = 0; u < o.uni; u++) {
        let ph = o.phases[u];
        const inc = o.incs[u];
        const gl = o.gl[u] * g, gr = o.gr[u] * g;
        for (let i = 0; i < n; i++) {
          const idx = ph | 0;
          const frac = ph - idx;
          const i2 = (idx + 1) & mask;
          const s0 = data[off0 + idx] + frac * (data[off0 + i2] - data[off0 + idx]);
          const s1 = data[off1 + idx] + frac * (data[off1 + i2] - data[off1 + idx]);
          const s = s0 + ft * (s1 - s0);
          tmpL[i] += s * gl;
          tmpR[i] += s * gr;
          ph += inc;
          if (ph >= size) ph -= size;
        }
        o.phases[u] = ph;
      }
    } else {
      // crossfade path — blend coarse mip with finer mip near mip boundary
      const off0b = o.off0b, off1b = o.off1b;
      for (let u = 0; u < o.uni; u++) {
        let ph = o.phases[u];
        const inc = o.incs[u];
        const gl = o.gl[u] * g, gr = o.gr[u] * g;
        for (let i = 0; i < n; i++) {
          const idx = ph | 0;
          const frac = ph - idx;
          const i2 = (idx + 1) & mask;
          // coarse mip
          const sc0 = data[off0 + idx] + frac * (data[off0 + i2] - data[off0 + idx]);
          const sc1 = data[off1 + idx] + frac * (data[off1 + i2] - data[off1 + idx]);
          const sc = sc0 + ft * (sc1 - sc0);
          // fine mip (richer, may alias slightly near the boundary)
          const sf0 = data[off0b + idx] + frac * (data[off0b + i2] - data[off0b + idx]);
          const sf1 = data[off1b + idx] + frac * (data[off1b + i2] - data[off1b + idx]);
          const sf = sf0 + ft * (sf1 - sf0);
          const s = sc + blend * (sf - sc);
          tmpL[i] += s * gl;
          tmpR[i] += s * gr;
          ph += inc;
          if (ph >= size) ph -= size;
        }
        o.phases[u] = ph;
      }
    }
  }

  // Compute one filter's block-rate coefficients. CUTOFF is shared across all
  // types: it sets corner frequency (SVF), comb pitch (COMB) or vowel morph
  // position (VOWEL). RES sets resonance / feedback / formant sharpness.
  setupFilter(fs, pre, v, e2, mCut, pm) {
    const p = this.p;
    const k = pre + '.';
    const ftype = p[pre + '.type'] | 0;
    fs.ftype = ftype;

    // The cutoff Log route is kept OUT of pm and passed as mCut here so the whole
    // exponent stays in a single Math.pow — bit-identical to the legacy
    // p[cutoff] × 2^(env·4·e2 + key·(note-60)/12 + x·5). env/key are still read from
    // pm so THEY remain modulatable; the base cutoff is read straight from p.
    let fc = p[k + 'cutoff'] *
      Math.pow(2, (pm[k + 'env'] ?? p[k + 'env']) * 4 * e2 + ((pm[k + 'key'] ?? p[k + 'key']) * (v.note - 60)) / 12 + mCut * MOD_LOG_D);
    if (!Number.isFinite(fc)) fc = 20; // JUCE's std::max(20.0, NaN) also yields 20
    fc = Math.min(sampleRate * 0.45, Math.max(20, fc));
    if (fs.cutSm <= 0) fs.cutSm = fc;
    fs.cutSm += (fc - fs.cutSm) * 0.5;
    const cut = fs.cutSm;
    const res = Math.min(0.999, Math.max(0, pm[k + 'res'] ?? p[k + 'res']));

    if (ftype <= 4) {
      // Cytomic SVF
      fs.twoPole = ftype === 1; // LP24 = two cascaded stages
      const g = Math.tan((Math.PI * cut) / sampleRate);
      const k = 2 - 1.93 * res;
      fs.k1 = k;
      fs.a1 = 1 / (1 + g * (g + k));
      fs.a2 = g * fs.a1;
      fs.a3 = g * fs.a2;
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
      const a1 = fs.a1, a2 = fs.a2, a3 = fs.a3, k1 = fs.k1;
      const F = fs.svf;
      for (let ch = 0; ch < 2; ch++) {
        const buf = ch === 0 ? outL : outR;
        const o1 = ch * 2;
        let ic1 = F[o1], ic2 = F[o1 + 1];
        for (let i = 0; i < n; i++) {
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
          for (let i = 0; i < n; i++) {
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
    } else if (ftype === 5) {
      // resonant comb: y = (1-fb)·x + fb·y[n-len], fractional read for tuning
      const len = fs.combLen, fb = fs.combFb, g0 = 1 - fb;
      const cl = fs.combL, cr = fs.combR;
      let w = fs.combW;
      for (let i = 0; i < n; i++) {
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

  lfoHz(pre) {
    if (this.p[pre + '.sync']) {
      const i = Math.min(LFO_DIV_F.length - 1, Math.max(0, this.p[pre + '.syncrate'] | 0));
      return (this.bpm / 60) * LFO_DIV_F[i];
    }
    return this.p[pre + '.rate'];
  }

  // Free-running global LFO phase, updated once per block. When synced, the
  // phase is derived from the transport position (ppq, in quarter notes) so a
  // synced LFO cycle starts on the downbeat. Unsynced LFOs free-run at their Hz.
  // (Retrig LFOs are per-voice and note-aligned, so they bypass this.)
  updateGlobalLfo(g, pre, ppq, n) {
    if (this.p[pre + '.sync']) {
      const i = Math.min(LFO_DIV_F.length - 1, Math.max(0, this.p[pre + '.syncrate'] | 0));
      let ph = ppq * LFO_DIV_F[i];
      ph -= Math.floor(ph);
      if (ph < g.phase) g.hold = Math.random() * 2 - 1; // grid wrap -> new S&H value
      g.phase = ph;
    } else {
      g.advance(this.p[pre + '.rate'], n);
    }
  }

  renderVoice(v, L, R, n) {
    const p = this.p;

    v.ampEnv.set(p['env1.a'], p['env1.d'], p['env1.s'], p['env1.r']);
    v.modEnv.set(p['env2.a'], p['env2.d'], p['env2.s'], p['env2.r']);

    // glide
    const gl = p['master.glide'] || 0;
    if (gl > 0.001) {
      const c = 1 - Math.exp(-n / (gl * 0.3 * sampleRate + 1));
      v.pitch += (v.note - v.pitch) * c;
    } else v.pitch = v.note;

    // mod sources (block rate)
    const rt1 = !!p['lfo1.retrig'], rt2 = !!p['lfo2.retrig'];
    const l1 = (rt1 ? v.lfo1 : this.gLfo1).valueOff(p['lfo1.shape'], p['lfo1.phase']) * v.lfo1.riseGain(p['lfo1.rise']);
    const l2 = (rt2 ? v.lfo2 : this.gLfo2).valueOff(p['lfo2.shape'], p['lfo2.phase']) * v.lfo2.riseGain(p['lfo2.rise']);
    const e2 = v.modEnv.level;
    const srcs = [0, l1, l2, e2, v.vel, (v.note - 60) / 24];

    // modulation destinations — sum every active slot assigned to each target.
    // The 16 fixed slots are read straight from `this.p` (mat{n}.src/.dst/.amt).
    // Globals (pitch/amp/pan) keep their legacy additive math; per-param dests
    // accumulate a route sum keyed by paramId, then fold into a per-voice modulated
    // snapshot `pm` via the Lin/Log curve rule. This mirrors the VST engine exactly,
    // so both engines sound identical (existing dests included).
    let mPitch = 0, mAmp = 0, mPan = 0;
    const accum = this._modAccum;
    for (const k in accum) delete accum[k];
    for (let s = 1; s <= MOD_MATRIX_SIZE; s++) {
      const src = p['mat' + s + '.src'] | 0;
      const dst = p['mat' + s + '.dst'] | 0;
      if (!src || !dst) continue;
      const x = srcs[src] * (p['mat' + s + '.amt'] || 0);
      const target = DST_TARGET[dst];
      if (target === DST_PITCH) mPitch += x;
      else if (target === DST_AMP) mAmp += x;
      else if (target === DST_PAN) mPan += x;
      else if (target) accum[target] = (accum[target] || 0) + x;
    }

    // Build the per-voice modulated snapshot: pm overrides only the targeted
    // paramIds (everything else reads through `p`). Lin: pm = p + x·(hi−lo);
    // Log: pm = p · 2^(x·D), D=5. Matches the engine's pm_ build.
    const pm = this._pm;
    for (const k in pm) delete pm[k];
    for (const id in accum) {
      const info = MOD_PARAM_INFO[id];
      if (!info) continue; // non-modulatable target — ignore (matches engine)
      // The filter cutoff routes are NOT folded into pm: they are applied as the
      // single-exponent mCut term inside setupFilter so the result is bit-identical
      // to the legacy single Math.pow. All other Log/Lin dests fold here.
      if (id === 'filter.cutoff' || id === 'filter2.cutoff') continue;
      const base = p[id];
      pm[id] = info.curve === 'log'
        ? base * Math.pow(2, accum[id] * MOD_LOG_D)
        : base + accum[id] * (info.hi - info.lo);
    }

    // SPLIT routing sends osc A through filter 1 and osc B through filter 2, so
    // they need separate source buffers; every other routing sums into one path.
    const route = p['filter.route'] | 0;
    const split = route === 2;

    const tmpL = this.tmpL, tmpR = this.tmpR;
    tmpL.fill(0, 0, n); tmpR.fill(0, 0, n);
    const bL = this.bL, bR = this.bR;
    if (split) { bL.fill(0, 0, n); bR.fill(0, 0, n); }

    const aOn = this.setupOsc(v.oA, 'oscA', v, pm, mPitch, mPan);
    const bOn = this.setupOsc(v.oB, 'oscB', v, pm, mPitch, mPan);
    if (aOn) this.renderOsc(v.oA, tmpL, tmpR, n);
    if (bOn) this.renderOsc(v.oB, split ? bL : tmpL, split ? bR : tmpR, n);

    // sub oscillator (polyblep square or sine)
    if (p['sub.on']) {
      const subLvl = pm['sub.level'] ?? p['sub.level'];
      const lvl = subLvl * subLvl * 0.3;
      if (lvl > 1e-6) {
        const sf = 440 * Math.pow(2, (v.pitch + this.bend + p['sub.oct'] * 12 + mPitch * 12 - 69) / 12);
        const inc = sf / sampleRate;
        if (inc > 0 && inc < 0.45) {
          let ph = v.subPhase;
          const square = (p['sub.shape'] | 0) === 1;
          for (let i = 0; i < n; i++) {
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
        }
      }
    }

    // noise
    if (p['noise.on']) {
      const noiseLvl = pm['noise.level'] ?? p['noise.level'];
      const lvl = noiseLvl * noiseLvl * 0.35;
      if (lvl > 1e-6) {
        if ((p['noise.type'] | 0) === 1) {
          const b = v.pb;
          for (let i = 0; i < n; i++) {
            const w = Math.random() * 2 - 1;
            b[0] = 0.99886 * b[0] + w * 0.0555179;
            b[1] = 0.99332 * b[1] + w * 0.0750759;
            b[2] = 0.969 * b[2] + w * 0.153852;
            b[3] = 0.8665 * b[3] + w * 0.3104856;
            b[4] = 0.55 * b[4] + w * 0.5329522;
            b[5] = -0.7616 * b[5] - w * 0.016898;
            const pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362) * 0.11;
            b[6] = w * 0.115926;
            const o = pink * lvl;
            tmpL[i] += o; tmpR[i] += o;
          }
        } else {
          for (let i = 0; i < n; i++) {
            const o = (Math.random() * 2 - 1) * lvl;
            tmpL[i] += o; tmpR[i] += o;
          }
        }
      }
    }

    // ---- per-voice filters with routing ----
    const f1on = !!p['filter.on'];
    const f2on = !!p['filter2.on'];
    if (f1on) this.setupFilter(v.f1, 'filter', v, e2, accum['filter.cutoff'] || 0, pm);
    if (f2on) this.setupFilter(v.f2, 'filter2', v, e2, accum['filter2.cutoff'] || 0, pm);

    const f1L = this.f1L, f1R = this.f1R, f2L = this.f2L, f2R = this.f2R;
    const dr1 = pm['filter.drive'] ?? p['filter.drive'], dr2 = pm['filter2.drive'] ?? p['filter2.drive'];
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

    const ampFactor = Math.min(2, Math.max(0, 1 + mAmp));
    for (let i = 0; i < n; i++) {
      const sl = oL[i], sr = oR[i];
      // Per-voice DC blocker (1-pole highpass, ~3.5 Hz) — removes DC before
      // it reaches the FX chain's saturator where it would cause asymmetric clipping.
      const yL = sl - v.dcxL + DC_R * v.dcyL;
      const yR = sr - v.dcxR + DC_R * v.dcyR;
      v.dcxL = sl; v.dcyL = yL;
      v.dcxR = sr; v.dcyR = yR;
      const amp = v.ampEnv.process() * v.velGain * ampFactor;
      L[i] += yL * amp;
      R[i] += yR * amp;
    }

    // advance block-rate modulators
    v.lfo1.advance(this.lfoHz('lfo1'), n);
    v.lfo2.advance(this.lfoHz('lfo2'), n);
    v.modEnv.processBlock(n);

    return { posA: v.oA.posSm, posB: v.oB.posSm };
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    const L = out[0];
    const R = out.length > 1 ? out[1] : out[0];
    L.fill(0);
    if (R !== L) R.fill(0);
    const n = L.length;

    // Update the global (free-run/transport-locked) LFOs before voices read
    // them. ppq = beats since audio start (block-start position).
    const ppq = this.transportBeats;
    this.updateGlobalLfo(this.gLfo1, 'lfo1', ppq, n);
    this.updateGlobalLfo(this.gLfo2, 'lfo2', ppq, n);
    this.transportBeats += (n / sampleRate) * (this.bpm / 60);

    // Advance the note sequencer (standalone) or the hosted clip transport.
    // Events fire at block boundaries (the same resolution live note messages
    // arrive at); step *durations* are counted in real samples so the clock
    // never drifts. Gate-off runs before the fire so a full-length gate
    // releases just ahead of its retrigger.
    if (this.seqToGateOff >= 0) {
      this.seqToGateOff -= n;
      if (this.seqToGateOff < 0) this.seqGateOff();
    }
    if (this.hosted) {
      this.hostTick(n);
    } else if (this.seqPlaying) {
      if (this.seqToNext <= 0) this.seqFire();
      this.seqToNext -= n;
    }

    let act = 0;
    let viz = null;
    for (const v of this.voices) {
      if (!v.active && v.pending) {
        const pd = v.pending;
        v.pending = null;
        v.noteOn(pd.n, pd.vel, pd.start, this.clock++, 1, 1);
      }
      if (!v.active) continue;
      const r = this.renderVoice(v, L, R, n);
      if (v.gate || !viz) viz = r;
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
    }
    return true;
  }
}

registerProcessor('fable-wt', FableProcessor);
