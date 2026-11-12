// Shared worklet module: allocation-free OTT-style dynamics, compressor, EQ,
// drive color and the common DSP primitives (globalThis.FableDsp, at the end).
// Load with audioWorklet.addModule before an instrument's processor module.
// Original implementation, not an emulation of a proprietary preset.
// Slow, stereo-linked energy matching. Measure the uncorrected wet signal so
// this is feed-forward compensation, not a feedback leveler. Hold in silence.
class AutoGain {
  constructor(sr) {
    this.energyCoef = 1 - Math.exp(-1 / (0.3 * sr));
    this.gainCoef = 1 - Math.exp(-1 / (0.15 * sr));
    this.reset();
  }
  reset() { this.input = 0; this.wet = 0; this.gain = 1; this.target = 1; this.tick = 0; }
  next(inL, inR, wetL, wetR) {
    const input = 0.5 * (inL * inL + inR * inR);
    const wet = 0.5 * (wetL * wetL + wetR * wetR);
    this.input += (input - this.input) * this.energyCoef;
    this.wet += (wet - this.wet) * this.energyCoef;
    if (this.tick === 0 && input > 1e-12 && this.input > 1e-12 && this.wet > 1e-16) {
      // -48 to +24 dB, sufficient to tame OTT boosts without runaway makeup.
      this.target = Math.max(0.003981071706, Math.min(15.848931925, Math.sqrt(this.input / this.wet)));
    }
    this.tick = (this.tick + 1) & 15;
    this.gain += (this.target - this.gain) * this.gainCoef;
    return this.gain;
  }
}

// Shared -1 dBFS sample-peak guard. No lookahead; stereo-linked 80 ms release.
class PeakGuard {
  constructor(sr) { this.release = 1 - Math.exp(-1 / (0.08 * sr)); this.gain = 1; }
  reset() { this.gain = 1; }
  gainFor(l, r) {
    const pk = Math.max(Math.abs(l), Math.abs(r));
    const required = pk > 0.8912509381337456 ? 0.8912509381337456 / pk : 1;
    this.gain = Math.min(required, this.gain + (1 - this.gain) * this.release);
    if (required === 1 && 1 - this.gain < 1e-8) this.gain = 1;
    return this.gain;
  }
  process(L, R, n) {
    for (let i = 0; i < n; i++) {
      const l = L[i], r = R[i];
      if (!Number.isFinite(l) || !Number.isFinite(r)) { L[i] = 0; R[i] = 0; continue; }
      const g = this.gainFor(l, r);
      if (g !== 1) { L[i] = l * g; R[i] = r * g; }
    }
  }
}

