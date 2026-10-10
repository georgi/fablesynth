import { beforeEach, describe, expect, it } from 'vitest';
import { copySequenceRhythm, defaultSequenceRhythm } from '../shared/sequenceRhythm';
import { factorySession } from './factory';
import { validateSession } from './protocol';
import { embedSessionPatches } from './sessionExport';
import { copySession } from './sessionPresets';
import { useStore } from '../store';
import { useBassStore } from '../bass/store';
import { loadSeqState } from '../noteseq';
import { patchToState } from '../bass/patches';
import { clipPattern, resetSeqStore, useSeqStore } from './store';
import type { SeqRig } from './rig';

beforeEach(() => {
  const storage = new Map<string, string>();
  Object.defineProperty(globalThis, 'localStorage', { configurable: true, value: {
    getItem: (key: string) => storage.get(key) ?? null,
    setItem: (key: string, value: string) => storage.set(key, value),
  } });
});
const poly = () => ({ ...defaultSequenceRhythm(), enabled: true, mode: 'fit' as const, steps: 5, rotation: 2 });
describe('web melodic POLY state', () => {
  it('exports, imports and recalls independent rhythm data, rejecting malformed and drum usage', () => {
    const session = factorySession(); session.scenes[2].clips[1]!.sequenceRhythm = poly();
    const exported = embedSessionPatches(session), recalled = copySession(session);
    expect(validateSession(JSON.parse(JSON.stringify(exported)))).toBeNull();
    expect(exported.scenes[2].clips[1]!.sequenceRhythm).toEqual(poly());
    exported.scenes[2].clips[1]!.sequenceRhythm!.steps = 9;
    recalled.scenes[2].clips[1]!.sequenceRhythm!.rotation = 0;
    expect(session.scenes[2].clips[1]!.sequenceRhythm).toEqual(poly());
    session.scenes[2].clips[0]!.sequenceRhythm = poly();
    expect(validateSession(session)).toContain('invalid melodic rhythm');
    delete session.scenes[2].clips[0]!.sequenceRhythm;
    session.scenes[2].clips[1]!.sequenceRhythm!.steps = 0;
    expect(validateSession(session)).toContain('invalid melodic rhythm');
  });
  it('WT-1 retains POLY across note edits and undo/redo, and persists it alongside notes', () => {
    useStore.setState({ hosted: false, sequenceRhythm: defaultSequenceRhythm() });
    useStore.getState()._clearSeqHistory();
    const before = useStore.getState().patterns.slice();
    useStore.getState().updateSequenceRhythm(poly());
    useStore.getState().drawNote(0, 7, 3, 0);
    const after = useStore.getState().patterns.slice();
    expect(loadSeqState().sequenceRhythm).toEqual(poly());
    useStore.getState().undoSeq();
    expect(useStore.getState().patterns).toEqual(before);
    expect(useStore.getState().sequenceRhythm).toEqual(poly());
    useStore.getState().undoSeq(); expect(useStore.getState().sequenceRhythm.enabled).toBe(false);
    useStore.getState().redoSeq(); useStore.getState().redoSeq();
    expect(useStore.getState().sequenceRhythm).toEqual(poly());
    expect(useStore.getState().patterns).toEqual(after);
  });
  it('BL-1 includes POLY in sequence history and user patch recall without mutating saved patches', () => {
    useBassStore.setState({ hosted: false, sequenceRhythm: defaultSequenceRhythm(), userPatches: [] });
    useBassStore.getState()._clearHistory();
    useBassStore.getState().updateSequenceRhythm(poly());
    useBassStore.getState().undo(); expect(useBassStore.getState().sequenceRhythm.enabled).toBe(false);
    useBassStore.getState().redo(); expect(useBassStore.getState().sequenceRhythm).toEqual(poly());
    useBassStore.getState().savePatch('POLY CHECK');
    const patch = useBassStore.getState().userPatches[0];
    useBassStore.getState().loadPatchByValue('f0'); expect(useBassStore.getState().sequenceRhythm.enabled).toBe(false);
    useBassStore.getState().loadPatchByValue('u0'); expect(useBassStore.getState().sequenceRhythm).toEqual(poly());
    const resolved = patchToState(patch); resolved.sequenceRhythm!.steps = 9;
    expect(patch.sequenceRhythm).toEqual(poly());
  });
  it('SQ-4 writes POLY only to the pending/live target and retains it through note-only updates', () => {
    resetSeqStore(factorySession());
    const updates: unknown[][] = [];
    const devices = Array.from({ length: 4 }, () => ({ updateClip: (...args: unknown[]) => updates.push(args) }));
    useSeqStore.setState({ rig: { devices } as unknown as SeqRig, owner: { 1: 2 }, queue: { 1: 3 } });
    const before = useSeqStore.getState().session.scenes[2].clips[0];
    const live = clipPattern(useSeqStore.getState().session, 2, 1)!;
    const liveBars = useSeqStore.getState().session.scenes[2].clips[1]!.bars;
    useSeqStore.getState().updateClipBytes(2, 1, live, liveBars, poly());
    expect(updates).toHaveLength(0);
    const queued = clipPattern(useSeqStore.getState().session, 3, 1)!;
    const queuedBars = useSeqStore.getState().session.scenes[3].clips[1]!.bars;
    useSeqStore.getState().updateClipBytes(3, 1, queued, queuedBars, poly());
    expect(updates).toHaveLength(1); expect(updates[0][5]).toEqual(poly());
    useSeqStore.getState().updateClipBytes(3, 1, queued, queuedBars);
    expect(updates[1][5]).toEqual(poly());
    expect(useSeqStore.getState().session.scenes[2].clips[0]).toBe(before);
    const saved = copySequenceRhythm(useSeqStore.getState().session.scenes[3].clips[1]!.sequenceRhythm);
    expect(saved).toEqual(poly());
    const revision = useSeqStore.getState().clipLoadRevision;
    useSeqStore.getState().undo();
    expect(useSeqStore.getState().session.scenes[3].clips[1]!.sequenceRhythm).toBeUndefined();
    expect(updates.slice(-1)[0][5]).toBeUndefined();
    expect(useSeqStore.getState().clipLoadRevision).toBeGreaterThan(revision);
    useSeqStore.getState().redo();
    expect(updates.slice(-1)[0][5]).toEqual(poly());
    expect(validateSession(useSeqStore.getState().session)).toBeNull();
  });
  it('SQ-4 treats a default hosted rhythm as absent and keeps note edits out of seq history', () => {
    const session = factorySession(); session.scenes[2].clips[1] = { ...session.scenes[2].clips[1]! };
    delete session.scenes[2].clips[1].sequenceRhythm; // earlier tests mutate the shared factory clip
    resetSeqStore(session);
    const bytes = clipPattern(useSeqStore.getState().session, 2, 1)!;
    const bars = useSeqStore.getState().session.scenes[2].clips[1]!.bars;
    expect(useSeqStore.getState().session.scenes[2].clips[1]!.sequenceRhythm).toBeUndefined();
    for (let i = 0; i < 60; i++) {
      const edited = bytes.slice(); edited[0] = i;
      useSeqStore.getState().updateClipBytes(2, 1, edited, bars, defaultSequenceRhythm());
    }
    expect(useSeqStore.getState().session.scenes[2].clips[1]!.sequenceRhythm).toBeUndefined();
    const before = useSeqStore.getState().session;
    useSeqStore.getState().undo();
    expect(useSeqStore.getState().session).toBe(before);
    useSeqStore.getState().updateClipBytes(2, 1, bytes, bars, poly());
    expect(useSeqStore.getState().session.scenes[2].clips[1]!.sequenceRhythm).toEqual(poly());
    useSeqStore.getState().undo();
    expect(useSeqStore.getState().session.scenes[2].clips[1]!.sequenceRhythm).toBeUndefined();
  });
});
