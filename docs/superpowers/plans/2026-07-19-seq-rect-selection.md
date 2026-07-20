# WT-1 / BL-1 Rectangle Selection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 1D step-range selection in the WT-1 NOTE SEQ and BL-1 PITCH SEQ grids with a shift-drag 2D rectangle (steps × note lanes) whose contents can be cut / copied / deleted / duplicated from the mini-menu and pasted with Cmd-V.

**Architecture:** Pure rect helpers join the existing range helpers in `src/shared/seqEdit.ts` (both grids share the 3-byte step layout: byte0 bit0 = on, byte1 bits0–6 = note lane 0..11, BL-1 slide flag in byte1 bit7 rides along untouched). Each store swaps `stepSel {from,to}` for `rectSel {stepFrom,stepTo,noteFrom,noteTo}` and re-implements its clipboard verbs on the rect helpers, keeping existing verb *names* so the keyboard hooks stay unchanged. A shared pointer hook drives shift-drag select and in-rect block-move in both panels.

**Tech Stack:** React 18 + zustand + TypeScript, vitest, packed `Uint8Array` pattern buffers.

**Spec:** `docs/superpowers/specs/2026-07-19-seq-rect-selection-design.md`

## Global Constraints

- Web only; do not touch `juce/`.
- Standalone only: every new interaction is gated on `!hosted` (and `pattern === editPattern`), exactly like the existing selection gating.
- One undo entry per verb. WT-1 convention: call `pushSeqHistory()` once before the mutation, then one `_setPatterns()` call. BL-1 convention: `_setPatterns()` itself pushes history — do NOT add explicit pushes there; make exactly one `_setPatterns()` call per verb.
- Verbs affect only notes with step ∈ rect AND note lane ∈ rect. Paste/move drops (never wraps or clamps) notes that land outside step 0..15 or lane 0..11.
- Keep verb names `copySteps/cutSteps/pasteSteps/duplicateSteps/deleteSteps/selectAllSteps/clearStepSel` (WT-1) and `copySelection/cutSelection/pasteSelection/duplicateSelection/deleteSelection/selectAllSteps/clearStepSelection` (BL-1) so `useSeqEditKeys` / `useBassKeys` keep working with minimal edits.
- No-selection fallbacks stay as they are today (whole-pattern copy/cut/paste, duplicate-bar).
- Run tests with `npx vitest run <file>`; full suite `npx vitest run`.
- Commit after each green task with a conventional message ending in the Claude Fable co-author trailer.

---

### Task 1: Shared rect helpers in `src/shared/seqEdit.ts`

**Files:**
- Modify: `src/shared/seqEdit.ts` (append a new section after `shiftRange`, before the pattern block ops)
- Test: `src/shared/seqEdit.rect.test.ts` (create)

**Interfaces:**
- Consumes: existing module-local `stepOffset`, `SeqLayout`, `Patterns`.
- Produces (later tasks rely on these exact signatures):
  - `interface RectSel { stepFrom: number; stepTo: number; noteFrom: number; noteTo: number }`
  - `interface RectCells { wSteps: number; noteLo: number; noteHi: number; cells: Array<{ dStep: number; bytes: Uint8Array }> }`
  - `rectNorm(r: RectSel): { stepLo: number; stepHi: number; noteLo: number; noteHi: number }`
  - `copyRect(p: Patterns, l: SeqLayout, pat: number, rect: RectSel): RectCells`
  - `clearRect(p: Patterns, l: SeqLayout, pat: number, rect: RectSel, emptyStep?: Uint8Array): Patterns`
  - `pasteRect(p: Patterns, l: SeqLayout, pat: number, atStep: number, dNote: number, data: RectCells, maxNote?: number): Patterns`
  - `moveRect(p: Patterns, l: SeqLayout, pat: number, rect: RectSel, dStep: number, dNote: number, opts?: { copy?: boolean; emptyStep?: Uint8Array; maxNote?: number }): Patterns`

- [ ] **Step 1: Write the failing tests**

Create `src/shared/seqEdit.rect.test.ts`. Build tiny buffers with the WT-1/BL-1 shape (`stride 3, stepsPerPattern 16, patternSize 48`), one pattern is enough. A lit step is `[1 | (dur<<2), note, oct+1]`.

