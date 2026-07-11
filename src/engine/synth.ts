// Main-thread engine: owns the AudioContext, the worklet node and the FX graph.
// Param routing: 'fx.*' and 'master.volume' drive native nodes here; everything
// else is forwarded to the worklet.

import { generateTables, type GeneratedTable } from './wavetables';
import { defaultParams, type ParamValues } from '../params';
// The DSP core runs in the audio render thread. `?url` makes Vite copy it
// verbatim and hand us the served URL for `audioWorklet.addModule`.
import workletUrl from './worklet.js?url';

export interface VizMessage {
  t: 'viz';
  a: number;
  b: number;
  n: number;
}

export interface StepMessage {
  t: 'step';
  s: number; // step index 0..15
  pat: number; // pattern being played
}

// Hosted-mode options (SQ-4): share an AudioContext and route the engine's
// output into a provided node instead of ctx.destination. Defaults keep the
// standalone behavior byte-for-byte. See docs/sq4-clips.md §7.
export interface EngineInitOpts {
  ctx?: AudioContext;
  output?: AudioNode;
}

export interface PosMessage {
  t: 'pos';
  step: number;
  bar: number;
}

export interface VizTable {
  name: string;
  frames: number;
  viz: Float32Array;
}

interface WetDry {
  dry: GainNode;
  wet: GainNode;
}

export class SynthEngine {
  params: ParamValues;
  tables: VizTable[] | null; // combined [{name, frames, viz}] kept for visualization
  procTables: GeneratedTable[]; // procedural tables (full mip data)
  userTables: GeneratedTable[]; // imported / drawn tables (full mip data)
  onviz: ((d: VizMessage) => void) | null;
  onstep: ((d: StepMessage) => void) | null;
  onclipstart: ((frame: number) => void) | null;
  onclipstop: ((frame: number) => void) | null;
  onpos: ((d: PosMessage) => void) | null;
  ready: boolean;
  output: AudioNode | null; // hosted-mode output (null = ctx.destination)

  ctx!: AudioContext;
  node!: AudioWorkletNode;

  fxInput!: GainNode;
  driveShaper!: WaveShaperNode;
  drivePre!: GainNode;
  driveMix!: WetDry;
  chDelay1!: DelayNode;
  chDelay2!: DelayNode;
  chLfo!: OscillatorNode;
  chDepth1!: GainNode;
  chDepth2!: GainNode;
  chorusMix!: WetDry;
  dlL!: DelayNode;
  dlR!: DelayNode;
  dlFb!: GainNode;
  dlFb2!: GainNode;
  dlDamp!: BiquadFilterNode;
  delayMix!: WetDry;
  convolver!: ConvolverNode;
  verbMix!: WetDry;
  verbTimer!: ReturnType<typeof setTimeout> | 0;
  masterGain!: GainNode;
  limiter!: DynamicsCompressorNode;
  scopeAnalyser!: AnalyserNode;
  specAnalyser!: AnalyserNode;

  constructor() {
    this.params = defaultParams();
    this.tables = null;
    this.procTables = [];
    this.userTables = [];
    this.onviz = null;
    this.onstep = null;
    this.onclipstart = null;
    this.onclipstop = null;
    this.onpos = null;
    this.ready = false;
    this.output = null;
  }

  async init(opts: EngineInitOpts = {}): Promise<void> {
    const Ctor = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    const ctx = opts.ctx ?? new Ctor({ latencyHint: 'interactive' });
    this.ctx = ctx;
    this.output = opts.output ?? null;
    await ctx.audioWorklet.addModule(workletUrl);

    this.procTables = generateTables();
    this.refreshViz();

    this.node = new AudioWorkletNode(ctx, 'fable-wt', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
    });
    this.node.port.onmessage = (e: MessageEvent) => {
      if (e.data.t === 'viz' && this.onviz) this.onviz(e.data as VizMessage);
      else if (e.data.t === 'step' && this.onstep) this.onstep(e.data as StepMessage);
      else if (e.data.t === 'pos' && this.onpos) this.onpos(e.data as PosMessage);
      else if (e.data.t === 'clipstart' && this.onclipstart) this.onclipstart(e.data.frame as number);
      else if (e.data.t === 'clipstop' && this.onclipstop) this.onclipstop(e.data.frame as number);
    };
    this.node.port.postMessage({ t: 'init', params: this.params });
    this.ready = true;
    this.pushTables();