// Stereo-linked compressor with a 9 dB knee and measured automatic gain.
class Compressor {
  constructor(sr) {
    this.smooth = 1 - Math.exp(-1 / (0.02 * sr));
    this.attack = 1 - Math.exp(-1 / (0.003 * sr));
    this.release = 1 - Math.exp(-1 / (0.25 * sr));
    this.autoGain = new AutoGain(sr);
    this.sr = sr; this.ratio = this.ratioTarget = 4;
    this.wet = 0; this.threshold = -16;
    // Detector input (max |L|,|R| per sample) since the last telemetry block,
    // so auto-comp can run this exact detector on the material. Bounded: an
    // undrained trace stops filling.
    this.trace = new Float32Array(Math.ceil(sr / 10)); this.traceLen = 0;
    // Side-chain: an external key drives the detector through a one-pole
    // high-pass. Automatic makeup is off while keyed, or it would undo the
    // ducking the key asks for.
    this.keyed = false; this.keyCoef = 0; this.keyLpL = 0; this.keyLpR = 0;
    this.setParams(false, -16); this.reset();
  }
  takeTrace() { const t = this.trace.slice(0, this.traceLen); this.traceLen = 0; return t; }
  setParams(on, threshold, attack = 0.003, release = 0.25, ratio = 4) {
    this.attack = 1 - Math.exp(-1 / (Math.max(0.0001, Math.min(0.1, attack)) * this.sr));
    this.release = 1 - Math.exp(-1 / (Math.max(0.01, Math.min(2, release)) * this.sr));
    this.ratioTarget = Math.max(1, Math.min(20, ratio));
    this.wetTarget = on ? 1 : 0;
    this.thresholdTarget = Math.max(-40, Math.min(0, threshold));
  }
  /** `keyed` selects the key input; `hpHz` high-passes it (20 Hz ≈ flat). */
  setKey(keyed, hpHz = 20) {
    if (keyed !== this.keyed) { this.keyLpL = this.keyLpR = 0; this.autoGain.reset(); }
    this.keyed = !!keyed;
    this.keyCoef = 1 - Math.exp(-2 * Math.PI * Math.max(10, Math.min(1000, hpHz)) / this.sr);
  }
  reset() { this.env = 0; this.gain = 1; this.step = 0; this.tick = 0; this.l = 0; this.r = 0; this.autoGain.reset(); }
  processSample(l, r, kl = 0, kr = kl) {
    let pk;
    if (this.keyed) {
      this.keyLpL += this.keyCoef * (kl - this.keyLpL);
      this.keyLpR += this.keyCoef * (kr - this.keyLpR);
      pk = Math.max(Math.abs(kl - this.keyLpL), Math.abs(kr - this.keyLpR));
    } else pk = Math.max(Math.abs(l), Math.abs(r));
    if (this.traceLen < this.trace.length) this.trace[this.traceLen++] = pk;
    if (this.wetTarget === 0 && this.wet < 1e-6) {
      if (this.wet !== 0) { this.wet = 0; this.reset(); }
      this.l = l; this.r = r; return;
    }
    this.env += (pk - this.env) * (pk > this.env ? this.attack : this.release);
    this.threshold += (this.thresholdTarget - this.threshold) * this.smooth;
    this.ratio += (this.ratioTarget - this.ratio) * this.smooth;
    if (this.tick === 0) {
      const over = 20 * Math.log10(Math.max(1e-9, this.env)) - this.threshold;
      const slope = 1 / this.ratio - 1;
      const db = over <= 0 ? 0 : over < 9 ? slope * over * over / 18 : slope * (over - 4.5);
      this.step = (Math.pow(10, db / 20) - this.gain) / 32;
    }
    this.tick = (this.tick + 1) & 31;
    this.gain += this.step;
    const g = this.keyed ? this.gain : this.gain * this.autoGain.next(l, r, this.gain * l, this.gain * r);
    this.wet += (this.wetTarget - this.wet) * this.smooth;
    this.l = l + this.wet * (g * l - l);
    this.r = r + this.wet * (g * r - r);
  }
  /** `KL`/`KR` are the key for these samples; omitted means no key signal. */
  process(L, R, n, KL = null, KR = KL) {
    if (this.keyed && KL) {
      for (let i = 0; i < n; i++) { this.processSample(L[i], R[i], KL[i] || 0, KR[i] || 0); L[i] = this.l; R[i] = this.r; }
    } else {
      for (let i = 0; i < n; i++) { this.processSample(L[i], R[i]); L[i] = this.l; R[i] = this.r; }
    }
  }
}

class OttCompressor {
  constructor(sr) {
    this.sr = sr;
    // Complementary one-pole splits reconstruct the input exactly at unity,
    // keeping parallel depth blending free of crossover phase cancellation.
    this.lowCoef = 1 - Math.exp(-2 * Math.PI * 120 / sr);
    this.highCoef = 1 - Math.exp(-2 * Math.PI * 2500 / sr);
    this.smooth = 1 - Math.exp(-1 / (0.015 * sr));
    this.split = new Float64Array(4);
    this.env = new Float64Array(3);
    this.gain = new Float64Array(3).fill(1);
    this.target = new Float64Array(3).fill(1);
    this.bandsL = new Float64Array(3);
    this.bandsR = new Float64Array(3);
    this.attack = new Float64Array(3);
    this.release = new Float64Array(3);
    this.depth = 0; this.tick = 0; this.l = 0; this.r = 0;
    this.autoGain = new AutoGain(sr);
    this.setParams(false, 0.35, 1, 1, 1);
  }

  setParams(on, depth, time, up, down) {
    this.depthTarget = on ? Math.max(0, Math.min(1, depth)) : 0;
    this.up = Math.max(0, Math.min(2, up));
    this.down = Math.max(0, Math.min(2, down));
    time = Math.max(0.01, Math.min(10, time));
    if (time !== this.time) {
      this.time = time;
      for (let b = 0; b < 3; b++) {
        this.attack[b] = 1 - Math.exp(-1 / ((b === 0 ? 0.008 : b === 1 ? 0.003 : 0.001) * time * this.sr));
        this.release[b] = 1 - Math.exp(-1 / ((b === 0 ? 0.18 : b === 1 ? 0.12 : 0.08) * time * this.sr));
      }
    }
  }

  reset() {
    this.split.fill(0); this.env.fill(0);
    this.gain.fill(1); this.target.fill(1); this.tick = 0;
    this.autoGain.reset();
  }

