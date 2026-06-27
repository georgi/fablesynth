# Unison: 16 voices + BLEND control — design

Date: 2026-06-28

## Summary

Extend the existing per-oscillator unison so it matches a Serum-style control
group: **UNISON (stepper, 1–16) · DETUNE · BLEND**. Today each oscillator
already has unison (count 1–7), DETUNE, SPREAD, random per-voice phase and
`1/√uni` gain compensation. This change:

1. Raises the unison voice cap **7 → 16**.
2. Adds a new per-oscillator **BLEND** knob (center-vs-detuned voice level).
3. Renders **UNISON as a numeric stepper** instead of a knob.
4. Keeps the existing **SPREAD** (stereo width) — BLEND and SPREAD are different
   axes (level balance vs. stereo placement).

Out of scope: detune warp/curve (DETUNE stays linear). No new DSP modes beyond
blend weighting.

The JUCE plugin (`juce/source`) and the web/worklet engine (`src`) are
structural twins and are changed in lockstep.

## BLEND semantics

Unison voices are positioned `sprd_u = u/(uni-1)·2 − 1 ∈ [−1, 1]` for `uni > 1`
(a single voice sits at 0). BLEND `b ∈ [0, 1]` scales how loud the outer
(most-detuned) voices are relative to the center:

```
weight_u = 1 − (1 − b)·|sprd_u|
```

- **b = 1 (100%)** → all weights = 1 → all voices equal. Identical to today.
- **b = 0** → linear taper; center voice at full, outermost silent → the
  detune/chorus effect collapses to essentially the center voice.

The weight folds into the existing per-voice pan gains
(`gl[u] *= weight_u`, `gr[u] *= weight_u`). Loudness is held roughly constant
across blend settings by normalizing on the actual weights instead of the voice
count:

```
gain = level * 0.32 / sqrt( Σ weight_u² )
```

When `b = 1`, `Σ weight_u² = uni`, so `gain = level*0.32/√uni` — **bit-identical
to the current code path**. This is the regression guarantee for existing
presets.

Chosen over a "hard center pick" (only the exact center voice at b=0) because the
linear taper is smooth and handles even voice counts gracefully (no single
center voice exists for even counts).

## Backward compatibility

- **BLEND default = 1.0 (100%)** → every existing preset and saved DAW project
  sounds identical, because full-equal unison is exactly today's behavior.
- Parameters are addressed by **string ID** (`oscA.blend`, `oscB.blend`), so old
  saved projects simply lack the param and fall back to the default.
- **BLEND is a mod destination, appended** as dest **26 (oscA) / 27 (oscB)** —
  never inserted. The existing destination wire format (1–25) and any saved mod
  routes remain valid.
- Raising MAXUNI to 16 only widens the unison range; saved counts (≤7) are
  unaffected.

## Component changes

### DSP / params (mirror JUCE ↔ web)

**JUCE**
- `dsp/Params.h`
  - Add `OSC_BLEND` to `enum OscField` (before `OSC_NFIELDS`). This shifts the
    flat `Pid` block bases automatically — fine, since runtime state is keyed by
    string ID, not flat index.
  - Append `dstTarget` cases `26 → OSCA_BASE+OSC_BLEND`, `27 → OSCB_BASE+OSC_BLEND`.
  - Append `"A BLEND"`, `"B BLEND"` to `paramDestNames`.
- `dsp/Params.cpp`
  - Unison range max `7 → 16`.
  - Register `oscX.blend`: range 0–1, def 1.0, `Curve::Lin`, `Kind::Float`.
- `dsp/Engine.h` — `MAXUNI 7 → 16`.
- `dsp/Engine.cpp` `setupOsc` — read `pm[base+OSC_BLEND]`, compute per-voice
  `weight_u`, fold into `gl/gr`, normalize gain on `Σ weight_u²`. The note-on
  phase-init loop already iterates `MAXUNI`, so new voices are covered.

**Web**
- `params.ts` — unison max `7 → 16`; add `oscX.blend` def 1.0; append
  `getParamId` cases 26/27; add the two dest names to the destination-name list.
- `engine/worklet.js` — `MAXUNI 7 → 16`; same blend weighting + normalization in
  the osc setup; uses the same `weight_u` formula.
- `params.test.ts` — assert blend param exists, range 0–1, def 1.0, dest 26/27.

### UI — UNISON as a numeric stepper

Both Steppers are currently **choice/enum-based** (iterate `def.options`).
Generalize each to support an **integer-range mode** driven by the param's
numeric range, stepping by 1 with **clamping** (not wrapping) at [min, max].

**JUCE** `ui/Controls.{h,cpp}` `Stepper`
- When the param is not an `AudioParameterChoice`, cast to
  `RangedAudioParameter` and drive from its `NormalisableRange` (start/end/
  interval). `displayName()` returns the integer value; `step()` adds ±1 clamped;
  `timerCallback()` tracks the float value. Reuse the existing `nameProvider`
  hook only if useful.

**Web** `components/Stepper.tsx`
- Numeric mode when `def.curve === 'int'` and the param has no `options`: render
  the integer value, step within `[def.min, def.max]` clamped.

### UI — osc panel layout

**JUCE** `ui/Panels.cpp` `OscPanel`
- UNISON leaves the knob row and becomes a `Stepper` member, positioned in the
  osc panel (sized like the existing sub-shape / table steppers).
- Remaining knob row: oct, semi, fine, **detune, spread, blend**, level, pan.
- BLEND knob wired as a mod target (dest 26/27, per osc), matching how DETUNE
  (11/14) and SPREAD (12/15) are wired.

**Web** `components/panels/OscPanel.tsx`
- Replace the unison `<Knob>` with `<Stepper paramId={prefix+'.unison'} />`.
- Add `<Knob paramId={prefix+'.blend'} />` after spread.

## Performance note

Worst case rises from 7 to 16 sub-voices per oscillator → up to **256
oscillator voices** (2 osc × 8-voice poly × 16 unison) in the inner render loop.
This is user-selectable at the extreme end; documented, not gated.

## Testing

- **Engine regression (JUCE `test/engine_test.cpp`):** with BLEND=1 and uni>1,
  rendered output matches the pre-change unison path (the math is bit-identical).
- **Blend extremes:** BLEND=0 with uni>1 produces ≈ single-voice output;
  loudness stays roughly constant across a blend sweep (RMS within tolerance).
- **16-voice render:** does not alias (reuse the existing spectrum/mip check at a
  high note with max detune).
- **Params (web `params.test.ts`):** blend present, range/default correct, dest
  mapping 26/27 wired; unison max is 16.

## Files touched

JUCE: `dsp/Params.h`, `dsp/Params.cpp`, `dsp/Engine.h`, `dsp/Engine.cpp`,
`ui/Controls.h`, `ui/Controls.cpp`, `ui/Panels.cpp` (and `ui/Panels.h` for the
new Stepper member), `test/engine_test.cpp`.

Web: `params.ts`, `engine/worklet.js`, `components/Stepper.tsx`,
`components/panels/OscPanel.tsx`, `params.test.ts`.