```ts
import { describe, expect, it } from 'vitest';
import {
  clearRect, copyRect, moveRect, pasteRect, rectNorm,
  type RectSel, type SeqLayout,
} from './seqEdit';

const L: SeqLayout = { stride: 3, stepsPerPattern: 16, patternSize: 48 };
const EMPTY = Uint8Array.of(1 << 2, 0, 1);

// One empty pattern; set(step, note, extra?) lights a step.
const makeP = (): Uint8Array => {
  const p = new Uint8Array(48);
  for (let i = 0; i < 48; i += 3) { p[i] = 1 << 2; p[i + 2] = 1; }
  return p;
};
const lit = (p: Uint8Array, step: number, note: number, b1extra = 0): void => {
  p[step * 3] = 1 | (1 << 2); p[step * 3 + 1] = note | b1extra; p[step * 3 + 2] = 1;
};
const noteAt = (p: Uint8Array, step: number): number => p[step * 3 + 1] & 0x7f;
const onAt = (p: Uint8Array, step: number): boolean => (p[step * 3] & 1) !== 0;

describe('rectNorm', () => {
  it('normalizes reversed corners', () => {
    expect(rectNorm({ stepFrom: 7, stepTo: 2, noteFrom: 9, noteTo: 4 }))
      .toEqual({ stepLo: 2, stepHi: 7, noteLo: 4, noteHi: 9 });
  });
});

describe('copyRect / clearRect', () => {
  it('picks only lit cells inside the pitch band', () => {
    const p = makeP();
    lit(p, 2, 5); lit(p, 3, 11); lit(p, 4, 6);
    const rect: RectSel = { stepFrom: 2, stepTo: 4, noteFrom: 4, noteTo: 7 };
    const c = copyRect(p, L, 0, rect);
    expect(c.wSteps).toBe(3);
    expect(c.cells.map((x) => x.dStep)).toEqual([0, 2]); // step 3 (note 11) excluded
    const cleared = clearRect(p, L, 0, rect, EMPTY);
    expect(onAt(cleared, 2)).toBe(false);
    expect(onAt(cleared, 3)).toBe(true); // out of band → untouched
    expect(onAt(cleared, 4)).toBe(false);
  });
});

describe('pasteRect', () => {
  it('stamps with transpose, drops out-of-range, keeps bit7', () => {
    const p = makeP();
    lit(p, 0, 10, 0x80); lit(p, 1, 3);
    const c = copyRect(p, L, 0, { stepFrom: 0, stepTo: 1, noteFrom: 0, noteTo: 11 });
    const out = pasteRect(makeP(), L, 0, 14, 2, c, 11);
    expect(onAt(out, 14)).toBe(false);        // note 10+2 → out of lane range, dropped
    expect(onAt(out, 15)).toBe(true);
    expect(noteAt(out, 15)).toBe(5);          // 3 + 2
    const out2 = pasteRect(makeP(), L, 0, 15, 0, c, 11);
    expect(onAt(out2, 15)).toBe(true);
    expect(out2[15 * 3 + 1] & 0x80).toBe(0x80); // slide bit preserved
    // dStep 1 lands at step 16 → dropped, no wrap to step 0
    expect(onAt(out2, 0)).toBe(false);
  });
  it('overwrites only steps where the clipboard has a cell', () => {
    const p = makeP();
    lit(p, 5, 7);
    const src = makeP(); lit(src, 0, 2);
    const c = copyRect(src, L, 0, { stepFrom: 0, stepTo: 1, noteFrom: 0, noteTo: 11 });
    const out = pasteRect(p, L, 0, 4, 0, c, 11);
    expect(noteAt(out, 4)).toBe(2);
    expect(noteAt(out, 5)).toBe(7); // dStep 1 has no cell → untouched
  });
});

describe('moveRect', () => {
  it('moves with transpose and clears the vacated cells', () => {
    const p = makeP();
    lit(p, 2, 5);
    const out = moveRect(p, L, 0, { stepFrom: 2, stepTo: 2, noteFrom: 5, noteTo: 5 }, 3, -2, { emptyStep: EMPTY });
    expect(onAt(out, 2)).toBe(false);
    expect(onAt(out, 5)).toBe(true);
    expect(noteAt(out, 5)).toBe(3);
  });
  it('copy keeps the source; overlapping move is safe', () => {
    const p = makeP();
    lit(p, 2, 5); lit(p, 3, 6);
    const rect: RectSel = { stepFrom: 2, stepTo: 3, noteFrom: 0, noteTo: 11 };
    const copied = moveRect(p, L, 0, rect, 4, 0, { copy: true, emptyStep: EMPTY });
    expect(onAt(copied, 2)).toBe(true);
    const moved = moveRect(p, L, 0, rect, 1, 0, { emptyStep: EMPTY });
    expect(onAt(moved, 2)).toBe(false);
    expect(noteAt(moved, 3)).toBe(5);
    expect(noteAt(moved, 4)).toBe(6);
  });
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `npx vitest run src/shared/seqEdit.rect.test.ts`
Expected: FAIL — `copyRect` etc. are not exported.

- [ ] **Step 3: Implement the helpers**

Append to `src/shared/seqEdit.ts` after `shiftRange` (before the "Whole-pattern block ops" section):

```ts
// ---------- rectangle (step × note-lane) selection ----------
// WT-1/BL-1 note grids only: stride-3 steps where byte0 bit0 = on and
// byte1 bits0..6 = note lane (0..11; BL-1 keeps its slide flag in bit7,
// which rides along untouched). Verbs touch only lit cells whose lane falls
// inside the rect's pitch band; paste/move DROPS cells that land outside the
// pattern or the lane range — never wraps or clamps.

export interface RectSel { stepFrom: number; stepTo: number; noteFrom: number; noteTo: number }

export interface RectCells {
  wSteps: number; // width of the source rect in steps
  noteLo: number; // normalized source pitch band
  noteHi: number;
  cells: Array<{ dStep: number; bytes: Uint8Array }>; // stride bytes, dStep from source stepLo
}