  process(L, R, n) {
    for (let i = 0; i < n; i++) { this.processSample(L[i], R[i]); L[i] = this.l; R[i] = this.r; }
  }

  processSample(inL, inR) {
    if (this.depthTarget === 0 && this.depth < 1e-6) {
      if (this.depth !== 0) { this.depth = 0; this.reset(); }
      this.l = inL; this.r = inR; return;
    }
    const s = this.split, l = this.bandsL, r = this.bandsR;
    s[0] += this.lowCoef * (inL - s[0]);
    s[1] += this.lowCoef * (inR - s[1]);
    s[2] += this.highCoef * (inL - s[0] - s[2]);
    s[3] += this.highCoef * (inR - s[1] - s[3]);
    l[0] = s[0]; l[1] = s[2]; l[2] = inL - s[0] - s[2];
    r[0] = s[1]; r[1] = s[3]; r[2] = inR - s[1] - s[3];
    let wetL = 0, wetR = 0;
    for (let b = 0; b < 3; b++) {
      // Stereo-linked detection prevents the image moving with dynamics.
      const pk = Math.max(Math.abs(l[b]), Math.abs(r[b]));
      this.env[b] += (pk - this.env[b]) * (pk > this.env[b] ? this.attack[b] : this.release[b]);
      if (this.tick === 0) {
        const db = 20 * Math.log10(Math.max(1e-9, this.env[b]));
        // At 100%: 4:1 upward below -36 dB, 10:1 downward above -18 dB.
        // Above 100% exaggerates the gain curve, including overcompression
        // (louder input can produce quieter output). Maximum boost: 48 dB.
        // Fade upward gain below -72 dB; never amplify digital silence.
        const floor = Math.max(0, Math.min(1, (db + 90) / 18));
        const boost = Math.min(24, Math.max(0, -36 - db) * 0.75) * this.up * floor;
        const cut = Math.max(0, db + 18) * 0.9 * this.down;
        this.target[b] = Math.pow(10, (boost - cut) / 20);
      }
      this.gain[b] += (this.target[b] - this.gain[b]) * this.smooth;
      wetL += l[b] * this.gain[b]; wetR += r[b] * this.gain[b];
    }
    this.tick = (this.tick + 1) & 15;
    this.depth += (this.depthTarget - this.depth) * this.smooth;
    const compensation = this.autoGain.next(inL, inR, wetL, wetR);
    this.l = inL + this.depth * (wetL * compensation - inL);
    this.r = inR + this.depth * (wetR * compensation - inR);
  }
}

globalThis.FableOttCompressor = OttCompressor;
globalThis.FableAutoGain = AutoGain;
globalThis.FablePeakGuard = PeakGuard;
globalThis.FableCompressor = Compressor;

// Shared four-band EQ; coefficient formulas match WT-1.
class EqBiquad {
  constructor(sr) { this.sr = sr; this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; }
  reset() { this.z1 = 0; this.z2 = 0; }
  copyFrom(other) {
    this.b0 = other.b0; this.b1 = other.b1; this.b2 = other.b2;
    this.a1 = other.a1; this.a2 = other.a2;
    this.z1 = other.z1; this.z2 = other.z2;
  }
  process(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
  lowpass(freq, q) {
    const w0 = 2 * Math.PI * Math.min(freq, this.sr * 0.49) / this.sr;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 - cw) / 2 / a0; this.b1 = (1 - cw) / a0; this.b2 = this.b0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha) / a0;
  }
  highpass(freq, q) {
    const w0 = 2 * Math.PI * Math.min(freq, this.sr * 0.49) / this.sr;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 + cw) / 2 / a0; this.b1 = -(1 + cw) / a0; this.b2 = this.b0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha) / a0;
  }
  lowShelf(freq, gainDb, q = Math.SQRT1_2) {
    const A = Math.pow(10, gainDb / 40);
    const w0 = 2 * Math.PI * Math.min(freq, this.sr * 0.49) / this.sr;
    const cw = Math.cos(w0), sw = Math.sin(w0);
    const alpha = sw / (2 * q);
    const tsa = 2 * Math.sqrt(A) * alpha;
    const a0 = (A + 1) + (A - 1) * cw + tsa;
    this.b0 = A * ((A + 1) - (A - 1) * cw + tsa) / a0;
    this.b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
    this.b2 = A * ((A + 1) - (A - 1) * cw - tsa) / a0;
    this.a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
    this.a2 = ((A + 1) + (A - 1) * cw - tsa) / a0;
  }
  highShelf(freq, gainDb, q = Math.SQRT1_2) {
    const A = Math.pow(10, gainDb / 40);
    const w0 = 2 * Math.PI * Math.min(freq, this.sr * 0.49) / this.sr;
    const cw = Math.cos(w0), sw = Math.sin(w0);
    const alpha = sw / (2 * q);
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
    const w0 = 2 * Math.PI * Math.min(freq, this.sr * 0.49) / this.sr;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q);
    const a0 = 1 + alpha / A;
    this.b0 = (1 + alpha * A) / a0; this.b1 = (-2 * cw) / a0; this.b2 = (1 - alpha * A) / a0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha / A) / a0;
  }
}


