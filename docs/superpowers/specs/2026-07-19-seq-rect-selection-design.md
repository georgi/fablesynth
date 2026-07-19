# WT-1 / BL-1 sequencer: shift-drag rectangle selection

**Date:** 2026-07-19 · **Scope:** web UI only (JUCE parity later)

## Goal

Replace the 1D step-range selection in the WT-1 NOTE SEQ and BL-1 PITCH SEQ
grids with a 2D rectangle selection:

1. Shift-drag anywhere in the grid selects a rectangle (step range × pitch
   range).
2. The selection can be **cut, copied, deleted, or duplicated** from the
   floating mini-menu; **paste** lands via Cmd-V.
3. Rectangle selection is the *only* selection gesture — the step-number-row
   sweep and shift-click-extend are removed.

## Non-goals

- Hosted (SQ-4) mode: standalone-only, matching existing gating.
- Cross-device clipboard (WT-1 ↔ BL-1 paste).
- JUCE port (tracked separately once web behavior settles).

## Design

### Selection model (both stores)

`stepSel { from, to }` becomes `rectSel { stepFrom, stepTo, noteFrom,
noteTo }` (grid row indices; normalize to lo/hi on read). The step-number row
becomes display-only. Selection is valid only for the edited pattern, as
today.

### Gesture (both panels)

- **Shift+pointerdown** anywhere in the grid anchors the rectangle;
  pointer-move updates the opposite corner (live highlight via the existing
  `selected` cell class); pointerup commits.
- Plain click still toggles notes; the existing single-note drag stays.
- **Pointerdown inside the current rectangle** drags the whole block — moves
  in both dimensions (step shift *and* transpose), Alt = copy. Committed once
  on release (one undo entry), like the current shift-drag.
- **Esc** or the menu ✕ dismisses the selection.

### Verbs

All verbs operate on notes with step ∈ rect AND pitch ∈ rect:

- **CUT** (new): copy + clear.
- **COPY**: clipboard stores entries relative to the rect's top-left
  (`{dStep, …note fields}`) plus rect dimensions. Per-store clipboards —
  WT-1 and BL-1 step shapes differ.
- **PASTE** (Cmd-V, no menu button): anchors at the current rect's top-left,
  else the last-clicked cell. Overwrites what it lands on; notes past the
  pattern end or off the pitch range are dropped. Selection moves to the
  pasted block.
- **DUP**: paste immediately right of the rect at the same pitches; selection
  moves to the new block.
- **DEL**: clear the rect's notes.
- Keyboard: Cmd-X/C/V/D, Delete/Backspace, Esc — updated in `useBassKeys`
  and the WT-1 key surface.

### Mini-menu

`SeqSelectionMenu` gains a CUT button: `CUT · COPY · DUP · DEL · ✕`.
Positioning unchanged (percent-centered over the selected columns, clamped,
scrolls with the grid).

### Code shape

- Shared rect pointer logic as a hook (e.g. `src/shared/useRectSelect.ts`)
  used by `SeqPanel` and `PitchSeq`.
- 2D region/clipboard math shared alongside `src/shared/seqEdit.ts`, with
  thin per-store verb adapters.
- Each verb is one history push + one `_setPatterns` → single undo entry,
  consistent with the existing `shiftStepSel` / `shiftSelection`.

### Testing

- Vitest on both stores: cut/copy/paste/dup/delete with pitch-band
  filtering, out-of-bounds drop on paste, move-with-transpose, single undo.
- Headless `/verify` pass on both surfaces for the interactive behavior.