    this.buildFx();
    this.node.connect(this.fxInput);
    this.applyAllFx();
  }

  // Combined table list (procedural first, then user) — the index space the
  // oscA/oscB.table params address.
  allTables(): GeneratedTable[] {
    return [...this.procTables, ...this.userTables];
  }

  // Rebuild the lightweight viz list used by the WavetableView displays.
  refreshViz(): void {
    this.tables = this.allTables().map((t) => ({ name: t.name, frames: t.frames, viz: t.viz }));
  }

  // Send the full mip data of every table to the worklet. Buffers are copied
  // (sliced) rather than transferred so the originals stay intact and can be
  // re-sent whenever the user-table set changes.
  pushTables(): void {
    if (!this.ready) return;
    const all = this.allTables();
    this.node.port.postMessage(
      { t: 'tables', list: all.map((t) => ({ frames: t.frames, mips: t.mips, size: t.size, buf: t.data.slice().buffer })) }
    );
  }

  // Replace the user-table set and push it to the worklet + refresh viz.
  setUserTables(tables: GeneratedTable[]): void {
    this.userTables = tables;
    this.refreshViz();
    this.pushTables();
  }

  // ---------- FX graph ----------
  mkWetDry(input: AudioNode, output: AudioNode): WetDry {
    const dry = this.ctx.createGain();
    const wet = this.ctx.createGain();
    input.connect(dry).connect(output);
    wet.connect(output);
    return { dry, wet };
  }

  buildFx(): void {
    const ctx = this.ctx;
    this.fxInput = ctx.createGain();

    // -- drive --
    const driveOut = ctx.createGain();
    this.driveShaper = ctx.createWaveShaper();
    this.driveShaper.oversample = '2x';
    this.drivePre = ctx.createGain();
    this.fxInput.connect(this.drivePre).connect(this.driveShaper);
    this.driveMix = this.mkWetDry(this.fxInput, driveOut);
    this.driveShaper.connect(this.driveMix.wet);

    // -- chorus --
    const chorusOut = ctx.createGain();
    const merger = ctx.createChannelMerger(2);
    this.chDelay1 = ctx.createDelay(0.1);
    this.chDelay2 = ctx.createDelay(0.1);
    this.chDelay1.delayTime.value = 0.012;
    this.chDelay2.delayTime.value = 0.017;
    driveOut.connect(this.chDelay1);
    driveOut.connect(this.chDelay2);
    this.chDelay1.connect(merger, 0, 0);
    this.chDelay2.connect(merger, 0, 1);
    this.chLfo = ctx.createOscillator();
    this.chDepth1 = ctx.createGain();
    this.chDepth2 = ctx.createGain();
    this.chLfo.connect(this.chDepth1).connect(this.chDelay1.delayTime);
    this.chLfo.connect(this.chDepth2).connect(this.chDelay2.delayTime);
    this.chDepth2.gain.value = -0.003;
    this.chLfo.start();
    this.chorusMix = this.mkWetDry(driveOut, chorusOut);
    merger.connect(this.chorusMix.wet);

    // -- ping-pong delay --
    const delayOut = ctx.createGain();
    this.dlL = ctx.createDelay(2);
    this.dlR = ctx.createDelay(2);
    this.dlFb = ctx.createGain();
    this.dlFb2 = ctx.createGain();
    this.dlDamp = ctx.createBiquadFilter();
    this.dlDamp.type = 'lowpass';
    this.dlDamp.frequency.value = 4500;
    const dlMerge = ctx.createChannelMerger(2);
    const dlIn = ctx.createGain();
    chorusOut.connect(dlIn);
    dlIn.connect(this.dlL);
    this.dlL.connect(dlMerge, 0, 0);
    this.dlR.connect(dlMerge, 0, 1);
    this.dlL.connect(this.dlFb).connect(this.dlDamp).connect(this.dlR);
    this.dlR.connect(this.dlFb2).connect(this.dlL);
    this.delayMix = this.mkWetDry(chorusOut, delayOut);
    dlMerge.connect(this.delayMix.wet);

    // -- reverb --
    const verbOut = ctx.createGain();
    this.convolver = ctx.createConvolver();
    delayOut.connect(this.convolver);
    this.verbMix = this.mkWetDry(delayOut, verbOut);
    this.convolver.connect(this.verbMix.wet);
    this.verbTimer = 0;
    this.renderImpulse();

    // -- master --
    this.masterGain = ctx.createGain();
    const dcBlock = ctx.createBiquadFilter();
    dcBlock.type = 'highpass';
    dcBlock.frequency.value = 8;
    this.limiter = ctx.createDynamicsCompressor();
    this.limiter.threshold.value = -8;
    this.limiter.knee.value = 4;
    this.limiter.ratio.value = 14;
    this.limiter.attack.value = 0.002;
    this.limiter.release.value = 0.22;

    this.scopeAnalyser = ctx.createAnalyser();
    this.scopeAnalyser.fftSize = 2048;
    this.specAnalyser = ctx.createAnalyser();
    this.specAnalyser.fftSize = 2048;
    this.specAnalyser.smoothingTimeConstant = 0.82;

    verbOut.connect(this.masterGain).connect(dcBlock).connect(this.limiter).connect(this.output ?? ctx.destination);
    this.masterGain.connect(this.scopeAnalyser);
    this.masterGain.connect(this.specAnalyser);
  }

  renderImpulse(): void {
    const size = this.params['fx.reverb.size'];
    const dur = 0.5 + size * 4.5;
    const sr = this.ctx.sampleRate;
    const len = Math.floor(dur * sr);
    const buf = this.ctx.createBuffer(2, len, sr);
    const decay = 2.2 + size * 1.5;
    for (let ch = 0; ch < 2; ch++) {
      const d = buf.getChannelData(ch);
      for (let i = 0; i < len; i++) {
        const t = i / len;
        d[i] = (Math.random() * 2 - 1) * Math.pow(1 - t, decay) * (i < 80 ? i / 80 : 1);
      }
    }
    this.convolver.buffer = buf;
  }

  setMix(mix: WetDry, on: number, amount: number): void {
    const t = this.ctx.currentTime;
    const wet = on ? Math.sin((amount * Math.PI) / 2) : 0;
    const dry = on ? Math.cos((amount * Math.PI) / 2) : 1;
    mix.wet.gain.setTargetAtTime(wet, t, 0.02);
    mix.dry.gain.setTargetAtTime(dry, t, 0.02);
  }

  applyAllFx(): void {
    const p = this.params;
    const t = this.ctx.currentTime;

    const amt = p['fx.drive.amt'];
    const k = 1 + amt * 24;
    const curve = new Float32Array(513);
    const norm = Math.tanh(k);
    for (let i = 0; i < 513; i++) {
      const x = (i / 256) - 1;
      curve[i] = Math.tanh(x * k) / norm;
    }
    this.driveShaper.curve = curve;
    this.drivePre.gain.value = 1 + amt * 2;
    this.setMix(this.driveMix, p['fx.drive.on'], p['fx.drive.mix']);

    this.chLfo.frequency.setTargetAtTime(p['fx.chorus.rate'], t, 0.05);
    const depth = 0.0008 + p['fx.chorus.depth'] * 0.0045;
    this.chDepth1.gain.setTargetAtTime(depth, t, 0.05);
    this.chDepth2.gain.setTargetAtTime(-depth * 0.8, t, 0.05);
    this.setMix(this.chorusMix, p['fx.chorus.on'], p['fx.chorus.mix'] * 0.8);

    this.dlL.delayTime.setTargetAtTime(p['fx.delay.time'], t, 0.08);
    this.dlR.delayTime.setTargetAtTime(p['fx.delay.time'], t, 0.08);
    this.dlFb.gain.setTargetAtTime(p['fx.delay.fb'], t, 0.02);
    this.dlFb2.gain.setTargetAtTime(p['fx.delay.fb'], t, 0.02);
    this.setMix(this.delayMix, p['fx.delay.on'], p['fx.delay.mix'] * 0.85);

    this.setMix(this.verbMix, p['fx.reverb.on'], p['fx.reverb.mix'] * 0.9);

    const vol = p['master.volume'];
    this.masterGain.gain.setTargetAtTime(vol * vol * 1.6, t, 0.02);
  }

  // ---------- param + note API ----------
  setParam(id: string, v: number): void {
    this.params[id] = v;
    if (!this.ready) return;
    if (id.startsWith('fx.') || id === 'master.volume') {
      if (id === 'fx.reverb.size') {
        clearTimeout(this.verbTimer);
        this.verbTimer = setTimeout(() => this.renderImpulse(), 180);
      }
      this.applyAllFx();
    } else {
      this.node.port.postMessage({ t: 'p', k: id, v });
    }
  }

  applyAllParams(): void {
    if (!this.ready) return;
    this.node.port.postMessage({ t: 'init', params: this.params });
    this.applyAllFx();
    this.renderImpulse();
  }

  noteOn(n: number, vel = 1): void { if (this.ready) this.node.port.postMessage({ t: 'on', n, v: vel }); }
  noteOff(n: number): void { if (this.ready) this.node.port.postMessage({ t: 'off', n }); }
  bend(semis: number): void { if (this.ready) this.node.port.postMessage({ t: 'bend', s: semis }); }
  panic(): void { if (this.ready) this.node.port.postMessage({ t: 'panic' }); }

  // ---------- note sequencer ----------
  setSeqPatterns(pats: Uint8Array): void { if (this.ready) this.node.port.postMessage({ t: 'pats', data: pats }); }
  setSeqChain(list: number[]): void { if (this.ready) this.node.port.postMessage({ t: 'chain', list }); }
  seqPlay(): void { if (this.ready) this.node.port.postMessage({ t: 'play' }); }
  seqStop(): void { if (this.ready) this.node.port.postMessage({ t: 'stop' }); }

  // ---------- hosted clip transport (SQ-4, docs/sq4-clips.md §6) ----------
  setHostMode(on: boolean): void { if (this.ready) this.node.port.postMessage({ t: 'host', on: on ? 1 : 0 }); }
  setTempo(bpm: number, swing: number, anchor: number): void {
    if (this.ready) this.node.port.postMessage({ t: 'tempo', bpm, swing, anchor });
  }
  scheduleClip(data: Uint8Array, bars: number, atFrame: number): void {
    if (this.ready) this.node.port.postMessage({ t: 'clip', data, bars, atFrame });
  }
  scheduleStop(atFrame: number): void { if (this.ready) this.node.port.postMessage({ t: 'clipstop', atFrame }); }
  updateClip(data: Uint8Array, bars: number): void { if (this.ready) this.node.port.postMessage({ t: 'clipupdate', data, bars }); }
}
