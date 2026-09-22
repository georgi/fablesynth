import { describe, expect, it } from 'vitest';
import { cloneDrumRhythm, validateDrumRhythm, type DrumRhythm } from './rhythm';

const rhythm = (): DrumRhythm => ({
  v: 1,
  lanes: Array.from({ length: 16 }, (_, i) => i === 0 ? {
    enabled: true, sourceBar: 0, steps: 3, rotation: 1, timing: { mode: 'fit', cycleBeats: 4 as const },
  } : null),
});

describe('DR-1 POLY rhythm contract', () => {
  it('validates and deeply clones lane metadata', () => {
    const source = rhythm();
    expect(validateDrumRhythm(source, 1)).toBeNull();
    const copy = cloneDrumRhythm(source, 1)!;
    expect(copy).toEqual(source);
    expect(copy).not.toBe(source);
    expect(copy.lanes).not.toBe(source.lanes);
    expect(copy.lanes[0]).not.toBe(source.lanes[0]);
  });

  it('rejects invalid versions, dimensions, source bars, and rotations', () => {
    expect(validateDrumRhythm({ ...rhythm(), v: 2 }, 1)).toContain('version');
    expect(validateDrumRhythm({ ...rhythm(), lanes: [] }, 1)).toContain('sixteen');
    const bad = rhythm();
    bad.lanes[0]!.sourceBar = 1;
    expect(validateDrumRhythm(bad, 1)).toContain('sourceBar');
    bad.lanes[0]!.sourceBar = 0;
    bad.lanes[0]!.rotation = 3;
    expect(validateDrumRhythm(bad, 1)).toContain('rotation');
  });
});
