// FableSynth DR-1 DSP core — AudioWorklet thread. Self-contained (no imports).
// 16 one-shot pad voices + sample-accurate step sequencer. See worklet.js for
// the reference implementations of the shared primitives (mip playback, SVF,
// ADAA drive) — copied here because worklets can't import.
//
// In:  {t:'init',params} {t:'tables',list} {t:'p',k,v} {t:'trig',pad,v}
//      {t:'pats',data} {t:'chain',list} {t:'play'} {t:'stop'} {t:'sel',pad} {t:'panic'}
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
const CHOKE_FADE = 0.12;
const BASE_NOTE = 60;
const DC_R = 0.9998;

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

function makeFilterState() {
  return {
    svf: new Float64Array(8),
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
    this.oA = makeOscState(); this.oB = makeOscState();
    this.f = makeFilterState();
    this.noiseY = 0;
    this.ringPhase = 0.25;
    this.dcxL = 0; this.dcxR = 0; this.dcyL = 0; this.dcyR = 0;
  }

  trigger(v) {
    this.active = true; this.choking = false;
    this.vel = v; this.rand = Math.random() * 2 - 1;
    this.t = 0; this.ampLevel = 0;
    this.oA.posSm = -1; this.oB.posSm = -1;
    this.f.svf.fill(0); this.f.cutSm = 0; this.f.satXL = 0; this.f.satXR = 0;
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
    this.p = Object.create(null);
    this.tables = [];
    this.voices = [];
    for (let i = 0; i < NPADS; i++) this.voices.push(new PadVoice());
    this.pats = new Uint8Array(NPATTERNS * NPADS * STEPS);
    this.chain = [0]; this.chainPos = 0;
    this.playing = false;
    this.step = -1;
    this.samplesToNext = 0;
    this.sel = 0;
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
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  onMsg(d) {
    switch (d.t) {
      case 'init':
        for (const k in d.params) {
          const v = d.params[k];
          if (Number.isFinite(v)) this.p[k] = v;
        }
        break;
      case 'p': if (Number.isFinite(d.v)) this.p[d.k] = d.v; break;
      case 'tables':
        this.tables = d.list.map((x) => ({
          frames: x.frames, mips: x.mips, size: x.size, mask: x.size - 1,
          data: new Float32Array(x.buf),
        }));
        break;
      case 'trig': this.trigger(d.pad | 0, d.v); break;
      case 'pats': this.pats = new Uint8Array(d.data.slice(0)); break;
      case 'chain':
        if (Array.isArray(d.list) && d.list.length) {
          this.chain = d.list.map((x) => x | 0);
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
    for (let i = 0; i < NPADS; i++) {
      const val = this.clip.data[(bar * NPADS + i) * STEPS + s];
      if (val) this.trigger(i, val === 2 ? ACCENT_VEL : PLAIN_VEL);
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
    this.port.postMessage({ t: 'pos', step: s, bar });
  }

  trigger(padI, vel) {
    if (padI < 0 || padI >= NPADS) return;
    const g = this.p['pad' + padI + '.choke'] | 0;
    if (g > 0) {
      for (let j = 0; j < NPADS; j++) {
        if (j !== padI && (this.p['pad' + j + '.choke'] | 0) === g) this.voices[j].choke();
      }
    }
    const v = this.voices[padI];
    v.trigger(Math.max(0, Math.min(1, Number.isFinite(vel) ? vel : 1)));
    const pre = 'pad' + padI + '.';
    const phaseA = (Math.max(0, Math.min(1, this.p[pre + 'oscA.phase'])) * 2048) % 2048;
    const phaseB = (Math.max(0, Math.min(1, this.p[pre + 'oscB.phase'])) * 2048) % 2048;
    for (let i = 0; i < MAXUNI; i++) {
      v.oA.phases[i] = phaseA;
      v.oB.phases[i] = phaseB;
    }
  }

  padMod(padI, v) {
    const p = this.p, pre = 'pad' + padI + '.';
    const dec = Math.max(0.002, p[pre + 'modenv.dec'] / 4.5);
    const env = Math.exp(-v.t / (dec * sampleRate));
    const srcs = [0, env, v.vel * p[pre + 'v2m'], v.rand];
    const m = { posA: 0, posB: 0, level: 0, cut: 0, pitch: 0, fineA: 0, fineB: 0, noise: 0, res: 0 };
    for (let n = 1; n <= 4; n++) {
      const src = p[pre + 'mod' + n + '.src'] | 0;
      const dst = p[pre + 'mod' + n + '.dst'] | 0;
      if (!src || !dst) continue;
      const x = srcs[src] * (p[pre + 'mod' + n + '.amt'] || 0);
      switch (dst) {
        case 1: m.posA += x; break;
        case 2: m.posB += x; break;
        case 3: m.level += x; break;
        case 4: m.cut += x; break;
        case 5: m.pitch += x * 24; break;
        case 6: m.fineA += x * 200; break;
        case 7: m.fineB += x * 200; break;
        case 8: m.noise += x; break;
        case 9: m.res += x; break;
      }
    }
    return m;
  }

  setupOsc(o, pre, pitchEnv, mPos, mFine, mPitch) {
    const p = this.p;
    const table = this.tables[p[pre + 'table'] | 0];
    if (!table) return false;

    const basePitch = BASE_NOTE + p[pre + 'tune'] + (p[pre + 'fine'] + mFine) / 100 + pitchEnv + mPitch;
    const freq = 440 * Math.pow(2, (basePitch - 69) / 12);
    if (!(freq > 0 && freq <= sampleRate * 0.45)) return false;

    let level = Math.min(1.2, Math.max(0, p[pre + 'level']));
    level *= level;
    if (!(level >= 1e-5)) return false;

    const uni = Math.max(1, Math.min(MAXUNI, p[pre + 'unison'] | 0));
    const det = p[pre + 'detune'];
    const spr = 0.6;

    let pos = Math.min(1, Math.max(0, p[pre + 'pos'] + mPos));
    if (o.posSm < 0) o.posSm = pos;
    o.posSm += (pos - o.posSm) * 0.35;
    const posF = o.posSm * (table.frames - 1);
    const f0 = posF | 0;
    const f1 = Math.min(table.frames - 1, f0 + 1);
    o.ft = posF - f0;

    const cps = freq / sampleRate;
    const maxRatio = Math.pow(2, (Math.abs(det) * 50) / 1200);
    const W = 0.07;
    const mipF = Math.log2((cps * maxRatio * 1024) / 0.475);
    let mip = 0, mipBlend = 0;
    if (mipF > 0) {
      mip = Math.min(table.mips - 1, Math.ceil(mipF));
      const over = mipF - (mip - 1);
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

    for (let u = 0; u < uni; u++) {
      const sprd = uni > 1 ? (u / (uni - 1)) * 2 - 1 : 0;
      const cents = sprd * det * 50;
      const ratio = Math.pow(2, cents / 1200);
      o.incs[u] = cps * ratio * table.size;
      const pan = Math.max(-1, Math.min(1, sprd * spr));
      const a = ((pan + 1) * Math.PI) / 4;
      o.gl[u] = Math.cos(a);
      o.gr[u] = Math.sin(a);
    }
    o.gain = (level * 0.32) / Math.sqrt(uni);
    return true;
  }

  renderOsc(o, tmpL, tmpR, off, n) {
    const data = o.data, mask = o.mask, size = o.size, ft = o.ft, g = o.gain;
    const off0 = o.off0, off1 = o.off1;
    const blend = o.mipBlend;
    if (blend < 0.001) {
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
          tmpL[off + i] += s * gl;
          tmpR[off + i] += s * gr;
          ph += inc;
          if (ph >= size) ph -= size;
        }
        o.phases[u] = ph;
      }
    } else {
      const off0b = o.off0b, off1b = o.off1b;
      for (let u = 0; u < o.uni; u++) {
        let ph = o.phases[u];
        const inc = o.incs[u];
        const gl = o.gl[u] * g, gr = o.gr[u] * g;
        for (let i = 0; i < n; i++) {
          const idx = ph | 0;
          const frac = ph - idx;
          const i2 = (idx + 1) & mask;
          const sc0 = data[off0 + idx] + frac * (data[off0 + i2] - data[off0 + idx]);
          const sc1 = data[off1 + idx] + frac * (data[off1 + i2] - data[off1 + idx]);
          const sc = sc0 + ft * (sc1 - sc0);
          const sf0 = data[off0b + idx] + frac * (data[off0b + i2] - data[off0b + idx]);
          const sf1 = data[off1b + idx] + frac * (data[off1b + i2] - data[off1b + idx]);
          const sf = sf0 + ft * (sf1 - sf0);
          const s = sc + blend * (sf - sc);
          tmpL[off + i] += s * gl;
          tmpR[off + i] += s * gr;
          ph += inc;
          if (ph >= size) ph -= size;
        }
        o.phases[u] = ph;
      }
    }
  }

  setupFilter(fs, pre, mCut, mRes) {
    const p = this.p;
    const ftype = p[pre + 'flt.type'] | 0;
    fs.ftype = ftype;
    let fc = p[pre + 'flt.cut'] * Math.pow(2, mCut * MOD_LOG_D);
    if (!Number.isFinite(fc)) fc = 20;
    fc = Math.min(sampleRate * 0.45, Math.max(20, fc));
    if (fs.cutSm <= 0) fs.cutSm = fc;
    fs.cutSm += (fc - fs.cutSm) * 0.5;
    const cut = fs.cutSm;
    const res = Math.min(0.999, Math.max(0, p[pre + 'flt.res'] + mRes));

    fs.twoPole = ftype === 1;
    const g = Math.tan((Math.PI * cut) / sampleRate);
    const k = 2 - 1.93 * res;
    fs.k1 = k;
    fs.a1 = 1 / (1 + g * (g + k));
    fs.a2 = g * fs.a1;
    fs.a3 = g * fs.a2;
  }

  runFilter(fs, inL, inR, outL, outR, drive, n) {
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
          case 0: case 1: buf[i] = v2; break;
          case 2: buf[i] = k1 * v1; break;
          case 3: buf[i] = x - k1 * v1 - v2; break;
          default: buf[i] = x - k1 * v1; break;
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
  }

  ampEnv(v, pre, i) {
    const p = this.p;
    const att = Math.max(1, p[pre + 'aenv.att'] * sampleRate);
    const hold = p[pre + 'aenv.hold'] * sampleRate;
    const dec = Math.max(1, p[pre + 'aenv.dec'] * sampleRate);
    const t = v.t + i;
    if (t < att) return t / att;
    const td = t - att - hold;
    if (td < 0) return 1;
    if (td >= dec) return 0;
    const lin = 1 - td / dec;
    const exp = Math.exp(-4.5 * td / dec);
    const c = p[pre + 'aenv.curve'];
    return lin + (exp - lin) * c;
  }

  renderPad(v, padI, L, R, off, n) {
    const p = this.p, pre = 'pad' + padI + '.';
    const m = this.padMod(padI, v);
    const tmpL = this.tmpL, tmpR = this.tmpR;
    tmpL.fill(0, 0, n); tmpR.fill(0, 0, n);

    const pDec = Math.max(0.002, p[pre + 'penv.dec']);
    const pAmt = p[pre + 'penv.amt'];
    for (let at = 0; at < n; at += 16) {
      const count = Math.min(16, n - at);
      const pe = pAmt * Math.exp(-4.5 * (v.t + at) / (pDec * sampleRate));
      const aOn = this.setupOsc(v.oA, pre + 'oscA.', pe, m.posA, m.fineA, m.pitch);
      const bOn = this.setupOsc(v.oB, pre + 'oscB.', pe, m.posB, m.fineB, m.pitch);
      if (aOn) this.renderOsc(v.oA, tmpL, tmpR, at, count);
      if (bOn) this.renderOsc(v.oB, tmpL, tmpR, at, count);
    }

    const noiseLevel = Math.min(1, Math.max(0, p[pre + 'noise.level'] + m.noise));
    const noiseGain = noiseLevel * noiseLevel * 0.35;
    if (noiseGain > 1e-6) {
      const color = Math.min(1, Math.max(-1, p[pre + 'noise.color']));
      const a = 0.02 + (color + 1) * 0.49;
      let y = v.noiseY;
      for (let i = 0; i < n; i++) {
        const w = Math.random() * 2 - 1;
        y += (w - y) * a;
        const s = y * noiseGain;
        tmpL[i] += s; tmpR[i] += s;
      }
      v.noiseY = y;
    }

    // Sine ring modulator. A fixed-Hz carrier deliberately breaks the
    // oscillator's harmonic series into inharmonic sidebands—the useful bit
    // for bells, struck metal and synthetic cymbals. MIX=0 is a true bypass.
    const ringMix = Math.min(1, Math.max(0, p[pre + 'ring.mix']));
    if (ringMix > 1e-6) {
      const ringFreq = Math.min(sampleRate * 0.45, Math.max(20, p[pre + 'ring.freq']));
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
    if (p[pre + 'flt.on']) {
      this.setupFilter(v.f, pre, m.cut, m.res);
      this.runFilter(v.f, tmpL, tmpR, this.fL, this.fR, p[pre + 'flt.drive'], n);
      srcL = this.fL; srcR = this.fR;
    }

    const velGain = 1 - p[pre + 'v2l'] * (1 - v.vel);
    const level = Math.min(1, Math.max(0, p[pre + 'lvl'] + m.level));
    const levelGain = level * level;
    const pan = Math.min(1, Math.max(-1, p[pre + 'pan']));
    const panA = ((pan + 1) * Math.PI) / 4;
    const panL = Math.cos(panA), panR = Math.sin(panA);
    for (let i = 0; i < n; i++) {
      const sl = srcL[i], sr = srcR[i];
      const yL = sl - v.dcxL + DC_R * v.dcyL;
      const yR = sr - v.dcxR + DC_R * v.dcyR;
      v.dcxL = sl; v.dcyL = yL;
      v.dcxR = sr; v.dcyR = yR;

      if (v.choking) {
        v.ampLevel *= 1 - CHOKE_FADE;
        if (v.ampLevel < 1e-4) {
          v.kill();
          break;
        }
      } else {
        v.ampLevel = this.ampEnv(v, pre, i);
      }
      const amp = v.ampLevel * velGain * levelGain;
      L[off + i] += yL * amp * panL;
      R[off + i] += yR * amp * panR;
    }

    v.t += n;
    const end = (p[pre + 'aenv.att'] + p[pre + 'aenv.hold'] + p[pre + 'aenv.dec']) * sampleRate;
    if (v.active && !v.choking && v.t >= end && v.ampLevel < 1e-4) v.kill();
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    const L = out[0], R = out.length > 1 ? out[1] : out[0];
    L.fill(0); if (R !== L) R.fill(0);
    const n = L.length;

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
        if (v.active) this.renderPad(v, i, L, R, pos, run);
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
        b: v.active ? v.oB.posSm : -1,
        env: v.active ? v.ampLevel : 0,
      });
    }
    return true;
  }

  fireStep() {
    const bpm = Math.max(60, Math.min(200, this.p['seq.bpm'] || 126));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = this.p['master.swing'] || 0;
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
