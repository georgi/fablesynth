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
  updateClip(pattern: Uint8Array, bars: number): void;
  panic(): void;
  onClipStart: ((frame: number) => void) | null;
  onClipStop: ((frame: number) => void) | null;
  onPos: ((step: number, bar: number) => void) | null;
  /** The live engine behind this device (absent on test fakes). */
  readonly engine?: DrumEngine | BassEngine | SynthEngine;
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

  updateClip(pattern: Uint8Array, bars: number): void {
    this.engine.updateClip(pattern, bars);
  }

  panic(): void {
    this.engine.panic();
  }

  protected applyParams(params: Record<string, number>): void {
    this.engine.params = { ...params };
    this.engine.applyAllParams();
  }
}

// Hosted patch edits snapshot raw engine params ({ params }) — docs spec
// 2026-07-12-sq4-device-focus-design.md §4. Pad names / user tables are v2.
function inlineParams(patch: PatchDoc): Record<string, number> | null {
  if (patch.kind !== 'inline') return null;
  const data = patch.data as { params?: Record<string, number> } | null;
  return data?.params ?? null;
}

export class Dr1Device extends EngineDevice<DrumEngine> {
  constructor() {
    super(new DrumEngine());
  }

  applyPatch(patch: PatchDoc): void {
    const inline = inlineParams(patch);
    if (inline) {
      this.applyParams(inline);
      return;
    }
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
    const inline = inlineParams(patch);
    if (inline) {
      this.applyParams(inline);
      return;
    }
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
    const inline = inlineParams(patch);
    if (inline) {
      this.applyParams(inline);
      return;
    }
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
