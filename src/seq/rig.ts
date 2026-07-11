// The SQ-4 audio rig: one shared AudioContext hosting all four device
// engines, per-track gain (fader × mute) + analyser taps, and the single
// final limiter (docs/sq4-clips.md §7). The store talks to the rig through
// the SeqRig interface so tests can substitute a silent fake.

import { makeDevice, type SeqDevice } from './devices';
import type { SessionDoc } from './protocol';

export interface SeqRig {
  sampleRate: number;
  devices: SeqDevice[];
  /** Current context frame (the shared timebase). */
  now(): number;
  suspend(): Promise<void>;
  resume(): Promise<void>;
  setTrackGain(t: number, gain: number): void;
  setMasterGain(gain: number): void;
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
  limiter!: DynamicsCompressorNode;
  sampleRate = 48000;

  async init(session: SessionDoc): Promise<void> {
    const Ctor = window.AudioContext
      || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    const ctx = new Ctor({ latencyHint: 'interactive' });
    this.ctx = ctx;
    this.sampleRate = ctx.sampleRate;

    this.masterGain = ctx.createGain();
    this.limiter = ctx.createDynamicsCompressor();
    this.limiter.threshold.value = -6;
    this.limiter.knee.value = 4;
    this.limiter.ratio.value = 12;
    this.limiter.attack.value = 0.002;
    this.limiter.release.value = 0.25;
    this.masterGain.connect(this.limiter).connect(ctx.destination);

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

  async suspend(): Promise<void> {
    await this.ctx.suspend();
  }

  async resume(): Promise<void> {
    await this.ctx.resume();
  }

  setTrackGain(t: number, gain: number): void {
    const g = this.trackGains[t];
    if (g) g.gain.setTargetAtTime(gain, this.ctx.currentTime, 0.015);
  }

  setMasterGain(gain: number): void {
    this.masterGain.gain.setTargetAtTime(gain, this.ctx.currentTime, 0.015);
  }

  sendTempo(bpm: number, swing: number, anchor: number): void {
    for (const d of this.devices) d.setTempo(bpm, swing, anchor);
  }

  panic(): void {
    for (const d of this.devices) d.panic();
  }
}
