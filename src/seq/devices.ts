// SeqDevice adapters — one per machine, each a thin wrapper over the
// existing engine class translating the uniform clip commands of
// docs/sq4-clips.md §8 into that engine's messages. Devices execute stamped
// commands; all musical decisions stay in the conductor (store.ts).

import { BassEngine } from '../bass/engine/bass-synth';
import { FACTORY_PATCHES, patchToState } from '../bass/patches';
import { DrumEngine } from '../drum/engine/drum-synth';
import { FACTORY_KITS, kitToState } from '../drum/kits';
import { SynthEngine } from '../engine/synth';
import { FACTORY_PRESETS, resolvePresetMods } from '../presets';
import type { PatchDoc } from './protocol';

export interface SeqDevice {
  init(ctx: AudioContext, output: AudioNode): Promise<void>;
  applyPatch(patch: PatchDoc): void;
  setTempo(bpm: number, swing: number, anchor: number): void;
  scheduleClip(pattern: Uint8Array, bars: number, atFrame: number): void;
  scheduleStop(atFrame: number): void;
  panic(): void;
  onClipStart: ((frame: number) => void) | null;
  onClipStop: ((frame: number) => void) | null;
  onPos: ((step: number, bar: number) => void) | null;
}

// The three engines expose an identical hosted surface (init opts, host/
// tempo/clip messages, callbacks) — this base class holds everything except
// patch application.
abstract class EngineDevice<E extends DrumEngine | BassEngine | SynthEngine> implements SeqDevice {
  engine: E;
  onClipStart: ((frame: number) => void) | null = null;
  onClipStop: ((frame: number) => void) | null = null;
  onPos: ((step: number, bar: number) => void) | null = null;

  constructor(engine: E) {
    this.engine = engine;
  }

  async init(ctx: AudioContext, output: AudioNode): Promise<void> {
    await this.engine.init({ ctx, output });
    this.engine.setHostMode(true);
    this.engine.onclipstart = (frame) => this.onClipStart?.(frame);
    this.engine.onclipstop = (frame) => this.onClipStop?.(frame);
    this.engine.onpos = (d: { step: number; bar: number }) => this.onPos?.(d.step, d.bar);
  }

  abstract applyPatch(patch: PatchDoc): void;

  setTempo(bpm: number, swing: number, anchor: number): void {
    this.engine.setTempo(bpm, swing, anchor);
  }

  scheduleClip(pattern: Uint8Array, bars: number, atFrame: number): void {
    this.engine.scheduleClip(pattern, bars, atFrame);
  }

  scheduleStop(atFrame: number): void {
    this.engine.scheduleStop(atFrame);
  }

  panic(): void {
    this.engine.panic();
  }

  protected applyParams(params: Record<string, number>): void {
    this.engine.params = { ...params };
    this.engine.applyAllParams();
  }
}

export class Dr1Device extends EngineDevice<DrumEngine> {
  constructor() {
    super(new DrumEngine());
  }

  applyPatch(patch: PatchDoc): void {
    // v1: factory kits only; inline kit payloads are a session-editing (v2)
    // feature — fall back to kit 0 rather than playing an unpatched engine.
    const index = patch.kind === 'factory' ? patch.index : 0;
    const kit = FACTORY_KITS[index] ?? FACTORY_KITS[0];
    this.applyParams(kitToState(kit).params);
  }
}

export class Bl1Device extends EngineDevice<BassEngine> {
  constructor() {
    super(new BassEngine());
  }

  applyPatch(patch: PatchDoc): void {
    const index = patch.kind === 'factory' ? patch.index : 0;
    const p = FACTORY_PATCHES[index] ?? FACTORY_PATCHES[0];
    this.applyParams(patchToState(p).params);
  }
}

export class Wt1Device extends EngineDevice<SynthEngine> {
  constructor() {
    super(new SynthEngine());
  }

  applyPatch(patch: PatchDoc): void {
    const index = patch.kind === 'factory' ? patch.index : 0;
    const p = FACTORY_PRESETS[index] ?? FACTORY_PRESETS[0];
    this.applyParams(resolvePresetMods(p.params, p.mods));
  }
}

/** Display name of a track's patch (the head-card chip). */
export function patchName(machine: 'DR1' | 'BL1' | 'WT1', patch: PatchDoc): string {
  if (patch.kind !== 'factory') return 'CUSTOM';
  switch (machine) {
    case 'DR1': return FACTORY_KITS[patch.index]?.name ?? 'KIT ?';
    case 'BL1': return FACTORY_PATCHES[patch.index]?.name ?? 'PATCH ?';
    default: return FACTORY_PRESETS[patch.index]?.name ?? 'PRESET ?';
  }
}

export function makeDevice(machine: 'DR1' | 'BL1' | 'WT1'): SeqDevice {
  switch (machine) {
    case 'DR1': return new Dr1Device();
    case 'BL1': return new Bl1Device();
    default: return new Wt1Device();
  }
}
