// Portable SQ-4 session serialization. Factory-bank positions are convenient
// in the UI, but exports must not rely on an identical bank being installed.

import { FACTORY_PATCHES as BASS_PATCHES, patchToState } from '../bass/patches';
import { defaultBassParams } from '../bass/params';
import { FACTORY_KITS, kitToState } from '../drum/kits';
import { defaultDrumParams } from '../drum/params';
import { defaultParams } from '../params';
import { FACTORY_PRESETS, resolvePresetMods } from '../presets';
import type { PatchDoc, SessionDoc } from './protocol';

function embeddedPatch(machine: SessionDoc['tracks'][number]['machine'], patch: PatchDoc): PatchDoc {
  if (patch.kind === 'inline') {
    const data = patch.data as { params?: Record<string, number> };
    const defaults = machine === 'DR1' ? defaultDrumParams() : machine === 'BL1' ? defaultBassParams() : defaultParams();
    return { kind: 'inline', data: { params: { ...defaults, ...data.params } } };
  }
  const index = patch.index;
  const params = machine === 'DR1'
    ? kitToState(FACTORY_KITS[index] ?? FACTORY_KITS[0]).params
    : machine === 'BL1'
      ? patchToState(BASS_PATCHES[index] ?? BASS_PATCHES[0]).params
      : resolvePresetMods((FACTORY_PRESETS[index] ?? FACTORY_PRESETS[0]).params, (FACTORY_PRESETS[index] ?? FACTORY_PRESETS[0]).mods);
  return { kind: 'inline', data: { params: { ...params } } };
}

/** A self-contained copy suitable for disk/session interchange. */
export function embedSessionPatches(session: SessionDoc): SessionDoc {
  return {
    ...session,
    tracks: session.tracks.map((track) => ({ ...track, patch: embeddedPatch(track.machine, track.patch) })),
    scenes: session.scenes.map((scene) => ({ ...scene, pass: scene.pass ? [...scene.pass] : undefined, clips: scene.clips.map((clip) => clip && { ...clip }) })),
  };
}
