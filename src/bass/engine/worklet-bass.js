// FableSynth BL-1 DSP core — AudioWorklet thread. Self-contained (no imports).
// One mono last-note-priority acid voice + sample-accurate pitch sequencer.
// See worklet.js / worklet-drum.js for the reference implementations of the
// shared primitives (mip playback, SVF, ADAA drive) — copied here because
// worklets can't import.
//
// In:  {t:'init',params} {t:'tables',list} {t:'p',k,v}
//      {t:'pats',data} {t:'chain',list} {t:'play'} {t:'stop'} {t:'panic'}
//      {t:'noteon',semi,vel} {t:'noteoff',semi}
// Out: {t:'step',s,pat,semi,acc,slide} per step while playing
//      {t:'viz',pos,env,cut,gate,semi} every 2048 samples

const MAXUNI = 7;
const STEPS = 16;
const NPATTERNS = 4;
const STEP_STRIDE = 3;
const ACCENT_VEL = 1.0;
const PLAIN_VEL = 0.72;
const GATE_FRAC = 0.55;
const SWING_MAX = 0.667;
const ROOT_MIDI = 36;
// Filter env sweep span (octaves at flt.env = ±100%) and LFO span (octaves at
// depth = 100%). Accent multiplies the env peak and shortens its decay.
const FENV_OCT = 5;
const LFO_OCT = 2;
const ACC_GAIN = 0.7;
const ACC_DEC_SHORTEN = 0.35;
const KEYTRACK_REF = 60;
const DC_R = 0.9998;
// Cycles per beat for each lfo.rate index — mirrors LFO_DIV_F in src/params.ts.
const LFO_DIV_F = [0.25, 0.5, 1, 2 / 3, 1.5, 2, 4 / 3, 3, 4, 6, 8];

function lcosh(z) {
  const a = Math.abs(z);
  return a + Math.log1p(Math.exp(-2 * a)) - Math.LN2;
}

class BassProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.p = Object.create(null);
    this.tables = [];
    this.pats = new Uint8Array(NPATTERNS * STEPS * STEP_STRIDE);
    this.chain = [0]; this.chainPos = 0;
    this.playing = false;
    this.step = -1;
    this.samplesToNext = 0;
    this.samplesToGateOff = -1;
    this.songPos = 0; // samples since play, for the bar-locked LFO
    // ---- hosted clip transport (SQ-4, docs/sq4-clips.md §6) ----
    this.hosted = false;
    this.hostBpm = 120;
    this.hostSwing = 0;
    this.hostAnchor = 0; // songStartFrame — the shared timebase's beat zero
    this.clip = null; // { data: Uint8Array, bars } — 3 bytes/step, bar-major
    this.clipPend = null; // { data, bars, at }
    this.clipStopAt = -1;
    this.clipStep = -1;
    this.clipToNext = 0;

    // ---- voice ----
    this.gate = false;
    this.acc = false;
    this.vel = PLAIN_VEL;
    this.semi = 0; // slid/current semitone offset from ROOT_MIDI
    this.semiTarget = 0;
    this.fenvT = 1e9; // samples since (non-slid) trigger
    this.ampStage = 0; // 0 idle · 1 att · 2 dec/sus · 3 rel
    this.ampLevel = 0;
    this.held = []; // keyboard stack, last = current

    // osc state
    this.phases = new Float64Array(MAXUNI);
    this.incs = new Float64Array(MAXUNI);
    this.gl = new Float32Array(MAXUNI);
    this.gr = new Float32Array(MAXUNI);
    this.uni = 1; this.off0 = 0; this.off1 = 0; this.off0b = 0; this.off1b = 0;
    this.mipBlend = 0; this.ft = 0; this.oscGain = 0;
    this.mask = 0; this.size = 0; this.data = null; this.posSm = -1;
    this.subPhase = 0;
    // filter state
    this.svf = new Float64Array(8);
    this.cutSm = 0; this.curCut = 0;
    this.satXL = 0; this.satXR = 0;
    this.ftype = 1; this.twoPole = true;
    this.a1 = 0; this.a2 = 0; this.a3 = 0; this.k1 = 0;
    this.shVal = 0; this.shPhase = -1;
    this.lfoPhase = 0;
    this.dcxL = 0; this.dcxR = 0; this.dcyL = 0; this.dcyR = 0;

    this.tmpL = new Float32Array(128); this.tmpR = new Float32Array(128);
    this.fL = new Float32Array(128); this.fR = new Float32Array(128);
    this.vizCount = 0;
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
      case 'pats': this.pats = new Uint8Array(d.data.slice(0)); break;
      case 'chain':
        if (Array.isArray(d.list) && d.list.length) {
          this.chain = d.list.map((x) => x | 0);
          this.chainPos = Math.min(this.chainPos, this.chain.length - 1);
        }
        break;
      case 'play':
        if (this.hosted) break; // conductor owns the transport
        this.playing = true; this.step = -1; this.chainPos = 0;
        this.samplesToNext = 0; this.samplesToGateOff = -1; this.songPos = 0;
        this.held.length = 0;
        break;
      case 'stop':
        this.playing = false; this.step = -1;
        this.samplesToGateOff = -1;
        this.release();
        break;
      case 'noteon': this.keyOn(d.semi | 0, d.vel); break;
      case 'noteoff': this.keyOff(d.semi | 0); break;
      case 'panic':
        this.kill(); this.held.length = 0;
        this.clip = null; this.clipPend = null; this.clipStopAt = -1; this.clipStep = -1;
        break;
      case 'host': this.hosted = !!d.on; break;
      case 'tempo':
        if (Number.isFinite(d.bpm)) { this.hostBpm = d.bpm; this.p['seq.bpm'] = d.bpm; } // bar-locked LFO follows
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
  clipRead(abs) {
    const o = abs * STEP_STRIDE;
    const flags = this.clip.data[o];
    return {
      on: (flags & 1) !== 0,
      acc: (flags & 2) !== 0,
      slide: (flags & 4) !== 0,
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
        this.samplesToGateOff = -1;
        this.release();
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
      this.songPos = Math.max(0, currentFrame - this.hostAnchor); // bar-locked LFO follows the global clock
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
    const st = this.clipRead(abs);

    if (st.on) {
      if (st.slide && this.gate) this.glideTo(st.semi, st.acc);
      else this.noteOn(st.semi, st.acc);
      const stN = this.clipRead((abs + 1) % total);
      this.samplesToGateOff = stN.on && stN.slide ? -1 : GATE_FRAC * dur;
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
    this.port.postMessage({ t: 'pos', step: s, bar: (abs / STEPS) | 0 });
  }

  // ---------- voice control ----------
  noteOn(semi, acc, vel) {
    this.gate = true;
    this.acc = !!acc;
    this.vel = Number.isFinite(vel) ? Math.max(0, Math.min(1, vel)) : (acc ? ACCENT_VEL : PLAIN_VEL);
    this.semi = semi;
    this.semiTarget = semi;
    this.fenvT = 0;
    this.ampStage = 1;
  }

  glideTo(semi, acc) {
    this.semiTarget = semi;
    if (acc) this.acc = true; // an accented slide target keeps the bite
    this.gate = true;
  }

  release() {
    this.gate = false;
    if (this.ampStage !== 0) this.ampStage = 3;
  }

  kill() {
    this.gate = false; this.ampStage = 0; this.ampLevel = 0;
    this.fenvT = 1e9;
    this.svf.fill(0); this.satXL = 0; this.satXR = 0;
    this.posSm = -1; this.cutSm = 0;
  }

  keyOn(semi, vel) {
    if (this.playing || this.clip) return; // audition when stopped · sequencer owns the voice
    const i = this.held.indexOf(semi);
    if (i >= 0) this.held.splice(i, 1);
    const legato = this.held.length > 0 && this.gate;
    this.held.push(semi);
    if (legato) this.glideTo(semi, false);
    else this.noteOn(semi, false, vel);
  }

  keyOff(semi) {
    const i = this.held.indexOf(semi);
    if (i >= 0) this.held.splice(i, 1);
    if (this.playing || this.clip) return;
    if (this.held.length === 0) {
      this.release();
    } else if (this.semiTarget !== this.held[this.held.length - 1]) {
      this.glideTo(this.held[this.held.length - 1], false);
    }
  }

  // ---------- sequencer ----------
  readStep(pat, s) {
    const o = (pat * STEPS + s) * STEP_STRIDE;
    const flags = this.pats[o];
    return {
      on: (flags & 1) !== 0,
      acc: (flags & 2) !== 0,
      slide: (flags & 4) !== 0,
      semi: Math.min(11, this.pats[o + 1]) + 12 * (Math.min(2, this.pats[o + 2]) - 1),
    };
  }

  fireStep() {
    const bpm = Math.max(60, Math.min(200, this.p['seq.bpm'] || 138));
    const dur = (60 / bpm / 4) * sampleRate;
    const swing = this.p['master.swing'] || 0;
    const next = this.step + 1;
    if (next >= STEPS) {
      this.step = -1;
      this.chainPos = (this.chainPos + 1) % this.chain.length;
    }
    const s = (this.step + 1) % STEPS;
    const pat = this.chain[this.chainPos] | 0;
    const st = this.readStep(pat, s);

    let semi = -100;
    if (st.on) {
      semi = st.semi;
      if (st.slide && this.gate) this.glideTo(semi, st.acc);
      else this.noteOn(semi, st.acc);
      // hold through the step when the NEXT step ties in with a slide
      const sN = (s + 1) % STEPS;
      const patN = sN === 0 ? this.chain[(this.chainPos + 1) % this.chain.length] | 0 : pat;
      const stN = this.readStep(patN, sN);
      this.samplesToGateOff = stN.on && stN.slide ? -1 : GATE_FRAC * dur;
    }

    this.step = s;
    const offNow = s % 2 === 1 ? swing * SWING_MAX * dur : 0;
    const sNext = (s + 1) % STEPS;
    const offNext = sNext % 2 === 1 ? swing * SWING_MAX * dur : 0;
    this.samplesToNext = dur - offNow + offNext;
    this.port.postMessage({ t: 'step', s, pat, semi, acc: st.on && st.acc, slide: st.on && st.slide });
  }

  // ---------- osc setup / render (per 16-sample sub-block) ----------
  setupOsc(noteAbs) {
    const p = this.p;
    const table = this.tables[p['osc.table'] | 0];
    if (!table) return false;
    const freq = 440 * Math.pow(2, (noteAbs - 69) / 12);
    if (!(freq > 0 && freq <= sampleRate * 0.45)) return false;

    let level = Math.min(1.2, Math.max(0, p['osc.level']));
    level *= level;
    if (!(level >= 1e-5)) return false;

    const uni = Math.max(1, Math.min(MAXUNI, p['osc.unison'] | 0));
    const det = p['osc.detune'];
    const spr = Math.min(1, Math.max(0, p['osc.spread']));

    const pos = Math.min(1, Math.max(0, p['osc.pos']));
    if (this.posSm < 0) this.posSm = pos;
    this.posSm += (pos - this.posSm) * 0.35;
    const posF = this.posSm * (table.frames - 1);
    const f0 = posF | 0;
    const f1 = Math.min(table.frames - 1, f0 + 1);
    this.ft = posF - f0;

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

    this.off0 = (f0 * table.mips + mip) * table.size;
    this.off1 = (f1 * table.mips + mip) * table.size;
    this.off0b = (f0 * table.mips + fineMip) * table.size;
    this.off1b = (f1 * table.mips + fineMip) * table.size;
    this.mipBlend = mipBlend;
    this.data = table.data;
    this.mask = table.mask;
    this.size = table.size;
    this.uni = uni;

    for (let u = 0; u < uni; u++) {
      const sprd = uni > 1 ? (u / (uni - 1)) * 2 - 1 : 0;
      const cents = sprd * det * 50;
      const ratio = Math.pow(2, cents / 1200);
      this.incs[u] = cps * ratio * table.size;
      const pan = Math.max(-1, Math.min(1, sprd * spr));
      const a = ((pan + 1) * Math.PI) / 4;
      this.gl[u] = Math.cos(a);
      this.gr[u] = Math.sin(a);
    }
    this.oscGain = (level * 0.32) / Math.sqrt(uni);
    return true;
  }

  renderOsc(tmpL, tmpR, off, n) {
    const data = this.data, mask = this.mask, size = this.size, ft = this.ft, g = this.oscGain;
    const off0 = this.off0, off1 = this.off1;
    const blend = this.mipBlend;
    for (let u = 0; u < this.uni; u++) {
      let ph = this.phases[u];
      const inc = this.incs[u];
      const gl = this.gl[u] * g, gr = this.gr[u] * g;
      if (blend < 0.001) {
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
      } else {
        const off0b = this.off0b, off1b = this.off1b;
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
      }
      this.phases[u] = ph;
    }
  }

  renderSub(tmpL, tmpR, off, n, noteRootAbs) {
    const p = this.p;
    let level = Math.min(1, Math.max(0, p['sub.level']));
    level *= level;
    const gain = level * 0.35;
    if (gain < 1e-6) return;
    const oct = Math.max(-2, Math.min(-1, p['sub.oct'] | 0 || -1));
    const freq = 440 * Math.pow(2, (noteRootAbs + 12 * oct - 69) / 12);
    if (!(freq > 4 && freq <= sampleRate * 0.45)) return;
    const inc = freq / sampleRate;
    const square = (p['sub.shape'] | 0) === 1;
    let ph = this.subPhase;
    if (square) {
      for (let i = 0; i < n; i++) {
        let s = ph < 0.5 ? 1 : -1;
        // polyblep both edges
        if (ph < inc) { const t = ph / inc; s += -(t * t) + 2 * t - 1; }
        else if (ph > 0.5 && ph < 0.5 + inc) { const t = (ph - 0.5) / inc; s -= -(t * t) + 2 * t - 1; }
        const v = s * gain * 0.8;
        tmpL[off + i] += v; tmpR[off + i] += v;
        ph += inc; if (ph >= 1) ph -= 1;
      }
    } else {
      for (let i = 0; i < n; i++) {
        const v = Math.sin(ph * 2 * Math.PI) * gain * 1.2;
        tmpL[off + i] += v; tmpR[off + i] += v;
        ph += inc; if (ph >= 1) ph -= 1;
      }
    }
    this.subPhase = ph;
  }

  // ---------- LFO (bar-locked while playing) ----------
  lfoValue() {
    const p = this.p;
    const bpm = Math.max(60, Math.min(200, p['seq.bpm'] || 138));
    const cpb = LFO_DIV_F[p['lfo.rate'] | 0] || 2;
    const phase = ((this.songPos / sampleRate) * (bpm / 60) * cpb) % 1;
    const shape = p['lfo.shape'] | 0;
    switch (shape) {
      case 1: return 1 - 4 * Math.abs(phase - 0.5); // tri
      case 2: return 1 - 2 * phase; // saw (falling)
      case 3: return phase < 0.5 ? 1 : -1; // sqr
      case 4: { // s&h
        const step = Math.floor((this.songPos / sampleRate) * (bpm / 60) * cpb);
        if (step !== this.shPhase) { this.shPhase = step; this.shVal = Math.random() * 2 - 1; }
        return this.shVal;
      }
      default: return Math.sin(phase * 2 * Math.PI);
    }
  }

  // ---------- filter ----------
  setupFilter(noteAbs) {
    const p = this.p;
    const accAmt = Math.min(1, Math.max(0, p['acc.amt']));
    const accBoost = this.acc ? accAmt : 0;

    // filter AD env — accent raises the peak and shortens the decay
    const att = Math.max(1, p['fenv.att'] * sampleRate);
    const dec = Math.max(1, p['fenv.dec'] * sampleRate * (1 - ACC_DEC_SHORTEN * accBoost));
    let env;
    if (this.fenvT < att) env = this.fenvT / att;
    else env = Math.exp(-4.5 * (this.fenvT - att) / dec);
    env *= 1 + accBoost;
    this.fenvVal = env;

    const lfo = (this.playing || this.clip) ? this.lfoValue() * Math.min(1, Math.max(0, p['lfo.depth'])) : 0;
    const track = Math.min(1, Math.max(0, p['flt.track']));
    const key = ((noteAbs - KEYTRACK_REF) / 12) * track;
    const oct = p['flt.env'] * env * FENV_OCT + lfo * LFO_OCT + key;

    let fc = p['flt.cut'] * Math.pow(2, oct);
    if (!Number.isFinite(fc)) fc = 20;
    fc = Math.min(sampleRate * 0.45, Math.max(20, fc));
    if (this.cutSm <= 0) this.cutSm = fc;
    this.cutSm += (fc - this.cutSm) * 0.5;
    this.curCut = this.cutSm;
    const res = Math.min(0.999, Math.max(0, p['flt.res']));

    const ftype = p['flt.type'] | 0;
    this.ftype = ftype;
    this.twoPole = ftype === 1;
    const g = Math.tan((Math.PI * this.cutSm) / sampleRate);
    const k = 2 - 1.93 * res;
    this.k1 = k;
    this.a1 = 1 / (1 + g * (g + k));
    this.a2 = g * this.a1;
    this.a3 = g * this.a2;
  }

  runFilter(inL, inR, outL, outR, drive, n) {
    if (drive > 0.005) {
      const dg = 1 + drive * 7;
      const dcomp = 1 / Math.pow(dg, 0.55);
      const kF = dcomp / dg;
      let xpL = this.satXL, xpR = this.satXR;
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
      this.satXL = xpL; this.satXR = xpR;
    } else {
      for (let i = 0; i < n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
      if (n > 0) { this.satXL = inL[n - 1]; this.satXR = inR[n - 1]; }
    }

    const ftype = this.ftype;
    const a1 = this.a1, a2 = this.a2, a3 = this.a3, k1 = this.k1;
    const F = this.svf;
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
    if (this.twoPole) {
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

  // ---------- render ----------
  renderVoice(L, R, off, n) {
    const p = this.p;
    if (this.ampStage === 0 && !this.gate) {
      // still advance the LFO clock
      return;
    }
    const tmpL = this.tmpL, tmpR = this.tmpR;
    tmpL.fill(0, 0, n); tmpR.fill(0, 0, n);

    // glide: one-pole approach of semiTarget with time-constant slide.time
    const tau = Math.max(0.005, p['slide.time']) * sampleRate;
    const gk16 = 1 - Math.exp(-16 / tau);

    for (let at = 0; at < n; at += 16) {
      const count = Math.min(16, n - at);
      if (this.semi !== this.semiTarget) {
        this.semi += (this.semiTarget - this.semi) * gk16;
        if (Math.abs(this.semiTarget - this.semi) < 0.001) this.semi = this.semiTarget;
      }
      const noteRootAbs = ROOT_MIDI + this.semi;
      const noteAbs = noteRootAbs + p['osc.tune'] + p['osc.fine'] / 100;
      if (this.setupOsc(noteAbs)) this.renderOsc(tmpL, tmpR, at, count);
      this.renderSub(tmpL, tmpR, at, count, noteRootAbs);
    }

    this.setupFilter(ROOT_MIDI + this.semi + p['osc.tune']);
    this.runFilter(tmpL, tmpR, this.fL, this.fR, p['flt.drive'], n);
    this.fenvT += n;

    // amp ADSR + accent gain
    const attK = 1 / Math.max(1, p['aenv.att'] * sampleRate);
    const sus = Math.min(1, Math.max(0, p['aenv.sus']));
    const decK = 1 - Math.exp(-4.5 / Math.max(1, p['aenv.dec'] * sampleRate));
    const relK = 1 - Math.exp(-4.5 / Math.max(1, p['aenv.rel'] * sampleRate));
    const accAmt = Math.min(1, Math.max(0, p['acc.amt']));
    const gain = this.vel * (1 + (this.acc ? accAmt * ACC_GAIN : 0)) * 0.9;

    for (let i = 0; i < n; i++) {
      switch (this.ampStage) {
        case 1:
          this.ampLevel += attK;
          if (this.ampLevel >= 1) { this.ampLevel = 1; this.ampStage = 2; }
          break;
        case 2:
          this.ampLevel += (sus - this.ampLevel) * decK;
          break;
        case 3:
          this.ampLevel += (0 - this.ampLevel) * relK;
          if (this.ampLevel < 1e-4) { this.ampLevel = 0; this.ampStage = 0; }
          break;
        default: this.ampLevel = 0;
      }
      const amp = this.ampLevel * gain;
      const sl = this.fL[i] * amp, sr = this.fR[i] * amp;
      const yL = sl - this.dcxL + DC_R * this.dcyL;
      const yR = sr - this.dcxR + DC_R * this.dcyR;
      this.dcxL = sl; this.dcyL = yL;
      this.dcxR = sr; this.dcyR = yR;
      L[off + i] += yL;
      R[off + i] += yR;
    }
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
        if (this.samplesToNext <= 0) this.fireStep();
        run = Math.min(run, Math.ceil(this.samplesToNext));
      }
      if (this.samplesToGateOff >= 0) {
        run = Math.min(run, Math.max(1, Math.ceil(this.samplesToGateOff)));
      }
      this.renderVoice(L, R, pos, run);
      if (standalone) this.samplesToNext -= run;
      if (standalone || this.clip) {
        this.songPos += run; // the bar-locked LFO tracks either transport
      }
      if (this.samplesToGateOff >= 0) {
        this.samplesToGateOff -= run;
        if (this.samplesToGateOff <= 0) {
          this.release();
          this.samplesToGateOff = -1;
        }
      }
      pos += run;
    }

    this.vizCount += n;
    if (this.vizCount >= 2048) {
      this.vizCount = 0;
      this.port.postMessage({
        t: 'viz',
        pos: this.ampStage !== 0 ? this.posSm : -1,
        env: this.ampLevel,
        fenv: this.ampStage !== 0 ? (this.fenvVal || 0) : 0,
        cut: this.ampStage !== 0 ? this.curCut : -1,
        gate: this.gate,
        semi: this.ampStage !== 0 ? Math.round(this.semiTarget) : -100,
      });
    }
    return true;
  }
}

registerProcessor('fable-bl', BassProcessor);