const EQ_FIELDS = ['on','low','mid','mfreq','high','lfreq','m2freq','hfreq','mid2','lq','ltype','lon','mq','mtype','mon','m2q','m2type','m2on','hq','htype','hon'].map(k => 'fx.eq.' + k);
const EQ_DEFAULTS = [0,0,0,900,0,120,2500,6000,0,Math.SQRT1_2,0,1,.9,1,1,.9,1,1,Math.SQRT1_2,2,1];
const EQ_KEYS = [[5,1,9,10,11],[3,2,12,13,14],[6,8,15,16,17],[7,4,18,19,20]];
class ParametricEq {
  constructor(sr) {
    this.sr = sr; this.steps = Math.max(1, Math.floor(sr * .015 / 32));
    this.wetCoef = 1 - Math.exp(-1 / (.005 * sr));
    this.guard = new PeakGuard(sr); this.wet = this.targetWet = 0; this.primed = false;
    this.typeFadeSamples = Math.max(1, Math.round(.003 * sr));
    this.bands = EQ_KEYS.map(keys => ({keys, l:new EqBiquad(sr), r:new EqBiquad(sr),
      oldL:new EqBiquad(sr), oldR:new EqBiquad(sr),
      cur:[0,0,0], target:[0,0,0], step:[0,0,0], left:[0,0,0],
      type:1, pendingType:1, xfLeft:0, dirty:true}));
    this.reset();
  }
  reset() {
    this.wet = this.targetWet; this.left = 0; this.guard.reset();
    for (const b of this.bands) {
      b.l.reset(); b.r.reset(); b.oldL.reset(); b.oldR.reset(); b.xfLeft=0; b.dirty = true;
      for (let i=0;i<3;i++) { b.cur[i]=b.target[i]; b.left[i]=0; }
    }
  }
  setParams(read) {
    const get = i => { const v=read(EQ_FIELDS[i]); return Number.isFinite(v)?v:EQ_DEFAULTS[i]; };
    const eqOn = get(0) > .5;
    this.targetWet = eqOn ? 1 : 0;
    if (!this.primed) this.wet = this.targetWet;
    for (const b of this.bands) {
      const k=b.keys;
      const values=[Math.log2(Math.max(20,Math.min(20000,get(k[0])))),
        eqOn&&get(k[4])>.5?Math.max(-15,Math.min(15,get(k[1]))):0, Math.max(.2,Math.min(12,get(k[2])))];
      for(let i=0;i<3;i++) {
        if(!this.primed) { b.cur[i]=b.target[i]=values[i]; b.left[i]=0; }
        else if(values[i]!==b.target[i]) { b.target[i]=values[i]; b.step[i]=(values[i]-b.cur[i])/this.steps; b.left[i]=this.steps; }
      }
      const type=Math.max(0,Math.min(2,get(k[3])|0));
      b.dirty ||= b.pendingType!==type; b.pendingType=type;
    }
    this.primed=true;
  }
  process(L,R,n) {
    for(let i=0;i<n;i++) {
      if(this.left--<=0) {
        this.left=31;
        for(const b of this.bands) {
          let changed=b.dirty;
          for(let j=0;j<3;j++) if(b.left[j]>0) { changed=true; b.cur[j]=--b.left[j]===0?b.target[j]:b.cur[j]+b.step[j]; }
          if(!changed) continue;
          const f=Math.pow(2,b.cur[0]),g=b.cur[1],q=b.cur[2];
          if(b.type!==b.pendingType) {
            b.oldL.copyFrom(b.l); b.oldR.copyFrom(b.r);
            b.type=b.pendingType; b.xfLeft=this.typeFadeSamples;
          }
          if(b.type===0) { b.l.lowShelf(f,g,q); b.r.lowShelf(f,g,q); }
          else if(b.type===2) { b.l.highShelf(f,g,q); b.r.highShelf(f,g,q); }
          else { b.l.peaking(f,q,g); b.r.peaking(f,q,g); }
          b.dirty=false;
        }
      }
      this.wet+=(this.targetWet-this.wet)*this.wetCoef;
      if(this.targetWet===0 && this.wet<1e-6) {
        if(this.wet!==0) { this.wet=0; for(const b of this.bands) { b.l.reset(); b.r.reset(); } }
        continue;
      }
      let l=L[i],r=R[i];
      for(const b of this.bands) {
        const inL=l,inR=r,newL=b.l.process(inL),newR=b.r.process(inR);
        if(b.xfLeft>0) {
          const oldL=b.oldL.process(inL),oldR=b.oldR.process(inR);
          const mix=1-b.xfLeft/this.typeFadeSamples;
          l=oldL+mix*(newL-oldL); r=oldR+mix*(newR-oldR); b.xfLeft--;
        } else { l=newL; r=newR; }
      }
      L[i]+=this.wet*(l-L[i]); R[i]+=this.wet*(r-R[i]);
      const gain=this.guard.gainFor(L[i],R[i]); L[i]*=gain; R[i]*=gain;
    }
  }
}
globalThis.FableParametricEq = ParametricEq;
globalThis.FableEqFields = EQ_FIELDS;