export const rectNorm = (r: RectSel): { stepLo: number; stepHi: number; noteLo: number; noteHi: number } => ({
  stepLo: Math.min(r.stepFrom, r.stepTo),
  stepHi: Math.max(r.stepFrom, r.stepTo),
  noteLo: Math.min(r.noteFrom, r.noteTo),
  noteHi: Math.max(r.noteFrom, r.noteTo),
});

const cellOn = (p: Patterns, o: number): boolean => (p[o] & 1) !== 0;
const cellNote = (p: Patterns, o: number): number => p[o + 1] & 0x7f;

export function copyRect(p: Patterns, l: SeqLayout, pat: number, rect: RectSel): RectCells {
  const { stepLo, stepHi, noteLo, noteHi } = rectNorm(rect);
  const cells: RectCells['cells'] = [];
  for (let s = stepLo; s <= stepHi; s++) {
    const o = stepOffset(l, pat, s);
    const n = cellNote(p, o);
    if (cellOn(p, o) && n >= noteLo && n <= noteHi) cells.push({ dStep: s - stepLo, bytes: p.slice(o, o + l.stride) });
  }
  return { wSteps: stepHi - stepLo + 1, noteLo, noteHi, cells };
}

export function clearRect(p: Patterns, l: SeqLayout, pat: number, rect: RectSel, emptyStep?: Uint8Array): Patterns {
  const { stepLo, stepHi, noteLo, noteHi } = rectNorm(rect);
  const next = p.slice();
  for (let s = stepLo; s <= stepHi; s++) {
    const o = stepOffset(l, pat, s);
    const n = cellNote(next, o);
    if (cellOn(next, o) && n >= noteLo && n <= noteHi) {
      for (let b = 0; b < l.stride; b++) next[o + b] = emptyStep ? emptyStep[b] : 0;
    }
  }
  return next;
}

export function pasteRect(
  p: Patterns, l: SeqLayout, pat: number, atStep: number, dNote: number, data: RectCells, maxNote = 11,
): Patterns {
  const next = p.slice();
  for (const c of data.cells) {
    const s = atStep + c.dStep;
    if (s < 0 || s >= l.stepsPerPattern) continue;
    const note = (c.bytes[1] & 0x7f) + dNote;
    if (note < 0 || note > maxNote) continue;
    const o = stepOffset(l, pat, s);
    next.set(c.bytes, o);
    next[o + 1] = (c.bytes[1] & 0x80) | note;
  }
  return next;
}

export function moveRect(
  p: Patterns, l: SeqLayout, pat: number, rect: RectSel, dStep: number, dNote: number,
  opts: { copy?: boolean; emptyStep?: Uint8Array; maxNote?: number } = {},
): Patterns {
  const { stepLo } = rectNorm(rect);
  const data = copyRect(p, l, pat, rect);
  const base = opts.copy ? p.slice() : clearRect(p, l, pat, rect, opts.emptyStep);
  return pasteRect(base, l, pat, stepLo + dStep, dNote, data, opts.maxNote ?? 11);
}
```

Also update the module's top comment (lines 1–5) to mention rect ops for the WT-1/BL-1 note grids.

- [ ] **Step 4: Run tests to verify they pass**

Run: `npx vitest run src/shared/seqEdit.rect.test.ts`
Expected: PASS (all). Also run `npx vitest run` — nothing else may break (helpers are additive).

- [ ] **Step 5: Commit**

```bash
git add src/shared/seqEdit.ts src/shared/seqEdit.rect.test.ts
git commit -m "feat(seq): shared rect-selection helpers (copy/clear/paste/move)"
```

---

### Task 2: Shared UI — `useSeqRectSelect` hook + CUT in the mini-menu

**Files:**
- Create: `src/components/useSeqRectSelect.ts`
- Modify: `src/components/SeqSelectionMenu.tsx`
- Modify: `src/components/panels/SeqPanel.tsx` (only the `SeqSelectionMenu` call: add `onCut={cutSteps}`, subscribing `const cutSteps = useStore((s) => s.cutSteps);`)
- Modify: `src/bass/components/PitchSeq.tsx` (only the `SeqSelectionMenu` call: add `onCut={cutSelection}`, subscribing `const cutSelection = useBassStore((s) => s.cutSelection);`)

The panels keep their current 1D behavior in this task — only the menu gains CUT, wired to the *existing* store verbs. The hook is exercised in Tasks 3/4; here it just needs to typecheck.

**Interfaces:**
- Consumes: `RectSel`, `rectNorm` from `src/shared/seqEdit` (Task 1).
- Produces:
  - `SeqSelectionMenuProps` gains required `onCut: () => void`; button order `CUT · COPY · DUP · DEL · ✕`.
  - `useSeqRectSelect(opts: { editPattern: number; onSelect: (rect: RectSel) => void; onMove: (dStep: number, dNote: number, copy: boolean) => void }): { pending: RectSel | null; startRectSelect: (ev: React.PointerEvent, step: number, note: number) => void; startRectMove: (ev: React.PointerEvent, step: number, note: number) => void; consumeRectClick: () => boolean }`

- [ ] **Step 1: Add CUT to the menu**

In `src/components/SeqSelectionMenu.tsx` add `onCut: () => void;` to the props interface (before `onCopy`), destructure it, and insert as the first button:

```tsx
<button type="button" onClick={onCut}>CUT</button>
```

Update the header comment to `CUT / COPY / DUP / DEL / dismiss`.

- [ ] **Step 2: Wire `onCut` in both panels**

In `SeqPanel.tsx` and `PitchSeq.tsx`, subscribe the store's existing cut verb (`cutSteps` / `cutSelection`) and pass it as `onCut` to `<SeqSelectionMenu … />`.

- [ ] **Step 3: Create the rect-select hook**

Create `src/components/useSeqRectSelect.ts`:

```tsx
// Shift-drag rectangle selection + in-rect block-move for the WT-1 / BL-1
// note grids (docs/superpowers/specs/2026-07-19-seq-rect-selection-design.md).
// Pointer targets are resolved via elementFromPoint → [data-seq-cell] of the
// edited pattern, matching useSeqNoteDrag. Both gestures commit once, on
// pointerup (one undo entry); Escape cancels. The pending rect is local state
// so the drag never touches the store until release.

