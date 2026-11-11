// FableSynth WT-1 — AudioWorklet host for the C++ engine compiled to
// WebAssembly (juce/source/dsp/wasm/wt_wasm.cpp, docs/wasm-wt1.md).
// Selected with ?engine=wasm. Self-contained (no imports): Vite loads
// worklets with ?url.
//
// The message protocol matches worklet.js, so the store and UI do not change.
// The main thread sends the compiled module bytes in 'init' as `wasm`;
// messages that arrive before instantiation finishes are queued.
//
// In:  init p tables table tablecount on off bend bpm pats chain play stop
//      panic arp scale sequenceRhythm seqauto dynamics echo reverb, and the
//      hosted SQ-4 messages host tempo clip clipstop clipupdate auto
//      (docs/wasm-sq4.md)
// Out: latency anchor step viz mod dynamics echo reverb clipstart clipstop pos
// Not implemented: the side-chain key input is not read.

const BLOCK = 128;
// FxTelemetry::Field order (juce/source/dsp/FxTelemetry.h).
const T = {
  ottIn: 0, ottOut: 1, compIn: 2, compOut: 3, echoL: 4, echoR: 5, verbL: 6, verbR: 7,
  ottLow: 8, gainLow: 11, ottAuto: 14, compAuto: 15, reduction: 16, time: 17,
  driftL: 18, driftR: 19, correlation: 20,
};

// Same validation as worklet.js readSequenceRhythm.
function readSequenceRhythm(r) {
  if (!r || r.v !== 1 || typeof r.enabled !== 'boolean' || !['grid', 'fit'].includes(r.mode)
    || !Number.isInteger(r.steps) || r.steps < 1 || r.steps > 16
    || !Number.isInteger(r.rotation) || r.rotation < 0 || r.rotation >= r.steps
    || ![4, 8].includes(r.cycleBeats)) return null;
  return r;
}

class WtWasmProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null;        // wasm exports once ready
    this.queue = [];
    this.ids = new Map(); // 'oscA.pos' -> flat id, read from Params.cpp in wasm
    this.bpm = 120;
    this.lastStep = -1; this.lastPat = -1;
    this.arpOn = false; this.polyOn = false;
    this.vizCount = 0;
    this.modIdleSent = true;
    this.meter = { dynamics: false, echo: false, reverb: false };
    // Hosted SQ-4 clips (docs/wasm-sq4.md). The engine's ClipHost owns the
    // transport; this mirrors its two slots so a later 'auto' or a
    // 'clipupdate' without automation can reach the slot worklet.js would.
    this.hosted = false;
    this.tempo = { bpm: 120, swing: 0, anchor: 0 };
    this.hPend = null; this.hLive = null; // { d, lanes, tag }
    this.deferred = null;                 // a clip/clipupdate waiting for its 'auto'
    this.tag = 0;
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  boot(bytes) {
    // Synchronous compile: the bytes are small (~240 kB) and this runs once,
    // on a message, before any audio is rendered through the module.
    const inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), {});
    const x = inst.exports;
    x._initialize();
    x.wt_create();
    x.wt_prepare(sampleRate);
    const mem = x.memory.buffer; // fixed size, never detaches
    this.u8 = new Uint8Array(mem);
    this.outL = new Float32Array(mem, x.wt_out_l(), BLOCK);
    this.outR = new Float32Array(mem, x.wt_out_r(), BLOCK);
    this.tele = new Float32Array(mem, x.wt_telemetry_ptr(), x.wt_telemetry_count());
    this.modBuf = new Float32Array(mem, x.wt_mod_ptr(), x.wt_mod_count());
    this.pats = new Uint8Array(mem, x.wt_patterns_ptr(), x.wt_patterns_bytes());
    this.chainBuf = new Int32Array(mem, x.wt_chain_ptr(), 64);
    this.arpMax = x.wt_arp_max();
    this.compMax = x.wt_comp_max_notes();
    this.arpNotes = new Int32Array(mem, x.wt_arp_notes_ptr(), this.arpMax);
    this.arpFlags = new Uint8Array(mem, x.wt_arp_flags_ptr(), 32);
    this.arpStack = new Uint8Array(mem, x.wt_arp_stack_ptr(), this.arpMax * this.compMax);
    this.arpStackCount = new Uint8Array(mem, x.wt_arp_stack_count_ptr(), this.arpMax);
    this.arpLens = new Uint8Array(mem, x.wt_arp_lens_ptr(), this.arpMax);
    this.scaleBuf = new Int8Array(mem, x.wt_scale_ptr(), 64 * 12);
    this.clipBuf = new Uint8Array(mem, x.wt_clip_ptr(), x.wt_clip_max());
    this.events = new Float64Array(mem, x.wt_events_ptr(), 64 * 5);
    for (let i = 0, n = x.wt_param_count(); i < n; i++) this.ids.set(this.cstr(x.wt_param_name(i)), i);
    this.seqBpmId = this.ids.get('seq.bpm');
    this.x = x;
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

  setBpm(v) { this.bpm = Math.min(1000, Math.max(1, v || 120)); this.x.wt_set_bpm(this.bpm); }

  // Non-finite values are dropped, as in worklet.js.
  setParam(k, v) {
    const id = this.ids.get(k);
    if (id === undefined || !Number.isFinite(v)) return;
    this.x.wt_set_param(id, v);
    if (id === this.seqBpmId) this.setBpm(v);
  }

  putTable(i, t) {
    const x = this.x;
    const ptr = x.wt_table_alloc(i, t.frames, t.mips, t.size);
    if (ptr) new Float32Array(x.memory.buffer, ptr, t.frames * t.mips * t.size).set(new Float32Array(t.buf));
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
        for (const k in d.params) {
          const id = this.ids.get(k), v = d.params[k];
          if (id !== undefined && Number.isFinite(v)) x.wt_set_param(id, v);
        }
        this.setBpm(d.params?.['seq.bpm']);
        this.port.postMessage({ t: 'latency', n: x.wt_latency() });
        break;
      case 'p': this.setParam(d.k, d.v); break;
      case 'tables':
        x.wt_table_count(0);
        d.list.forEach((t, i) => this.putTable(i, t));
        x.wt_tables_commit();
        break;
      case 'table': {
        const i = d.i | 0;
        if (i >= 0 && i < 256) { this.putTable(i, d); x.wt_tables_commit(); }
        break;
      }
      case 'tablecount': x.wt_table_count(d.n | 0); x.wt_tables_commit(); break;
      case 'on': x.wt_note_on(d.n | 0, Number.isFinite(d.v) ? d.v : 1); break;
      case 'off': x.wt_note_off(d.n | 0); break;
      case 'bend': if (Number.isFinite(d.s)) x.wt_bend(d.s); break;
      case 'bpm': this.setBpm(d.v > 1 ? d.v : 120); break;
      case 'pats': this.pats.set(new Uint8Array(d.data).subarray(0, this.pats.length)); x.wt_set_patterns(); break;
      case 'chain':
        if (Array.isArray(d.list) && d.list.length) {
          const n = Math.min(d.list.length, this.chainBuf.length);
          for (let i = 0; i < n; i++) this.chainBuf[i] = d.list[i] | 0;
          x.wt_set_chain(n);
        }
        break;
      case 'play':
        x.wt_play(); this.lastStep = -1;
        this.port.postMessage({ t: 'anchor', frame: typeof currentFrame === 'number' ? currentFrame : 0 });
        break;
      case 'stop': x.wt_stop(); this.lastStep = -1; break;
      case 'panic':
        x.wt_panic();
        if (!d.preserveTransport) { x.wt_host_clear(); this.hPend = this.hLive = null; }
        break;
      case 'seqauto': x.wt_auto_commit(Math.max(0, this.writeAuto(Array.isArray(d.lanes) ? d.lanes : []))); break;
      case 'host':
        this.hosted = !!d.on; x.wt_host(this.hosted ? 1 : 0);
        if (!this.hosted) this.hPend = this.hLive = null;
        break;
      case 'tempo': {
        const t = this.tempo;
        if (Number.isFinite(d.bpm)) { t.bpm = d.bpm; this.setBpm(d.bpm); }
        if (Number.isFinite(d.swing)) t.swing = d.swing;
        if (Number.isFinite(d.anchor)) t.anchor = d.anchor;
        x.wt_host_tempo(t.bpm, t.swing, t.anchor);
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
        // A late 'auto' re-sends the slot it belongs to, as worklet.js attaches it.
        if (this.hPend) { this.hPend.lanes = d.lanes; this.hostCall(this.hPend, false); }
        else if (this.hLive) { this.hLive.lanes = d.lanes; this.hostCall(this.hLive, true); }
        break;
      case 'clipstop':
        this.hPend = null;
        x.wt_host_stop(+d.atFrame || 0);
        break;
      case 'arp': this.setArp(d.config); break;
      case 'scale': this.setScale(d.map); break;
      case 'sequenceRhythm': {
        const r = readSequenceRhythm(d.rhythm);
        if (d.rhythm != null && !r) break;
        this.polyOn = !!r?.enabled;
        if (!r) x.wt_set_rhythm(0, 0, 16, 0, 4);
        else x.wt_set_rhythm(r.enabled ? 1 : 0, r.mode === 'fit' ? 1 : 0, r.steps, r.rotation, r.cycleBeats);
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

  // One engine hostClip/hostClipUpdate call for a slot (worklet.js 'clip').
  hostCall(slot, update) {
    const x = this.x, d = slot.d;
    const data = new Uint8Array(d.data);
    const bytes = Math.min(data.length, this.clipBuf.length);
    this.clipBuf.set(data.subarray(0, bytes));
    const a = this.writeArp(d.arp);
    const r = readSequenceRhythm(d.sequenceRhythm);
    const scale = this.writeScale(d.scale);
    x.wt_host_clip(update ? 1 : 0, bytes, Math.max(1, d.bars | 0), +d.atFrame || 0, slot.tag,
      a ? 1 : 0, a ? a.rate : 0.25, a ? a.gate : 0.65, a ? a.n : 16, a?.comp ? 1 : 0, a ? a.strum : 0,
      r?.enabled ? 1 : 0, r?.mode === 'fit' ? 1 : 0, r ? r.steps : 16, r ? r.rotation : 0, r ? r.cycleBeats : 4,
      scale, this.writeAuto(slot.lanes));
  }

  // Stages automation tables; returns the lane count, or -1 for none.
  writeAuto(list) {
    if (!Array.isArray(list)) return -1;
    const x = this.x, max = x.wt_auto_max_lanes();
    let n = 0;
    for (const l of list) {
      if (n >= max) break;
      const id = this.ids.get(l.k), len = Math.max(1, l.len | 0), table = new Float32Array(l.table);
      if (id === undefined || table.length !== len * 16) continue;
      const fx = l.k.includes('fx.') || l.k === 'master.volume';
      const ptr = x.wt_auto_lane(n, id, len, Math.max(0, +l.fit || 0) | 0, (+l.rot || 0) | 0, fx ? 1 : 0);
      if (!ptr) continue;
      new Float32Array(x.memory.buffer, ptr, table.length).set(table);
      n++;
    }
    return n;
  }

  // Engine events of the last render: clipstart / clipstop / pos.
  drainEvents() {
    const x = this.x, e = this.events;
    for (let n = x.wt_events_take(); n > 0; n = x.wt_events_take()) {
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
    const x = this.x;
    if (!a) { this.arpOn = false; x.wt_set_arp(0, 0.25, 0.65, 16, 0, 0); return; }
    const w = this.writeArp(a);
    if (!w) return;
    this.arpOn = true;
    x.wt_set_arp(1, w.rate, w.gate, w.n, w.comp ? 1 : 0, w.strum);
  }

  // Writes a valid arp into the arp buffers. Same validation as worklet.js,
  // but the C++ ArpPattern holds at most kMaxArpCycle (192) notes instead of 1024.
  writeArp(a) {
    if (!a || !Array.isArray(a.notes) || a.notes.length < 16 || a.notes.length > this.arpMax || a.notes.length % 16 ||
      !Array.isArray(a.hits) || !Array.isArray(a.accents) || !Number.isFinite(a.rate) || !Number.isFinite(a.gate)) return null;
    const n = a.notes.length;
    a.notes.forEach((v, i) => { this.arpNotes[i] = Number.isInteger(v) && v >= 0 && v <= 127 ? v : -1; });
    for (let i = 0; i < 16; i++) {
      this.arpFlags[i] = a.hits[i] ? 1 : 0;
      this.arpFlags[16 + i] = a.accents[i] ? 1 : 0;
    }
    // worklet.js readCompStack: a stack and lens of the cycle length make a comp.
    const comp = Array.isArray(a.stack) && Array.isArray(a.lens) && a.stack.length === n && a.lens.length === n;
    if (comp) {
      for (let s = 0; s < n; s++) {
        const st = Array.isArray(a.stack[s]) ? a.stack[s].filter(v => Number.isInteger(v) && v >= 0 && v <= 127).slice(0, this.compMax) : [];
        this.arpStackCount[s] = st.length;
        st.forEach((v, k) => { this.arpStack[s * this.compMax + k] = v; });
        const l = a.lens[s];
        this.arpLens[s] = Number.isInteger(l) && l >= 0 && l <= n ? l : 1;
      }
    }
    const strum = comp && Number.isFinite(a.strum) ? Math.max(0, Math.min(60, a.strum)) : 0;
    return { rate: Math.max(.125, Math.min(1, a.rate)), gate: Math.max(.05, Math.min(.95, a.gate)), n, comp, strum };
  }

  setScale(m) { this.x.wt_set_scale(this.writeScale(m)); }

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
    x.wt_render(n, typeof currentFrame === 'number' ? currentFrame : 0);
    if (this.hosted) this.drainEvents();
    L.set(n === BLOCK ? this.outL : this.outL.subarray(0, n));
    if (out.length > 1) out[1].set(n === BLOCK ? this.outR : this.outR.subarray(0, n));
    else for (let i = 0; i < n; i++) L[i] = 0.5 * (L[i] + this.outR[i]);

    const step = x.wt_current_step();
    const pat = this.arpOn || this.polyOn ? 0 : x.wt_current_pattern();
    if (step >= 0 && (step !== this.lastStep || pat !== this.lastPat)) this.port.postMessage({ t: 'step', s: step, pat });
    this.lastStep = step; this.lastPat = pat;

    if ((this.meter.dynamics || this.meter.echo || this.meter.reverb) && x.wt_telemetry_take()) this.postTelemetry();

    this.vizCount += n;
    if (this.vizCount >= 2048) {
      this.vizCount = 0;
      this.port.postMessage({ t: 'viz', a: x.wt_viz(0), b: x.wt_viz(1), n: x.wt_viz(2) });
      if (x.wt_mod_take()) {
        this.port.postMessage({ t: 'mod', d: this.modBuf.slice() });
        this.modIdleSent = false;
      } else if (!this.modIdleSent) {
        this.modIdleSent = true;
        this.port.postMessage({ t: 'mod', d: null });
      }
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

registerProcessor('fable-wt-wasm', WtWasmProcessor);
