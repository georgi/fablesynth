// The SQ-4 audio rig: one shared AudioContext hosting all four device
// engines, per-track gain (fader × mute) + analyser taps, and the single
// final limiter (docs/sq4-clips.md §7). The store talks to the rig through
// the SeqRig interface so tests can substitute a silent fake.

import { makeDevice, type SeqDevice } from './devices';
import type { SessionDoc } from './protocol';
import { masterFxParams, type MasterFxParams } from './masterFx';
import masterWorkletUrl from './master-worklet.js?url';

export interface SeqRig {
  sampleRate: number;
  devices: SeqDevice[];
  /** Current context frame (the shared timebase). */
  now(): number;
  setTrackGain(t: number, gain: number): void;
  setMasterGain(gain: number): void;
  /** Optional while test rigs migrate; the browser rig always implements it. */
  setMasterFx?(params: MasterFxParams): void;
  sendTempo(bpm: number, swing: number, anchor: number): void;
  panic(): void;
  /** Per-track post-fader analysers (scope traces + VU), null in fakes. */
  trackAnalysers: AnalyserNode[] | null;
}

export class WebAudioRig implements SeqRig {
  ctx!: AudioContext;
  devices: SeqDevice[] = [];
  trackGains: GainNode[] = [];
  trackAnalysers: AnalyserNode[] | null = null;
  masterGain!: GainNode;
  masterEq: BiquadFilterNode[] = [];
  masterOtt!: AudioWorkletNode;
  masterComp!: DynamicsCompressorNode;
  limiter!: DynamicsCompressorNode;
  sampleRate = 48000;

  async init(session: SessionDoc): Promise<void> {
    const Ctor = window.AudioContext
      || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    const ctx = new Ctor({ latencyHint: 'interactive' });
    this.ctx = ctx;
    this.sampleRate = ctx.sampleRate;

    await ctx.audioWorklet.addModule(masterWorkletUrl);
    this.masterGain = ctx.createGain();
    this.masterEq = Array.from({ length: 4 }, () => ctx.createBiquadFilter());
    this.masterOtt = new AudioWorkletNode(ctx, 'fable-sq-master-ott', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
    this.masterComp = ctx.createDynamicsCompressor();
    this.masterComp.threshold.value = -16; this.masterComp.knee.value = 9; this.masterComp.ratio.value = 4;
    this.masterComp.attack.value = 0.003; this.masterComp.release.value = 0.25;
    this.limiter = ctx.createDynamicsCompressor();
    this.limiter.threshold.value = -1;
    this.limiter.knee.value = 0;
    this.limiter.ratio.value = 20;
    this.limiter.attack.value = 0.002;
    this.limiter.release.value = 0.25;
    this.masterGain.connect(this.masterEq[0]);
    this.masterEq.reduce((from, to) => { from.connect(to); return to; }).connect(this.masterOtt).connect(this.masterComp).connect(this.limiter).connect(ctx.destination);
    this.setMasterFx(masterFxParams(session.masterFx));

    this.trackAnalysers = [];
    const inits: Promise<void>[] = [];
    for (const track of session.tracks) {
      const gain = ctx.createGain();
      const analyser = ctx.createAnalyser();
      analyser.fftSize = 1024;
      gain.connect(this.masterGain);
      gain.connect(analyser);
      this.trackGains.push(gain);
      this.trackAnalysers.push(analyser);

      const device = makeDevice(track.machine);
      this.devices.push(device);
      inits.push(device.init(ctx, gain).then(() => device.applyPatch(track.patch)));
    }
    await Promise.all(inits);
  }

  now(): number {
    return Math.round(this.ctx.currentTime * this.ctx.sampleRate);
  }

  setTrackGain(t: number, gain: number): void {
    const g = this.trackGains[t];
    if (g) g.gain.setTargetAtTime(gain, this.ctx.currentTime, 0.015);
  }

  setMasterGain(gain: number): void {
    this.masterGain.gain.setTargetAtTime(gain, this.ctx.currentTime, 0.015);
  }

  setMasterFx(params: MasterFxParams): void {
    const p = masterFxParams(params), now = this.ctx.currentTime;
    const bands = [
      ['l', 'low', 'lfreq', 'lq', 'ltype', 'lon'], ['m', 'mid', 'mfreq', 'mq', 'mtype', 'mon'],
      ['m2', 'mid2', 'm2freq', 'm2q', 'm2type', 'm2on'], ['h', 'high', 'hfreq', 'hq', 'htype', 'hon'],
    ] as const;
    bands.forEach(([tag, gain, freq, q, type, on], i) => {
      const node = this.masterEq[i], shape = Math.round(p[`master.fx.eq.${type}`]);
      node.type = shape === 0 ? 'lowshelf' : shape === 2 ? 'highshelf' : 'peaking';
      node.frequency.setTargetAtTime(p[`master.fx.eq.${freq}`], now, .015);
      node.Q.setTargetAtTime(p[`master.fx.eq.${q}`], now, .015);
      node.gain.setTargetAtTime(p['master.fx.eq.on'] > .5 && p[`master.fx.eq.${on}`] > .5 ? p[`master.fx.eq.${gain}`] : 0, now, .015);
      void tag;
    });
    this.masterOtt.port.postMessage({ t: 'params', params: { on: p['master.fx.ott.on'], depth: p['master.fx.ott.depth'], time: p['master.fx.ott.time'], up: p['master.fx.ott.up'], down: p['master.fx.ott.down'] } });
    const compOn = p['master.fx.comp.on'] > .5;
    this.masterComp.threshold.setTargetAtTime(compOn ? p['master.fx.comp.thr'] : 0, now, .015);
    this.masterComp.attack.setTargetAtTime(p['master.fx.comp.att'], now, .015);
    this.masterComp.release.setTargetAtTime(p['master.fx.comp.rel'], now, .015);
    this.masterComp.ratio.setTargetAtTime(compOn ? p['master.fx.comp.ratio'] : 1, now, .015);
    this.limiter.threshold.setTargetAtTime(p['master.fx.limiter.on'] > .5 ? p['master.fx.limiter.ceiling'] : 0, now, .015);
  }

  sendTempo(bpm: number, swing: number, anchor: number): void {
    for (const d of this.devices) d.setTempo(bpm, swing, anchor);
  }

  panic(): void {
    for (const d of this.devices) d.panic();
  }
}
