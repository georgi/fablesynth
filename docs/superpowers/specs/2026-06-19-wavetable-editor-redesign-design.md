# Wavetable Editor Redesign — Design Spec

**Date:** 2026-06-19
**Status:** Approved (design); pending implementation plan
**Source:** Claude Design handoff bundle `Synthesizer mockup design-handoff.zip`
(`synthesizer-mockup-design/project/Wavetable Editor.dc.html`)

## Goal

Rebuild the user-wavetable editor in both the web app (`src/components/WavetableEditor.tsx`)
and the JUCE plugin (`juce/source/ui/WavetableEditor.{h,cpp}`) to match the mockup: a
**library manager** with factory + user tables alongside the existing create/import editor,
plus the mockup's richer draw tools. The change is structural — the palette/typography are
already shared 1:1 between both codebases (`Theme.h` ⇆ `src/index.css :root`).

## Scope

In scope:
- Library sidebar: FACTORY + USER sections, per-row waveform thumbnail, search filter, `+ NEW`.
- CRUD on user tables: rename (inline), duplicate, delete (delete already exists).
- Duplicate a factory table → new editable user table copied from its frames.
- DRAW pane additions: seed shapes (SINE/SAW/SQUARE/TRI), PEN/SMOOTH brush, SNAP (quantize 1/8).
- IMPORT pane: keep the three slice modes + perspective preview; restyle to the mockup.
- Layout: modal scales to ~fill the plugin window; animated cyan top scanline; header title.

Out of scope:
- `Fable Synth.dc.html` (full synth screen) and `FableKnob.dc.html` (standalone knob) — not
  part of the wavetable editor.
- The mockup's header **target dropdown**. We keep the existing "open for a specific osc"
  launch model (see Interaction).

## Interaction model (decided)

**Hybrid.** The editor is still launched *for one oscillator* (`openFor(osc)` / `openEditor(osc)`).
That osc is the fixed assignment target — header reads `WAVETABLE → OSC A`, no dropdown. Inside,
the editor behaves as a full library manager. Closes on the X button or backdrop click.

**Row click = assign + load:**
- Clicking any library row assigns that table to the target osc **and** loads it as the
  working table (name + thumbnail reflected in the editor).
- **1-frame user table** → loads into the DRAW pad; fully editable; button reads `UPDATE TABLE`
  and writes back to that table.
- **Multi-frame user table, or any factory table** → DRAW pad shows frame 0 read-only (dimmed);
  draw-editing disabled. The button reads `CREATE TABLE` (a fresh table), not UPDATE. To get an
  editable copy, use DUPLICATE.

**Factory tables are read-only.** They can be selected (assign + load read-only) and duplicated,
never edited in place or deleted.

### The single-cycle vs. multi-frame wrinkle

The mockup treats every table as one 256-sample cycle. Real tables are not: factory tables are
16-frame morphs, and auto/fixed-len imports yield up to `MAX_FRAMES` (64) frames. A multi-frame
table cannot round-trip through a single-cycle draw pad without loss. Resolution:

- The DRAW pad only auto-opens (editable) for **1-frame** tables.
- Duplicating a multi-frame/factory table copies **all** frames into the new user table; the copy
  is assignable / renamable / deletable. Draw-editing it would still collapse to one cycle, so the
  pad stays read-only (frame 0) for it; the user edits via a fresh draw or re-import instead.
- Thumbnails and the "working table" preview show **frame 0**.

## Architecture

Build **web-first, then port to JUCE** — the established direction in this repo (every JUCE file
documents itself as a port of the corresponding `src/*.tsx`). Web allows live in-browser visual
verification against the mockup before the costlier hand-laid-out C++ pass.

### Shared data-model additions

Two new operations on each side, mirroring the existing add/delete:

- **Web** (`src/store.ts`, backed by `src/engine/usertables.ts`):
  - `renameUserTable(poolIndex: number, name: string)` — rename, persist to localStorage, re-push
    engine tables.
  - `duplicateUserTable(poolIndex: number)` — clone an existing **user** table.
  - Duplicating a **factory** table is a library-level action (the factory list isn't in the user
    pool): read the factory table's source frames and `makeUserTable(...)` a copy into the pool.
  - Persistence reuses the existing `saveUserTablePool` / `loadUserTablePool` path.
