// Main-thread DR-1 engine: owns the AudioContext, drum worklet and FX graph.

import { generateTables, type GeneratedTable } from '../../engine/wavetables';
import { type ParamValues } from '../../params';
import { defaultDrumParams } from '../params';
import { generateDrumTables } from './drumtables';
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

interface WetDry {
  dry: GainNode;
  wet: GainNode;
}

export const isFxParam = (id: string): boolean => id.startsWith('fx.') || id === 'master.volume';

export class DrumEngine {
  params: ParamValues;
  tables: VizTable[] | null;
  builtInTables: GeneratedTable[];
  userTables: GeneratedTable[];
  ready: boolean;
  onstep: ((d: { s: number; pat: number; hits: number[] }) => void) | null;
  onviz: ((d: { a: number; b: number; env: number }) => void) | null;

  ctx!: AudioContext;
  node!: AudioWorkletNode;

  fxInput!: GainNode;
  driveShaper!: WaveShaperNode;
  drivePre!: GainNode;
  driveMix!: WetDry;
  compressor!: DynamicsCompressorNode;
  compMakeup!: GainNode;
  compMix!: WetDry;
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
  dcBlock!: BiquadFilterNode;
  limiter!: DynamicsCompressorNode;
  scopeAnalyser!: AnalyserNode;

  constructor() {
    this.params = defaultDrumParams();
    this.tables = null;
    this.builtInTables = [];
    this.userTables = [];
    this.ready = false;
    this.onstep = null;
    this.onviz = null;
  }

  async init(): Promise<void> {
    const Ctor = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    const ctx = new Ctor({ latencyHint: 'interactive' });
    this.ctx = ctx;
    await ctx.audioWorklet.addModule(workletUrl);

    this.builtInTables = [...generateDrumTables(), ...generateTables()];
    this.refreshViz();

    this.node = new AudioWorkletNode(ctx, 'fable-dr', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [2],
    });
    this.node.port.onmessage = (e: MessageEvent) => {
      if (e.data.t === 'step' && this.onstep) this.onstep(e.data as StepMessage);
      if (e.data.t === 'viz' && this.onviz) this.onviz(e.data as DrumVizMessage);
    };
    this.node.port.postMessage({ t: 'init', params: this.params });
    this.ready = true;
    this.pushTables();

    this.buildFx();
    this.node.connect(this.fxInput);
    this.applyAllFx();
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
    if (!this.ready) return;
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

    // -- compressor --
    const compOut = ctx.createGain();
    this.compressor = ctx.createDynamicsCompressor();
    this.compressor.ratio.value = 4;
    this.compressor.knee.value = 9;
    this.compressor.attack.value = 0.003;
    this.compressor.release.value = 0.25;
    this.compMakeup = ctx.createGain();
    driveOut.connect(this.compressor).connect(this.compMakeup);
    this.compMix = this.mkWetDry(driveOut, compOut);
    this.compMakeup.connect(this.compMix.wet);

    // -- chorus --
    const chorusOut = ctx.createGain();
    const merger = ctx.createChannelMerger(2);
    this.chDelay1 = ctx.createDelay(0.1);
    this.chDelay2 = ctx.createDelay(0.1);
    this.chDelay1.delayTime.value = 0.012;
    this.chDelay2.delayTime.value = 0.017;
    compOut.connect(this.chDelay1);
    compOut.connect(this.chDelay2);
    this.chDelay1.connect(merger, 0, 0);
    this.chDelay2.connect(merger, 0, 1);
    this.chLfo = ctx.createOscillator();
    this.chDepth1 = ctx.createGain();
    this.chDepth2 = ctx.createGain();
    this.chLfo.connect(this.chDepth1).connect(this.chDelay1.delayTime);
    this.chLfo.connect(this.chDepth2).connect(this.chDelay2.delayTime);
    this.chDepth2.gain.value = -0.003;
    this.chLfo.start();
    this.chorusMix = this.mkWetDry(compOut, chorusOut);
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
    this.dcBlock = ctx.createBiquadFilter();
    this.dcBlock.type = 'highpass';
    this.dcBlock.frequency.value = 8;
    this.limiter = ctx.createDynamicsCompressor();
    this.limiter.threshold.value = -8;
    this.limiter.knee.value = 4;
    this.limiter.ratio.value = 14;
    this.limiter.attack.value = 0.002;
    this.limiter.release.value = 0.22;

    this.scopeAnalyser = ctx.createAnalyser();
    this.scopeAnalyser.fftSize = 2048;

    verbOut.connect(this.masterGain).connect(this.dcBlock).connect(this.limiter).connect(ctx.destination);
    this.masterGain.connect(this.scopeAnalyser);
  }

  renderImpulse(): void {
    if (!this.ready) return;
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
    if (!this.ready) return;
    const t = this.ctx.currentTime;
    const wet = on ? Math.sin((amount * Math.PI) / 2) : 0;
    const dry = on ? Math.cos((amount * Math.PI) / 2) : 1;
    mix.wet.gain.setTargetAtTime(wet, t, 0.02);
    mix.dry.gain.setTargetAtTime(dry, t, 0.02);
  }

  applyAllFx(): void {
    if (!this.ready) return;
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

    this.compressor.threshold.setTargetAtTime(p['fx.comp.thr'], t, 0.02);
    this.compMakeup.gain.setTargetAtTime(Math.pow(10, p['fx.comp.gain'] / 20), t, 0.02);
    this.setMix(this.compMix, p['fx.comp.on'], 1);

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

  // ---------- parameter + transport API ----------
  setParam(id: string, v: number): void {
    this.params[id] = v;
    if (!this.ready) return;
    if (isFxParam(id)) {
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

  panic(): void {
    if (this.ready) this.node.port.postMessage({ t: 'panic' });
  }
}
