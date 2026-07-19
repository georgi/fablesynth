# WT-1 / BL-1 sequencer UX: note drag, grid selection, selection mini-menu

**Date:** 2026-07-19 · **Scope:** web UI only (JUCE parity later)

## Goal

Direct manipulation in the WT-1 NOTE SEQ and BL-1 PITCH SEQ grids:

1. Drag a lit note cell to another step and/or lane to move it (Alt-drag = copy).
2. Select notes from the grid itself (shift-click a cell extends the step range,
   in addition to the existing step-number-row sweep).
3. When a selection exists, a floating mini-menu appears over it with
   COPY · DUP · DEL · ✕ (dismiss), wired to the existing clipboard verbs.

## Non-goals

- Hosted (SQ-4) mode: new interactions are standalone-only, matching the
  existing selection gating in `SeqPanel` and the keyboard-verb surfaces.
- Multi-note (poly/chord) drag in hosted WT-1.
- JUCE port (tracked separately once web behavior settles).

## Design

### Store verb (both stores)

`moveStepNote(from, to, note, opts?: { copy?: boolean }, pattern?)`:

- No-op if the source step is off, or nothing changes.
- Reads the source step, clears it (unless `copy`), writes `{on, note}` at the
  destination preserving oct / acc / duration (+ slide on BL-1).
- One history push, one `_setPatterns` call → single undo entry, consistent
  with `shiftStepSel` / `shiftSelection`.

### Note drag (both panels)

- `pointerdown` on a **lit** cell arms a drag; it becomes one only after the
  pointer leaves the source cell (so a plain tap still toggles the note off —
  the panel suppresses the click after a real drag).
- Hover target found via `elementFromPoint` → `[data-seq-cell]` with
  `data-step` / `data-note` / `data-pattern`, matching the sweep-drag pattern
  already in `PitchSeq`. Only cells of the edited pattern accept a drop.
- Visual feedback: `drag-src` class on the source cell, `drag-over` on the
  hovered cell (local React state).
- `pointerup` commits via `moveStepNote` (Alt held = copy) and selects the
  destination step; `Escape` cancels.

### Selection mini-menu

New shared presentational component `src/components/SeqSelectionMenu.tsx`:

- Props: `prefix` ('ns' | 'bl'), `selLo`, `selHi`, `totalSteps`, and
  `onCopy` / `onDuplicate` / `onDelete` / `onDismiss` callbacks.
- Rendered inside the grid (absolute, percent-positioned over the selected
  columns, clamped so it stays in view; scrolls with the grid).
- Buttons call the existing store verbs (`copySteps`/`duplicateSteps`/
  `deleteSteps`/`clearStepSel` on WT-1; `copySelection`/`duplicateSelection`/
  `cutSelection`-free `deleteSelection` equivalents on BL-1).
- Shown only when a selection exists on the edited pattern and not hosted.

### Testing

- Vitest: `moveStepNote` unit tests in both store test files (move, copy,
  pitch-change in place, off-source no-op, field preservation, single undo).
- Headless `/verify` pass on both surfaces for the interactive behavior.
