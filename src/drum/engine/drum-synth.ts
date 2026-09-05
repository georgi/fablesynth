// Main-thread DR-1 engine: owns the AudioContext, drum worklet and FX graph.

import { generateTables, type GeneratedTable } from '../../engine/wavetables';
import { type ParamValues } from '../../params';
import { defaultDrumParams, OUT_NAMES, PAD_COUNT, pad } from '../params';
import { generateDrumTables } from './drumtables';
import { generateSampledDrumTables } from './sampledtables.gen';
import { loadDrumOneShots, type DrumOneShot } from './oneshots.gen';
import workletUrl from './worklet-drum.js?url';

export interface VizTable {
  name: string;
  frames: number;
  viz: Float32Array;
}

export interface StepMessage {
  t: 'step';
  s: number;
  pat: number;
  hits: number[];
}

export interface DrumVizMessage {
  t: 'viz';
  a: number;
  b: number;
  env: number;
}

// One output bus per OUT_NAMES entry. The FX rack — per-pad drive, compressor,
// chorus and delay, the shared reverbs and the per-bus gain/DC/limiter — now
// runs inside the worklet (review W6, docs/audio-engine-review.md §3). The
// native graph is only the meter tap and the route to the device.
const BUS_COUNT = OUT_NAMES.length;

// Hosted-mode options (SQ-4): share an AudioContext and route the engine's
// output into a provided node instead of ctx.destination. Defaults keep the
// standalone behavior byte-for-byte. See docs/sq4-clips.md §7.
export interface EngineInitOpts {
  ctx?: AudioContext;
  output?: AudioNode;
}

export const isFxParam = (id: string): boolean =>
  id.startsWith('fx.') || /^pad(?:[0-9]|1[0-5])\.fx\./.test(id) || id === 'master.volume';

export function fxPadFromParam(id: string): number | null {
  const match = /^pad([0-9]|1[0-5])\.fx\./.exec(id);
  return match ? Number(match[1]) : null;
}

export class DrumEngine {
  params: ParamValues;
  tables: VizTable[] | null;
  builtInTables: GeneratedTable[];
  userTables: GeneratedTable[];
  samples: DrumOneShot[];
  ready: boolean;
  onstep: ((d: { s: number; pat: number; hits: number[] }) => void) | null;
  onviz: ((d: { a: number; b: number; env: number }) => void) | null;
  onclipstart: ((frame: number) => void) | null;
  onclipstop: ((frame: number) => void) | null;
  onpos: ((d: { step: number; bar: number }) => void) | null;
  // Pads triggered by a hosted clip step, so the UI can flash their LEDs.
  // Standalone playback reports hits via onstep instead.
  onhit: ((pads: number[]) => void) | null;
  output: AudioNode | null; // hosted-mode output (null = ctx.destination)

  ctx!: AudioContext;
  node!: AudioWorkletNode;

  scopeAnalyser!: AnalyserNode;
  // Chain latency reported by the worklet: drive FIR group delay + limiter
  // lookahead (99 samples at 48 kHz), the same figure the plugin reports.
  latencySamples: number;

  constructor() {
    this.params = defaultDrumParams();
    this.tables = null;
    this.builtInTables = [];
    this.userTables = [];
    this.samples = [];
    this.ready = false;
    this.onstep = null;
    this.onviz = null;
    this.onclipstart = null;
    this.onclipstop = null;
    this.onpos = null;
    this.onhit = null;
    this.output = null;
    this.latencySamples = 0;
  }

  async init(opts: EngineInitOpts = {}): Promise<void> {
    const Ctor = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    const ctx = opts.ctx ?? new Ctor({ latencyHint: 'interactive' });
    this.ctx = ctx;
    this.output = opts.output ?? null;
    await ctx.audioWorklet.addModule(workletUrl);

    this.builtInTables = [...generateDrumTables(), ...generateTables(), ...generateSampledDrumTables()];
    this.samples = await loadDrumOneShots();
    this.refreshViz();

    this.node = new AudioWorkletNode(ctx, 'fable-dr', {
      numberOfInputs: 0,
      numberOfOutputs: BUS_COUNT,
      outputChannelCount: Array(BUS_COUNT).fill(2),
    });
    this.node.port.onmessage = (e: MessageEvent) => {
      if (e.data.t === 'step' && this.onstep) this.onstep(e.data as StepMessage);
      if (e.data.t === 'viz' && this.onviz) this.onviz(e.data as DrumVizMessage);
      if (e.data.t === 'pos') {
        if (this.onpos) this.onpos({ step: e.data.step as number, bar: e.data.bar as number });
        const hits = e.data.hits as number[] | undefined;
        if (this.onhit && hits && hits.length) this.onhit(hits);
      }
      if (e.data.t === 'latency') this.latencySamples = e.data.n as number;
      if (e.data.t === 'clipstart' && this.onclipstart) this.onclipstart(e.data.frame as number);
      if (e.data.t === 'clipstop' && this.onclipstop) this.onclipstop(e.data.frame as number);
    };
    this.node.port.postMessage({ t: 'init', params: this.params });
    this.ready = true;
    this.pushTables();
    this.pushSamples();

    this.buildOut();
  }

