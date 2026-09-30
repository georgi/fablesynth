import { beforeEach, describe, expect, it, vi } from 'vitest';
import { drumEngine, useDrumStore } from './store';
import { cloneDrumSequence, updateDrumLane } from './rhythm';
import { patIdx } from './seq';

const st = useDrumStore.getState;
beforeEach(() => {
  useDrumStore.setState({ patterns: new Uint8Array(1024), chain: [0], drumRhythm: undefined,
    editPattern: 2, sel: 0, hosted: false, kitDirty: false });
  st()._clearHistory();
});
describe('complete DR-1 sequence transactions', () => {
  it('silently selects lanes and enables from bar one', () => {
    const trigger = vi.spyOn(drumEngine, 'trigger');
    st().selectLane(7);
    expect(trigger).not.toHaveBeenCalled();
    expect(st().drumRhythm).toBeUndefined();
    st().setLaneEnabled(7, true);
    expect(st().drumRhythm?.lanes[7]).toEqual({ enabled: true, steps: 16, sourceBar: 0, rotation: 0, timing: { mode: 'grid' } });
    expect(st().kitDirty).toBe(true);
    trigger.mockRestore();
  });
  it('activates a shortened loop in one edit without losing excluded notes', () => {
    st().toggleSourceCell(0, 0, 15);
    st()._clearHistory();
    const publish = vi.spyOn(drumEngine, 'setSequence');
    st().updateLaneRhythm(0, { steps: 3 });
    expect(st().drumRhythm?.lanes[0]).toMatchObject({ enabled: true, steps: 3, sourceBar: 0 });
    expect(st().patterns[patIdx(0, 0, 15)]).toBe(1);
    expect(publish).toHaveBeenCalledTimes(1);
    expect(publish.mock.calls[0][2]?.lanes[0]?.enabled).toBe(true);
    st().undo();
    expect(st().drumRhythm).toBeUndefined();
    expect(st().patterns[patIdx(0, 0, 15)]).toBe(1);
    publish.mockRestore();
  });
  it('retains disabled settings, normalizes rotation, and makes immutable copies', () => {
    st().updateLaneRhythm(0, { steps: 15, rotation: 14, timing: { mode: 'fit', cycleBeats: 8 } });
    const before = st().drumRhythm;
    st().updateLaneRhythm(0, { steps: 3 });
    st().setLaneEnabled(0, false);
    st().setLaneEnabled(0, true);
    expect(before?.lanes[0]?.steps).toBe(15);
    expect(st().drumRhythm?.lanes[0]).toMatchObject({ enabled: true, steps: 3, rotation: 2, sourceBar: 0, timing: { mode: 'fit', cycleBeats: 8 } });
    const copy = cloneDrumSequence(st()); copy.patterns[0] = 2; copy.drumRhythm!.lanes[0]!.steps = 7;
    expect(st().patterns[0]).toBe(0); expect(st().drumRhythm?.lanes[0]?.steps).toBe(3);
  });
  it('edits fixed source cells while another bar is displayed', () => {
    st().setLaneEnabled(0, true); st().setEditPattern(0);
    st().toggleSourceCell(0, 0, 15);
    expect(st().patterns[patIdx(0, 0, 15)]).toBe(1);
    expect(st().patterns[patIdx(2, 0, 15)]).toBe(0);
  });
  it('publishes once and restores notes and rhythm together with one undo', () => {
    const publish = vi.spyOn(drumEngine, 'setSequence');
    const next = cloneDrumSequence(st()); next.patterns[0] = 2;
    next.drumRhythm = updateDrumLane(undefined, 0, { enabled: true, steps: 3 }, 2);
    st().commitSequence(next);
    expect(publish).toHaveBeenCalledTimes(1);
    st().undo(); expect(st().patterns[0]).toBe(0); expect(st().drumRhythm).toBeUndefined();
    st().redo(); expect(st().patterns[0]).toBe(2); expect(st().drumRhythm?.lanes[0]?.steps).toBe(3);
    publish.mockRestore();
  });
  it('coalesces a held stepper and rejects invalid drafts before publication', () => {
    st().setLaneEnabled(0, true); st()._clearHistory();
    st().beginSequenceGesture();
    for (let n = 15; n >= 3; n--) st().updateLaneRhythm(0, { steps: n });
    st().endSequenceGesture(); st().undo();
    expect(st().drumRhythm?.lanes[0]?.steps).toBe(16);
    st().redo(); expect(st().drumRhythm?.lanes[0]?.steps).toBe(3);
    const before = st().drumRhythm; st().updateLaneRhythm(0, { steps: 0 });
    expect(st().drumRhythm).toBe(before); expect(st().sequenceError).toContain('steps');
    st().updateLaneRhythm(0, { sourceBar: 4 }); expect(st().drumRhythm?.lanes[0]?.sourceBar).toBe(0);
  });
  it('allows standalone shortening but rejects removal of disabled hosted sources', () => {
    st().updateLaneRhythm(0, { enabled: false, sourceBar: 3 }); st().setSequenceLength(1);
    expect(st().sequenceError).toBeNull();
    useDrumStore.setState({ hosted: true, chain: [0, 1, 2, 3] });
    st().setSequenceLength(3);
    expect(st().chain).toHaveLength(4); expect(st().sequenceError).toContain('BAR 4');
  });
  it('duplicates a bar and extends the chain in a single transaction', () => {
    st().setEditPattern(0); st().toggleStep(3); st()._clearHistory();
    st().duplicateSelection(); expect(st().chain).toHaveLength(2);
    st().undo(); expect(st().chain).toHaveLength(1); expect(st().patterns[patIdx(1, 0, 3)]).toBe(0);
  });
});

it('keeps micro timing independent of POLY and carries it through move, paste, bar copy, and undo', () => {
  st().setEditPattern(0);
  st().setMicroDelay(0, -12);
  st().setMicroDelay(0, 23, 1);
  st().toggleSourceCell(0, 0, 1);
  expect(st().drumRhythm?.lanes[0]?.enabled).toBe(false);
  st().setRectSel({ stepFrom: 1, stepTo: 1, padFrom: 0, padTo: 0 });
  st().moveRectSel(2, 1);
  expect(st().drumRhythm?.lanes[0]?.stepDelayMs?.[1]).toBe(0);
  expect(st().drumRhythm?.lanes[1]?.stepDelayMs?.[3]).toBe(23);
  expect(st().drumRhythm?.lanes[0]?.delayMs).toBe(-12);
  st().copySelection();
  st().setRectSel({ stepFrom: 6, stepTo: 6, padFrom: 1, padTo: 1 });
  st().pasteSelection();
  expect(st().drumRhythm?.lanes[1]?.stepDelayMs?.[6]).toBe(23);
  st().undo(); expect(st().drumRhythm?.lanes[1]?.stepDelayMs?.[6] ?? 0).toBe(0);
  st().clearStepSel(); st().duplicateSelection();
  expect(st().drumRhythm?.lanes[1]?.stepDelayMs?.[19]).toBe(23);
  st().undo(); expect(st().drumRhythm?.lanes[1]?.stepDelayMs?.[19] ?? 0).toBe(0);
  const copy = cloneDrumSequence(st()); copy.drumRhythm!.lanes[1]!.stepDelayMs![3] = -5;
  expect(st().drumRhythm?.lanes[1]?.stepDelayMs?.[3]).toBe(23);
});
