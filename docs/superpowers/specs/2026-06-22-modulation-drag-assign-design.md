# Serum-style drag-to-assign modulation — web + VST (shared 16-slot model)

Date: 2026-06-22
Status: approved, implementing

## Goal & principle

Both codebases converge on **one model**: a fixed pool of **16** mod slots, each
`{src, dst, amt}`. The Serum-style UX (drag a source chip onto a control, colored
depth rings, right-click remove, live matrix list) is a **view over those slots** —
drag-assign writes into the next free slot. Net effect:

- Cross-compatible presets (web ↔ VST) — identical `mat*` ids and source/dest ordering.
- Identical DSP — both iterate a fixed slot array (per-dest scaling unchanged → sound identical).
- Host-automatable mod depths on the VST (each `amt` is a real APVTS parameter).
- The web sheds its divergent dynamic `ModConnection[]` model.

## Decisions

- **Pool size: 16 slots** (`mat1`..`mat16`).
- **Both codebases reworked in one effort** (no interim divergence).
- **Matrix list keeps src/dst dropdowns** (not chips-only) so globals PITCH/AMP/PAN
  and fine edits stay reachable.
- **Ring-drag for depth is included** on the VST (full Serum fidelity), in addition
  to the matrix amt knob.

## Shared data model (identical on both sides)

- **Slots:** `mat1`..`mat16`, each `.src` / `.dst` / `.amt` (amt −1..1).
  - VST: APVTS params (auto-persist + automatable).
  - Web: plain params in the store.
- **Sources** (index → color):
  `— / LFO 1 / LFO 2 / MOD ENV / VELO / NOTE`
  → `'' / #4de8ff / #ffa14d / #b18cff / #9fb4d8 / #9fb4d8`.
- **Dests:** `— / A POS / B POS / F1 CUT / PITCH / AMP / PAN / A LVL / B LVL / F2 CUT / F2 RES`.
- **DEST_OF_CONTROL:** `oscA.pos→1, oscB.pos→2, filter.cutoff→3, oscA.level→7,
  oscB.level→8, filter2.cutoff→9, filter2.res→10`. `PITCH(4)/AMP(5)/PAN(6)` are
  global — assignable only from the matrix list.
- **Slot helper** (mirrored per language): `findFreeSlot()` → first slot with src=0;
  `addRoute(src,dst,amt)`; `clearSlot(n)`. Used by drop-to-assign and ADD ROUTE.

## VST architecture

- **DnD plumbing:** `PluginEditor` becomes a `juce::DragAndDropContainer`.
- **`ModSourceChip`** (new, `ui/`): draggable pill (grip + label, source-colored),
  carries its source index. Placed in the LFO panel (LFO 1/2), Env panel (MOD ENV),
  and the matrix panel (VELO/NOTE) — mirroring the web placement.
- **`Knob` + `VSlider`** gain `int modDest` and implement `juce::DragAndDropTarget`:
  highlight when a compatible drag hovers; on drop → `addRoute(src, thisDest, default)`.
- **Mod rings:** in `paint`, each control draws one arc per slot whose `dst == modDest`
  (knob: arc from current value to current+amt, stacked at increasing radii;
  VSlider: a side depth-band), colored by source. **Depth/remove:** `mouseDown` in the
  ring zone grabs that route — vertical drag writes `mat{n}.amt`; right-click clears the
  slot. Body presses behave as the normal control. Ring zone is a clear annulus outside
  the knob body so the two interactions never fight.
- **`MatrixPanel` rework:** from 4 fixed dropdown rows → a **live list of active slots**
  (rebuilt on a low-rate timer that diffs slot usage to avoid combobox flicker). Each row
  = src dropdown · `→` · dst dropdown · amt knob · remove ✕. Top: the 5 source chips +
  an **ADD ROUTE** button (allocates next free slot).
- **DSP:** `Engine.cpp` mod loop `1..4` → `1..16`; `Params` enum/`addMat` extended to 16
  (`FXDRIVE_ON` shifts after `MAT16_BASE`). Per-dest scaling unchanged.

## Web rework

Replace dynamic `ModConnection[]` with the 16 fixed slot params in the store;
worklet/synth iterate the fixed slots. Keep the existing drag UX (`ModSourceChip`,
`Knob` rings, `VSlider` band, matrix list) but repoint it at slots via the shared helper.
User-preset `mods` arrays migrate to slots on load (reverse of the existing migration).

## State / preset compatibility

APVTS persistence is automatic. Existing 4-slot VST presets load unchanged (5..16 default
off). Web ↔ VST presets become interchangeable (identical `mat*` ids + index ordering).

## Testing

- **DSP** (`engine_test`): assert a slot beyond #4 modulates its dest; assert 16 slots accumulate correctly.
- **Slot helper:** unit tests for find-free / add / clear (both languages).
- **`plugin_host_test`:** assign a route via params, render PNG, confirm the ring draws.
- **Web:** mirror existing test patterns for slot helpers + `mods`→slots migration.

## Risks

- Ring hit-testing vs. normal control drag — mitigated by a clear annulus/side-band ring zone.
- Matrix-list rebuild churn — mitigated by diffing slot usage before rebuilding rows.
