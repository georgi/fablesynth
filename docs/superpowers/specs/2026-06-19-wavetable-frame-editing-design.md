# Wavetable Frame Editing (Serum-style) — Design Spec

**Date:** 2026-06-19
**Status:** Approved (design); pending implementation plan
**Builds on:** `docs/superpowers/specs/2026-06-19-wavetable-editor-redesign-design.md`

## Goal

Let the wavetable editor author **multi-frame** wavetables by hand — the Serum model
where a table is an ordered stack of single-cycle frames and the oscillator's `POS`
parameter morphs through them. Today the engine already plays, morphs, visualizes, and
imports multi-frame tables; only the **DRAW** editor is stuck at a single frame. This adds
a frame model + frame-strip UI + per-frame editing to the editor, in both the web app and
the JUCE plugin.

## Motivation / context

- The engine is already fully frame-based: each osc has a `.pos` param (0–1, `src/params.ts`)
  that morphs between frames; `WavetableView` renders the live 3D terrain tracking the
  modulated position; factory tables are 16-frame morphs; audio import slices up to
  `MAX_FRAMES` (64) frames.
- The editor's DRAW mode only ever produces **one** frame, so hand-built tables can't morph.
- The recent read-only / "collapse to 1 frame" rules for multi-frame user tables were a
  workaround for the editor's missing frame model. **This feature replaces them** (see below).

## Scope

In scope:
- Editor holds an ordered **frame list** instead of a single curve; the draw pad edits the
  currently-selected frame.
- **Frame strip** UI: per-frame thumbnails, select, `[+]` add (duplicate current), `[✕]`
  delete (min 1), drag-to-reorder, frame count.
- **Live 3D stack preview** in the editor showing all frames with the current one highlighted.
- Per-frame SEED / PEN / SMOOTH / SNAP / CLEAR (all act on the current frame).
- Selecting a user table loads **all** its frames editable; audio import populates the strip;
  factory tables load all frames **read-only**.
- CREATE / UPDATE writes the whole multi-frame table.

Out of scope (clean later additions, explicitly deferred):
- Keyframe + auto-interpolation authoring (draw sparse keyframes, generate in-betweens).
- POS-scrub auditioning inside the editor (sweep the morph position to preview interpolation).
- Per-frame spectral/FFT effects, frame-blend/morph-fill tools.
- Raising `MAX_FRAMES` beyond 64.

## Supersedes the read-only / collapse rules

The redesign added two rules as a stopgap for the single-cycle pad:
1. multi-frame user tables load **read-only**;
2. drawing on / UPDATE-ing a multi-frame table **collapses** it to one cycle.

Both are **removed** by this feature. With a real frame model:
- Selecting a multi-frame **user** table loads all frames into the strip, fully editable.
- UPDATE writes every frame back — lossless round-trip; nothing collapses.
- **Factory** tables remain read-only (duplicate to edit), but their frames are now visible
  in the strip.

Affected code to revert/replace: web `selectUser` (`src/components/WavetableEditor.tsx`) and
JUCE `WavetableEditor::selectUser` (`juce/source/ui/WavetableEditor.cpp`) — both currently
load only frame 0; they will load the full frame array. The `readOnly`/`readOnlySel` state
remains, driven solely by factory-vs-user.

## Interaction model

- **Authoring:** explicit frame list. A table is an ordered list of single-cycle frames you
  edit one at a time. Playback morphs (linear-interpolates) between adjacent frames via the
  `POS` knob — identical to factory tables; no editor-side interpolation engine.
- **Current frame:** one selected frame is shown in the draw pad and highlighted in the strip
  and 3D preview. The pad's existing math is unchanged; it just targets `frames[current]`.
- **Add `[+]`:** insert a copy of the current frame immediately after it; select the new copy.
- **Delete `[✕]`:** remove the selected frame; clamp selection; refuse when only one remains.
- **Reorder:** drag a thumbnail to a new position in the strip; the frame order is the morph
  order. Selection follows the dragged frame.
- **NEW:** creates a 1-frame sine table (unchanged), which you then grow via `[+]`.
- **Import:** the sliced frames become the editor's frame list, editable before commit.

## Architecture

