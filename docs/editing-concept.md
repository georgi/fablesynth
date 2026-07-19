# Editing concept

How selection, clipboard and undo/redo work across FableSynth's grid-based
editors. Two families share the same shape (anchor/head → normalized
rectangle → verbs that read the rectangle), implemented separately because
their underlying data differs:

- **SQ-4 session grid** — scene × track cells holding whole clips
  (`src/seq/gridEdit.ts`).
- **WT-1 / BL-1 step sequencers** — step × note-lane cells inside one pattern
  buffer (`src/shared/seqEdit.ts`, shared by both synths and reused by DR-1
  for its pad-row range ops).

## SQ-4: scene × track selection

`GridSel` is an anchor/head pair over `(s, t)` grid positions; the UI derives
the covered `GridRect` from it. Copy captures a rectangle of cells
(`GridClipboard`) tagged with each source column's `MachineId` — paste
refuses to write a cell whose target track is a different machine, since
clip pattern bytes are machine-specific and `validateSession` would reject
the mismatch. Writes are planned as pure `CellWrite[]` (`clip: null` clears a
slot) and applied by the store in one place, so cache sync / hot-swap /
persistence stay centralized.

## WT-1 / BL-1: step × note-lane rectangle selection

Both note grids (WT-1's NOTE SEQ, BL-1's PITCH SEQ) select a **rectangle**
over step (x) and note lane (y), not a 1-D step range. Shift-drag any cell of
the pattern currently being edited to sweep the rect; the highlighted cells
update live as the pointer moves, and the selection commits once on
pointer-release (one gesture, no intermediate store writes). Escape while
dragging cancels without committing. Selection is standalone-only — hosted
(SQ-4-driven) instances of these grids don't expose it.

A floating **CUT · COPY · DUP · DEL · ✕** menu appears centered over the
selected columns once a rectangle exists:

- **CUT / COPY** capture only the *lit* cells whose note lane falls inside
  the rect's pitch band — a lit cell in the step range but outside the pitch
  band is left untouched. The clipboard stores the rect's width, its
  normalized pitch band, and each captured cell's byte-offset from the rect's
  left edge.
- **DUP** copies the rect immediately to its right (same pitch band).
- **DEL** clears the same in-band-only cells CUT would have captured.
- **✕** dismisses the selection without touching the pattern.

**Cmd-C / Cmd-X / Cmd-V / Cmd-D / Delete·Backspace / Cmd-A / Escape / Cmd-Z /
Cmd-Shift-Z** drive the same verbs from the keyboard (copy / cut / paste /
duplicate / delete / select-all / clear-selection / undo / redo). Plain click
on a lit or empty cell still just toggles that one note — the step-number
row underneath the grid is a plain label now, not a click target, and no
longer starts a range selection.

**Paste anchor**: pasting re-anchors the clipboard rect's top-left corner at
whichever of these is available, in order — the *current* rectangle
selection's top-left, else the last cell you clicked or dragged, else the
pattern's start. So the common flow is: select and copy a rect, click an
empty cell to move the anchor there (Cmd-V would otherwise paste back onto
the still-active selection), then Cmd-V. Paste drops any cell that would land
outside the pattern's step range or the grid's note-lane range — it never
wraps or clamps into bounds. After a paste, the rectangle selection moves to
cover the pasted cells.

**Drag-move**: dragging from inside the current rectangle (rather than
starting a fresh shift-drag) moves the whole block — both step and note lane
can shift, i.e. the block can transpose as it moves. Alt-drag copies instead
of moving. Like selection, the move commits once on pointer-release, so
Cmd-Z undoes the entire block move — including the transpose — in a single
step. A plain click-without-drag on a cell inside the rectangle falls through
to the normal single-cell toggle.

Both grids share `src/shared/seqEdit.ts`'s pure helpers
(`copyRect` / `clearRect` / `pasteRect` / `moveRect`, all layout-parameterized
by a `SeqLayout` describing byte stride and pattern size) plus a bounded
undo/redo history (`makeHistory`) — the store pushes a pre-mutation snapshot
before each verb and pops it back on undo/redo.
