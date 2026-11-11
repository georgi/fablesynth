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
//      sequenceRhythm dynamics echo reverb, and the hosted SQ-4 messages
//      host tempo clip clipstop clipupdate auto (docs/wasm-sq4.md)
// Out: latency step viz dynamics echo reverb clipstart clipstop pos

const BLOCK = 128;
const STEPS = 16;
// Same validation as worklet-bass.js readSequenceRhythm.
function readSequenceRhythm(r) {
  if (!r || r.v !== 1 || typeof r.enabled !== 'boolean' || !['grid', 'fit'].includes(r.mode)
    || !Number.isInteger(r.steps) || r.steps < 1 || r.steps > 16
    || !Number.isInteger(r.rotation) || r.rotation < 0 || r.rotation >= r.steps
    || ![4, 8].includes(r.cycleBeats)) return null;
  return r;
}
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
    // Hosted SQ-4 clips (docs/wasm-sq4.md). The engine's ClipHost owns the
    // transport; this mirrors its two slots so a later 'auto' or a
    // 'clipupdate' without automation can reach the slot worklet-bass.js would.
    this.hosted = false;
    this.tempo = { bpm: 120, swing: 0, anchor: 0 };
    this.hPend = null; this.hLive = null; // { d, lanes, tag }
    this.deferred = null;                 // a clip/clipupdate waiting for its 'auto'
    this.tag = 0;
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
    this.clipBuf = new Uint8Array(mem, x.bl_clip_ptr(), x.bl_clip_max());
    this.events = new Float64Array(mem, x.bl_events_ptr(), 64 * 5);
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
    if (this.deferred && d.t !== 'auto') this.flush();
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
      case 'panic':
        x.bl_panic();
        if (!d.preserveTransport) { x.bl_host_clear(); this.hPend = this.hLive = null; }
        break;
      case 'host':
        this.hosted = !!d.on; x.bl_host(this.hosted ? 1 : 0);
        if (!this.hosted) this.hPend = this.hLive = null;
        break;
      case 'tempo': {
        const t = this.tempo;
        if (Number.isFinite(d.bpm)) t.bpm = d.bpm;
        if (Number.isFinite(d.swing)) t.swing = d.swing;
        if (Number.isFinite(d.anchor)) t.anchor = d.anchor;
        x.bl_host_tempo(t.bpm, t.swing, t.anchor);
        break;
      }
      case 'clip':
        this.hPend = { d, lanes: null, tag: ++this.tag };
        this.deferred = { slot: this.hPend, update: false, lanes: undefined };
        break;
      case 'clipupdate': {
        const slot = this.hPend || this.hLive;
        if (!slot) break;
        slot.d = { ...d, atFrame: slot.d.atFrame };
        this.deferred = { slot, update: true, lanes: undefined };
        break;
      }
      case 'auto':
        if (this.deferred) { this.deferred.lanes = d.lanes; this.flush(); break; }
        // A late 'auto' re-sends the slot it belongs to, as worklet-bass.js attaches it.
        if (this.hPend) { this.hPend.lanes = d.lanes; this.hostCall(this.hPend, false); }
        else if (this.hLive) { this.hLive.lanes = d.lanes; this.hostCall(this.hLive, true); }
        break;
      case 'clipstop':
        this.hPend = null;
        x.bl_host_stop(+d.atFrame || 0);
        break;
      case 'arp': this.setArp(d.config); break;
      case 'scale': this.setScale(d.map); break;
      case 'sequenceRhythm': {
        const r = d.rhythm;
        if (r == null) { x.bl_set_rhythm(0, 0, 16, 0, 4); break; }
        if (r.v !== 1 || typeof r.enabled !== 'boolean' || !['grid', 'fit'].includes(r.mode)) break;
        x.bl_set_rhythm(r.enabled ? 1 : 0, r.mode === 'fit' ? 1 : 0, r.steps | 0, r.rotation | 0, r.cycleBeats | 0);
        break;
      }
      default: break;
    }
  }

  flush() {
    const f = this.deferred;
    this.deferred = null;
    if (f.lanes !== undefined) f.slot.lanes = f.lanes;
    this.hostCall(f.slot, f.update);
  }

  // One engine hostClip/hostClipUpdate call for a slot (worklet-bass.js 'clip').
  hostCall(slot, update) {
    const x = this.x, d = slot.d;
    const data = new Uint8Array(d.data);
    const bytes = Math.min(data.length, this.clipBuf.length);
    this.clipBuf.set(data.subarray(0, bytes));
    const arp = this.writeArp(d.arp);
    const r = readSequenceRhythm(d.sequenceRhythm);
    const scale = this.writeScale(d.scale);
    x.bl_host_clip(update ? 1 : 0, bytes, Math.max(1, d.bars | 0), +d.atFrame || 0, slot.tag,
      arp ? 1 : 0, arp ? arp.rate : 0.25, arp ? arp.gate : 0.65, arp ? arp.n : 16,
      r?.enabled ? 1 : 0, r?.mode === 'fit' ? 1 : 0, r ? r.steps : 16, r ? r.rotation : 0, r ? r.cycleBeats : 4,
      scale, this.writeAuto(slot.lanes));
  }

  // Stages automation tables; returns the lane count, or -1 for none.
  writeAuto(list) {
    if (!Array.isArray(list)) return -1;
    const x = this.x, max = x.bl_auto_max_lanes();
    let n = 0;
    for (const l of list) {
      if (n >= max) break;
      const id = this.ids.get(l.k), len = Math.max(1, l.len | 0), table = new Float32Array(l.table);
      if (id === undefined || table.length !== len * 16) continue;
      const fx = l.k.includes('fx.') || l.k === 'master.volume';
      const ptr = x.bl_auto_lane(n, id, len, Math.max(0, +l.fit || 0) | 0, (+l.rot || 0) | 0, fx ? 1 : 0);
      if (!ptr) continue;
      new Float32Array(x.memory.buffer, ptr, table.length).set(table);
      n++;
    }
    return n;
  }

  // Engine events of the last render: clipstart / clipstop / pos.
  drainEvents() {
    const x = this.x, e = this.events;
    for (let n = x.bl_events_take(); n > 0; n = x.bl_events_take()) {
      for (let i = 0; i < n; i++) {
        const o = i * 5, type = e[o];
        if (type === 0) {
          if (this.hPend && this.hPend.tag === e[o + 4]) { this.hLive = this.hPend; this.hPend = null; }
          this.port.postMessage({ t: 'clipstart', frame: e[o + 1] });
        } else if (type === 1) {
          this.hLive = null;
          this.port.postMessage({ t: 'clipstop', frame: e[o + 1] });
        } else this.port.postMessage({ t: 'pos', step: e[o + 2], bar: e[o + 3] });
      }
    }
  }

  setArp(a) {
    if (!a) { this.x.bl_set_arp(0, 0.25, 0.65, 16); return; }
    const w = this.writeArp(a);
    if (w) this.x.bl_set_arp(1, w.rate, w.gate, w.n);
  }

  // Writes a valid arp into the arp buffers. Same validation as worklet-bass.js,
  // but the C++ ArpPattern holds at most kMaxArpCycle (192) notes instead of 1024.
  writeArp(a) {
    if (!a || !Array.isArray(a.notes) || a.notes.length < STEPS || a.notes.length > this.arpMax || a.notes.length % STEPS ||
      !Array.isArray(a.hits) || !Array.isArray(a.accents) || !Number.isFinite(a.rate) || !Number.isFinite(a.gate)) return null;
    a.notes.forEach((n, i) => { this.arpNotes[i] = Number.isInteger(n) && n >= 0 && n <= 127 ? n : -1; });
    for (let i = 0; i < STEPS; i++) {
      this.arpFlags[i] = a.hits[i] ? 1 : 0;
      this.arpFlags[16 + i] = a.accents[i] ? 1 : 0;
      this.arpFlags[32 + i] = Array.isArray(a.slides) && a.slides[i] ? 1 : 0;
    }
    return { rate: Math.max(.125, Math.min(1, a.rate)), gate: Math.max(.05, Math.min(.95, a.gate)), n: a.notes.length };
  }

  setScale(m) { this.x.bl_set_scale(this.writeScale(m)); }

  // Writes a valid scale map into the scale buffer; returns its length (0 = none).
  writeScale(m) {
    const ok = Array.isArray(m) && m.length > 0 && m.length <= 64 &&
      m.every(t => Array.isArray(t) && t.length === 12 && t.every(v => Number.isInteger(v) && v > -12 && v < 12));
    if (ok) m.forEach((t, i) => this.scaleBuf.set(t, i * 12));
    return ok ? m.length : 0;
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const L = out[0];
    const x = this.x;
    if (!x) { for (const ch of out) ch.fill(0); return true; }
    if (this.deferred) this.flush();
    const n = Math.min(L.length, BLOCK);
    x.bl_render(n, typeof currentFrame === 'number' ? currentFrame : 0);
    if (this.hosted) this.drainEvents();
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