// Per-channel drive color. Saturation switches crossfade at 4x; the tone
// shelf runs after decimation. Zero tone is exactly transparent.
class DriveColor {
  constructor(sr) {
    this.fade = 1 - Math.exp(-1 / (0.01 * sr * 4));
    this.smooth = 1 - Math.exp(-1 / (0.02 * sr));
    this.pole = 1 - Math.exp(-2 * Math.PI * 1000 / sr);
    this.tapeTarget = this.hardTarget = this.toneTarget = 0;
    this.reset();
  }
  setParams(type = 0, tone = 0) {
    this.tapeTarget = Math.round(type) === 1 ? 1 : 0;
    this.hardTarget = Math.round(type) === 2 ? 1 : 0;
    this.toneTarget = Math.max(-1, Math.min(1, tone));
  }
  reset() { this.low = 0; this.tape = this.tapeTarget; this.hard = this.hardTarget; this.tone = this.toneTarget; }
  copyShapeFrom(o) { this.tape = o.tape; this.hard = o.hard; }
  shape(x, k, norm) {
    this.tape += (this.tapeTarget - this.tape) * this.fade;
    this.hard += (this.hardTarget - this.hard) * this.fade;
    const z = x * k, soft = Math.tanh(z);
    const tape = Math.abs(z) < 1 ? 1.5 * z - 0.5 * z * z * z : Math.sign(z);
    const hard = Math.max(-1, Math.min(1, z));
    return (soft + this.tape * (tape - soft) + this.hard * (hard - soft)) * norm;
  }
  processTone(x) {
    this.tone += (this.toneTarget - this.tone) * this.smooth;
    this.low += this.pole * (x - this.low);
    // ±6 dB high shelf, neutral at centre; keeps the low-end foundation.
    return x + this.tone * (this.tone < 0 ? 0.5 : 1) * (x - this.low);
  }
}
globalThis.FableDriveColor = DriveColor;