import { useRef, useState } from 'react';
import type { RectSel } from '../shared/seqEdit';

interface HookOpts {
  editPattern: number;
  onSelect: (rect: RectSel) => void;
  onMove: (dStep: number, dNote: number, copy: boolean) => void;
}

export function useSeqRectSelect({ editPattern, onSelect, onMove }: HookOpts) {
  const [pending, setPending] = useState<RectSel | null>(null);
  const suppressClick = useRef(false);

  const findCell = (ev: PointerEvent): { step: number; note: number } | null => {
    const el = document.elementFromPoint(ev.clientX, ev.clientY);
    const cell = el instanceof Element ? el.closest<HTMLElement>('[data-seq-cell]') : null;
    if (!cell || Number(cell.dataset.pattern) !== editPattern) return null;
    return { step: Number(cell.dataset.step), note: Number(cell.dataset.note) };
  };

  const track = (
    onPointerMove: (ev: PointerEvent) => void,
    onUp: (ev: PointerEvent) => void,
    onCancel: () => void,
  ) => {
    const cleanup = () => {
      window.removeEventListener('pointermove', onPointerMove);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('keydown', keydown);
    };
    const up = (ev: PointerEvent) => { cleanup(); onUp(ev); };
    const keydown = (ev: KeyboardEvent) => { if (ev.key === 'Escape') { cleanup(); onCancel(); } };
    window.addEventListener('pointermove', onPointerMove);
    window.addEventListener('pointerup', up);
    window.addEventListener('keydown', keydown);
  };

  const startRectSelect = (ev: React.PointerEvent, step: number, note: number) => {
    ev.preventDefault();
    let rect: RectSel = { stepFrom: step, stepTo: step, noteFrom: note, noteTo: note };
    setPending(rect);
    track(
      (e) => {
        const c = findCell(e);
        if (!c) return;
        rect = { stepFrom: step, stepTo: c.step, noteFrom: note, noteTo: c.note };
        setPending(rect);
      },
      () => { setPending(null); suppressClick.current = true; onSelect(rect); },
      () => setPending(null),
    );
  };

  const startRectMove = (ev: React.PointerEvent, step: number, note: number) => {
    ev.preventDefault();
    let dest = { step, note };
    track(
      (e) => { const c = findCell(e); if (c) dest = c; },
      (e) => {
        if (dest.step === step && dest.note === note) return; // plain tap inside rect: fall through to click
        suppressClick.current = true;
        onMove(dest.step - step, dest.note - note, e.altKey);
      },
      () => undefined,
    );
  };

  const consumeRectClick = (): boolean => {
    const v = suppressClick.current;
    suppressClick.current = false;
    return v;
  };

  return { pending, startRectSelect, startRectMove, consumeRectClick };
}
```

- [ ] **Step 4: Verify build + suite**

Run: `npx tsc --noEmit && npx vitest run`
Expected: clean typecheck, all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/components/useSeqRectSelect.ts src/components/SeqSelectionMenu.tsx src/components/panels/SeqPanel.tsx src/bass/components/PitchSeq.tsx
git commit -m "feat(seq): CUT in selection menu + shared rect-select hook"
```

---

### Task 3: WT-1 — rect selection state, verbs, panel wiring