- **JUCE** (`juce/source/PluginProcessor.{h,cpp}`, backed by `dsp/UserTables.h`):
  - `renameUserTable(int poolIndex, std::string name)`
  - `duplicateUserTable(int poolIndex)`
  - Factory duplicate sources frames from the generated `tables`; new tables persist via the
    existing plugin-state save (only raw frames are stored; the band-limited pyramid is rebuilt).

Factory tables remain identified separately from the user pool (web: `f<i>` vs `u<i>` ids /
`TABLE_NAMES` offset; JUCE: `tables` vs `userTables`). No change to the osc `.table` parameter
encoding (combined factory-then-user index).

### UI structure (both platforms)

```
┌ WAVETABLE → OSC A ───────────────────────────────[✕]┐  ← header + animated cyan scanline
│ ┌ LIBRARY ─────────┐ ┌ EDITOR ───────────────────┐ │
│ │ [search]  [+ NEW] │ │ [DRAW] [IMPORT AUDIO]     │ │
│ │ FACTORY           │ │ NAME [____________]       │ │
│ │  ▸ thumb  PRIME ⎘ │ │ ┌ draw pad / import ────┐ │ │
│ │  ▸ thumb  BLOOM ⎘ │ │ │                       │ │ │
│ │ USER              │ │ └───────────────────────┘ │ │
│ │  ▸ thumb  MINE ✎⎘✕│ │ SEED: SINE SAW SQ TRI     │ │
│ │  ...              │ │ PEN SMOOTH SNAP  CLEAR ▸  │ │
│ └───────────────────┘ │            CREATE/UPDATE  │ │
│                       └───────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

- **Library aside** (~306px fixed): search box, `+ NEW`; FACTORY rows (thumbnail + name + frames +
  duplicate) and USER rows (thumbnail + name + frames + rename/duplicate/delete; ✎ swaps the name
  to an inline text field). Selected row carries the accent glow border.
- **Editor section** (flex): DRAW / IMPORT AUDIO tabs, NAME field, active pane.
  - **DRAW pane:** the pad; SEED buttons fill the pad with a base shape; PEN/SMOOTH brush toggle
    (SMOOTH applies the existing neighbour-average on paint); SNAP quantizes drawn values to 1/8;
    CLEAR; CREATE or UPDATE.
  - **IMPORT pane:** dashed choose-file button, SINGLE/AUTO/FIXED toggles (+ samples field when
    FIXED), perspective frame preview (existing `TablePreview` / canvas terrain), CREATE TABLE.

**Thumbnails:** render the table's frame-0 waveform small. JUCE: a lightweight cached path per
row (or a tiny child component); web: a `<canvas>` per row drawn with the existing canvas helper.

**Accent:** per-osc (cyan A `#4de8ff` / amber B `#ffa14d`), not the mockup's fixed cyan —
preserves the app's existing convention.

## Components / boundaries

- **Library list** (new): pure presentation over `{factoryTables, userTables, selectedId}` +
  callbacks (`onSelect/onRename/onDuplicate/onDelete/onNew/onSearch`). Testable from data alone.
- **Draw pad** (extend existing `DrawPad` / draw canvas): add seed-fill, brush mode, snap, and a
  read-only state. Same point buffer (`DRAW_N = 256`).
- **Import pane** (extend existing): unchanged logic, restyled.
- **Store / processor** (extend): rename + duplicate, persistence, factory-frame sourcing.

## Error handling / edge cases

- Pool full on create/duplicate → surface the existing "table pool full" message; keep editor open.
- Rename to empty → fall back to `USER` (matches existing `finalName`), max 14 chars (existing cap).
- Selecting a multi-frame/factory table → pad read-only, UPDATE hidden; no silent data loss.
- Duplicate name collisions are allowed (tables are index-identified, names are labels).
- Search with no matches → empty section, library chrome (search/+NEW) still visible.

## Testing

- **Web:** visual verification in-browser via the chrome-debug skill against the mockup; extend
  engine tests for rename/duplicate (pure functions in `usertables.ts`).
- **JUCE:** extend the headless engine tests for `renameUserTable` / `duplicateUserTable` and
  factory-duplicate frame sourcing (no GUI required; logic lives in the processor/DSP layer).
- Both: confirm assign-to-osc still drives the `.table` parameter correctly after select/create.

## Open questions

None blocking. Implementation plan to sequence: (1) web data-model + store, (2) web UI,
(3) web visual verification, (4) JUCE data-model/processor, (5) JUCE UI port, (6) tests.