// ---------------------------------------------------------------------------
// Shared DSP primitives for the instrument worklets (WT-1, DR-1, BL-1) and the
// SQ-4 master. Each one used to be hand-copied into every worklet. They live
// in an IIFE so their names cannot collide with a processor module that tests
// concatenate after this file; consumers destructure globalThis.FableDsp at
// their top level, which runs after this module because every loader adds it
// first. Rate-dependent constructors and designs take the rate explicitly.
// ---------------------------------------------------------------------------
(() => {
const ADAA_FADE_WIDTH = 0.1;
const ADAA_INPUT_LIMIT = 16;
const LIM_CEILING = 0.8912509381337456; // -1 dBFS

// Numerically stable ln(cosh(z)) — the antiderivative of tanh, used by the
// anti-aliased (ADAA) saturator below. cosh overflows for |z| > ~710, so we
// fold large arguments to |z| - ln2 + log1p(e^-2|z|). Exact for small z too
// (ln cosh 0 = 0), so it is safe across the whole drive range.
function lcosh(z) {
  const a = Math.abs(z);
  return a + Math.log1p(Math.exp(-2 * a)) - Math.LN2;
}
function adaaInput(x) {
  return Number.isFinite(x) ? Math.max(-ADAA_INPUT_LIMIT, Math.min(ADAA_INPUT_LIMIT, x)) : 0;
}

// 2x oversampler for the in-voice drive saturator. Only the nonlinearity runs
// at 2x (interpolate -> ADAA tanh at the high rate -> decimate); the SVF stays
// at the base rate. Both converters are the polyphase IIR half-band that
// juce/source/dsp/DriveOs.h documents: allpass branches A0 = {c0, c2, c4} and
// A1 = {c1, c3}. While the saturator is idle and the stage has faded out, the
// input is returned unchanged (bit-identical to a voice without the stage); the
// stage fades in and out over DRIVE_OS_RAMP_SEC with a linear crossfade.
const DRIVE_OS_COEF = new Float64Array([
  0.058131122888383423, 0.21191463147606193, 0.4170395803350932,
  0.63844438482644794, 0.87095859811277021]);
const DRIVE_OS_RAMP_SEC = 0.004;
// Allpass state, (x1, y1) pairs: u0 at 0,2,4; u1 at 6,8; d0 at 10,12,14; d1 at 16,18.
function makeDriveOs() {
  return {
    s: new Float64Array(20), dprev: 0,
    xp: 0,   // previous high-rate ADAA input
    F: 0,    // kF * lcosh(dg * xp), valid while the saturator is active
    w: 0,    // stage weight: 0 = bypass, 1 = stage
  };
}
function resetDriveOs(o) { o.s.fill(0); o.dprev = 0; o.xp = 0; o.F = 0; o.w = 0; }
function copyDriveOs(d, s) { d.s.set(s.s); d.dprev = s.dprev; d.xp = s.xp; d.F = s.F; d.w = s.w; }
function driveOsAp(s, i, a, x) {
  const y = a * (x - s[i + 1]) + s[i];
  s[i] = x; s[i + 1] = y;
  return y;
}
function driveOsSat(o, a, xpv, dg, dcomp, kF) {
  const dx = a - xpv;
  const F = kF * lcosh(dg * a);
  // |dx| tiny -> ADAA is numerically unstable; fall back to midpoint tanh.
  const r = dx > 1e-5 || dx < -1e-5 ? (F - o.F) / dx : dcomp * Math.tanh(dg * 0.5 * (a + xpv));
  o.F = F;
  return r;
}
// One base-rate sample. engage: the stage belongs in the path; satOn: the
// saturator runs with dry/wet mix m.
function driveOsStep(o, x, engage, satOn, m, wStep, dg, dcomp, kF) {
  const target = engage ? 1 : 0;
  if (o.w < target) o.w = Math.min(target, o.w + wStep);
  else if (o.w > target) o.w = Math.max(target, o.w - wStep);
  if (o.w <= 0 && !engage) { o.xp = adaaInput(x); return x; }
  const s = o.s, C = DRIVE_OS_COEF;
  const u0 = driveOsAp(s, 4, C[4], driveOsAp(s, 2, C[2], driveOsAp(s, 0, C[0], x)));
  const u1 = driveOsAp(s, 8, C[3], driveOsAp(s, 6, C[1], x));
  const a0 = adaaInput(u0), a1 = adaaInput(u1);
  let o0 = a0, o1 = a1;
  if (satOn) {
    o0 = a0 + m * (driveOsSat(o, a0, o.xp, dg, dcomp, kF) - a0);
    o1 = a1 + m * (driveOsSat(o, a1, a0, dg, dcomp, kF) - a1);
  }
  o.xp = a1;
  const da = driveOsAp(s, 14, C[4], driveOsAp(s, 12, C[2], driveOsAp(s, 10, C[0], o0)));
  const db = driveOsAp(s, 18, C[3], driveOsAp(s, 16, C[1], o1));
  const y = 0.5 * (da + o.dprev);
  o.dprev = db;
  return o.w >= 1 ? y : x + o.w * (y - x);
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

// Band-limited square for the SUB oscillator. Mirrors subSquareTable() /
// subSquareMip() / subSquareRead() in juce/source/dsp/Wavetables.{h,cpp}: odd
// harmonics 1/k, coarsest mip first (mip m keeps harmonics 1..(1024>>m)), a
// 4096-point cycle with 1 wrapped guard sample before and 2 after, read with
// the same mip rule and cubic Hermite as the wavetable oscillators. Built once
// per worklet scope (constructors call it), never on the audio thread.
const SUBSQ_SIZE = 4096, SUBSQ_STRIDE = SUBSQ_SIZE + 3, SUBSQ_MIPS = 11;
let subSquareTbl = null;
function subSquareTable() {
  if (subSquareTbl) return subSquareTbl;
  const lut = new Float64Array(SUBSQ_SIZE), acc = new Float64Array(SUBSQ_SIZE);
  for (let j = 0; j < SUBSQ_SIZE; j++) lut[j] = Math.sin(2 * Math.PI * j / SUBSQ_SIZE);
  const out = new Float32Array(SUBSQ_MIPS * SUBSQ_STRIDE);
  const coef = 4 / Math.PI;
  let k = 1;
  for (let m = SUBSQ_MIPS - 1; m >= 0; m--) {
    const maxHarm = 1024 >> m;
    for (; k <= maxHarm; k += 2) {
      const inv = 1 / k;
      for (let i = 0; i < SUBSQ_SIZE; i++) acc[i] += lut[(k * i) & (SUBSQ_SIZE - 1)] * inv;
    }
    const base = m * SUBSQ_STRIDE;
    for (let i = -1; i < SUBSQ_SIZE + 2; i++)
      out[base + i + 1] = acc[(i + SUBSQ_SIZE) & (SUBSQ_SIZE - 1)] * coef;
  }
  return (subSquareTbl = out);
}
// Mip for phase increment `inc` into the reusable `r` {off, offFine, blend}.
function subSquareMip(r, inc) {
  const mipF = Math.log2((inc * 1024) / 0.475);
  let mip = 0; r.blend = 0;
  if (mipF > 0) {
    mip = Math.min(SUBSQ_MIPS - 1, Math.ceil(mipF));
    const over = mipF - (mip - 1);
    if (over < 0.07) r.blend = 1 - over / 0.07;
  }
  r.off = mip * SUBSQ_STRIDE;
  r.offFine = (mip > 0 ? mip - 1 : 0) * SUBSQ_STRIDE;
  return r;
}
function subSquareReadMip(t, off, ph) {
  const p = ph * SUBSQ_SIZE;
  const idx = p | 0;
  const f = p - idx;
  const q = off + idx;
  const ym1 = t[q], y0 = t[q + 1], y1 = t[q + 2], y2 = t[q + 3];
  const c1 = 0.5 * (y1 - ym1);
  const c2 = ym1 - 2.5 * y0 + 2 * y1 - 0.5 * y2;
  const c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
  return ((c3 * f + c2) * f + c1) * f + y0;
}
function subSquareRead(t, m, ph) {
  const sc = subSquareReadMip(t, m.off, ph);
  if (m.blend < 0.001) return sc;
  return sc + m.blend * (subSquareReadMip(t, m.offFine, ph) - sc);
}

function besselI0(x) {
  let sum = 1, term = 1;
  for (let k = 1; k < 64; k++) {
    term *= x * x / (4 * k * k);
    sum += term;
    if (term < 1e-16 * sum) break;
  }
  return sum;
}

class LfoClock {
  constructor() { this.ready = false; }
  reset() { this.ready = false; }
  update(position, rate) {
    if (!this.ready || position < this.position) {
      this.cycles = position * rate;
      this.ready = true;
    } else this.cycles += (position - this.position) * this.rate;
    this.position = position; this.rate = rate;
    return this.cycles;
  }
  phase(position, rate) { const c = this.update(position, rate); return c - Math.floor(c); }
}

// RBJ cookbook biquad, transposed direct form II.
class Biquad {
  constructor() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; }
  reset() { this.z1 = 0; this.z2 = 0; }
  process(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
  lowpass(freq, q, sr) {
    const w0 = 2 * Math.PI * Math.min(freq, sr * 0.49) / sr;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 - cw) / 2 / a0; this.b1 = (1 - cw) / a0; this.b2 = this.b0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha) / a0;
  }
  highpass(freq, q, sr) {
    const w0 = 2 * Math.PI * Math.min(freq, sr * 0.49) / sr;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 + cw) / 2 / a0; this.b1 = -(1 + cw) / a0; this.b2 = this.b0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha) / a0;
  }
}

