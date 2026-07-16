import { describe, expect, it } from 'vitest';
import { factorySession } from './factory';
import { b64ToBytes, WT_POLY_LANES, NOTE_STRIDE } from './protocol';

/** step → set of sounding pitches (o*12+n), honouring durations, for one WT-1 clip. */
function activePitches(pattern: string, bars: number): Map<number, Set<number>> {
  const bytes = b64ToBytes(pattern);
  const active = new Map<number, Set<number>>();
  for (let step = 0; step < bars * 16; step++) {
    for (let lane = 0; lane < WT_POLY_LANES; lane++) {
      const offset = (step * WT_POLY_LANES + lane) * NOTE_STRIDE;
      if (!(bytes[offset]! & 1)) continue;
      const duration = bytes[offset]! >> 2;
      const pitch = (bytes[offset + 2]! - 1) * 12 + (bytes[offset + 1]! & 0x7f);
      for (let t = step; t < Math.min(bars * 16, step + duration); t++) {
        if (!active.has(t)) active.set(t, new Set());
        active.get(t)!.add(pitch);
      }
    }
  }
  return active;
}

describe('NEON TALE factory session registers', () => {
  it('never sounds the same pitch on lead and pad at the same step', () => {
    const session = factorySession();
    for (const scene of session.scenes) {
      const lead = scene.clips[2];
      const pad = scene.clips[3];
      if (!lead || !pad) continue;
      const leadActive = activePitches(lead.pattern, lead.bars);
      const padActive = activePitches(pad.pattern, pad.bars);
      // Compare over the least common loop length of the two clips.
      const span = (lead.bars * pad.bars * 16) / (lead.bars === pad.bars ? lead.bars : 1);
      for (let t = 0; t < span; t++) {
        const l = leadActive.get(t % (lead.bars * 16)) ?? new Set();
        const p = padActive.get(t % (pad.bars * 16)) ?? new Set();
        for (const pitch of l) {
          expect(p.has(pitch), `${scene.name} step ${t} pitch ${pitch}`).toBe(false);
        }
      }
    }
  });

  it('keeps FOG pads at or below the root octave, under every lead', () => {
    const session = factorySession();
    // DROP A / DROP B / BREAK: the scenes where a lead and pad both play.
    for (const sceneIndex of [2, 3, 4]) {
      const pad = session.scenes[sceneIndex]!.clips[3]!;
      const bytes = b64ToBytes(pad.pattern);
      for (let i = 0; i < bytes.length; i += NOTE_STRIDE) {
        if (!(bytes[i]! & 1)) continue;
        const pitch = (bytes[i + 2]! - 1) * 12 + (bytes[i + 1]! & 0x7f);
        expect(pitch, `${pad.name} note`).toBeLessThanOrEqual(0);
      }
    }
  });
});
