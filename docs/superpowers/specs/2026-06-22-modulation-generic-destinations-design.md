# Generic per-voice modulation destinations (web + VST)

Date: 2026-06-22
Status: approved, implementing
Builds on: 2026-06-22-modulation-drag-assign-design.md (the 16-slot drag-to-assign feature)

## Goal

Today only 7 controls (+3 globals) are modulation destinations. Make (almost) any
continuous **synthesis** knob a destination via a uniform curve-aware modulation
layer, so dragging a source works on nearly any knob. Keep the existing 10
destinations bit-identical so factory presets sound unchanged. FX, master volume,
env ADSR and LFO params are deferred (same mechanism, a later pass).

## Scaling that MUST be reproduced (from the current engine)

- POS / LEVEL / RES: applied as native `value += x` where `x = Σ(src·amt)` over routes.
- CUTOFF: applied as `fc × 2^(x·5)` (5 octaves at full), inside the existing
  `p[CUTOFF] · 2^(env·4·e2 + key·(note-60)/12 + …)` formula.
- PITCH: `±12 semitones` (x·12), added to both oscs + sub. Global.
- AMP: `ampFactor = clamp(1 + x, 0, 2)` on voice gain. Global.
- PAN: native `+= x` on BOTH oscs' pan. Global.

## Generic curve rules (when building the modulated param value)

For a route sum `x = Σ(src·amt)` targeting param P with ParamInfo {curve, lo, hi}:
- **Lin Float:** `pm[P] = p[P] + x·(hi − lo)`  → reproduces POS/LEVEL/RES (width-1) exactly.
- **Log Float:** `pm[P] = p[P] · 2^(x·D)`, **D = 5** → reproduces CUTOFF exactly.
- **Int / Enum / Bool:** not modulatable (excluded from destinations; `pm[P] = p[P]`).

Clamping stays at the existing use sites (pos→[0,1], level→[0,1.2], res→[0,0.999],
cutoff→[20, sr·0.45], pan→[-1,1]). `pm` itself is unclamped.

## Implementation contract (locked — single source of truth)

### Destinations (append-only; existing indices keep their exact meaning)
1. `MOD_DESTS` (Params.h + params.ts), index → label → target:
   - 0 `—` → none
   - 1 `A POS` → oscA.pos · 2 `B POS` → oscB.pos · 3 `F1 CUT` → filter.cutoff
   - 4 `PITCH` → GLOBAL · 5 `AMP` → GLOBAL · 6 `PAN` → GLOBAL
   - 7 `A LVL` → oscA.level · 8 `B LVL` → oscB.level
   - 9 `F2 CUT` → filter2.cutoff · 10 `F2 RES` → filter2.res
   - **new:** 11 `A DETUNE`→oscA.detune · 12 `A SPREAD`→oscA.spread · 13 `A PAN`→oscA.pan ·
     14 `B DETUNE`→oscB.detune · 15 `B SPREAD`→oscB.spread · 16 `B PAN`→oscB.pan ·
     17 `F1 RES`→filter.res · 18 `F1 DRIVE`→filter.drive · 19 `F1 ENV`→filter.env · 20 `F1 KEY`→filter.key ·
     21 `F2 DRIVE`→filter2.drive · 22 `F2 ENV`→filter2.env · 23 `F2 KEY`→filter2.key ·
     24 `SUB LVL`→sub.level · 25 `NOISE LVL`→noise.level
2. **`dstTarget(dst)`** — the canonical dst→target map, defined once per language:
   - C++: in Params.h (juce-free), `inline int dstTarget(int dst)` returning the flat
     Pid (e.g. `OSCA_BASE + OSC_DETUNE`) for per-param dests, or sentinels
     `DST_PITCH/DST_AMP/DST_PAN/DST_NONE` (negative) for globals/none.
   - TS: in params.ts, `dstTarget(dst)` returning the paramId string or a global token.
3. `DEST_OF_CONTROL` (C++, used to set each control's modDest) / `DEST_OF_PARAM`
   (web) get an entry for every new continuous control (the reverse of dstTarget).

### Engine (Engine.cpp)
4. Replace the per-dest `switch` in the slot loop with `dstTarget(dst)`: if a global
   sentinel, accumulate into `mPitch`/`mAmp`/`mPan` (unchanged math); else
   `modAccum[targetPid] += x`. Keep `if (!src || !dst) continue`.
5. After the loop build a per-voice `double pm_[NUM_PARAMS]`: `memcpy(pm_, p_, …)`,
   then for each modulatable param P with `modAccum[P] != 0` apply the curve rule
   (§ "Generic curve rules") using `paramInfo()[P]` {curve, min, max}, D=5 for Log.
6. `setupOsc`/`setupFilter` take the modulated array (read `pm_` for pos, level, pan,
   detune, spread, cutoff, res, env, key); the sub block reads `pm_[SUB_LEVEL]`, the
   noise block `pm_[NOISE_LEVEL]`, the drive read (`dr1/dr2`, ~line 540) reads `pm_`.
   DROP the `mPos/mLvl/mCut/mRes2/mCut2` offset params (now folded into `pm_`); KEEP
   `mPitch`/`mAmp`/`mPan`. Reading `pm_` for non-modulated fields is safe (pm_==p_ there).
7. `pm_` is a member buffer (reused per voice/block — no per-call allocation). Engine.h
   comment unchanged ("16-slot mod matrix").

### Web (worklet.js + params.ts)
8. worklet mirrors the engine: after sampling sources, accumulate globals + per-param
   offsets via `dstTarget`, compute the modulated value per targeted paramId with the
   SAME Lin/Log rules (D=5), and read the modulated value at the synthesis sites
   (osc pos/level/pan/detune/spread, filter cutoff/res/drive/env/key, sub/noise level).
   Keep the global pitch/amp/pan handling identical. Sound must stay identical for the
   existing dests.

### UI
9. VST: pass `modDest` at EVERY new continuous knob construction site in Panels.cpp
   (osc detune/spread/pan, filter res/drive/env/key for both filters, sub level, noise
   level). The MatrixPanel dst ComboBox is built from `MOD_DESTS` so it auto-grows;
   keep it FLAT in index order (grouping needs a custom attachment — out of scope v1).
10. Web: set the new controls' `dest` from `DEST_OF_PARAM` so drag-assign + rings work.
    Optional `optgroup` in the matrix dst select is allowed (web maps by value), but not
    required for v1.

### Compatibility
11. dsts 1–10 unchanged; new dsts appended; web/VST indices identical → presets stay
    cross-compatible and factory presets sound identical. `defaultParams` unaffected
    (mat slots already default to dst 0).

### Tests
12. engine_test: assert a NEW dst modulates (e.g. filter.res via mat5, two fresh
    engines, deterministic diff) and that an existing dst still matches its documented
    rule (cutoff `×2^(x·5)`): set a known route and check the effective cutoff.
13. web vitest: `dstTarget` parity table (index→target identical to the C++ list) and
    the Lin/Log curve-rule helpers.
14. plugin_host_test: assign a new-dest route via APVTS, render, snapshot, assert no crash.

## Risks
- Reproducing existing-dest sound: mitigated by rules proven equal to the current code
  and a cutoff-rule assertion test.
- Missing a `p_`→`pm_` read site in the render path → that knob silently won't modulate;
  caught by the review pass enumerating every synthesis read site.
- Dropdown length (26 items) — acceptable v1; grouping deferred.
