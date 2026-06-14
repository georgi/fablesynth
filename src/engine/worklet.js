// FableSynth DSP core — runs in the AudioWorklet thread. Self-contained (no imports).
// Protocol (port messages in):
//   {t:'init', params:{id:value,...}}
//   {t:'tables', list:[{frames,mips,size,buf:ArrayBuffer}]}
//   {t:'p', k, v}                    single param change
//   {t:'on', n, v} {t:'off', n}      note events (n=midi note, v=0..1)
//   {t:'bend', s}                    pitch bend in semitones
//   {t:'panic'}
// Out: {t:'viz', a, b, n}            modulated wt positions + active voice count

const NVOICES = 8;
const MAXUNI = 7;

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
    }
    return this.level;
  }
  processBlock(n) { for (let i = 0; i < n; i++) this.process(); return this.level; }
}

class LFO {
  constructor() { this.phase = 0; this.hold = 0; }
  reset() { this.phase = 0; this.hold = Math.random() * 2 - 1; }
  value(shape) {
    const p = this.phase;
    switch (shape | 0) {
      case 0: return Math.sin(2 * Math.PI * p);
      case 1: return 1 - 4 * Math.abs(p - 0.5);
      case 2: return 1 - 2 * p;
      case 3: return p < 0.5 ? 1 : -1;
      default: return this.hold;
    }
  }
  advance(rate, n) {
    this.phase += (rate * n) / sampleRate;
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
    this.ampEnv = new Env(); this.modEnv = new Env();
    this.lfo1 = new LFO(); this.lfo2 = new LFO();
    this.oA = makeOscState(); this.oB = makeOscState();
    this.subPhase = 0;
    this.pb = [0, 0, 0, 0, 0, 0, 0]; // pink noise filter state
    this.f = new Float64Array(8); // svf states: 2 stages x 2 ch x (ic1,ic2)
    this.cutSm = 0;
    this.dcxL = 0; this.dcxR = 0; this.dcyL = 0; this.dcyR = 0;
    this.satXL = 0; this.satXR = 0; // ADAA drive: previous input per channel
  }
  get active() { return this.ampEnv.state !== 0; }

  noteOn(note, vel, startPitch, age, phaseRandA, phaseRandB) {
    this.note = note; this.vel = vel; this.gate = true; this.age = age;
    this.pitch = startPitch;
    this.velGain = 0.25 + 0.75 * vel * vel;
    this.ampEnv.trigger(); this.modEnv.trigger();
    this.lfo1.reset(); this.lfo2.reset();
    for (let i = 0; i < MAXUNI; i++) {
      this.oA.phases[i] = phaseRandA ? Math.random() : 0;
      this.oB.phases[i] = phaseRandB ? Math.random() : 0;
    }
    this.oA.posSm = -1; this.oB.posSm = -1;
    this.subPhase = 0;
    this.f.fill(0);
    this.cutSm = 0;
    this.dcxL = this.dcxR = this.dcyL = this.dcyR = 0;
    this.satXL = this.satXR = 0;
  }
  noteOff() { this.gate = false; this.ampEnv.release(); this.modEnv.release(); }
  kill() { this.gate = false; this.ampEnv.kill(); this.modEnv.kill(); }
}

class FableProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.p = Object.create(null);
    this.tables = [];
    this.voices = [];
    for (let i = 0; i < NVOICES; i++) this.voices.push(new Voice());
    this.bend = 0;
    this.lastPitch = 60;
    this.clock = 0;
    this.vizCount = 0;
    this.tmpL = new Float32Array(128);
    this.tmpR = new Float32Array(128);
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  onMsg(d) {
    switch (d.t) {
      case 'init': Object.assign(this.p, d.params); break;
      case 'p': this.p[d.k] = d.v; break;
      case 'tables':
        this.tables = d.list.map((x) => ({
          frames: x.frames, mips: x.mips, size: x.size, mask: x.size - 1,
          data: new Float32Array(x.buf),
        }));
        break;
      case 'on': this.noteOn(d.n, d.v); break;
      case 'off': this.noteOff(d.n); break;
      case 'bend': this.bend = d.s; break;
      case 'panic': for (const v of this.voices) v.kill(); break;
    }
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
    voice.noteOn(n, vel, start, this.clock++, 1, 1);
    this.lastPitch = n;
  }

  noteOff(n) {
    for (const v of this.voices) if (v.gate && v.note === n) v.noteOff();
  }

  // Configure one oscillator's per-block render state. Returns true if audible.
  setupOsc(o, pre, voice, mPos, mPitch, mLvl, mPan) {
    const p = this.p;
    if (!p[pre + '.on']) return false;
    const table = this.tables[p[pre + '.table'] | 0];
    if (!table) return false;

    const basePitch = voice.pitch + this.bend + p[pre + '.oct'] * 12 + p[pre + '.semi'] + p[pre + '.fine'] / 100 + mPitch * 12;
    const freq = 440 * Math.pow(2, (basePitch - 69) / 12);
    if (freq <= 0 || freq > sampleRate * 0.45) return false;

    let level = Math.min(1.2, Math.max(0, p[pre + '.level'] + mLvl));
    level *= level;
    if (level < 1e-5) return false;

    const uni = Math.max(1, Math.min(MAXUNI, p[pre + '.unison'] | 0));
    const det = p[pre + '.detune'];
    const spr = p[pre + '.spread'];
    const basePan = Math.max(-1, Math.min(1, p[pre + '.pan'] + mPan));

    // position smoothing (avoids zipper on fast morph modulation)
    let pos = Math.min(1, Math.max(0, p[pre + '.pos'] + mPos));
    if (o.posSm < 0) o.posSm = pos;
    o.posSm += (pos - o.posSm) * 0.35;
    const posF = o.posSm * (table.frames - 1);
    const f0 = posF | 0;
    const f1 = Math.min(table.frames - 1, f0 + 1);
    o.ft = posF - f0;

    const cps = freq / sampleRate;
    const maxRatio = Math.pow(2, (det * 50) / 1200);

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
    o.gain = (level * 0.32) / Math.sqrt(uni);

    for (let u = 0; u < uni; u++) {
      const sprd = uni > 1 ? (u / (uni - 1)) * 2 - 1 : 0;
      const cents = sprd * det * 50;
      const ratio = Math.pow(2, cents / 1200);
      o.incs[u] = cps * ratio * table.size;
      const pan = Math.max(-1, Math.min(1, sprd * spr + basePan));
      const a = ((pan + 1) * Math.PI) / 4;
      o.gl[u] = Math.cos(a);
      o.gr[u] = Math.sin(a);
    }
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
    const l1 = v.lfo1.value(p['lfo1.shape']);
    const l2 = v.lfo2.value(p['lfo2.shape']);
    const e2 = v.modEnv.level;
    const srcs = [0, l1, l2, e2, v.vel, (v.note - 60) / 24];

    // matrix destinations
    let mPosA = 0, mPosB = 0, mCut = 0, mPitch = 0, mAmp = 0, mPan = 0, mLvlA = 0, mLvlB = 0;
    for (let s = 1; s <= 4; s++) {
      const src = p['mat' + s + '.src'] | 0;
      const dst = p['mat' + s + '.dst'] | 0;
      if (!src || !dst) continue;
      const x = srcs[src] * p['mat' + s + '.amt'];
      switch (dst) {
        case 1: mPosA += x; break;
        case 2: mPosB += x; break;
        case 3: mCut += x; break;
        case 4: mPitch += x; break;
        case 5: mAmp += x; break;
        case 6: mPan += x; break;
        case 7: mLvlA += x; break;
        case 8: mLvlB += x; break;
      }
    }

    const tmpL = this.tmpL, tmpR = this.tmpR;
    tmpL.fill(0, 0, n); tmpR.fill(0, 0, n);

    const aOn = this.setupOsc(v.oA, 'oscA', v, mPosA, mPitch, mLvlA, mPan);
    const bOn = this.setupOsc(v.oB, 'oscB', v, mPosB, mPitch, mLvlB, mPan);
    if (aOn) this.renderOsc(v.oA, tmpL, tmpR, n);
    if (bOn) this.renderOsc(v.oB, tmpL, tmpR, n);

    // sub oscillator (polyblep square or sine)
    if (p['sub.on']) {
      const lvl = p['sub.level'] * p['sub.level'] * 0.3;
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
      const lvl = p['noise.level'] * p['noise.level'] * 0.35;
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

    // filter coefficients (block rate, smoothed)
    const filtOn = !!p['filter.on'];
    let g1 = 0, k1 = 0, a1 = 0, a2 = 0, a3 = 0, ftype = 0, twoPole = false;
    if (filtOn) {
      ftype = p['filter.type'] | 0;
      twoPole = ftype === 1; // LP24
      let fc = p['filter.cutoff'] *
        Math.pow(2, p['filter.env'] * 4 * e2 + (p['filter.key'] * (v.note - 60)) / 12 + mCut * 5);
      fc = Math.min(sampleRate * 0.45, Math.max(20, fc));
      if (v.cutSm <= 0) v.cutSm = fc;
      v.cutSm += (fc - v.cutSm) * 0.5;
      g1 = Math.tan((Math.PI * v.cutSm) / sampleRate);
      const res = p['filter.res'];
      k1 = 2 - 1.93 * res;
      a1 = 1 / (1 + g1 * (g1 + k1));
      a2 = g1 * a1;
      a3 = g1 * a2;
    }
    const drive = p['filter.drive'];
    const dg = 1 + drive * 7;
    const dcomp = 1 / Math.pow(dg, 0.55);
    const useDrive = drive > 0.005;

    const ampFactor = Math.min(2, Math.max(0, 1 + mAmp));
    const F = v.f;

    // Anti-aliased drive (first-order ADAA of dcomp·tanh(dg·x)). A plain
    // per-sample tanh folds the harmonics it creates back below Nyquist —
    // broadband aliasing that would undo the oscillators' band-limiting. ADAA
    // outputs the average of the nonlinearity over [x[n-1], x[n]] via its
    // antiderivative F(x) = (dcomp/dg)·ln(cosh(dg·x)), which suppresses that
    // aliasing by ~10–20 dB at no oversampling cost (Parker/Välimäki; the same
    // trick Surge XT and Vital use on their shapers).
    const kF = dcomp / dg;
    let xpL = v.satXL, xpR = v.satXR;
    let FpL = kF * lcosh(dg * xpL), FpR = kF * lcosh(dg * xpR);

    for (let i = 0; i < n; i++) {
      let sl = tmpL[i], sr = tmpR[i];
      if (useDrive) {
        const inL = sl, inR = sr;
        const dxL = inL - xpL;
        const FL = kF * lcosh(dg * inL);
        // |Δx| tiny → ADAA is numerically unstable; fall back to midpoint tanh.
        sl = dxL > 1e-5 || dxL < -1e-5 ? (FL - FpL) / dxL : dcomp * Math.tanh(dg * 0.5 * (inL + xpL));
        xpL = inL; FpL = FL;
        const dxR = inR - xpR;
        const FR = kF * lcosh(dg * inR);
        sr = dxR > 1e-5 || dxR < -1e-5 ? (FR - FpR) / dxR : dcomp * Math.tanh(dg * 0.5 * (inR + xpR));
        xpR = inR; FpR = FR;
      }
      if (filtOn) {
        // stage 1, both channels (Simper SVF)
        for (let ch = 0; ch < 2; ch++) {
          const x = ch === 0 ? sl : sr;
          const o1 = ch * 2;
          const v3 = x - F[o1 + 1];
          const v1 = a1 * F[o1] + a2 * v3;
          const v2 = F[o1 + 1] + a2 * F[o1] + a3 * v3;
          F[o1] = 2 * v1 - F[o1];
          F[o1 + 1] = 2 * v2 - F[o1 + 1];
          let y;
          switch (ftype) {
            case 0: case 1: y = v2; break;            // LP
            case 2: y = k1 * v1; break;               // BP (unity peak-ish)
            case 3: y = x - k1 * v1 - v2; break;      // HP
            default: y = x - k1 * v1; break;          // notch
          }
          if (ch === 0) sl = y; else sr = y;
        }
        if (twoPole) {
          for (let ch = 0; ch < 2; ch++) {
            const x = ch === 0 ? sl : sr;
            const o1 = 4 + ch * 2;
            const v3 = x - F[o1 + 1];
            const v1 = a1 * F[o1] + a2 * v3;
            const v2 = F[o1 + 1] + a2 * F[o1] + a3 * v3;
            F[o1] = 2 * v1 - F[o1];
            F[o1 + 1] = 2 * v2 - F[o1 + 1];
            if (ch === 0) sl = v2; else sr = v2;
          }
        }
      }
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
    if (useDrive) { v.satXL = xpL; v.satXR = xpR; }

    // advance block-rate modulators
    v.lfo1.advance(p['lfo1.rate'], n);
    v.lfo2.advance(p['lfo2.rate'], n);
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

    let act = 0;
    let viz = null;
    for (const v of this.voices) {
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
