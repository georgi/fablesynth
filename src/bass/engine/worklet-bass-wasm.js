// FableSynth BL-1 — AudioWorklet host for the C++ engine compiled to
// WebAssembly (juce/source/bass/wasm/bass_wasm.cpp, docs/wasm-bl1.md).
// Proof of concept, selected with /bass/?engine=wasm. Self-contained (no
// imports): Vite loads worklets with ?url.
//
// The message protocol matches worklet-bass.js, so the store and UI do not
// change. The main thread sends the compiled module bytes in 'init' as
// `wasm`; messages that arrive before instantiation finishes are queued.
//
// In:  init p tables pats chain play stop noteon noteoff panic arp scale
//      sequenceRhythm dynamics echo reverb
// Out: latency step viz dynamics echo reverb
// Not implemented: the hosted SQ-4 messages (host tempo clip clipstop
//      clipupdate auto) are ignored, and no clipstart/clipstop/pos is sent.

const BLOCK = 128;
const STEPS = 16;
// FxTelemetry::Field order (juce/source/dsp/FxTelemetry.h).
const T = {
  ottIn: 0, ottOut: 1, compIn: 2, compOut: 3, echoL: 4, echoR: 5, verbL: 6, verbR: 7,
  ottLow: 8, gainLow: 11, ottAuto: 14, compAuto: 15, reduction: 16, time: 17,
  driftL: 18, driftR: 19, correlation: 20,
};

class BassWasmProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null;        // wasm exports once ready
    this.queue = [];
    this.ids = new Map(); // 'flt.cut' -> flat id, read from BassParams in wasm
    this.lastStep = -1; this.lastPat = -1;
    this.vizCount = 0;
    this.meter = { dynamics: false, echo: false, reverb: false };
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  boot(bytes) {
    // Synchronous compile: the bytes are small (~200 kB) and this runs once,
    // on a message, before any audio is rendered through the module.
    const inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), {});
    const x = inst.exports;
    x._initialize();
    x.bl_create();
    x.bl_prepare(sampleRate);
    const mem = x.memory.buffer; // fixed size, never detaches
    this.u8 = new Uint8Array(mem);
    this.outL = new Float32Array(mem, x.bl_out_l(), BLOCK);
    this.outR = new Float32Array(mem, x.bl_out_r(), BLOCK);
    this.tele = new Float32Array(mem, x.bl_telemetry_ptr(), x.bl_telemetry_count());
    this.pats = new Uint8Array(mem, x.bl_patterns_ptr(), 4 * STEPS * 3);
    this.chainBuf = new Int32Array(mem, x.bl_chain_ptr(), 64);
    this.arpMax = x.bl_arp_max();
    this.arpNotes = new Int32Array(mem, x.bl_arp_notes_ptr(), this.arpMax);
    this.arpFlags = new Uint8Array(mem, x.bl_arp_flags_ptr(), 48);
    this.scaleBuf = new Int8Array(mem, x.bl_scale_ptr(), 64 * 12);
    for (let i = 0, n = x.bl_param_count(); i < n; i++) this.ids.set(this.cstr(x.bl_param_name(i)), i);
    this.x = x;
    this.port.postMessage({ t: 'latency', n: x.bl_latency() });
    const q = this.queue; this.queue = [];
    for (const d of q) this.onMsg(d);
  }

  cstr(ptr) {
    let end = ptr;
    while (this.u8[end]) end++;
    let s = '';
    for (let i = ptr; i < end; i++) s += String.fromCharCode(this.u8[i]);
    return s;
  }

  setParam(k, v) {
    const id = this.ids.get(k);
    if (id !== undefined && Number.isFinite(v)) this.x.bl_set_param(id, v);
  }

  onMsg(d) {
    if (d.t === 'dynamics' || d.t === 'echo' || d.t === 'reverb') { this.meter[d.t] = !!d.on; return; }
    if (!this.x) {
      if (d.t === 'init' && d.wasm) this.boot(d.wasm); else { this.queue.push(d); return; }
    }
    const x = this.x;
    switch (d.t) {
      case 'init':
        for (const k in d.params) this.setParam(k, d.params[k]);
        x.bl_snap_params(); // a patch load is not a glide
        break;
      case 'p': this.setParam(d.k, d.v); break;
      case 'tables':
        d.list.forEach((t, i) => {
          const ptr = x.bl_table_alloc(i, t.frames, t.mips, t.size);
          if (ptr) new Float32Array(x.memory.buffer, ptr, t.frames * t.mips * t.size).set(new Float32Array(t.buf));
        });
        x.bl_tables_commit(d.list.length);
        break;
      case 'pats': this.pats.set(new Uint8Array(d.data).subarray(0, this.pats.length)); x.bl_set_patterns(); break;
      case 'chain':
        if (Array.isArray(d.list) && d.list.length) {
          const n = Math.min(d.list.length, this.chainBuf.length);
          for (let i = 0; i < n; i++) this.chainBuf[i] = d.list[i] | 0;
          x.bl_set_chain(n);
        }
        break;
      case 'play': x.bl_play(); this.lastStep = -1; break;
      case 'stop': x.bl_stop(); this.lastStep = -1; break;
      case 'noteon': x.bl_key_on(d.semi | 0, Number.isFinite(d.vel) ? d.vel : 0.72); break;
      case 'noteoff': x.bl_key_off(d.semi | 0); break;
      case 'panic': x.bl_panic(); break;
      case 'arp': this.setArp(d.config); break;
      case 'scale': this.setScale(d.map); break;
      case 'sequenceRhythm': {
        const r = d.rhythm;
        if (r == null) { x.bl_set_rhythm(0, 0, 16, 0, 4); break; }
        if (r.v !== 1 || typeof r.enabled !== 'boolean' || !['grid', 'fit'].includes(r.mode)) break;
        x.bl_set_rhythm(r.enabled ? 1 : 0, r.mode === 'fit' ? 1 : 0, r.steps | 0, r.rotation | 0, r.cycleBeats | 0);
        break;
      }
      default: break; // hosted SQ-4 messages: not implemented in this proof of concept
    }
  }

  setArp(a) {
    const x = this.x;
    if (!a) { x.bl_set_arp(0, 0.25, 0.65, 16); return; }
    // Same validation as worklet-bass.js, but the C++ ArpPattern holds at most
    // kMaxArpCycle (192) notes instead of 1024.
    if (!Array.isArray(a.notes) || a.notes.length < STEPS || a.notes.length > this.arpMax || a.notes.length % STEPS ||
      !Array.isArray(a.hits) || !Array.isArray(a.accents) || !Number.isFinite(a.rate) || !Number.isFinite(a.gate)) return;
    a.notes.forEach((n, i) => { this.arpNotes[i] = Number.isInteger(n) && n >= 0 && n <= 127 ? n : -1; });
    for (let i = 0; i < STEPS; i++) {
      this.arpFlags[i] = a.hits[i] ? 1 : 0;
      this.arpFlags[16 + i] = a.accents[i] ? 1 : 0;
      this.arpFlags[32 + i] = Array.isArray(a.slides) && a.slides[i] ? 1 : 0;
    }
    x.bl_set_arp(1, Math.max(.125, Math.min(1, a.rate)), Math.max(.05, Math.min(.95, a.gate)), a.notes.length);
  }

  setScale(m) {
    const ok = Array.isArray(m) && m.length > 0 && m.length <= 64 &&
      m.every(t => Array.isArray(t) && t.length === 12 && t.every(v => Number.isInteger(v) && v > -12 && v < 12));
    if (ok) m.forEach((t, i) => this.scaleBuf.set(t, i * 12));
    this.x.bl_set_scale(ok ? m.length : 0);
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const L = out[0];
    const x = this.x;
    if (!x) { for (const ch of out) ch.fill(0); return true; }
    const n = Math.min(L.length, BLOCK);
    x.bl_render(n);
    L.set(n === BLOCK ? this.outL : this.outL.subarray(0, n));
    if (out.length > 1) out[1].set(n === BLOCK ? this.outR : this.outR.subarray(0, n));
    else for (let i = 0; i < n; i++) L[i] = 0.5 * (L[i] + this.outR[i]);

    const step = x.bl_current_step(), pat = x.bl_current_pattern();
    if (step >= 0 && (step !== this.lastStep || pat !== this.lastPat)) {
      const info = x.bl_step_info(pat, step), on = !!(info & 1);
      this.port.postMessage({ t: 'step', s: step, pat, semi: (info >> 8) - 128, acc: on && !!(info & 2), slide: on && !!(info & 4) });
    }
    this.lastStep = step; this.lastPat = pat;

    if ((this.meter.dynamics || this.meter.echo || this.meter.reverb) && x.bl_telemetry_take()) this.postTelemetry();

    this.vizCount += n;
    if (this.vizCount >= 2048) {
      this.vizCount = 0;
      this.port.postMessage({
        t: 'viz', pos: x.bl_viz(0), env: x.bl_viz(1), fenv: x.bl_viz(2), cut: x.bl_viz(3),
        gate: x.bl_viz(4) > 0, semi: x.bl_viz(5),
      });
    }
    return true;
  }

  postTelemetry() {
    const v = this.tele;
    if (this.meter.reverb) this.port.postMessage({ t: 'reverb', left: v[T.verbL], right: v[T.verbR], correlation: v[T.correlation] });
    // The native meter does not measure the delay input; -90 dB marks it unknown.
    if (this.meter.echo) this.port.postMessage({ t: 'echo', input: -90, left: v[T.echoL], right: v[T.echoR], time: v[T.time], driftL: v[T.driftL], driftR: v[T.driftR] });
    if (this.meter.dynamics) this.port.postMessage({ t: 'dynamics',
      ott: { input: v[T.ottIn], output: v[T.ottOut], levels: [v[T.ottLow], v[T.ottLow + 1], v[T.ottLow + 2]], gains: [v[T.gainLow], v[T.gainLow + 1], v[T.gainLow + 2]], makeup: v[T.ottAuto] },
      comp: { input: v[T.compIn], output: v[T.compOut], reduction: v[T.reduction], trace: [], makeup: v[T.compAuto] },
    });
  }
}

registerProcessor('fable-bl-wasm', BassWasmProcessor);