// Circular delay line with linear and 4-point Catmull-Rom (Hermite) reads.
// Float32 storage mirrors the plugin's `float` buffers, so a feedback path
// quantizes identically.
class DelayLine {
  constructor(n) { this.buf = new Float32Array(Math.max(4, n | 0)); this.w = 0; }
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
  // For the modulated reads (chorus, echo). Chromium's DelayNode interpolated
  // linearly; this is what the plugin does.
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
  // Adopt another line's contents and write position (mono fast path).
  copyFrom(o) { this.buf.set(o.buf); this.w = o.w; }
  // Block form of write-then-read at a fixed delay (the drive's dry path).
  blockRead(src, dst, n, d) {
    for (let i = 0; i < n; i++) { this.write(src[i]); dst[i] = this.read(d); }
  }
}

// Stereo-linked lookahead peak limiter: a 1.5 ms delay, a monotonic ring queue
// for the minimum required gain over the window, an attack that develops inside
// the window, a ~200 ms release and a final clamp that catches smoothing
// residue (hard -1 dBFS sample-peak ceiling by default). makeup scales the input. process() handles one sample and leaves the
// result in outL/outR; processBlock() runs the same recurrence in place.
class LookaheadLimiter {
  constructor(sr, makeup = 1) {
    this.la = Math.max(8, Math.round(0.0015 * sr));
    this.qcap = this.la + 2;
    this.dlL = new Float32Array(this.la);
    this.dlR = new Float32Array(this.la);
    this.qv = new Float64Array(this.qcap);
    this.qi = new Float64Array(this.qcap);
    this.atk = 1 - Math.exp(-4 / this.la);
    this.rel = 1 - Math.exp(-1 / (0.2 * sr));
    this.makeup = makeup;
    this.reset();
  }
  reset() {
    this.dlL.fill(0); this.dlR.fill(0); this.qv.fill(1); this.qi.fill(0);
    this.qh = 0; this.qt = 0; this.w = 0; this.t = 0; this.env = 1;
    this.outL = 0; this.outR = 0;
  }
  process(l, r, ceiling = LIM_CEILING) {
    const cap = this.qcap, qv = this.qv, qi = this.qi;
    const xl = l * this.makeup, xr = r * this.makeup;
    const pk = Math.max(Math.abs(xl), Math.abs(xr));
    const g = pk > ceiling ? ceiling / pk : 1;
    while (this.qh !== this.qt) {
      const prev = this.qt > 0 ? this.qt - 1 : cap - 1;
      if (qv[prev] < g) break;
      this.qt = prev;
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
    const pd = Math.max(Math.abs(dl), Math.abs(dr));
    if (gg * pd > ceiling) gg = ceiling / pd;
    this.outL = dl * gg; this.outR = dr * gg;
  }
  processBlock(L, R, n) {
    const la = this.la, qcap = this.qcap, qv = this.qv, qi = this.qi;
    const dlL = this.dlL, dlR = this.dlR, mk = this.makeup;
    let w = this.w, qh = this.qh, qt = this.qt, t = this.t, env = this.env;
    for (let i = 0; i < n; i++) {
      const xl = L[i] * mk, xr = R[i] * mk;
      const pk = Math.max(Math.abs(xl), Math.abs(xr));
      const g = pk > LIM_CEILING ? LIM_CEILING / pk : 1;
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
      if (gg * pd > LIM_CEILING) gg = LIM_CEILING / pd;
      L[i] = dl * gg; R[i] = dr * gg;
    }
    this.w = w; this.qh = qh; this.qt = qt; this.t = t; this.env = env;
  }
}

// Freeverb building blocks (WT-1 and BL-1; DR-1 inlines its own network).
class FvComb {
  constructor(n) { this.buf = new Float32Array(Math.max(1, n | 0)); this.idx = 0; this.filt = 0; this.damp1 = 0.2; this.damp2 = 0.8; this.feedback = 0.84; }
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
  constructor(n) { this.buf = new Float32Array(Math.max(1, n | 0)); this.idx = 0; this.feedback = 0.5; }
  reset() { this.buf.fill(0); }
  process(x) {
    const bo = this.buf[this.idx];
    const out = -x + bo;
    this.buf[this.idx] = x + bo * this.feedback;
    if (++this.idx >= this.buf.length) this.idx = 0;
    return out;
  }
}

// Equal-power wet/dry gain for an FX stage's mix knob.
function mixGate(on, amount, wet) {
  if (wet) return on ? Math.sin(amount * Math.PI / 2) : 0;
  return on ? Math.cos(amount * Math.PI / 2) : 1;
}

globalThis.FableDsp = {
  ADAA_FADE_WIDTH, DRIVE_OS_RAMP_SEC, LIM_CEILING,
  lcosh, adaaInput, makeDriveOs, resetDriveOs, copyDriveOs, driveOsStep,
  rdH, subSquareTable, subSquareMip, subSquareRead,
  besselI0, LfoClock, Biquad, DelayLine, LookaheadLimiter, FvComb, FvAllpass, mixGate,
};
})();
