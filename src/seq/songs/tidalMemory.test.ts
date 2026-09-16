import { describe, expect, it } from 'vitest';
import { defaultBassParams } from '../../bass/params';
import { defaultDrumParams } from '../../drum/params';
import { defaultParams } from '../../params';
import { b64ToBytes, bytesPerBar, HOSTED_MAX_BARS, validateSession } from '../protocol';
import { AUTHORED_SESSION_PRESETS, copySession, FACTORY_SESSION_PRESETS, GENERATED_SESSION_PRESETS } from '../sessionPresets';
import { exportSessionJson, importSessionJson } from '../sessionLibrary';
import { TIDAL_MEMORY } from './tidalMemory';
import { PHASE_RUNNER } from './phaseRunner';
import { LATE_CHECKOUT } from './lateCheckout';

describe('authored SQ-4 pilot', () => {
  it('appends the pilot without moving existing host programs', () => {
    expect(FACTORY_SESSION_PRESETS).toHaveLength(42);
    expect(FACTORY_SESSION_PRESETS.slice(0, 40)).toEqual(GENERATED_SESSION_PRESETS);
    expect(FACTORY_SESSION_PRESETS[40]).toBe(TIDAL_MEMORY);
    expect(AUTHORED_SESSION_PRESETS).toEqual([TIDAL_MEMORY, PHASE_RUNNER]);
  });

  it.each([...AUTHORED_SESSION_PRESETS, { name: LATE_CHECKOUT.name, session: LATE_CHECKOUT }])('round-trips $name with its custom sounds', ({ session }) => {
    expect(validateSession(session)).toBeNull();
    const loaded = importSessionJson(exportSessionJson(session));
    expect('session' in loaded).toBe(true);
    if (!('session' in loaded)) throw new Error(loaded.error);
    expect(loaded.session.scenes).toEqual(session.scenes);
    expect(loaded.session.tracks).toEqual(session.tracks);
    for (const scene of session.scenes) {
      scene.clips.forEach((clip, track) => {
        if (!clip) return;
        expect(clip.bars).toBeLessThanOrEqual(HOSTED_MAX_BARS);
        const bytes = b64ToBytes(clip.pattern);
        expect(bytes.length).toBe(clip.bars * bytesPerBar(session.tracks[track].machine));
        if (track === 0) return;
        let active = 0;
        for (let offset = 0; offset < bytes.length; offset += 3) {
          if (!(bytes[offset] & 1)) continue;
          active++;
          expect(bytes[offset] >> 2).toBeGreaterThan(0);
          expect(bytes[offset + 1]).toBeLessThan(12);
          expect(bytes[offset + 2]).toBeLessThanOrEqual(2);
        }
        expect(active).toBeGreaterThan(0);
      });
    }
    session.tracks.forEach((track) => {
      expect(track.patch.kind).toBe('inline');
      if (track.patch.kind !== 'inline') return;
      const params = (track.patch.data as { params: Record<string, number> }).params;
      const defaults = track.machine === 'DR1' ? defaultDrumParams() : track.machine === 'BL1' ? defaultBassParams() : defaultParams();
      expect(Object.keys(params).sort()).toEqual(Object.keys(defaults).sort());
      expect(Object.values(params).every(Number.isFinite)).toBe(true);
    });
  });

  it('keeps patch edits on a recalled session out of the factory score', () => {
    const originalPatch = TIDAL_MEMORY.session.tracks[2].patch;
    if (originalPatch.kind !== 'inline') throw new Error('Expected custom chord patch');
    const originalCutoff = (originalPatch.data as { params: Record<string, number> }).params['filter.cutoff'];
    const recalled = copySession(TIDAL_MEMORY.session);
    const patch = recalled.tracks[2].patch;
    if (patch.kind !== 'inline') throw new Error('Expected custom chord patch');
    (patch.data as { params: Record<string, number> }).params['filter.cutoff'] = 999;
    recalled.scenes[0].clips[2]!.name = 'EDIT';
    expect((originalPatch.data as { params: Record<string, number> }).params['filter.cutoff']).toBe(originalCutoff);
    expect(TIDAL_MEMORY.session.scenes[0].clips[2]!.name).toBe('DISTANT CHORD');
    expect(copySession(TIDAL_MEMORY.session)).not.toEqual(recalled);
    expect(copySession(TIDAL_MEMORY.session).tracks[2].patch).toEqual(TIDAL_MEMORY.session.tracks[2].patch);
  });
});