Build **web-first, then port to JUCE** (the repo's established direction).

### Editor frame state

- **Web** (`src/components/WavetableEditor.tsx`): replace the single
  `pointsRef: number[]` with `framesRef: number[][]` (each `DRAW_N = 256` points) plus a
  `currentFrame` state index. A `frameVersion` counter forces strip/pad/preview repaint. The
  draw pad reads/writes `framesRef.current[currentFrame]`. Commit:
  `makeUserTable(name, framesRef.current.map(frameFromDrawing))`.
- **JUCE** (`juce/source/ui/WavetableEditor.{h,cpp}`): the editor owns
  `std::vector<std::vector<float>> frames` (each `DrawPad::DRAW_N`) + `int currentFrame`.
  `DrawPad` is told which buffer to edit (it already holds one `pts` vector — the editor
  swaps its contents on frame change, or `DrawPad` gains `setPoints`/`points` round-trips,
  which already exist). Commit uses `fable::makeUserTable(name, frames)` (frames resampled to
  `SIZE` via the existing `frameFromDrawing`).

### FrameStrip component (new)

Pure presentation over `{ frames, currentFrame }` + callbacks
`{ onSelect, onAdd, onDelete, onReorder }`:
- **Web:** `src/components/wavetable/FrameStrip.tsx` — a flex row of small `<canvas>` thumbnails
  (reusing `TableThumb`-style frame-0 drawing, but per frame), current frame highlighted in
  accent; `[+]` button; per-thumb `[✕]` on hover/selected; drag-reorder via pointer events
  (compute target index from x). Shows `N frames`.
- **JUCE:** a `FrameStrip` child component drawing thumbnail cells (reuse the `TableThumb`
  painter per frame), with click-select, `[+]`/`[✕]` buttons, and pointer-drag reorder.

### Live 3D stack preview

Reuse the perspective terrain already used by `TablePreview` (JUCE) and the
`WavetableView`/`TablePreview` canvas (web): feed it the current `frames` and highlight the
current frame (e.g. draw it in accent, the rest dimmed). Static terrain (not POS-animated) in
v1 — it updates as frames are edited/added/reordered.

## Components / boundaries

- **FrameStrip** (new, web + JUCE): renders frames, emits select/add/delete/reorder. No state
  of its own beyond drag-in-progress.
- **DrawPad / draw canvas** (extend): unchanged drawing math; now targets the current frame
  buffer. Read-only state driven by factory-vs-user only.
- **Stack preview** (reuse): renders `frames` with current highlighted.
- **Editor** (orchestrate): owns `frames` + `currentFrame`; wires strip/pad/preview; commit.
- **Frame ops** (pure helpers): add/duplicate/delete/reorder on a `frames` array — small,
  testable functions shared in spirit across web (`drawmodel.ts`) and JUCE (free functions).

## Error handling / edge cases

- Delete with one frame remaining → no-op (a table needs ≥ 1 frame).
- Add beyond `MAX_FRAMES` (64) → no-op; surface a brief "max 64 frames" status, like the
  pool-full message.
- Reorder drag onto itself / out of bounds → clamp; no change.
- Switching frames mid-draw → commit the in-progress stroke to the leaving frame first.
- Selecting a factory table → frames load read-only; `[+]/[✕]`/reorder/draw disabled; the
  strip is browse-only; DUPLICATE makes an editable user copy (existing behavior).
- Empty/short imported frames → already normalized by `sliceToFrames`/`makeUserTable`.

## Testing

- **Web:** in-browser visual verification (chrome-debug) — build a 3-frame table, reorder,
  delete, confirm the 3D preview and thumbnails track; confirm a multi-frame user table
  round-trips (select → edit a middle frame → UPDATE → reselect shows the edit). `tsc` clean.
- **JUCE:** TDD the pure frame-ops in `juce/test/engine_test.cpp` — add/duplicate/delete/
  reorder produce the expected frame count and a `makeUserTable` over N frames yields a
  `frames*SIZE` wave and an audible, finite band-limited pyramid. Full build + `ctest`.
- Both: confirm assignment still drives the osc `.table` param, and that the engine morphs the
  authored frames (POS sweeps through them) — manual check in the rack's `WavetableView`.

## Open questions

None blocking. Implementation plan to sequence: (1) web frame state + ops, (2) web FrameStrip,
(3) web stack preview + wiring + remove read-only/collapse, (4) web visual verification,
(5) JUCE frame ops (+ engine_test), (6) JUCE FrameStrip + wiring, (7) build/test.