**Files:**
- Modify: `src/store.ts` (types ~line 35–39, state fields ~73–74 & 191–192, actions block 384–470, `toggleCell`, `moveStepNote`'s selection follow-up is in the panel)
- Modify: `src/components/panels/SeqPanel.tsx`
- Modify: `src/index.css` (`.ns-cell.sel` style; retire `.ns-step-sel` selection overlay usage)
- Test: `src/store.test.ts` (extend)

**Interfaces:**
- Consumes: `RectSel`, `RectCells`, `rectNorm`, `copyRect`, `clearRect`, `pasteRect`, `moveRect` from `src/shared/seqEdit` (Task 1); `useSeqRectSelect` (Task 2).
- Produces (store API — Task 5 verifies against this):
  - State: `rectSel: RectSel | null` (replaces `stepSel`), `lastCell: { step: number; note: number } | null`, `clipboard: SeqClipboard` where `SeqClipboard = { kind: 'rect'; data: RectCells } | { kind: 'pattern'; data: Uint8Array } | null`.
  - Actions: `setRectSel(sel: RectSel | null)` (replaces `setStepSel`), `moveRectSel(dStep: number, dNote: number, opts?: { copy?: boolean })` (replaces `shiftStepSel`); `selectAllSteps/clearStepSel/copySteps/cutSteps/pasteSteps/duplicateSteps/deleteSteps` keep their names.

- [ ] **Step 1: Write failing store tests**

Append to `src/store.test.ts` (follow the file's existing setup helpers for resetting the store — read the top of the file first and reuse its pattern; the assertions below are the required behavior):

```ts
describe('rect selection verbs', () => {
  // helper: light a step via the store
  const light = (step: number, note: number) => useStore.getState().toggleCell(step, note, 0);

  it('deleteSteps clears only notes inside the pitch band', () => {
    light(2, 5); light(3, 11);
    useStore.getState().setRectSel({ stepFrom: 2, stepTo: 3, noteFrom: 4, noteTo: 7 });
    useStore.getState().deleteSteps();
    const p = useStore.getState().patterns;
    expect(getStep(p, 0, 2).on).toBe(false);
    expect(getStep(p, 0, 3).on).toBe(true);
  });

  it('cutSteps copies then clears; pasteSteps anchors at the rect top-left', () => {
    light(0, 5); light(1, 7);
    useStore.getState().setRectSel({ stepFrom: 0, stepTo: 1, noteFrom: 0, noteTo: 11 });
    useStore.getState().cutSteps();
    expect(getStep(useStore.getState().patterns, 0, 0).on).toBe(false);
    useStore.getState().setRectSel({ stepFrom: 8, stepTo: 9, noteFrom: 0, noteTo: 11 });
    useStore.getState().pasteSteps();
    const p = useStore.getState().patterns;
    expect(getStep(p, 0, 8).note).toBe(5);
    expect(getStep(p, 0, 9).note).toBe(7);
  });

  it('pasteSteps with no selection anchors at lastCell, transposing to it', () => {
    light(0, 5);
    useStore.getState().setRectSel({ stepFrom: 0, stepTo: 0, noteFrom: 5, noteTo: 5 });
    useStore.getState().copySteps();
    useStore.getState().clearStepSel();
    light(10, 8); // sets lastCell = { step: 10, note: 8 }
    useStore.getState().pasteSteps();
    expect(getStep(useStore.getState().patterns, 0, 10).note).toBe(8);
  });

  it('duplicateSteps pastes right of the rect and reselects', () => {
    light(2, 5);
    useStore.getState().setRectSel({ stepFrom: 2, stepTo: 3, noteFrom: 0, noteTo: 11 });
    useStore.getState().duplicateSteps();
    expect(getStep(useStore.getState().patterns, 0, 4).note).toBe(5);
    expect(useStore.getState().rectSel).toEqual({ stepFrom: 4, stepTo: 5, noteFrom: 0, noteTo: 11 });
  });

  it('moveRectSel moves in both dimensions as a single undo entry', () => {
    light(2, 5);
    useStore.getState().setRectSel({ stepFrom: 2, stepTo: 2, noteFrom: 5, noteTo: 5 });
    useStore.getState().moveRectSel(3, -2);
    let p = useStore.getState().patterns;
    expect(getStep(p, 0, 2).on).toBe(false);
    expect(getStep(p, 0, 5).note).toBe(3);
    useStore.getState().undoSeq();
    p = useStore.getState().patterns;
    expect(getStep(p, 0, 2).note).toBe(5);
    expect(getStep(p, 0, 5).on).toBe(false);
  });

  it('selectAllSteps selects the full grid rect', () => {
    useStore.getState().selectAllSteps();
    expect(useStore.getState().rectSel).toEqual({ stepFrom: 0, stepTo: 15, noteFrom: 0, noteTo: 11 });
  });
});
```

Run: `npx vitest run src/store.test.ts` — Expected: FAIL (`setRectSel` undefined).

- [ ] **Step 2: Rework the store**

In `src/store.ts`:

1. Replace the `StepSel` type usage: delete `export interface StepSel { from: number; to: number }` and re-export `export type { RectSel } from './shared/seqEdit';`. Change `SeqClipboard`'s range arm to `{ kind: 'rect'; data: RectCells }`. Import `type RectSel, type RectCells, rectNorm, copyRect, clearRect, pasteRect, moveRect` from `./shared/seqEdit` and `NOTE_LANES` from `./noteseq`.
2. State: `stepSel: StepSel | null` → `rectSel: RectSel | null` (init `null`); add `lastCell: { step: number; note: number } | null` (init `null`). Interface declarations likewise: `setRectSel: (sel: RectSel | null) => void;` and `moveRectSel: (dStep: number, dNote: number, opts?: { copy?: boolean }) => void;` replace `setStepSel`/`shiftStepSel`.
3. In `toggleCell`, after computing `pat`, record the anchor when editing: `if (pat === get().editPattern) set({ lastCell: { step, note } });` (keep the rest as is).
4. Replace the selection/clipboard actions:

```ts
setRectSel: (sel) => set({
  rectSel: sel
    ? {
        stepFrom: Math.min(STEPS - 1, Math.max(0, sel.stepFrom | 0)),
        stepTo: Math.min(STEPS - 1, Math.max(0, sel.stepTo | 0)),
        noteFrom: Math.min(NOTE_LANES - 1, Math.max(0, sel.noteFrom | 0)),
        noteTo: Math.min(NOTE_LANES - 1, Math.max(0, sel.noteTo | 0)),
      }
    : null,
}),

selectAllSteps: () => set({ rectSel: { stepFrom: 0, stepTo: STEPS - 1, noteFrom: 0, noteTo: NOTE_LANES - 1 } }),

clearStepSel: () => set({ rectSel: null }),

copySteps: () => {
  const { patterns, editPattern, rectSel } = get();
  if (rectSel) {
    set({ clipboard: { kind: 'rect', data: copyRect(patterns, WT1_LAYOUT, editPattern, rectSel) } });
  } else {
    set({ clipboard: { kind: 'pattern', data: copyPattern(patterns, WT1_LAYOUT, editPattern) } });
  }
},

cutSteps: () => {
  const { patterns, editPattern, rectSel } = get();
  get().copySteps();
  pushSeqHistory();
  if (rectSel) {
    get()._setPatterns(clearRect(patterns, WT1_LAYOUT, editPattern, rectSel, EMPTY_STEP));
  } else {
    get()._setPatterns(clearRange(patterns, WT1_LAYOUT, editPattern, 0, STEPS - 1, EMPTY_STEP));
  }
},

pasteSteps: () => {
  const { patterns, editPattern, rectSel, lastCell, clipboard } = get();
  if (!clipboard) return;
  pushSeqHistory();
  if (clipboard.kind === 'pattern') {
    get()._setPatterns(pastePattern(patterns, WT1_LAYOUT, editPattern, clipboard.data));
    return;
  }
  // Anchor: current rect's top-left, else the last-touched cell, else in place.
  const anchor = rectSel
    ? { step: rectNorm(rectSel).stepLo, note: rectNorm(rectSel).noteHi }
    : lastCell ?? { step: 0, note: clipboard.data.noteHi };
  const dNote = anchor.note - clipboard.data.noteHi;
  get()._setPatterns(pasteRect(patterns, WT1_LAYOUT, editPattern, anchor.step, dNote, clipboard.data, NOTE_LANES - 1));
  get().setRectSel({
    stepFrom: anchor.step,
    stepTo: anchor.step + clipboard.data.wSteps - 1,
    noteFrom: anchor.note - (clipboard.data.noteHi - clipboard.data.noteLo),
    noteTo: anchor.note,
  });
},

duplicateSteps: () => {
  const { patterns, editPattern, rectSel, chain } = get();
  if (rectSel) {
    const { stepLo, stepHi, noteLo, noteHi } = rectNorm(rectSel);
    const at = stepHi + 1;
    if (at >= STEPS) return; // nothing past the last step — full no-op
    pushSeqHistory();
    const data = copyRect(patterns, WT1_LAYOUT, editPattern, rectSel);
    get()._setPatterns(pasteRect(patterns, WT1_LAYOUT, editPattern, at, 0, data, NOTE_LANES - 1));
    get().setRectSel({ stepFrom: at, stepTo: at + (stepHi - stepLo), noteFrom: noteLo, noteTo: noteHi });
  } else {
    /* keep the existing duplicate-bar else-branch verbatim */
  }
},

deleteSteps: () => {
  const { patterns, editPattern, rectSel } = get();
  if (!rectSel) return;
  pushSeqHistory();
  get()._setPatterns(clearRect(patterns, WT1_LAYOUT, editPattern, rectSel, EMPTY_STEP));
},

moveRectSel: (dStep, dNote, opts = {}) => {
  const { patterns, editPattern, rectSel } = get();
  if (!rectSel) return;
  const { stepLo, stepHi, noteLo, noteHi } = rectNorm(rectSel);
  // Clamp so the whole rect stays inside the grid — stored selection and
  // pasted content must always agree.
  const ds = Math.min(STEPS - 1 - stepHi, Math.max(-stepLo, dStep | 0));
  const dn = Math.min(NOTE_LANES - 1 - noteHi, Math.max(-noteLo, dNote | 0));
  if (ds === 0 && dn === 0) return;
  pushSeqHistory();
  get()._setPatterns(moveRect(patterns, WT1_LAYOUT, editPattern, rectSel, ds, dn, { copy: opts.copy, emptyStep: EMPTY_STEP, maxNote: NOTE_LANES - 1 }));
  set({ rectSel: { stepFrom: stepLo + ds, stepTo: stepHi + ds, noteFrom: noteLo + dn, noteTo: noteHi + dn } });
},
```

Delete `shiftStepSel` and `setStepSel`. Update `src/hooks/useSeqEditKeys.ts` only if names changed (they didn't — no edit needed). Fix any other `stepSel` references: run `grep -rn "stepSel\|setStepSel\|shiftStepSel" src --include="*.ts*"` and update every hit (expected: `SeqPanel.tsx`, handled next step; `src/store.test.ts` old tests — update them to the rect API, preserving what they assert where still meaningful, e.g. full-width rects).

- [ ] **Step 3: Run store tests**

Run: `npx vitest run src/store.test.ts`
Expected: PASS.

- [ ] **Step 4: Rewire `SeqPanel.tsx`**

1. Replace subscriptions: `stepSel`→`rectSel`, `setStepSel`→`setRectSel`, `shiftStepSel`→`moveRectSel` (keep `cutSteps` from Task 2).
2. Delete the sweep/move machinery: `sweepingRef`, `moveRef`, and the whole `useEffect` at lines 70–91.
3. Add the hook after `useSeqNoteDrag`:

```tsx
const { pending, startRectSelect, startRectMove, consumeRectClick } = useSeqRectSelect({
  editPattern,
  onSelect: setRectSel,
  onMove: (dStep, dNote, copy) => moveRectSel(dStep, dNote, { copy }),
});
const rect = pending ?? rectSel;
const inRect = (step: number, note: number): boolean => {
  if (!rect) return false;
  const { stepLo, stepHi, noteLo, noteHi } = rectNorm(rect);
  return step >= stepLo && step <= stepHi && note >= noteLo && note <= noteHi;
};
```

(import `rectNorm` from `../../shared/seqEdit`).
4. Note-drag follow-up (line 59): `setStepSel({ from: to, to })` → `setRectSel({ stepFrom: to, stepTo: to, noteFrom: note, noteTo: note })`.
5. Cell `onPointerDown` (line 170): replace the early `if (hosted || onToggleChordNote || event.shiftKey) return;` with:

```tsx
if (hosted || onToggleChordNote) return;
if (editable && event.shiftKey) { startRectSelect(event, step, note); return; }
if (editable && rectSel && inRect(step, note) && !pending) { startRectMove(event, step, note); return; }
```

6. Cell `onClick`: `if (consumeRectClick() || consumeDragClick()) return;` and delete the shift-click branch (lines 190–194).
7. Cell class: append `(editable && inRect(step, note) ? ' sel' : '')`.
8. Step-number footer: drop selection behavior — remove `onPointerDown`/`onPointerEnter`, `aria-pressed`, and the `selected` class from `ns-step-num`; remove the `<div className={"ns-step-sel…"}>` element and the `selLo/selHi/selected` computations.
9. Menu block (line 264): condition on `rectSel`; compute `const { stepLo, stepHi } = rectNorm(rectSel);` and pass `visibleLo={barOffset + stepLo} visibleHi={barOffset + stepHi}`.
10. Update the panel's header hint if it mentions selection, and the top-of-file comment.

- [ ] **Step 5: CSS**

In `src/index.css`: remove the `.ns-step-sel` rules (1272–1285) and add next to `.ns-cell.on`:

```css
.ns-cell.sel {
  box-shadow: inset 0 0 0 1px color-mix(in srgb, var(--ac-a) 75%, transparent);
  background: color-mix(in srgb, var(--ac-a) 14%, transparent);
}
.ns-cell.on.sel { box-shadow: inset 0 0 0 2px var(--ac-a); }
```

(Match the file's existing accent-variable idiom — check how `.ns-step-sel` colored itself and reuse that exact color source.)

- [ ] **Step 6: Typecheck + full suite**

Run: `npx tsc --noEmit && npx vitest run`
Expected: clean; all tests pass.

- [ ] **Step 7: Commit**

```bash
git add -A src docs
git commit -m "feat(wt1): shift-drag rectangle selection with 2D cut/copy/paste/dup"
```

---

### Task 4: BL-1 — rect selection state, verbs, panel wiring

**Files:**
- Modify: `src/bass/store.ts`
- Modify: `src/bass/components/PitchSeq.tsx`
- Modify: `src/bass/hooks/useBassKeys.ts` (only the `getState()` destructure at ~line 66: `stepSel` → `rectSel`)
- Modify: `src/bass/bass.css` (`.bl-cell.sel`; retire `.bl-seq-col.selected` rules at 463–468)
- Test: `src/bass/store.test.ts` (extend)

**Interfaces:**
- Consumes: same shared helpers as Task 3; `useSeqRectSelect`.
- Produces: mirror of Task 3 on `useBassStore`, with BL-1 names: `rectSel`, `lastCell`, `setRectSel(sel)`, `moveRectSel(dStep, dNote, opts?)` (replaces `shiftSelection`; **delete `shiftClickStep`**), keeping `selectAllSteps/clearStepSelection/copySelection/cutSelection/pasteSelection/duplicateSelection/deleteSelection` names.

- [ ] **Step 1: Write failing store tests**

Append to `src/bass/store.test.ts` the same six scenarios as Task 3 Step 1, translated to the BL-1 API (`useBassStore`, `copySelection`/`cutSelection`/`pasteSelection`/`duplicateSelection`/`deleteSelection`, `getStep` from `../bass/seq`). Add one BL-1-specific test:

```ts
it('rect move preserves the slide flag', () => {
  useBassStore.getState().toggleCell(2, 5, 0);
  useBassStore.getState().toggleStepSlide(2, 0);
  useBassStore.getState().setRectSel({ stepFrom: 2, stepTo: 2, noteFrom: 5, noteTo: 5 });
  useBassStore.getState().moveRectSel(1, 1);
  const s = getStep(useBassStore.getState().patterns, 0, 3);
  expect(s.on).toBe(true);
  expect(s.note).toBe(6);
  expect(s.slide).toBe(true);
});
```

Run: `npx vitest run src/bass/store.test.ts` — Expected: FAIL.

- [ ] **Step 2: Rework the store**

Mirror Task 3 Step 2 in `src/bass/store.ts`, with two BL-1 differences:

1. **History:** BL-1's `_setPatterns` pushes the undo entry itself — do NOT call any explicit history push. Each verb makes exactly one `_setPatterns` call (`copySelection` makes none).
2. Delete `shiftClickStep` and `shiftSelection`; add `moveRectSel` (same body as Task 3's, minus `pushSeqHistory()`, using `LAYOUT` and BL-1's `EMPTY_STEP`/`NOTE_LANES` imports from `../seq` — note BL-1 already imports `EMPTY_STEP` from its own `seq.ts`).
3. `seqNorm` and the local `StepSel` type go away; use `rectNorm` from `../shared/seqEdit`.
4. `toggleCell` records `lastCell` when `pat === get().editPattern`, same as WT-1.
5. `pasteSelection`'s pattern-kind branch stays; the rect branch is identical to Task 3's `pasteSteps` rect branch (anchor at rect top-left → `lastCell` → in-place).

Then `grep -rn "stepSel\|setStepSelection\|shiftSelection\|shiftClickStep" src/bass` and fix every hit (expected: `PitchSeq.tsx` — next step, `useBassKeys.ts` line ~66, old tests in `store.test.ts`).

- [ ] **Step 3: Run store tests**

Run: `npx vitest run src/bass/store.test.ts`
Expected: PASS.

- [ ] **Step 4: Rewire `PitchSeq.tsx`**

Mirror Task 3 Step 4:

1. Subscriptions: `stepSel`→`rectSel`, `setStepSelection`→`setRectSel`, `shiftSelection`→`moveRectSel`; drop `shiftClickStep`.
2. Delete `sweepAnchor`/`startSweep` (lines 55–73) and `shiftDrag`/`startShiftDrag` (lines 78–104) and `isInSelection`.
3. Add `useSeqRectSelect` + `rect`/`inRect` exactly as in Task 3 (hook opts: `editPattern`, `onSelect: setRectSel`, `onMove: (dStep, dNote, copy) => moveRectSel(dStep, dNote, { copy })`).
4. Note-drag follow-up (line 48): select the dropped cell as a 1×1 rect.
5. Cell `onPointerDown`: replace `if (hosted || event.shiftKey) return;` with the same three-branch gate as Task 3 (BL-1 has no chord callbacks — gate is `pattern === editPattern` for the rect branches).
6. Cell `onClick`: `if (consumeRectClick() || consumeDragClick()) return;` and delete the `shiftClickStep` branch.
7. Cell class: append `(pattern === editPattern && inRect(step, note) ? ' sel' : '')`; remove `inSel` from the column div.
8. `bl-step-num` span: remove its `onPointerDown` and the `data-step-num` attributes' interactive role (keep the label).
9. Menu block: condition on `rectSel`, positions from `rectNorm(rectSel)`.
10. Update the file-top comment (selection is now shift-drag rectangle).

- [ ] **Step 5: CSS**

In `src/bass/bass.css`: delete the `.bl-seq-col.selected` rules and add next to the `.bl-cell.on` rule:

```css
.bl-cell.sel {
  box-shadow: inset 0 0 0 1px color-mix(in srgb, var(--ac-a) 75%, transparent);
  background: color-mix(in srgb, var(--ac-a) 14%, transparent);
}
.bl-cell.on.sel { box-shadow: inset 0 0 0 2px var(--ac-a); }
```

(Reuse the exact color idiom the old `.bl-seq-col.selected` used.)

- [ ] **Step 6: Typecheck + full suite**

Run: `npx tsc --noEmit && npx vitest run`
Expected: clean; all tests pass.

- [ ] **Step 7: Commit**

```bash
git add -A src
git commit -m "feat(bl1): shift-drag rectangle selection with 2D cut/copy/paste/dup"
```

---

### Task 5: Headless verify + docs

**Files:**
- Modify: `docs/editing-concept.md` (selection section — describe the rectangle model)
- No code changes expected; fixes only if verification finds bugs.

- [ ] **Step 1: Full suite + typecheck**

Run: `npx tsc --noEmit && npx vitest run`
Expected: all green.

- [ ] **Step 2: Headless interactive verification**

Invoke the project `/verify` skill (`Skill: verify`) for the WT-1 and BL-1 surfaces. Verify at minimum, on each surface:
1. Shift-drag across cells produces a highlighted rectangle and the mini-menu shows `CUT · COPY · DUP · DEL · ✕`.
2. DEL clears only notes inside the rectangle's pitch band (place one note inside, one outside the band in the step range first).
3. Drag inside the rectangle moves the block right and down/up (notes transpose); Cmd-Z restores in one undo.
4. Cmd-C then clicking an empty cell then Cmd-V pastes at that cell.
5. Step-number row no longer selects; plain click still toggles notes; single-note drag still works.

- [ ] **Step 3: Update docs**

Rewrite the selection paragraphs of `docs/editing-concept.md` to the rectangle model (shift-drag rect, 2D verbs, paste anchor rules, standalone-only gating unchanged).

- [ ] **Step 4: Commit**

```bash
git add docs
git commit -m "docs(seq): editing concept — rectangle selection model"
```
