import { beforeEach, describe, expect, it } from 'vitest';
import { sessionPatchOps } from './sessionPatchOps';
import { resetSeqStore, useSeqStore } from './store';

const patchOf = (t: number) => useSeqStore.getState().session.tracks[t].patch;

describe('SQ-4 session patch browsing', () => {
  beforeEach(() => resetSeqStore());

  it('loads, varies, and restores the track patch exactly', () => {
    const t = 2;
    const ops = () => sessionPatchOps(t, patchOf(t));
    const kept = ops().capture();

    ops().load('f4');
    expect(patchOf(t)).toEqual({ kind: 'factory', index: 4 });
    expect(ops().value).toBe('f4');

    ops().applyParams({ 'filter.cutoff': 900 });
    expect(patchOf(t)).toEqual({ kind: 'inline', data: { params: { 'filter.cutoff': 900 } }, base: 4 });
    expect(ops().capture()).toMatchObject({ value: 'f4', dirty: true });

    ops().restore(kept);
    expect(patchOf(t)).toEqual(kept.patch);
    expect(ops().value).toBe(kept.value);
  });

  it('snapshots are copies, not live references', () => {
    useSeqStore.getState().setTrackPatch(1, { kind: 'inline', data: { params: { 'osc.pos': 0.2 } }, base: 3 });
    const snap = sessionPatchOps(1, patchOf(1)).capture();
    (patchOf(1) as { data: { params: Record<string, number> } }).data.params['osc.pos'] = 0.9;
    expect((snap.patch as { data: { params: Record<string, number> } }).data.params['osc.pos']).toBe(0.2);
  });

  it('auditions only while the SQ-4 transport is stopped', () => {
    const ops = sessionPatchOps(0, patchOf(0));
    useSeqStore.setState({ playing: false });
    expect(ops.canAudition()).toBe(true);
    useSeqStore.setState({ playing: true });
    expect(ops.canAudition()).toBe(false);
  });
});
