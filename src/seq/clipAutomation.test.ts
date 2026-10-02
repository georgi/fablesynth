import { describe, expect, it } from 'vitest';
import {
  addPoint, autoTargetLabel, autoTargets, bendSegment, compileClipAutomation, drawLine, evalLane, lanePhase,
  laneCycle, newAutoLane, paintCell, validateAutomation, type AutoLane,
} from './clipAutomation';
import type { ClipDoc } from './protocol';

const clip = (automation: AutoLane[], bars = 1): ClipDoc => ({ name: 'A', bars, pattern: '', automation });

describe('clip automation curves', () => {
  it('interpolates ramps and wraps the last point to the first', () => {
    const pts = [{ t: 0, v: 0 }, { t: 8, v: 1 }];
    expect(evalLane(pts, 16, 4)).toBeCloseTo(0.5);
    expect(evalLane(pts, 16, 12)).toBeCloseTo(0.5);
    expect(evalLane([{ t: 4, v: 0.3 }], 16, 0)).toBeCloseTo(0.3);
    expect(evalLane([], 16, 0)).toBeNull();
  });

  it('bends a segment without moving its end points', () => {
    const pts = bendSegment([{ t: 0, v: 0 }, { t: 8, v: 1 }], 0, 0.8);
    expect(evalLane(pts, 16, 0)).toBeCloseTo(0);
    expect(evalLane(pts, 16, 4)!).toBeLessThan(0.3);
    expect(evalLane(pts, 16, 7.999)!).toBeCloseTo(1, 2);
  });

  it('paints held cells and keeps the rest of the curve', () => {
    const ramp = [{ t: 0, v: 0 }, { t: 15, v: 1 }];
    const pts = paintCell(ramp, 16, 4.6, 0.9, 1);
    expect(evalLane(pts, 16, 4)).toBeCloseTo(0.9);
    expect(evalLane(pts, 16, 4.9)).toBeCloseTo(0.9);
    expect(evalLane(pts, 16, 3)).toBeCloseTo(3 / 15);
    expect(evalLane(pts, 16, 10)).toBeCloseTo(10 / 15);
    // Painting neighbouring cells leaves one point per cell edge.
    const two = paintCell(pts, 16, 5, 0.2, 1);
    expect(two.filter(p => p.t === 5)).toHaveLength(1);
  });

  it('draws a straight line over the existing curve', () => {
    const pts = drawLine([{ t: 0, v: 0.5, hold: true }], 16, 12, 1, 4, 0);
    expect(evalLane(pts, 16, 2)).toBeCloseTo(0.5);
    expect(evalLane(pts, 16, 8)).toBeCloseTo(0.5);
    expect(evalLane(pts, 16, 11)).toBeCloseTo(7 / 8);
    expect(evalLane(pts, 16, 14)).toBeCloseTo(0.5);
  });

  it('adds points on the quarter-step grid', () => {
    const { points, index } = addPoint([{ t: 0, v: 0 }], 3.13, 0.4);
    expect(points[index]).toEqual({ t: 3.25, v: 0.4 });
  });
});

describe('POLY timing', () => {
  it('cycles GRID lanes independently of the clip', () => {
    const lane = { ...newAutoLane('filter.cutoff'), time: { mode: 'grid' as const, steps: 3 } };
    const cycle = laneCycle(lane, 4);
    expect(lanePhase(cycle, 7)).toBeCloseTo(1);
  });

  it('fits FIT slots into the bar cycle', () => {
    const lane = { ...newAutoLane('filter.cutoff'), time: { mode: 'fit' as const, steps: 5, cycleBeats: 4 as const } };
    const cycle = laneCycle(lane, 1);
    expect(lanePhase(cycle, 8)).toBeCloseTo(2.5);
  });

  it('follows the target pad POLY lane and falls back to the clip', () => {
    const lane = { ...newAutoLane('pad2.lvl'), time: { mode: 'pad' as const } };
    const lanes = Array(16).fill(null);
    lanes[2] = { enabled: true, sourceBar: 0, steps: 7, rotation: 2, timing: { mode: 'fit', cycleBeats: 8 } };
    expect(laneCycle(lane, 2, { v: 1, lanes })).toEqual({ len: 7, fit: 8, rot: 2, follows: true });
    expect(laneCycle(lane, 2, null).len).toBe(32);
  });
});

describe('automation documents', () => {
  it('validates targets per machine', () => {
    expect(validateAutomation([newAutoLane('filter.cutoff')], 'WT1')).toBeNull();
    expect(validateAutomation([newAutoLane('filter.type')], 'WT1')).toMatch(/target/);
    expect(validateAutomation([newAutoLane('seq.bpm')], 'DR1')).toMatch(/target/);
    expect(validateAutomation([{ ...newAutoLane('flt.cut'), time: { mode: 'pad' } }], 'BL1')).toMatch(/PAD/);
  });

  it('compiles absolute values in the parameter curve', () => {
    const lane = { ...newAutoLane('filter.cutoff'), points: [{ t: 0, v: 0.5, hold: true }] };
    const [config] = compileClipAutomation(clip([lane]), 'WT1');
    expect(config.len).toBe(16);
    expect(config.table[0]).toBeCloseTo(Math.sqrt(20 * 20000), 3);
  });

  it('skips disabled, empty, and shadowed lanes', () => {
    const a = { ...newAutoLane('oscA.pos'), points: [{ t: 0, v: 1 }] };
    const lanes = [a, { ...a, points: [{ t: 0, v: 0 }] }, { ...newAutoLane('oscB.pos') }, { ...a, target: 'oscA.level', enabled: false }];
    const out = compileClipAutomation(clip(lanes), 'WT1');
    expect(out.map(c => c.k)).toEqual(['oscA.pos']);
    expect(out[0].table[0]).toBe(1);
  });

  it('labels and groups targets', () => {
    expect(autoTargetLabel('WT1', 'filter.cutoff')).toBe('F1 CUTOFF');
    expect(autoTargetLabel('DR1', 'pad3.lvl')).toBe('P04 LVL');
    const drum = autoTargets('DR1', 3).flatMap(g => g.ids);
    expect(drum).toContain('pad3.lvl');
    expect(drum).not.toContain('pad2.lvl');
  });
});
