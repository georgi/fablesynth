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

---

## Implementation contract (locked decisions — single source of truth for all agents)

Resolves the inconsistencies found during mapping. Implement EXACTLY this.

### Model & params
1. **16 slots.** `mat1..mat16`, fields `.src/.dst/.amt`. Add `constexpr int MOD_MATRIX_SIZE = 16;` to `Params.h` (juce-free — no juce types in the DSP header).
2. **C++ enum layout:** insert `MAT5_BASE..MAT16_BASE` contiguously between `MAT4_BASE` and `FXDRIVE_ON` (each `= prev + MAT_NFIELDS`); set `FXDRIVE_ON = MAT16_BASE + MAT_NFIELDS`. `matBase(s)` is unchanged (valid 1..16); update its comment to `s = 1..16`. **Accept the downstream index shift as a known breaking change** for hosts that cache automation by parameter index in *existing* sessions; APVTS state/preset recall is by string id, so reload is safe.
3. **`Params.cpp`:** replace the 4 explicit `addMat` calls with `for (int i = 1; i <= 16; ++i) addMat(v, i, matBase(i));`. `NUM_PARAMS` grows by 36 automatically.
4. **`params.ts`:** add `mat1..16` defs (`.src` enum `MOD_SOURCES` def 0; `.dst` enum `MOD_DESTS` def 0; `.amt` lin min -1 max 1 def 0 fmt `fmtBi`), inserted after the lfo2 block and before `fx.drive.on` to mirror the C++ order. `defaultParams()` must then emit all 16 slots.
5. **MOD_SOURCES index-0 glyph:** change the VST `Params.h` index-0 label from `"-"` to `"—"` (em dash) to match the web for label parity.

### Shared constants & defaults
6. **Default amt = `0.3` (REAL value), one constant per language.** C++: `constexpr float MOD_DEFAULT_AMT = 0.3f;` in `ui/Modulation.h`. TS: `export const MOD_DEFAULT_AMT = 0.3` in `store/slotHelpers.ts`. Both `addRoute` helpers use it. No `0.5` anywhere.
7. **Source colors — single home:** `juce::Colour fui::modSourceColour(int srcIndex)` in `Theme.h`, bounds-checked 0..5: `[transparent, #4de8ff, #ffa14d, #b18cff, #9fb4d8, #9fb4d8]`. Do NOT add `SOURCE_COLORS` to `Params.h` or a private copy in `Controls.cpp`. Web keeps its `SOURCE_COLORS` in `params.ts` unchanged.
8. **No global C++ `DEST_OF_CONTROL` map.** Each control's `modDest` is passed as a literal at its construction site (see §16). Web keeps the string-keyed `DEST_OF_PARAM` as-is.

### Slot predicates (use these exact definitions everywhere)
9. `findFreeSlot` → first slot 1..16 with **src==0 AND dst==0** (fully empty, so a half-configured ADD-ROUTE row is never clobbered). `isSlotActive` (engine apply / matrix "live") → **src!=0 AND dst!=0**. `rowVisible` (matrix list shows it) → **src!=0 OR dst!=0**. The engine loop already does `if (!src || !dst) continue` — unchanged.

### VST slot helpers — single home `ui/Modulation.{h,cpp}`, namespace `fui` (APVTS-based)
10. `int findFreeSlot(APVTS&)`, `int addRoute(APVTS&, int src, int dst, float amt = MOD_DEFAULT_AMT)` (returns slot or -1), `void clearSlot(APVTS&, int slot)` (zeroes src, dst, AND amt), `bool isSlotActive(APVTS&, int slot)`. **All read/write REAL values** via `param->convertFrom0to1(param->getValue())` and `param->setValueNotifyingHost(param->convertTo0to1((float)realValue))`. `Controls.cpp` and `Panels.cpp` MUST call these — no private copies, no `value/size` normalization.

### VST UI mechanics
11. **Drag payload:** `juce::var` description `"mod-src:" + juce::String(src)`. `isInterestedInDragSource` checks the description starts with `"mod-src:"`; `itemDropped` parses the int after the colon.
12. **`Knob`/`VSlider` ctor:** append a trailing `int modDest = 0` parameter (last arg; 0 = not a mod target). When `modDest>0` the control is a `DragAndDropTarget` and paints rings.
13. **`Controls.h`:** keep existing `private juce::Timer`; add `public juce::DragAndDropTarget` to `Knob` and `VSlider`. Members: `int modDest_ = 0; bool dragHover_ = false; struct Ring { int slot, src; float amt; }; std::vector<Ring> rings_; int grabbedRing_ = -1;`. Cache the 48 mat `juce::RangedAudioParameter*` once in the ctor. Declare DnD overrides: `isInterestedInDragSource`, `itemDragEnter`, `itemDragExit`, `itemDropped`. Drop the `onModDrop` callback (call `fui::addRoute` directly in `itemDropped`).
14. **Ring render + hit-test (Knob):** rebuild `rings_` in `timerCallback` ONLY when this dest's active-slot signature changed (compare a cheap vector/hash; don't churn). Draw each ring as an arc in `fui::modSourceColour(src)` from the current-value angle to the angle at `clamp(currentNorm + amt)` (bipolar), at radius `bodyR + gap + i*ringThk` (stacked). `mouseDown`: if the point is in the ring annulus `[bodyR+gap, outerR]`, pick the ring by radius band `i = floor((dist-(bodyR+gap))/ringThk)`, set `grabbedRing_`; **right-button → `fui::clearSlot(slot)` and return**. `mouseDrag`: if `grabbedRing_>=0`, `deltaY` adjusts that slot's amt (real, clamped -1..1) via `setValueNotifyingHost`; else existing value-drag. `mouseUp`: `grabbedRing_=-1`. Body presses (dist < bodyR+gap) keep existing behavior.
15. **`VSlider` depth band:** a vertical band beside the track from current pos to current+amt, colored by source; side-band grab zone; right-click clears. Same slot semantics.
16. **`PluginEditor`:** add `public juce::DragAndDropContainer` (multiple inheritance). `Rack` (parent of panels) is already a child of the editor, so DnD walks up correctly.
17. **`ModSourceChip`** (in `ui/Modulation.{h,cpp}`): `juce::Component`, draggable. On drag start: `if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this)) c->startDragging("mod-src:" + juce::String(src_), this);`. Paints a pill (grip glyph + optional label, tinted `modSourceColour(src_)`); `compact` flag = grip-only for tight headers.