  allTables(): GeneratedTable[] {
    return [...this.builtInTables, ...this.userTables];
  }

  refreshViz(): void {
    this.tables = this.allTables().map((t) => ({ name: t.name, frames: t.frames, viz: t.viz }));
  }

  pushTables(): void {
    if (!this.ready) return;
    const all = this.allTables();
    this.node.port.postMessage({
      t: 'tables',
      list: all.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })),
    });
  }

  pushSamples(): void {
    if (!this.ready) return;
    this.node.port.postMessage({
      t: 'samples',
      list: this.samples.map((s) => ({
        sampleRate: s.sampleRate,
        buf: s.data.slice().buffer,
      })),
    });
  }

  setUserTables(tables: GeneratedTable[]): void {
    this.userTables = tables;
    this.refreshViz();
    this.pushTables();
  }

  // ---------- output graph ----------
  // Each worklet output is one finished bus: it already carries that bus's
  // gain, DC block and -1 dBFS limiter. Nothing is left to do here but meter
  // it and hand it to the device.
  buildOut(): void {
    if (!this.ready) return;
    const ctx = this.ctx;
    this.scopeAnalyser = ctx.createAnalyser();
    this.scopeAnalyser.fftSize = 2048;
    const dest = this.output ?? ctx.destination;
    for (let b = 0; b < BUS_COUNT; b++) {
      this.node.connect(dest, b);
      this.node.connect(this.scopeAnalyser, b);
    }
  }

  // ---------- parameter + transport API ----------
  setParam(id: string, v: number): void {
    // Accept the old global IDs as a compatibility API by broadcasting them.
    // New callers always use pad-scoped IDs.
    if (id.startsWith('fx.')) {
      for (let i = 0; i < PAD_COUNT; i++) {
        const k = pad(i, id);
        this.params[k] = v;
        if (this.ready) this.node.port.postMessage({ t: 'p', k, v });
      }
      delete this.params[id];
      return;
    }
    this.params[id] = v;
    if (!this.ready) return;
    this.node.port.postMessage({ t: 'p', k: id, v });
  }

  applyAllParams(): void {
    if (!this.ready) return;
    this.node.port.postMessage({ t: 'init', params: this.params });
  }

  trigger(pad: number, vel: number): void {
    if (this.ready) this.node.port.postMessage({ t: 'trig', pad, v: vel });
  }

  play(): void {
    if (this.ready) this.node.port.postMessage({ t: 'play' });
  }

  stop(): void {
    if (this.ready) this.node.port.postMessage({ t: 'stop' });
  }

  setPatterns(p: Uint8Array): void {
    if (this.ready) this.node.port.postMessage({ t: 'pats', data: p.slice().buffer });
  }

  setChain(c: number[]): void {
    if (this.ready) this.node.port.postMessage({ t: 'chain', list: c });
  }

  selectPad(i: number): void {
    if (this.ready) this.node.port.postMessage({ t: 'sel', pad: i });
  }

  // Hosted preset changes reset voices while preserving active and queued clips.
  panic(preserveTransport = false): void {
    if (this.ready) this.node.port.postMessage({ t: 'panic', preserveTransport });
  }

  // ---------- hosted clip transport (SQ-4, docs/sq4-clips.md §6) ----------
  setHostMode(on: boolean): void {
    if (this.ready) this.node.port.postMessage({ t: 'host', on: on ? 1 : 0 });
  }

  setTempo(bpm: number, swing: number, anchor: number): void {
    if (this.ready) this.node.port.postMessage({ t: 'tempo', bpm, swing, anchor });
  }

  scheduleClip(data: Uint8Array, bars: number, atFrame: number): void {
    if (this.ready) this.node.port.postMessage({ t: 'clip', data, bars, atFrame });
  }

  scheduleStop(atFrame: number): void {
    if (this.ready) this.node.port.postMessage({ t: 'clipstop', atFrame });
  }

  updateClip(data: Uint8Array, bars: number): void {
    if (this.ready) this.node.port.postMessage({ t: 'clipupdate', data, bars });
  }
}
