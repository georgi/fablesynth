// FableSynth DR-1 — AudioWorklet host for the C++ engine compiled to
// WebAssembly (juce/source/drum/wasm/drum_wasm.cpp, docs/wasm-dr1.md).
// Proof of concept, selected with /drum/?engine=wasm. Self-contained (no
// imports): Vite loads worklets with ?url.
//
// The message protocol matches worklet-drum.js, so the store and UI do not
// change. The main thread sends the compiled module bytes in 'init' as
// `wasm`; messages that arrive before instantiation finishes are queued.
//
// In:  init p tables trig pats seq rhythm chain play stop sel panic seqauto
//      dynamics echo reverb padCapture meterPad, and the hosted SQ-4
//      messages host tempo clip clipstop clipupdate auto (docs/wasm-sq4.md)
// Out: latency anchor step viz padAudio dynamics echo reverb clipstart
//      clipstop pos
// Ignored: 'samples' and 'seed' (the module embeds the same one-shot bank and
//      its own RNG seed). No 'poly' message is sent; 'viz' carries the lanes.

const BLOCK = 128;
const NPADS = 16;
const BUSES = 5;
const PATTERN_BYTES = 4 * NPADS * 16;
const LANE_INTS = 10 + 256;
const CAPTURE_LEN = 4096;
// FxTelemetry::Field order (juce/source/dsp/FxTelemetry.h).
const T = {
  ottIn: 0, ottOut: 1, compIn: 2, compOut: 3, echoL: 4, echoR: 5, verbL: 6, verbR: 7,
  ottLow: 8, gainLow: 11, ottAuto: 14, compAuto: 15, reduction: 16, time: 17,
  driftL: 18, driftR: 19, correlation: 20,
};
const OUT_KEY = /^pad([0-9]|1[0-5])\.out$/;

class DrumWasmProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null;        // wasm exports once ready
    this.queue = [];
    this.ids = new Map(); // 'pad0.flt.cut' -> flat id, read from DrumParams in wasm
    this.padOut = new Int32Array(NPADS);
    this.lastStep = -1; this.lastPat = -1;
    this.vizCount = 0;
    this.sel = 0; this.meterPad = 0;
    this.meter = { dynamics: false, echo: false, reverb: false };
    this.capture = false;
    // Hosted SQ-4 clips (docs/wasm-sq4.md). The engine's ClipHost owns the
    // transport; this mirrors its two slots so a later 'auto' or a
    // 'clipupdate' without automation can reach the slot worklet-drum.js would.
    this.hosted = false;
    this.tempo = { bpm: 120, swing: 0, anchor: 0 };
    this.hPend = null; this.hLive = null; // { d, lanes, tag }
    this.deferred = null;                 // a clip/clipupdate waiting for its 'auto'
    this.tag = 0;
    this.hitMask = 0;
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  boot(bytes) {
    // Synchronous compile, once, on a message, before any audio is rendered
    // through the module. Most of the bytes are the one-shot sample bank.
    const inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), {});
    const x = inst.exports;
    x._initialize();
    x.dr_create();
    x.dr_prepare(sampleRate);
    const mem = x.memory.buffer; // fixed size, never detaches
    this.u8 = new Uint8Array(mem);
    this.outs = [];
    for (let b = 0; b < BUSES; b++)
      this.outs.push([new Float32Array(mem, x.dr_out_ptr(b, 0), BLOCK), new Float32Array(mem, x.dr_out_ptr(b, 1), BLOCK)]);
    this.kick = new Float32Array(mem, x.dr_kick_ptr(), BLOCK);
    this.keyIn = [];
    for (let c = 0, n = x.dr_key_channels(); c < n; c++) this.keyIn.push(new Float32Array(mem, x.dr_key_in_ptr(c), BLOCK));
    this.tele = new Float32Array(mem, x.dr_telemetry_ptr(), x.dr_telemetry_count());
    this.pats = new Uint8Array(mem, x.dr_patterns_ptr(), PATTERN_BYTES);
    this.chainBuf = new Int32Array(mem, x.dr_chain_ptr(), x.dr_chain_max());
    this.rhythmBuf = new Int32Array(mem, x.dr_rhythm_ptr(), NPADS * LANE_INTS);
    this.capBuf = new Float32Array(mem, x.dr_capture_ptr(), CAPTURE_LEN);
    this.clipBuf = new Uint8Array(mem, x.dr_clip_ptr(), x.dr_clip_max());
    this.events = new Float64Array(mem, x.dr_events_ptr(), 64 * 5);
    for (let i = 0, n = x.dr_param_count(); i < n; i++) this.ids.set(this.cstr(x.dr_param_name(i)), i);
    this.x = x;
    this.port.postMessage({ t: 'latency', n: x.dr_latency() });
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
    if (id === undefined || !Number.isFinite(v)) return;
    this.x.dr_set_param(id, v);
    const m = OUT_KEY.exec(k);
    if (m) this.padOut[+m[1]] = Math.max(0, Math.min(BUSES - 1, v | 0));
  }

  onMsg(d) {
    if (d.t === 'dynamics' || d.t === 'echo' || d.t === 'reverb') { this.meter[d.t] = !!d.on; return; }
    if (d.t === 'meterPad') { this.meterPad = Math.max(0, Math.min(NPADS - 1, d.pad | 0)); return; }
    if (!this.x) {
      if (d.t === 'init' && d.wasm) this.boot(d.wasm); else { this.queue.push(d); return; }
    }
    const x = this.x;
    if (this.deferred && d.t !== 'auto') this.flush();
    switch (d.t) {
      case 'init': for (const k in d.params) this.setParam(k, d.params[k]); break;
      case 'p': this.setParam(d.k, d.v); break;
      case 'tables':
        d.list.forEach((t, i) => {
          const ptr = x.dr_table_alloc(i, t.frames, t.mips, t.size);
          if (ptr) new Float32Array(x.memory.buffer, ptr, t.frames * t.mips * t.size).set(new Float32Array(t.buf));
        });
        x.dr_tables_commit(d.list.length);
        break;
      case 'trig': x.dr_trigger(d.pad | 0, Number.isFinite(d.v) ? d.v : 0); break;
      case 'pats': this.setPatterns(d.data); break;
      case 'seq':
        if (d.data) this.setPatterns(d.data);
        this.setChain(d.chain);
        this.setRhythm(d.rhythm ?? d.drumRhythm);
        break;
      case 'rhythm': this.setRhythm(d.rhythm ?? d.drumRhythm); break;
      case 'chain': this.setChain(d.list); break;
      case 'play':
        x.dr_play(); this.lastStep = -1;
        this.port.postMessage({ t: 'anchor', frame: currentFrame });
        break;
      case 'stop': x.dr_stop(); this.lastStep = -1; break;
      case 'sel': this.sel = Math.max(0, Math.min(NPADS - 1, d.pad | 0)); x.dr_select(this.sel); break;
      case 'panic':
        x.dr_panic();
        if (!d.preserveTransport) { x.dr_host_clear(); this.hPend = this.hLive = null; }
        break;
      case 'host':
        this.hosted = !!d.on; x.dr_host(this.hosted ? 1 : 0);
        if (!this.hosted) this.hPend = this.hLive = null;
        break;
      case 'tempo': {
        // worklet-drum.js: a bpm change on the same anchor keeps the beat
        // position by moving the anchor.
        const t = this.tempo, now = typeof currentFrame === 'number' ? currentFrame : 0;
        const anchorMatches = Number.isFinite(d.anchor) && d.anchor === t.anchor;
        if (Number.isFinite(d.bpm)) {
          if (anchorMatches && t.bpm > 0 && d.bpm > 0 && t.bpm !== d.bpm) {
            const beat = (now - t.anchor) / ((60 / t.bpm) * sampleRate);
            t.anchor = now - beat * (60 / d.bpm) * sampleRate;
          }
          t.bpm = d.bpm;
        }
        if (Number.isFinite(d.swing)) t.swing = d.swing;
        if (Number.isFinite(d.anchor) && !anchorMatches) t.anchor = d.anchor;
        x.dr_host_tempo(t.bpm, t.swing, t.anchor);
        break;
      }
      case 'clip':
        this.hPend = { d, lanes: null, tag: ++this.tag };
        this.deferred = { slot: this.hPend, update: false, lanes: undefined };
        break;
      case 'clipupdate': {
        const slot = this.hPend || this.hLive;
        if (!slot) break;
        // No rhythm in the message keeps the slot's rhythm (worklet-drum.js).
        const has = Object.prototype.hasOwnProperty.call(d, 'rhythm') || Object.prototype.hasOwnProperty.call(d, 'drumRhythm');
        slot.d = { ...d, atFrame: slot.d.atFrame, rhythm: has ? d.rhythm ?? d.drumRhythm : slot.d.rhythm ?? slot.d.drumRhythm };
        this.deferred = { slot, update: true, lanes: undefined };
        break;
      }
      case 'auto':
        if (this.deferred) { this.deferred.lanes = d.lanes; this.flush(); break; }
        // A late 'auto' re-sends the slot it belongs to, as worklet-drum.js attaches it.
        if (this.hPend) { this.hPend.lanes = d.lanes; this.hostCall(this.hPend, false); }
        else if (this.hLive) { this.hLive.lanes = d.lanes; this.hostCall(this.hLive, true); }
        break;
      case 'clipstop':
        this.hPend = null;
        x.dr_host_stop(+d.atFrame || 0);
        break;
      case 'padCapture': this.capture = !!d.on; x.dr_capture(this.capture ? 1 : 0); break;
      case 'seqauto': this.setAuto(d.lanes); break;
      default: break; // samples and seed: see the header
    }
  }

  flush() {
    const f = this.deferred;
    this.deferred = null;
    if (f.lanes !== undefined) f.slot.lanes = f.lanes;
    this.hostCall(f.slot, f.update);
  }

  // One engine hostClip/hostClipUpdate call for a slot (worklet-drum.js 'clip').
  hostCall(slot, update) {
    const x = this.x, d = slot.d;
    const data = new Uint8Array(d.data);
    const bytes = Math.min(data.length, this.clipBuf.length);
    this.clipBuf.set(data.subarray(0, bytes));
    const rhythm = this.writeRhythm(d.rhythm ?? d.drumRhythm);
    const n = this.writeAuto(slot.lanes);
    x.dr_host_clip(update ? 1 : 0, bytes, Math.max(1, d.bars | 0), +d.atFrame || 0, slot.tag, rhythm ? 1 : 0, n);
  }

  // Engine events of the last render: clipstart / clipstop / pos.
  drainEvents() {
    const x = this.x, e = this.events;
    for (let n = x.dr_events_take(); n > 0; n = x.dr_events_take()) {
      for (let i = 0; i < n; i++) {
        const o = i * 5, type = e[o];
        if (type === 0) {
          if (this.hPend && this.hPend.tag === e[o + 4]) { this.hLive = this.hPend; this.hPend = null; }
          this.port.postMessage({ t: 'clipstart', frame: e[o + 1] });
        } else if (type === 1) {
          this.hLive = null;
          this.port.postMessage({ t: 'clipstop', frame: e[o + 1] });
        } else {
          const hits = [];
          for (let p = 0; p < NPADS; p++) if (this.hitMask & (1 << p)) hits.push(p);
          this.port.postMessage({ t: 'pos', step: e[o + 2], bar: e[o + 3], hits, frame: e[o + 1] });
        }
      }
    }
  }

  setPatterns(data) {
    const src = new Uint8Array(data);
    this.pats.fill(0);
    this.pats.set(src.subarray(0, PATTERN_BYTES));
    this.x.dr_set_patterns();
  }

  setChain(list) {
    if (!Array.isArray(list) || !list.length) return;
    const n = Math.min(list.length, this.chainBuf.length);
    for (let i = 0; i < n; i++) this.chainBuf[i] = list[i] | 0;
    this.x.dr_set_chain(n);
  }

  // The same normalization as worklet-drum.js normalizeRhythm, packed for
  // dr_set_rhythm. An invalid or missing rhythm clears it.
  setRhythm(value) { this.x.dr_set_rhythm(this.writeRhythm(value) ? 1 : 0); }

  // Packs a valid rhythm into the rhythm buffer; false for none.
  writeRhythm(value) {
    if (!value || !Array.isArray(value.lanes) || value.lanes.length !== NPADS) return false;
    const buf = this.rhythmBuf;
    buf.fill(0);
    value.lanes.forEach((lane, i) => {
      if (!lane || (!lane.enabled && lane.delayMs === undefined && lane.stepDelayMs === undefined)) return;
      const o = i * LANE_INTS;
      const steps = Math.max(1, Math.min(16, lane.steps | 0));
      const fit = lane.timing && String(lane.timing.mode).toLowerCase() === 'fit';
      const delays = Array.isArray(lane.stepDelayMs) ? lane.stepDelayMs.slice(0, 256) : [];
      buf[o] = 1;
      buf[o + 1] = lane.enabled ? 1 : 0;
      buf[o + 2] = lane.delayMs !== undefined || lane.stepDelayMs !== undefined ? 1 : 0;
      buf[o + 3] = Math.max(-50, Math.min(50, lane.delayMs | 0));
      buf[o + 4] = Math.max(0, lane.sourceBar | 0);
      buf[o + 5] = steps;
      buf[o + 6] = (((lane.rotation | 0) % steps) + steps) % steps;
      buf[o + 7] = fit ? 1 : 0;
      buf[o + 8] = fit && lane.timing.cycleBeats === 8 ? 8 : 4;
      buf[o + 9] = delays.length;
      delays.forEach((v, k) => { buf[o + 10 + k] = Math.max(-50, Math.min(50, v | 0)); });
    });
    return true;
  }

  // 'seqauto' lanes: { k, table (len * 16 values), len, fit, rot }.
  setAuto(list) { this.x.dr_auto_commit(Math.max(0, this.writeAuto(Array.isArray(list) ? list : []))); }

  // Stages automation tables; returns the lane count, or -1 for none.
  writeAuto(list) {
    if (!Array.isArray(list)) return -1;
    const x = this.x, max = x.dr_auto_max_lanes(), size = x.dr_auto_table_size();
    let n = 0;
    for (const l of list) {
      if (n >= max) break;
      const id = this.ids.get(l.k);
      const len = Math.max(1, l.len | 0);
      const table = new Float32Array(l.table);
      if (id === undefined || table.length !== len * 16 || table.length > size) continue;
      const fx = /(^|\.)fx\./.test(l.k) || l.k === 'master.volume';
      const ptr = x.dr_auto_lane(n, id, len, Math.max(0, +l.fit || 0) | 0, (+l.rot || 0) | 0, fx ? 1 : 0);
      if (!ptr) continue;
      new Float32Array(x.memory.buffer, ptr, table.length).set(table);
      n++;
    }
    return n;
  }

  process(inputs, outputs) {
    const x = this.x;
    if (!x) { for (const out of outputs) for (const ch of out) ch.fill(0); return true; }
    const n = Math.min(outputs[0][0].length, BLOCK);
    const key = inputs[0];
    const withKey = key && key.length > 0 && key[0].length > 0;
    if (withKey) for (let c = 0; c < this.keyIn.length; c++) {
      if (key[c]) this.keyIn[c].set(key[c].subarray(0, n)); else this.keyIn[c].fill(0);
    }
    if (this.deferred) this.flush();
    x.dr_render(n, withKey ? 1 : 0, typeof currentFrame === 'number' ? currentFrame : 0);
    for (let b = 0; b < BUSES; b++) {
      const out = outputs[b];
      if (!out || !out.length) continue;
      const [l, r] = this.outs[b];
      out[0].set(n === BLOCK ? l : l.subarray(0, n));
      if (out.length > 1) out[1].set(n === BLOCK ? r : r.subarray(0, n));
      else for (let i = 0; i < n; i++) out[0][i] = 0.5 * (l[i] + r[i]);
    }
    const kickOut = outputs[BUSES];
    if (kickOut && kickOut[0]) kickOut[0].set(n === BLOCK ? this.kick : this.kick.subarray(0, n));

    const hitMask = x.dr_consume_hits();
    this.hitMask = hitMask;
    if (this.hosted) this.drainEvents();
    const step = x.dr_current_step(), pat = x.dr_current_pattern();
    if (step >= 0 && (step !== this.lastStep || pat !== this.lastPat)) {
      const hits = [];
      for (let i = 0; i < NPADS; i++) if (hitMask & (1 << i)) hits.push(i);
      this.port.postMessage({ t: 'step', s: step, pat, hits });
    }
    this.lastStep = step; this.lastPat = pat;

    if (this.capture) for (let i = 0; i < NPADS; i++) {
      const frames = x.dr_capture_pull(i);
      if (frames > 0) this.port.postMessage({ t: 'padAudio', pad: i, frames, data: this.capBuf.slice() });
    }

    const bus = this.padOut[this.meterPad];
    if ((this.meter.dynamics || this.meter.echo || this.meter.reverb) && x.dr_telemetry_take(this.meterPad, bus))
      this.postTelemetry(bus);

    this.vizCount += n;
    if (this.vizCount >= 2048) {
      this.vizCount = 0;
      const lanes = new Array(NPADS);
      for (let i = 0; i < NPADS; i++) lanes[i] = x.dr_lane_position(i);
      this.port.postMessage({ t: 'viz', lanes, a: x.dr_viz(0), b: x.dr_viz(1), env: x.dr_viz(2) });
    }
    return true;
  }

  postTelemetry(bus) {
    const v = this.tele, pad = this.meterPad;
    if (this.meter.reverb) this.port.postMessage({ t: 'reverb', pad, left: v[T.verbL], right: v[T.verbR], correlation: v[T.correlation], bus, shared: true });
    // The native meter does not measure the delay input; -90 dB marks it unknown.
    if (this.meter.echo) this.port.postMessage({ t: 'echo', pad, input: -90, left: v[T.echoL], right: v[T.echoR], time: v[T.time], driftL: v[T.driftL], driftR: v[T.driftR] });
    if (this.meter.dynamics) this.port.postMessage({ t: 'dynamics', pad,
      ott: { input: v[T.ottIn], output: v[T.ottOut], levels: [v[T.ottLow], v[T.ottLow + 1], v[T.ottLow + 2]], gains: [v[T.gainLow], v[T.gainLow + 1], v[T.gainLow + 2]], makeup: v[T.ottAuto] },
      comp: { input: v[T.compIn], output: v[T.compOut], reduction: v[T.reduction], trace: [], makeup: v[T.compAuto] },
    });
  }
}

registerProcessor('fable-dr-wasm', DrumWasmProcessor);
