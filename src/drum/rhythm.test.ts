import { describe, expect, it } from 'vitest';
import { cloneDrumRhythm, drumFitSteps, validateDrumRhythm, type DrumRhythm } from './rhythm';

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

describe('FIT timeline positions', () => {
  it('places three events at exact thirds of a bar', () => {
    const lane = { ...rhythm().lanes[0]!, rotation: 0 };
    const positions = drumFitSteps(lane, 0)!;
    expect(positions.map(p => p.phase)).toEqual([0, 1 / 3, 2 / 3]);
    expect(positions.map(p => p.beat)).toEqual([0, 4 / 3, 8 / 3]);
    expect(positions.map(p => p.sourceStep)).toEqual([0, 1, 2]);
  });
  it('maps a rotated five-step phrase onto its two-bar playback times', () => {
    const lane = { ...rhythm().lanes[0]!, steps: 5, rotation: 2, timing: { mode: 'fit' as const, cycleBeats: 8 as const } };
    const positions = drumFitSteps(lane, 0)!;
    expect(positions.map(p => p.sourceStep)).toEqual([3, 4, 0, 1, 2]);
    expect(positions.map(p => p.beat)).toEqual([0, 1.6, 3.2, 4.8, 6.4]);
    // At 120 BPM / 48 kHz these are the DSP's established FIT fixture times.
    expect(positions.map(p => p.beat * 24000)).toEqual([0, 38400, 76800, 115200, 153600]);
  });
  it('keeps disabled lanes, GRID, and other source bars in their normal grid', () => {
    const lane = rhythm().lanes[0]!;
    expect(drumFitSteps({ ...lane, enabled: false }, 0)).toBeNull();
    expect(drumFitSteps({ ...lane, timing: { mode: 'grid' } }, 0)).toBeNull();
    expect(drumFitSteps(lane, 1)).toBeNull();
  });
});

it('validates signed timing and deep copies step offsets', () => {
  const data = rhythm(); data.lanes[0]!.delayMs = -50; data.lanes[0]!.stepDelayMs = [50, -12];
  expect(validateDrumRhythm(data)).toBeNull();
  const copy = cloneDrumRhythm(data)!; copy.lanes[0]!.stepDelayMs![1] = 9;
  expect(data.lanes[0]!.stepDelayMs![1]).toBe(-12);
  for (const value of [NaN, Infinity, 51, -51, .5]) {
    data.lanes[0]!.delayMs = value; expect(validateDrumRhythm(data)).toContain('delay');
  }
});
