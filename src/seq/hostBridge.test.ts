import { describe, expect, it } from 'vitest';
import { makeEmptyPatterns as drumEmpty } from '../drum/seq';
import { makeEmptyPatterns as bassEmpty } from '../bass/seq';
import { makeEmptyPatterns as wtEmpty } from '../noteseq';
import { bytesPerBar, emptyClipBytes, wtNoteIdx } from './protocol';
import { clipToPatterns, patternsToClip } from './hostBridge';

describe('clip ↔ pattern codec', () => {
  it('round-trips DR1 bytes through the pattern buffer', () => {
    const clip = emptyClipBytes('DR1', 2);
    clip[0] = 1; // bar 0, pad 0, step 0
    clip[256 + 16] = 2; // bar 1, pad 1, step 0
    const pats = clipToPatterns('DR1', clip, drumEmpty());
    expect(pats.length).toBe(4 * 256); // NPATTERNS buffer
    expect(pats[0]).toBe(1);
    expect(pats[256 + 16]).toBe(2);
    expect(patternsToClip('DR1', pats, 2)).toEqual(clip);
  });

  it('round-trips BL1 bytes and keeps the neutral oct default beyond the clip', () => {
    const clip = emptyClipBytes('BL1', 1);
    clip[0] = 1; clip[1] = 7; // step 0 on, note 7
    const pats = clipToPatterns('BL1', clip, bassEmpty());
    expect(patternsToClip('BL1', pats, 1)).toEqual(clip);
    // growing to 2 bars pulls a silent-but-neutral bar 1 from the buffer
    const grown = patternsToClip('BL1', pats, 2);
    expect(grown.length).toBe(2 * bytesPerBar('BL1'));
    expect(grown[bytesPerBar('BL1') + 2]).toBe(1); // oct byte neutral
  });

  it('edits WT1 lane zero without discarding chord voices', () => {
    const clip = emptyClipBytes('WT1', 1);
    clip.set([1, 0, 1], wtNoteIdx(0, 0, 0));
    clip.set([1, 3, 1], wtNoteIdx(0, 0, 1));
    clip.set([1, 7, 1], wtNoteIdx(0, 0, 2));
    const pats = clipToPatterns('WT1', clip, wtEmpty());
    pats.set([1, 2, 1], 0);
    const result = patternsToClip('WT1', pats, 1, clip);
    expect(result.slice(wtNoteIdx(0, 0, 0), wtNoteIdx(0, 0, 0) + 3)).toEqual(Uint8Array.from([1, 2, 1]));
    expect(result.slice(wtNoteIdx(0, 0, 1), wtNoteIdx(0, 0, 1) + 3)).toEqual(Uint8Array.from([1, 3, 1]));
    expect(result.slice(wtNoteIdx(0, 0, 2), wtNoteIdx(0, 0, 2) + 3)).toEqual(Uint8Array.from([1, 7, 1]));
  });
});
