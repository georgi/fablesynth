import { describe, expect, it } from 'vitest';
import { makeEmptyPatterns as drumEmpty } from '../drum/seq';
import { makeEmptyPatterns as bassEmpty } from '../bass/seq';
import { bytesPerBar, emptyClipBytes } from './protocol';
import { clipToPatterns, patternsToClip } from './hostBridge';

describe('clip ↔ pattern codec', () => {
  it('round-trips DR1 bytes through the pattern buffer', () => {
    const clip = emptyClipBytes('DR1', 2);
    clip[0] = 1; // bar 0, pad 0, step 0
    clip[256 + 16] = 2; // bar 1, pad 1, step 0
    const pats = clipToPatterns(clip, drumEmpty());
    expect(pats.length).toBe(4 * 256); // NPATTERNS buffer
    expect(pats[0]).toBe(1);
    expect(pats[256 + 16]).toBe(2);
    expect(patternsToClip('DR1', pats, 2)).toEqual(clip);
  });

  it('round-trips BL1 bytes and keeps the neutral oct default beyond the clip', () => {
    const clip = emptyClipBytes('BL1', 1);
    clip[0] = 1; clip[1] = 7; // step 0 on, note 7
    const pats = clipToPatterns(clip, bassEmpty());
    expect(patternsToClip('BL1', pats, 1)).toEqual(clip);
    // growing to 2 bars pulls a silent-but-neutral bar 1 from the buffer
    const grown = patternsToClip('BL1', pats, 2);
    expect(grown.length).toBe(2 * bytesPerBar('BL1'));
    expect(grown[bytesPerBar('BL1') + 2]).toBe(1); // oct byte neutral
  });
});
