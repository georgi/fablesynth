// Pure grid-editing helper tests: selection rectangles, clipboard capture,
// paste/move write plans and immutable application — no store, no audio.

import { describe, expect, it } from 'vitest';
import {
  applyWrites, copyRect, type GridSel, inRect, isDropTarget, moveWrites, pasteWrites, selRect,
} from './gridEdit';
import type { ClipDoc, SessionDoc } from './protocol';

const clip = (name: string): ClipDoc => ({ name, bars: 1, pattern: 'AAAA' });

// 4 tracks (DR1, BL1, WT1, WT1) × 3 scenes — mirrors the factory layout shape.
function makeSession(): SessionDoc {
  return {
    v: 1,
    name: 'TEST',
    bpm: 120,
    swing: 0,
    quant: '1 BAR',
    tracks: (['DR1', 'BL1', 'WT1', 'WT1'] as const).map((machine, i) => ({
      machine, name: `T${i}`, color: '#fff', gain: 1, patch: { kind: 'factory', index: 0 },
    })),
    scenes: [
      { name: 'S0', clips: [clip('D0'), clip('B0'), clip('W0'), null] },
      { name: 'S1', clips: [null, null, clip('W1'), clip('P1')] },
      { name: 'S2', clips: [clip('D2'), null, null, null] },
    ],
  };
}

describe('selRect / inRect', () => {
  it('normalizes an inverted anchor/head into a rectangle', () => {
    const sel: GridSel = { anchor: { s: 2, t: 3 }, head: { s: 0, t: 1 } };
    expect(selRect(sel)).toEqual({ s0: 0, s1: 2, t0: 1, t1: 3 });
  });

  it('inRect includes edges and excludes outside cells', () => {
    const r = { s0: 0, s1: 1, t0: 1, t1: 2 };
    expect(inRect(r, 0, 1)).toBe(true);
    expect(inRect(r, 1, 2)).toBe(true);
    expect(inRect(r, 2, 1)).toBe(false);
    expect(inRect(r, 0, 3)).toBe(false);
  });
});

describe('copyRect', () => {
  it('captures cells scene-major with per-column machine tags', () => {
    const cb = copyRect(makeSession(), { s0: 0, s1: 1, t0: 1, t1: 2 });
    expect(cb.machines).toEqual(['BL1', 'WT1']);
    expect(cb.cells.map((row) => row.map((c) => c?.name ?? null))).toEqual([
      ['B0', 'W0'],
      [null, 'W1'],
    ]);
  });
});

describe('pasteWrites', () => {
  it('anchors the payload top-left at the target and writes nulls too', () => {
    const session = makeSession();
    const cb = copyRect(session, { s0: 0, s1: 1, t0: 2, t1: 3 });
    const { writes, skipped } = pasteWrites(session, cb, { s: 1, t: 2 });
    expect(skipped).toBe(0);
    expect(writes).toEqual([
      { s: 1, t: 2, clip: session.scenes[0].clips[2] },
      { s: 1, t: 3, clip: null },
      { s: 2, t: 2, clip: session.scenes[1].clips[2] },
      { s: 2, t: 3, clip: session.scenes[1].clips[3] },
    ]);
  });

  it('skips machine-mismatched cells and counts them', () => {
    const session = makeSession();
    const cb = copyRect(session, { s0: 0, s1: 0, t0: 0, t1: 1 }); // DR1, BL1
    const { writes, skipped } = pasteWrites(session, cb, { s: 1, t: 1 }); // lands on BL1, WT1
    expect(writes).toEqual([]);
    expect(skipped).toBe(2);
  });

  it('clamps rows and columns past the grid edge without counting them', () => {
    const session = makeSession();
    const cb = copyRect(session, { s0: 0, s1: 1, t0: 2, t1: 3 });
    const { writes, skipped } = pasteWrites(session, cb, { s: 2, t: 3 });
    expect(skipped).toBe(0);
    expect(writes).toEqual([{ s: 2, t: 3, clip: session.scenes[0].clips[2] }]);
  });
});

