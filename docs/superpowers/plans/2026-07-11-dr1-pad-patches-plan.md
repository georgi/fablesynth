# DR-1 Pad Patch System — Implementation Plan

## Goal

A per-pad patch system: save/load/switch a SINGLE pad's sound (bassdrum,
snare, hat…) quickly, independent of kits. Web app + JUCE plugin.

## Data model

A patch is one pad's sound:

```ts
interface PadPatch {
  v: 1;
  name: string;                    // e.g. "808 KICK", displayed uppercase
  params: Record<string, number>;  // PAD-RELATIVE ids -> values, overrides on defaults
}
```

- Keys are pad-relative field ids from `PAD_FIELDS` (`src/drum/params.ts`),
  e.g. `oscA.table`, `aenv.dec`, `mod1.src`.
- **Excluded fields: `out` and `choke`** — routing/kit-level behavior, never
  saved into or applied from a patch.
- Apply semantics: `defaults ∪ patch.params` for every included field — a
  patch fully resets the pad's sound (no leftover state from the previous
  sound). `out`/`choke` keep their current values.
- Factory patches author only overrides; user patches snapshot all included
  fields (minus values equal to defaults is fine but not required).

## Factory bank (shared web + JUCE, same names/order)

~18 patches, prefix = category, using the sampled 808 tables where apt:
kicks (THUD-based deep/punchy/sub + an 808-flavored one), snares (CRACK +
noise, 808SD-based, rim-ish), clap (808CP), hats (808CH closed, 808OH open,
tighter CH variant), cymbal (808CY), toms lo/mid/hi (THUD tuned), perc/vox/
glitch flavors from existing tables. Author for musical quality: sensible
aenv decays, penv for kicks, noise layer for snares/claps, filter on hats.
Table indices: DRUM_TABLE_NAMES order (0..14; 10..14 = 808SD CP CH OH CY).
Factory patches must reference ONLY built-in tables (index ≤ 14).

## Web

- `src/drum/patches.ts`: `PadPatch`, `FACTORY_PATCHES`, `applyPatchToParams
  (params, padI, patch)` (returns new id->value entries for pad padI),
  `extractPatch(params, padI, name)`, localStorage user patches
  (`fable-dr-patches`, same read/write resilience pattern as kits.ts),
  `patchOptions(userPatches)` (factory then user, value keys `f0…`/`u0…`
  like kitOptions).
- Store (`src/drum/store.ts`): `patchValue: string`, `userPatches:
  PadPatch[]`, `applyPatchByValue(value)`, `stepPatch(delta)`,
  `savePatch(name)`. Applying sets each param via the existing `setParam`
  (engine + state stay in sync). `selectPad` resets `patchValue` to '' (a
  pad edit/selection isn't a known patch). Saving uses the CURRENT selected
  pad.
- UI (`src/drum/components/SelBar.tsx`): replace the static hint with a
  patch stepper: `◂ [PATCH NAME] ▸` + SAVE button. Stepper cycles factory+
  user patches applying to the selected pad. SAVE prompts for a name
  (window.prompt, same as kit save flow if that's what Header does — check
  and mirror). Empty patchValue shows "—". Match existing dr-* CSS idiom
  (drum.css); keep the bar single-line.

## JUCE

- `juce/source/drum/dsp/DrumPatches.h/.cpp` (JUCE-free, mirrors DrumKits):
  `struct PadPatch { std::string name; std::vector<std::pair<std::string,
  float>> params; }` with RELATIVE ids; `const std::vector<PadPatch>&
  factoryPatches();` — SAME names/values/order as web FACTORY_PATCHES.
  `applyPatchToPad(DrumAudioProcessor-agnostic)`: given pad index, produce
  absolute (pid, value) pairs for all included fields (defaults ∪
  overrides, excluding out/choke).
- Processor/editor: apply via `apvts` params `setValueNotifyingHost
  (convertTo0to1(...))` — same path `setCurrentProgram` uses
  (`DrumProcessor.cpp:221-236`). Wrap in begin/endChangeGesture? Mirror
  what setCurrentProgram does (it doesn't; fine).
- UI: in the drum editor near the pad name/selection area, a patch stepper
  (prev/next + name readout) styled like the kit stepper in `DrumHeader.cpp`
  (~line 196). Factory bank only on JUCE (host state persists user tweaks;
  user-patch persistence is web-only for now).
- Tests: extend `juce/test/drum_engine_test.cpp`: factoryPatches() count
  matches web; spot-check 3 patches' key values equal the web values
  (hardcode expected numbers from patches.ts, like the existing kit
  parity checks); applying a patch to pad 3 sets `pad3.*` values and leaves
  `pad3.out`/`pad3.choke` and other pads untouched.

## Global constraints

- Patch params use RELATIVE ids; `out` + `choke` excluded everywhere.
- Factory banks byte-equal across engines (names, order, values).
- No engine/DSP changes; patches ride entirely on existing setParam/APVTS.
- No changes to kits format, param defs, or table list.
- Web: `npx vitest run` green + `npx tsc --noEmit` clean.
- JUCE: `drum_engine_test` builds+passes; `FableDrum_VST3` target builds.