### VST panel wiring (`Panels.cpp`) — pass `modDest` at every construction site
18. OscPanel: `pos` VSlider → `modDest` 1 (oscA) / 2 (oscB); `level` Knob → 7 / 8. FilterPanel block: `cutoff` Knob → 3 (f1) / 9 (f2); `res` Knob → 10 (f2 only). LfoPanel: add `ModSourceChip` for LFO1 (src 1) / LFO2 (src 2) in block headers. EnvPanel: MOD ENV is source-only → add a `ModSourceChip` (src 3); its knobs get `modDest 0`.
19. **`MatrixPanel` rework:** 16 `Row` structs (src ComboBox, `→`, dst ComboBox, amt `Knob`, remove `✕` → `fui::clearSlot`); a row is built/shown only when `rowVisible` (§9). Header: 5 `ModSourceChip`s (LFO1=1, LFO2=2, MOD ENV=3, VELO=4, NOTE=5) + an **ADD ROUTE** button → `fui::addRoute` into next free slot with `src=1 (LFO 1), dst=0 (—)` (visible, editable, inactive until dst set). Low-rate timer (~3 Hz) diffs the visible-slot vector before rebuilding to avoid combobox flicker/focus loss.

### Web
20. **Store (`store.ts` + new `store/slotHelpers.ts`) — params-as-truth.** Remove the `mods` field and `addMod/updateMod/removeMod`. `slotHelpers.ts`: `findFreeSlot/getActiveSlots/getModsByDest/setMatSlot/clearSlot` over `ParamValues` (use §9 predicates). store: `addRoute(src,dst,amt=MOD_DEFAULT_AMT)/updateSlot(n,patch)/clearSlot(n)` routing through `setParam('mat{n}.*')`. Keep `setModDrag`.
21. **Engine (`worklet.js` + `synth.ts`) — fixed slots.** worklet iterates `mat1..16` from `this.p` (remove `this.mods` + `case 'mods'`); synth removes the `mods` field, `setMods`, and the `mods` postMessage. DSP then mirrors the VST exactly.
22. **Presets (`presets.ts`):** `resolvePresetMods` keeps factory `mat1..4` in params (zero `mat5..16`), expands any explicit user-preset `mods[]` into slots in order (truncate >16, zero the rest), returns params only. Saving extracts `mat*` from params. Test both directions + a >16 truncation case.
23. **Components (`Knob.tsx`, `VSlider.tsx`, `panels/MatrixPanel.tsx` + new `hooks/useModsByDest.ts`):** add `useModsByDest(dest)` returning active routes WITH absolute `slotIndex`. Repoint rings to carry `slotNum` and call `updateSlot(slotNum,{amt})/clearSlot(slotNum)`; `onDrop`→`addRoute(src,dest)`; key matrix rows by `slotIndex`. `ModSourceChip/LfoPanel/EnvPanel` presentational, mostly unchanged. (Fixes the latent filter-local-index targeting bug.)

### Build & tests
24. **CMake:** add `source/ui/Modulation.cpp` to the `FableSynth` plugin target AND `plugin_host_test`. (No `ModMatrix` file.)
25. **`engine_test`:** add a section using TWO fresh `Engine` instances — assert a slot >4 (e.g. `mat5` → PITCH, high amt) produces an audible/deterministic diff vs. off, and that 3 routes to one dest accumulate while staying bounded/finite.
26. **`plugin_host_test`:** set `mat5.src/dst/amt` via APVTS (`convertTo0to1`), render a few blocks, snapshot a PNG, assert no crash.
27. **Verify matrix:** `cmake -S juce -B juce/build -DCMAKE_BUILD_TYPE=Release`; build + run `engine_test`; build + run `plugin_host_test`; full plugin build; `npx tsc --noEmit`; `npm run build`. Confirm param count = baseline + 36 and factory presets load (mat1..4 audible, 5..16 off).
