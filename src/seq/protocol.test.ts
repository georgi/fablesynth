import { describe, expect, it } from 'vitest';
import { factorySession } from './factory';
import { previewSteps } from './model';
import {
  b64ToBytes, barFrames, boundaryFrame, bytesPerBar, bytesToB64, dr1Idx,
  noteIdx, samplesPerBeat, songPosition, validateSession,
} from './protocol';

describe('base64 codec', () => {
  it('round-trips arbitrary byte patterns at every length mod 3', () => {
    for (const len of [0, 1, 2, 3, 47, 48, 49, 256, 2048]) {
      const u8 = new Uint8Array(len);
      for (let i = 0; i < len; i++) u8[i] = (i * 37 + 11) & 0xff;
      expect(b64ToBytes(bytesToB64(u8))).toEqual(u8);
    }
  });
});

describe('boundary math', () => {
  const sr = 48000;
  const bpm = 120; // beat = 24000 frames, bar = 96000

  it('quant OFF is immediate (frame 0 = "this block")', () => {
    expect(boundaryFrame('OFF', 500000, 1000, bpm, sr)).toBe(0);
  });

  it('1/4 snaps up to the next beat from the anchor', () => {
    expect(boundaryFrame('1/4', 1000, 1000, bpm, sr)).toBe(1000); // exactly on
    expect(boundaryFrame('1/4', 1001, 1000, bpm, sr)).toBe(25000);
    expect(boundaryFrame('1/4', 25000, 1000, bpm, sr)).toBe(25000);
  });

  it('1 BAR snaps up to the next bar from the anchor', () => {
    expect(boundaryFrame('1 BAR', 1001, 1000, bpm, sr)).toBe(97000);
    expect(boundaryFrame('1 BAR', 97000, 1000, bpm, sr)).toBe(97000);
    expect(boundaryFrame('1 BAR', 97001, 1000, bpm, sr)).toBe(193000);
  });

  it('a boundary is identical for any caller at the same now', () => {
    // sync guarantee: the frame is pure arithmetic, not per-device state
    const f1 = boundaryFrame('1 BAR', 123456, 1000, 122, 44100);
    const f2 = boundaryFrame('1 BAR', 123456, 1000, 122, 44100);
    expect(f1).toBe(f2);
    expect(f1).toBeGreaterThanOrEqual(123456);
    expect((f1 - 1000) % barFrames(122, 44100)).toBeCloseTo(0, 6);
  });

  it('songPosition counts beats and bars from the anchor', () => {
    expect(songPosition(1000, 1000, bpm, sr)).toEqual({ beat: 0, bar: 1 });
    expect(songPosition(1000 + samplesPerBeat(bpm, sr) * 5, 1000, bpm, sr)).toEqual({ beat: 1, bar: 2 });
  });
});

describe('factory session', () => {
  const doc = factorySession();

  it('validates against the schema and machine layouts', () => {
    expect(validateSession(doc)).toBeNull();
  });

  it('matches the mock: 6 scenes × 4 tracks with the right clip slots', () => {
    expect(doc.scenes.map((s) => s.name)).toEqual(['INTRO', 'BUILD', 'DROP A', 'DROP B', 'BREAK', 'OUTRO']);
    expect(doc.tracks.map((t) => t.machine)).toEqual(['DR1', 'BL1', 'WT1', 'WT1']);
    // BREAK has no drums; INTRO/OUTRO have no bass or lead
    expect(doc.scenes[4].clips[0]).toBeNull();
    expect(doc.scenes[0].clips[1]).toBeNull();
    expect(doc.scenes[5].clips[2]).toBeNull();
    // every scene has at least one clip
    for (const sc of doc.scenes) expect(sc.clips.some(Boolean)).toBe(true);
  });

  it('drum clips keep four-on-the-floor kicks where expected', () => {
    const drop = doc.scenes[2].clips[0]!;
    const bytes = b64ToBytes(drop.pattern);
    for (let bar = 0; bar < drop.bars; bar++) {
      for (const s of [0, 4, 8, 12]) {
        expect(bytes[dr1Idx(bar, 0, s)]).toBeGreaterThan(0);
      }
    }
  });

  it('note clips stay within lane/oct ranges and ties never open a clip', () => {
    doc.scenes.forEach((sc) => {
      sc.clips.forEach((c, t) => {
        if (!c || doc.tracks[t].machine === 'DR1') return;
        const bytes = b64ToBytes(c.pattern);
        const steps = c.bars * 16;
        expect(bytes.length).toBe(steps * 3);
        for (let i = 0; i < steps; i++) {
          const o = noteIdx(Math.floor(i / 16), i % 16);
          expect(bytes[o + 1]).toBeLessThan(12);
          expect(bytes[o + 2]).toBeGreaterThanOrEqual(0);
          expect(bytes[o + 2]).toBeLessThanOrEqual(2);
        }
        // step 0 must not tie in — there is nothing before it at launch
        expect(bytes[0] & 4).toBe(0);
      });
    });
  });

  it('previews derive from real bytes', () => {
    const kit = b64ToBytes(doc.scenes[2].clips[0]!.pattern);
    const pv = previewSteps('DR1', kit, 2);
    expect(pv).toHaveLength(16);
    expect(pv[0].on).toBe(true); // downbeat kick
    const bass = b64ToBytes(doc.scenes[2].clips[1]!.pattern);
    const pvB = previewSteps('BL1', bass, 1);
    expect(pvB[0].on).toBe(true);
  });

  it('clip payload sizes match bytesPerBar', () => {
    expect(bytesPerBar('DR1')).toBe(256);
    expect(bytesPerBar('BL1')).toBe(48);
    expect(bytesPerBar('WT1')).toBe(48);
  });
});
