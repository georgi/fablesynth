import { updateDrumLane } from '../drum/rhythm';
import { validateSession } from './protocol';
import { describe, expect, it } from 'vitest';
import { factorySession } from './factory';
import { embedSessionPatches } from './sessionExport';

describe('portable session exports', () => {
  it('embeds every factory device patch as a complete inline parameter map', () => {
    const exported = embedSessionPatches(factorySession());
    expect(exported.tracks.every((track) => track.patch.kind === 'inline')).toBe(true);
    for (const track of exported.tracks) {
      const data = track.patch.kind === 'inline' ? track.patch.data as { params?: Record<string, number> } : {};
      expect(data.params).toBeDefined();
      expect(Object.keys(data.params ?? {}).length).toBeGreaterThan(10);
    }
  });

  it('expands partial inline patches to a complete parameter map', () => {
    const session = factorySession();
    session.tracks[1].patch = { kind: 'inline', data: { params: { 'flt.cut': 123 } } };
    const patch = embedSessionPatches(session).tracks[1].patch;
    expect(patch).toMatchObject({ kind: 'inline', data: { params: { 'flt.cut': 123, 'master.volume': expect.any(Number) } } });
  });
});


it('preserves enabled and disabled POLY settings in portable session round trips', () => {
  const session = factorySession();
  const clip = session.scenes[0].clips[0]!;
  clip.drumRhythm = updateDrumLane(undefined, 0, { enabled: false, steps: 7, rotation: 2, delayMs: -12, stepDelayMs: [0, 23, -31] }, 0, clip.bars);
  clip.drumRhythm = updateDrumLane(clip.drumRhythm, 15, { enabled: true, steps: 3, timing: { mode: 'fit', cycleBeats: 8 } }, 0, clip.bars);
  const restored = JSON.parse(JSON.stringify(embedSessionPatches(session)));
  expect(validateSession(restored)).toBeNull();
  expect(restored.scenes[0].clips[0].drumRhythm).toEqual(clip.drumRhythm);
});