describe('moveWrites', () => {
  it('moves a single filled cell: source cleared, target written', () => {
    const session = makeSession();
    const { writes, skipped } = moveWrites(
      session, { s0: 0, s1: 0, t0: 2, t1: 2 }, { s: 0, t: 2 }, { s: 2, t: 3 }, false,
    );
    expect(skipped).toBe(0);
    expect(writes).toEqual([
      { s: 0, t: 2, clip: null },
      { s: 2, t: 3, clip: session.scenes[0].clips[2] },
    ]);
  });

  it('keeps the clip in overlapping cells that are both source and target', () => {
    const session = makeSession();
    // Column 2 holds W0 (s0) and W1 (s1); shift the pair down one scene.
    const { writes } = moveWrites(
      session, { s0: 0, s1: 1, t0: 2, t1: 2 }, { s: 0, t: 2 }, { s: 1, t: 2 }, false,
    );
    const at = (s: number) => writes.find((w) => w.s === s && w.t === 2)?.clip?.name ?? null;
    expect(at(0)).toBeNull();
    expect(at(1)).toBe('W0'); // landing spot wins over its own vacate
    expect(at(2)).toBe('W1');
  });

  it('skips machine-mismatched and off-grid targets, keeping their sources', () => {
    const session = makeSession();
    const { writes, skipped } = moveWrites(
      session, { s0: 0, s1: 0, t0: 0, t1: 0 }, { s: 0, t: 0 }, { s: 0, t: 1 }, false,
    );
    expect(writes).toEqual([]);
    expect(skipped).toBe(1);
    const off = moveWrites(
      session, { s0: 2, s1: 2, t0: 0, t1: 0 }, { s: 2, t: 0 }, { s: 3, t: 0 }, false,
    );
    expect(off.writes).toEqual([]);
    expect(off.skipped).toBe(1);
  });

  it('Alt-copy plans no source clears', () => {
    const session = makeSession();
    const { writes } = moveWrites(
      session, { s0: 0, s1: 0, t0: 2, t1: 2 }, { s: 0, t: 2 }, { s: 2, t: 2 }, true,
    );
    expect(writes).toEqual([{ s: 2, t: 2, clip: session.scenes[0].clips[2] }]);
  });

  it('a zero displacement is a no-op', () => {
    const session = makeSession();
    const { writes } = moveWrites(
      session, { s0: 0, s1: 0, t0: 2, t1: 2 }, { s: 0, t: 2 }, { s: 0, t: 2 }, false,
    );
    expect(writes).toEqual([]);
  });
});

describe('applyWrites', () => {
  it('rebuilds only the touched scenes and never mutates the input', () => {
    const session = makeSession();
    const next = applyWrites(session, [{ s: 0, t: 3, clip: clip('NEW') }]);
    expect(session.scenes[0].clips[3]).toBeNull();
    expect(next.scenes[0].clips[3]?.name).toBe('NEW');
    expect(next.scenes[1]).toBe(session.scenes[1]);
    expect(next.scenes[2]).toBe(session.scenes[2]);
  });

  it('returns the same session for an empty plan', () => {
    const session = makeSession();
    expect(applyWrites(session, [])).toBe(session);
  });
});

describe('isDropTarget', () => {
  const session = makeSession();

  it('highlights the projected landing cells of the grabbed block', () => {
    const sel: GridSel = { anchor: { s: 0, t: 2 }, head: { s: 1, t: 2 } };
    const drag = { from: { s: 0, t: 2 }, to: { s: 1, t: 3 } };
    expect(isDropTarget(session, sel, drag, 1, 3)).toBe(true); // W0 lands here
    expect(isDropTarget(session, sel, drag, 2, 3)).toBe(true); // W1 lands here
    expect(isDropTarget(session, sel, drag, 1, 2)).toBe(false);
  });

  it('shows nothing for machine-mismatched targets or without a drag', () => {
    const drag = { from: { s: 0, t: 0 }, to: { s: 0, t: 1 } }; // DR1 → BL1
    expect(isDropTarget(session, null, drag, 0, 1)).toBe(false);
    expect(isDropTarget(session, null, null, 0, 1)).toBe(false);
  });

  it('drags a single cell when the grab point is outside the selection', () => {
    const sel: GridSel = { anchor: { s: 0, t: 0 }, head: { s: 0, t: 0 } };
    const drag = { from: { s: 0, t: 2 }, to: { s: 2, t: 2 } };
    expect(isDropTarget(session, sel, drag, 2, 2)).toBe(true);
    expect(isDropTarget(session, sel, drag, 2, 0)).toBe(false);
  });
});
