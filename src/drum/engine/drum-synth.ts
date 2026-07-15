// Main-thread DR-1 engine: owns the AudioContext, drum worklet and FX graph.

import { generateTables, type GeneratedTable } from '../../engine/wavetables';
import { makeDriveCurve } from '../../engine/drive';
import { type ParamValues } from '../../params';
import { defaultDrumParams, PAD_COUNT, pad } from '../params';
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

interface WetDry {
  dry: GainNode;
  wet: GainNode;
}

interface PadFxChain {
  input: GainNode;
  driveShaper: WaveShaperNode;
  drivePre: GainNode;
  driveMix: WetDry;
  compressor: DynamicsCompressorNode;
  compMakeup: GainNode;
  compMix: WetDry;
  chDelay1: DelayNode;
  chDelay2: DelayNode;
  chLfo: OscillatorNode;
  chDepth1: GainNode;
  chDepth2: GainNode;
  chorusMix: WetDry;
  dlL: DelayNode;
  dlR: DelayNode;
  dlFb: GainNode;
  dlFb2: GainNode;
  delayMix: WetDry;
  convolver: ConvolverNode;
  verbMix: WetDry;
  verbTimer: ReturnType<typeof setTimeout> | 0;
}

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
  output: AudioNode | null; // hosted-mode output (null = ctx.destination)

  ctx!: AudioContext;
  node!: AudioWorkletNode;

  fxChains: PadFxChain[];
  masterGain!: GainNode;
  dcBlock!: BiquadFilterNode;
  limiter!: DynamicsCompressorNode;
  scopeAnalyser!: AnalyserNode;

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
    this.output = null;
    this.fxChains = [];
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
      numberOfOutputs: PAD_COUNT,
      outputChannelCount: Array(PAD_COUNT).fill(2),
    });
    this.node.port.onmessage = (e: MessageEvent) => {
      if (e.data.t === 'step' && this.onstep) this.onstep(e.data as StepMessage);
      if (e.data.t === 'viz' && this.onviz) this.onviz(e.data as DrumVizMessage);
      if (e.data.t === 'pos' && this.onpos) this.onpos({ step: e.data.step as number, bar: e.data.bar as number });
      if (e.data.t === 'clipstart' && this.onclipstart) this.onclipstart(e.data.frame as number);
      if (e.data.t === 'clipstop' && this.onclipstop) this.onclipstop(e.data.frame as number);
    };
    this.node.port.postMessage({ t: 'init', params: this.params });
    this.ready = true;
    this.pushTables();
    this.pushSamples();

    this.buildFx();
    this.fxChains.forEach((chain, i) => this.node.connect(chain.input, i));
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

    // -- shared master (the only stage after the per-pad chains) --
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
    this.masterGain.connect(this.dcBlock).connect(this.limiter).connect(this.output ?? ctx.destination);
    this.masterGain.connect(this.scopeAnalyser);

    this.fxChains = Array.from({ length: PAD_COUNT }, () => this.buildPadFx());
    for (let i = 0; i < PAD_COUNT; i++) this.renderImpulse(i);
  }

  buildPadFx(): PadFxChain {
    const ctx = this.ctx;
    const input = ctx.createGain();

    // -- drive --
    const driveOut = ctx.createGain();
    const driveShaper = ctx.createWaveShaper();
    driveShaper.oversample = '2x';
    const drivePre = ctx.createGain();
    input.connect(drivePre).connect(driveShaper);
    const driveMix = this.mkWetDry(input, driveOut);
    driveShaper.connect(driveMix.wet);

    // -- compressor --
    const compOut = ctx.createGain();
    const compressor = ctx.createDynamicsCompressor();
    compressor.ratio.value = 4;
    compressor.knee.value = 9;
    compressor.attack.value = 0.003;
    compressor.release.value = 0.25;
    const compMakeup = ctx.createGain();
    driveOut.connect(compressor).connect(compMakeup);
    const compMix = this.mkWetDry(driveOut, compOut);
    compMakeup.connect(compMix.wet);

    // -- chorus --
    const chorusOut = ctx.createGain();
    const merger = ctx.createChannelMerger(2);
    const chDelay1 = ctx.createDelay(0.1);
    const chDelay2 = ctx.createDelay(0.1);
    chDelay1.delayTime.value = 0.012;
    chDelay2.delayTime.value = 0.017;
    compOut.connect(chDelay1);
    compOut.connect(chDelay2);
    chDelay1.connect(merger, 0, 0);
    chDelay2.connect(merger, 0, 1);
    const chLfo = ctx.createOscillator();
    const chDepth1 = ctx.createGain();
    const chDepth2 = ctx.createGain();
    chLfo.connect(chDepth1).connect(chDelay1.delayTime);
    chLfo.connect(chDepth2).connect(chDelay2.delayTime);
    chDepth2.gain.value = -0.003;
    chLfo.start();
    const chorusMix = this.mkWetDry(compOut, chorusOut);
    merger.connect(chorusMix.wet);

    // -- ping-pong delay --
    const delayOut = ctx.createGain();
    const dlL = ctx.createDelay(2);
    const dlR = ctx.createDelay(2);
    const dlFb = ctx.createGain();
    const dlFb2 = ctx.createGain();
    const dlDamp = ctx.createBiquadFilter();
    dlDamp.type = 'lowpass';
    dlDamp.frequency.value = 4500;
    const dlMerge = ctx.createChannelMerger(2);
    const dlIn = ctx.createGain();
    chorusOut.connect(dlIn);
    dlIn.connect(dlL);
    dlL.connect(dlMerge, 0, 0);
    dlR.connect(dlMerge, 0, 1);
    dlL.connect(dlFb).connect(dlDamp).connect(dlR);
    dlR.connect(dlFb2).connect(dlL);
    const delayMix = this.mkWetDry(chorusOut, delayOut);
    dlMerge.connect(delayMix.wet);

    // -- reverb --
    const verbOut = ctx.createGain();
    const convolver = ctx.createConvolver();
    delayOut.connect(convolver);
    const verbMix = this.mkWetDry(delayOut, verbOut);
    convolver.connect(verbMix.wet);
    verbOut.connect(this.masterGain);

    return {
      input, driveShaper, drivePre, driveMix, compressor, compMakeup, compMix,
      chDelay1, chDelay2, chLfo, chDepth1, chDepth2, chorusMix,
      dlL, dlR, dlFb, dlFb2, delayMix, convolver, verbMix, verbTimer: 0,
    };
  }

  renderImpulse(padI: number): void {
    if (!this.ready) return;
    const chain = this.fxChains[padI];
    if (!chain) return;
    const size = this.params[pad(padI, 'fx.reverb.size')];
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
    chain.convolver.buffer = buf;
  }

  setMix(mix: WetDry, on: number, amount: number): void {
    if (!this.ready) return;
    const t = this.ctx.currentTime;
    const wet = on ? Math.sin((amount * Math.PI) / 2) : 0;
    const dry = on ? Math.cos((amount * Math.PI) / 2) : 1;
    mix.wet.gain.setTargetAtTime(wet, t, 0.02);
    mix.dry.gain.setTargetAtTime(dry, t, 0.02);
  }

  applyPadFx(padI: number): void {
    if (!this.ready) return;
    const p = this.params;
    const chain = this.fxChains[padI];
    if (!chain) return;
    const id = (field: string) => pad(padI, field);
    const t = this.ctx.currentTime;

    const amt = p[id('fx.drive.amt')];
    const { curve, preGain } = makeDriveCurve(amt);
    chain.driveShaper.curve = curve;
    chain.drivePre.gain.value = preGain;
    this.setMix(chain.driveMix, p[id('fx.drive.on')], p[id('fx.drive.mix')]);

    chain.compressor.threshold.setTargetAtTime(p[id('fx.comp.thr')], t, 0.02);
    chain.compMakeup.gain.setTargetAtTime(Math.pow(10, p[id('fx.comp.gain')] / 20), t, 0.02);
    this.setMix(chain.compMix, p[id('fx.comp.on')], 1);

    chain.chLfo.frequency.setTargetAtTime(p[id('fx.chorus.rate')], t, 0.05);
    const depth = 0.0008 + p[id('fx.chorus.depth')] * 0.0045;
    chain.chDepth1.gain.setTargetAtTime(depth, t, 0.05);
    chain.chDepth2.gain.setTargetAtTime(-depth * 0.8, t, 0.05);
    this.setMix(chain.chorusMix, p[id('fx.chorus.on')], p[id('fx.chorus.mix')] * 0.8);

    chain.dlL.delayTime.setTargetAtTime(p[id('fx.delay.time')], t, 0.08);
    chain.dlR.delayTime.setTargetAtTime(p[id('fx.delay.time')], t, 0.08);
    chain.dlFb.gain.setTargetAtTime(p[id('fx.delay.fb')], t, 0.02);
    chain.dlFb2.gain.setTargetAtTime(p[id('fx.delay.fb')], t, 0.02);
    this.setMix(chain.delayMix, p[id('fx.delay.on')], p[id('fx.delay.mix')] * 0.85);

    this.setMix(chain.verbMix, p[id('fx.reverb.on')], p[id('fx.reverb.mix')] * 0.9);
  }

  applyAllFx(): void {
    if (!this.ready) return;
    for (let i = 0; i < PAD_COUNT; i++) this.applyPadFx(i);

    const vol = this.params['master.volume'];
    this.masterGain.gain.setTargetAtTime(vol * vol * 1.6, this.ctx.currentTime, 0.02);
  }

  // ---------- parameter + transport API ----------
  setParam(id: string, v: number): void {
    // Accept the old global IDs as a compatibility API by broadcasting them.
    // New callers always use pad-scoped IDs.
    if (id.startsWith('fx.')) {
      for (let i = 0; i < PAD_COUNT; i++) this.params[pad(i, id)] = v;
      delete this.params[id];
    } else {
      this.params[id] = v;
    }
    if (!this.ready) return;
    if (isFxParam(id)) {
      const targets = id.startsWith('fx.')
        ? Array.from({ length: PAD_COUNT }, (_, i) => i)
        : [fxPadFromParam(id)].filter((i): i is number => i !== null);
      for (const padI of targets) {
        const chain = this.fxChains[padI];
        if (id.endsWith('fx.reverb.size')) {
          clearTimeout(chain.verbTimer);
          chain.verbTimer = setTimeout(() => this.renderImpulse(padI), 180);
        }
        this.applyPadFx(padI);
      }
      if (id === 'master.volume') this.applyAllFx();
    } else {
      this.node.port.postMessage({ t: 'p', k: id, v });
    }
  }

  applyAllParams(): void {
    if (!this.ready) return;
    this.node.port.postMessage({ t: 'init', params: this.params });
    this.applyAllFx();
    for (let i = 0; i < PAD_COUNT; i++) this.renderImpulse(i);
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
