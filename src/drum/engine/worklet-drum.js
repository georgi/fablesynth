// FableSynth DR-1 DSP core — AudioWorklet thread. Self-contained (no imports).
// 16 one-shot pad voices + sample-accurate step sequencer. See worklet.js for
// the reference implementations of the shared primitives (mip playback, SVF,
// ADAA drive) — copied here because worklets can't import.
//
// Fidelity contract (docs/audio-engine-review.md §4): Hermite table reads,
// intra-block ramps for every increment, sample-rate-derived smoothing and
// filter poles, and a seedable RNG — the same choices the JUCE port makes, so
// the two engines stay comparable.
//
// In:  {t:'init',params} {t:'tables',list} {t:'samples',list} {t:'p',k,v} {t:'trig',pad,v}
//      {t:'pats',data} {t:'chain',list} {t:'play'} {t:'stop'} {t:'sel',pad} {t:'panic'}
//      {t:'seed',v}
// Out: {t:'step',s,pat,hits} per step while playing
//      {t:'viz',a,b,env} every 2048 samples for the selected pad

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
const P_V2L = fid('v2l'), P_V2M = fid('v2m'), P_CHOKE = fid('choke');

const GLOBALS = ['seq.bpm', 'master.swing'];
const G_BPM = NPADS * NF, G_SWING = G_BPM + 1;
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
    this.tmpL = new Float32Array(128); this.tmpR = new Float32Array(128);
    this.fL = new Float32Array(128); this.fR = new Float32Array(128);
    this.xL = new Float32Array(128); this.xR = new Float32Array(128);
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  setParam(k, v) {
    const i = PARAM_INDEX.get(k);
    if (i !== undefined) this.pv[i] = v;
  }

  onMsg(d) {
    switch (d.t) {
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
        for (const v of this.voices) v.kill();
        for (const v of this.tails) v.kill();
        this.clip = null; this.clipPend = null; this.clipStopAt = -1; this.clipStep = -1;
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
    // One stereo worklet output per pad. The main thread connects each output
    // to that pad's own FX rack; summing here would make independent inserts
    // impossible. Missing outputs are tolerated by the lightweight test
    // harness and older hosts during upgrades.
    for (const out of outputs) {
      for (const channel of out) channel.fill(0);
    }
    const n = outputs[0]?.[0]?.length || 128;

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
        const out = outputs[i];
        if (!out) continue;
        const L = out[0], R = out.length > 1 ? out[1] : out[0];
        const v = this.voices[i];
        if (v.active) this.renderPad(v, i, L, R, pos, run);
        const tail = this.tails[i];
        if (tail.active) this.renderPad(tail, i, L, R, pos, run);
      }
      if (standalone) this.samplesToNext -= run;
      pos += run;
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
