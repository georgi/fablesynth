// The SQ-4 audio rig: one shared AudioContext hosting all four device
// engines, per-track gain (fader × mute) + analyser taps, and the single
// final limiter (docs/sq4-clips.md §7). The store talks to the rig through
// the SeqRig interface so tests can substitute a silent fake.

import { makeDevice, type SeqDevice } from './devices';
import type { SessionDoc } from './protocol';
import { masterFxParams, type MasterFxParams } from './masterFx';
import masterWorkletUrl from './master-worklet.js?url';
import sharedFxWorkletUrl from '../engine/ott-worklet.js?url';

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
  masterBus!: AudioWorkletNode;
  trackDelays: DelayNode[] = [];
  sampleRate = 48000;

  async init(session: SessionDoc): Promise<void> {
    const Ctor = window.AudioContext
      || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    const ctx = new Ctor({ latencyHint: 'interactive' });
    this.ctx = ctx;
    this.sampleRate = ctx.sampleRate;

    await ctx.audioWorklet.addModule(sharedFxWorkletUrl);
    await ctx.audioWorklet.addModule(masterWorkletUrl);
    this.masterBus = new AudioWorkletNode(ctx, 'fable-sq-master', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
    });
    this.masterBus.connect(ctx.destination);
    this.setMasterFx(masterFxParams(session.masterFx));

    this.trackAnalysers = [];
    const inits: Promise<void>[] = [];
    const deviceOutputs: GainNode[] = [];
    for (const track of session.tracks) {
      const gain = ctx.createGain();
      const analyser = ctx.createAnalyser();
      analyser.fftSize = 1024;
      this.trackGains.push(gain);
      this.trackAnalysers.push(analyser);

      const device = makeDevice(track.machine);
      this.devices.push(device);
      const deviceOutput = ctx.createGain();
      deviceOutputs.push(deviceOutput);
      gain.connect(this.masterBus);
      gain.connect(analyser);
      inits.push(device.init(ctx, deviceOutput).then(() => device.applyPatch(track.patch)));
    }
    await Promise.all(inits);

    // Worklets expose their internal latency, but the message that carries it
    // can arrive after init() resolves. Keep a deterministic fallback matching
    // the native 63/17 FIR contract and align every device before the shared
    // summing bus, including the drum pad+group pair.
    const limiter = Math.max(8, Math.round(0.0015 * ctx.sampleRate));
    const fallback = (machine: string) => machine === 'DR1' ? 70 + limiter : 35 + limiter;
    const latencies = this.devices.map((device, i) => device.latencySamples || fallback(session.tracks[i].machine));
    const longest = Math.max(...latencies);
    this.trackDelays = [];
    for (let i = 0; i < deviceOutputs.length; i++) {
      const delay = ctx.createDelay(1);
      delay.delayTime.value = Math.max(0, longest - latencies[i]) / ctx.sampleRate;
      deviceOutputs[i].connect(delay);
      delay.connect(this.trackGains[i]);
      this.trackDelays.push(delay);
    }
  }

  now(): number {
    return Math.round(this.ctx.currentTime * this.ctx.sampleRate);
  }

  setTrackGain(t: number, gain: number): void {
    const g = this.trackGains[t];
    if (g) g.gain.setTargetAtTime(gain, this.ctx.currentTime, 0.015);
  }

  setMasterGain(gain: number): void {
    this.masterBus.port.postMessage({ t: 'gain', value: gain });
  }

  setMasterFx(params: MasterFxParams): void {
    this.masterBus.port.postMessage({ t: 'params', params: masterFxParams(params) });
  }

  sendTempo(bpm: number, swing: number, anchor: number): void {
    for (const d of this.devices) d.setTempo(bpm, swing, anchor);
  }

  panic(): void {
    for (const d of this.devices) d.panic();
  }
}
