# Unison 16 + BLEND Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a per-oscillator BLEND control (center-vs-detuned voice level) and raise the unison voice cap from 7 to 16, with UNISON rendered as a numeric stepper, mirrored across the JUCE plugin and the web/worklet engine.

**Architecture:** Each oscillator keeps DETUNE and SPREAD and gains BLEND. In the unison loop each voice gets `weight = 1 - (1-blend)·|sprd|`; weights fold into the equal-power pan gains and the per-osc gain is normalized on `√(Σ weight²)` instead of `√uni`. At `blend=1` every weight is 1, `Σweight² = uni`, so output is bit-identical to today — the backward-compat guarantee, since BLEND defaults to 1.0. BLEND is appended as mod destinations 26 (oscA) / 27 (oscB); state is string-ID-keyed so existing presets/projects are unaffected.

**Tech Stack:** C++17 / JUCE 8 (CMake) for the plugin; TypeScript + an AudioWorklet (`worklet.js`) + Vitest for the web app. JUCE DSP tests use a custom `check()` harness in `engine_test.cpp`.

## Global Constraints

- **BLEND param:** id `oscX.blend`, label `BLEND`, range `0..1`, default `1.0`, curve `lin`/`Curve::Lin`, kind `Float`. Default MUST be 1.0 (reproduces today's equal-voice unison = preset/project back-compat).
- **Unison range:** `1..16` (was `1..7`). `MAXUNI = 16` in both engines.
- **Blend math (identical in both engines):** `weight = 1 - (1-blend)·|sprd|`; accumulate `sumW2 += weight²`; per-voice `gl/gr *= weight`; `gain = level*0.32 / sqrt(GUARD)`.
- **Divide-by-zero guard (lockstep-critical):** C++ `sumW2 > 0.0 ? sumW2 : 1.0`; JS `sumW2 || 1`. These swap to 1 ONLY at exactly 0, so they agree at every other value (do NOT use `std::max(sumW2, 1.0)` — it diverges from JS at uni=4/blend=0).
- **Mod destinations are append-only:** add 26/27 at the tail of every dest list; never insert into 1–25 (saved routes store the raw index). MOD_DESTS itself must grow (not just `dstTarget`), or `addRoute`→`convertTo0to1` clamps a `dst=26` route down to 25 and silently mis-routes.
- **JUCE ↔ web parity:** dest names, dest indices, and the blend def/unison max must match between `juce/source/dsp/Params.h` and `src/params.ts` / `src/engine/worklet.js`.
- **Known design corner (document, don't fix):** uni=2 + blend=0 is near-silent (both voices are endpoints, weight 0). The guard keeps it finite; the silence is acceptable for this release.

## Parallelization map (for workflow execution)

Two fully independent tracks; run them concurrently. Within each track, tasks are sequential (each keeps the build green).

- **JUCE track:** Task 1 → Task 2 → Task 3 → Task 4
- **Web track:** Task 5 → Task 6 → Task 7

Task 1 and Task 5 have no dependency on each other; a worker may start both at once. UI tasks (4, 7) depend on their param/engine predecessors landing first.

---

## Task 1: JUCE param foundation (BLEND param, unison max, mod dests 26/27)

**Files:**
- Modify: `juce/source/dsp/Params.h` (enum `OscField`, `MOD_DESTS`, `dstTarget`)
- Modify: `juce/source/dsp/Params.cpp` (`addOsc` unison max + blend registration)

**Interfaces:**
- Produces: `OSC_BLEND` (enum `OscField`, appended before `OSC_NFIELDS`); APVTS params `oscA.blend` / `oscB.blend`; `dstTarget(26)→OSCA_BASE+OSC_BLEND`, `dstTarget(27)→OSCB_BASE+OSC_BLEND`; `MOD_DESTS[26]="A BLEND"`, `MOD_DESTS[27]="B BLEND"`. Consumed by Tasks 2 (engine reads `pm[base+OSC_BLEND]`) and 4 (knob modDest 26/27).

- [ ] **Step 1: Append `OSC_BLEND` to `enum OscField`**

In `juce/source/dsp/Params.h`, replace:

```cpp
enum OscField  { OSC_ON, OSC_TABLE, OSC_POS, OSC_OCT, OSC_SEMI, OSC_FINE,
                 OSC_UNISON, OSC_DETUNE, OSC_SPREAD, OSC_LEVEL, OSC_PAN, OSC_NFIELDS };
```

with:

```cpp
enum OscField  { OSC_ON, OSC_TABLE, OSC_POS, OSC_OCT, OSC_SEMI, OSC_FINE,
                 OSC_UNISON, OSC_DETUNE, OSC_SPREAD, OSC_LEVEL, OSC_PAN, OSC_BLEND, OSC_NFIELDS };
```

`OSC_BLEND` is placed AFTER `OSC_PAN` and BEFORE `OSC_NFIELDS` so the existing field indices (UNISON/DETUNE/SPREAD/LEVEL/PAN) are unchanged; `OSC_NFIELDS` grows 11→12 and the flat `Pid` block bases recompute automatically.

- [ ] **Step 2: Append `dstTarget` cases 26/27**

In `juce/source/dsp/Params.h`, replace:

```cpp
        case 24: return SUB_LEVEL;
        case 25: return NOISE_LEVEL;
        default: return DST_NONE;
```

with:

```cpp
        case 24: return SUB_LEVEL;
        case 25: return NOISE_LEVEL;
        case 26: return OSCA_BASE   + OSC_BLEND;
        case 27: return OSCB_BASE   + OSC_BLEND;
        default: return DST_NONE;
```

- [ ] **Step 3: Append `MOD_DESTS` labels (the dest-name list — note: the symbol is `MOD_DESTS`, there is no `paramDestNames`)**

In `juce/source/dsp/Params.h`, replace:

```cpp
    "SUB LVL", "NOISE LVL"};
```

with:

```cpp
    "SUB LVL", "NOISE LVL", "A BLEND", "B BLEND"};
```

This grows the matrix dropdown and the `mat.dst` choice range automatically (both derive from `MOD_DESTS.size()`), so a `dst=26/27` route survives the `convertTo0to1` clamp.

- [ ] **Step 4: Raise unison max 7→16 and register `oscX.blend`**

In `juce/source/dsp/Params.cpp`, `addOsc()`, replace the unison line:

```cpp
    v.push_back({base + OSC_UNISON, pre + ".unison", "UNI",    1, 7, 1,        Curve::Int, Kind::Float, nullptr});
```

with:

```cpp
    v.push_back({base + OSC_UNISON, pre + ".unison", "UNI",    1, 16, 1,       Curve::Int, Kind::Float, nullptr});
```

Then insert the blend descriptor between spread and level — replace:

```cpp
    v.push_back({base + OSC_SPREAD, pre + ".spread", "SPREAD", 0, 1, 0.6f,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + OSC_LEVEL,  pre + ".level",  "LEVEL",  0, 1, 0.75f,    Curve::Lin, Kind::Float, nullptr});
```

with:

```cpp
    v.push_back({base + OSC_SPREAD, pre + ".spread", "SPREAD", 0, 1, 0.6f,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + OSC_BLEND,  pre + ".blend",  "BLEND",  0, 1, 1.0f,     Curve::Lin, Kind::Float, nullptr});
    v.push_back({base + OSC_LEVEL,  pre + ".level",  "LEVEL",  0, 1, 0.75f,    Curve::Lin, Kind::Float, nullptr});
```

`addOsc` runs once for each of `oscA`/`oscB`, so this single line registers both `oscA.blend` and `oscB.blend`. Descriptors are placed by `info.id` (`out[info.id] = info`), so push order doesn't matter — only the correct `base + OSC_BLEND` id does.

- [ ] **Step 5: Build to verify it compiles and runs**

Run: `cmake --build juce/build --parallel`
Expected: builds clean. (`Engine.cpp` does not yet read `OSC_BLEND` — the param exists but is inert; this is fine and the plugin runs.)

- [ ] **Step 6: Commit**

```bash
git add juce/source/dsp/Params.h juce/source/dsp/Params.cpp
git commit -m "feat(juce): register per-osc BLEND param, unison max 16, mod dests 26/27"
```

---

## Task 2: JUCE engine BLEND math + MAXUNI 16 (TDD)

**Files:**
- Test: `juce/test/engine_test.cpp` (append section 12 in `main()`)
- Modify: `juce/source/dsp/Engine.h` (`MAXUNI`)
- Modify: `juce/source/dsp/Engine.cpp` (`setupOsc`)

**Interfaces:**
- Consumes: `OSC_BLEND` from Task 1; existing harness helpers `check`, `finite`, `peak`, `renderNote`, `aliasFloorDb`, and the param API `auto p = defaultParams(); p[BASE+FIELD]=…; eng.setParams(p);`.
- Produces: audible BLEND behavior; the regression/guard guarantees other tasks rely on.

- [ ] **Step 1: Write the failing tests**

In `juce/test/engine_test.cpp`, replace the final summary block:

```cpp
    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : (std::to_string(g_fail) + " CHECK(S) FAILED").c_str());
    return g_fail == 0 ? 0 : 1;
}
```

with:

```cpp
    printf("\n== 12. Unison BLEND & 16-voice ==\n");
    {
        check(defaultParams()[OSCA_BASE + OSC_BLEND] == 1.0f,
              "oscA.blend defaults to 1.0 (preset back-compat)");

        // Render oscA only (bare oscillators, no filter), fast attack + full
        // sustain, and return stereo buffers. Uses the verified harness param
        // API: build a ParamArray, then eng.setParams(p).
        auto renderUni = [&](int uni, float blend, float detune, float spread,
                             int note, std::vector<float>& L, std::vector<float>& R) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto p = defaultParams();
            p[OSCA_BASE + OSC_ON]     = 1;
            p[OSCA_BASE + OSC_UNISON] = (float)uni;
            p[OSCA_BASE + OSC_BLEND]  = blend;
            p[OSCA_BASE + OSC_DETUNE] = detune;
            p[OSCA_BASE + OSC_SPREAD] = spread;
            p[OSCA_BASE + OSC_PAN]    = 0;
            p[OSCA_BASE + OSC_LEVEL]  = 0.8f;
            p[OSCB_BASE + OSC_ON]     = 0;                 // oscA only
            p[FILTER1_BASE + FLT_ON]  = 0;                 // bare oscillators
            p[ENV1_BASE + 0] = 0.001f; p[ENV1_BASE + 2] = 1.0f; // fast attack, full sustain
            e.setParams(p);
            e.noteOn(note, 1.0);
            int n = (int)(0.5 * sr);
            L.assign(n, 0.0f); R.assign(n, 0.0f);
            e.render(L.data(), R.data(), n);
        };
        // Mean stereo power (L^2 + R^2) over the steady-state tail (skip attack).
        auto totalPower = [&](const std::vector<float>& L, const std::vector<float>& R) {
            int start = (int)(0.1 * sr); double s = 0; int cnt = 0;
            for (int i = start; i < (int)L.size(); i++) {
                s += (double)L[i] * L[i] + (double)R[i] * R[i]; cnt++;
            }
            return cnt ? s / cnt : 0.0;
        };

        std::vector<float> L1, R1, L0, R0, Lu1, Ru1;

        // (1) Regression: at uni=1 the single voice sits at sprd=0, so its weight
        // is 1-(1-b)*0 = 1 for ANY blend -> blend is a perfect no-op. Fresh engines
        // share a deterministic RNG seed (Engine.h: s = 0x9e3779b9u), so the two
        // renders are sample-for-sample identical. This proves the new weight/
        // normalization reduces exactly to the legacy single-voice path.
        renderUni(1, 1.0f, 0.3f, 0.5f, 69, L1, R1);
        renderUni(1, 0.0f, 0.3f, 0.5f, 69, L0, R0);
        bool uni1Identical = L1.size() == L0.size();
        for (size_t i = 0; uni1Identical && i < L1.size(); i++)
            if (L1[i] != L0[i] || R1[i] != R0[i]) uni1Identical = false;
        check(uni1Identical,
              "blend is a sample-exact no-op at uni=1 (single voice at sprd=0, weight=1)");

        // (2) blend=0, uni=4 ~= single voice loudness. Equal-power panning
        // (gl^2+gr^2=1) + normalization on sqrt(Sum w^2) make total stereo power
        // independent of uni/blend. Assert within 3 dB.
        renderUni(4, 0.0f, 0.4f, 0.6f, 69, L0, R0);
        renderUni(1, 1.0f, 0.4f, 0.6f, 69, Lu1, Ru1);
        double pBlend0 = totalPower(L0, R0), pUni1 = totalPower(Lu1, Ru1);
        double collapseDb = 10.0 * std::log10(pBlend0 / pUni1);
        check(std::abs(collapseDb) < 3.0,
              "blend=0 uni=4 ~= single voice (uni=1) loudness (within 3 dB)",
              "delta=" + std::to_string(collapseDb) + " dB");

        // (3) Loudness ~constant across the blend sweep at uni=4. Assert within 6 dB.
        renderUni(4, 1.0f, 0.4f, 0.6f, 69, L1, R1);
        double pBlend1 = totalPower(L1, R1);
        double sweepDb = 10.0 * std::log10(pBlend0 / pBlend1);
        check(std::abs(sweepDb) < 6.0,
              "loudness ~constant across blend at uni=4 (total power within 6 dB)",
              "delta=" + std::to_string(sweepDb) + " dB");

        // (4) uni=2, blend=0 degenerate: both voices are endpoints (|sprd|=1 ->
        // weight 0 -> sumW2=0). The divide-by-zero guard must keep output finite.
        // (Near-silent by design; we only assert it never blows up to NaN/Inf.)
        {
            std::vector<float> L, R;
            renderUni(2, 0.0f, 0.4f, 0.6f, 69, L, R);
            check(finite(L) && finite(R), "uni=2 blend=0 stays finite (sumW2=0 guard)");
        }

        // (5) 16-voice render must not alias. aliasFloorDb scores any energy off
        // the exact-harmonic comb as "alias", so detune (which legitimately places
        // partials between harmonics) would false-fail. Use detune=0 to isolate the
        // band-limited mip path for 16 summed voices; reuse the -55 dB threshold.
        {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto p = defaultParams();
            p[OSCA_BASE + OSC_POS]    = 0.66f;            // saw-rich frame
            p[OSCA_BASE + OSC_UNISON] = 16;               // worst-case voice count
            p[OSCA_BASE + OSC_BLEND]  = 1.0f;             // all 16 voices at full level
            p[OSCA_BASE + OSC_DETUNE] = 0.0f;             // isolate aliasing from detune sidebands
            p[OSCA_BASE + OSC_SPREAD] = 0.0f;
            p[OSCA_BASE + OSC_PAN]    = 0;
            p[FILTER1_BASE + FLT_ON]  = 0;
            p[ENV1_BASE + 0] = 0.001f; p[ENV1_BASE + 2] = 1.0f;
            e.setParams(p);
            for (int note : {96, 103, 108}) {             // C7, G7, C8
                double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
                auto buf = renderNote(e, note, 0.4, sr);
                int N = 16384;
                double db = aliasFloorDb(buf.data() + (buf.size() - N), N, sr, f0);
                check(db < -55.0, "16-voice alias floor low @ note " + std::to_string(note),
                      std::to_string(db) + " dB");
                e.panic();
                std::vector<float> flush((size_t)(sr * 0.2), 0);
                e.render(flush.data(), flush.data(), 0);
            }
        }

        // (6) 16 voices at MAX detune (dense-cluster worst case): the alias metric
        // is not meaningful (detune sidebands count as inharmonic), so only assert
        // the render stays finite and bounded.
        {
            std::vector<float> L, R;
            renderUni(16, 1.0f, 1.0f, 1.0f, 103, L, R);   // G7, max detune+spread
            check(finite(L) && finite(R) && peak(L) < 4.0f && peak(R) < 4.0f,
                  "16-voice max-detune render finite/bounded",
                  "peakL=" + std::to_string(peak(L)));
        }
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : (std::to_string(g_fail) + " CHECK(S) FAILED").c_str());
    return g_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Build and run to verify check (2) fails**

Run: `cmake --build juce/build --parallel && juce/build/engine_test`
Expected: section 12 prints; `[FAIL] blend=0 uni=4 ~= single voice (uni=1) loudness` (blend is still ignored, so uni=4 is louder/different than a single voice). Other checks may pass; final line shows ≥1 FAILED.

- [ ] **Step 3: Raise `MAXUNI` to 16**

In `juce/source/dsp/Engine.h`, replace:

```cpp
constexpr int MAXUNI   = 7;
```

with:

```cpp
constexpr int MAXUNI   = 16;
```

This widens the `OscState` arrays (`phases`/`incs`/`gl`/`gr[MAXUNI]`) and the note-on phase-init loop (which already iterates `MAXUNI`) automatically.

- [ ] **Step 4: Implement the blend weighting in `setupOsc`**

In `juce/source/dsp/Engine.cpp`, first read blend alongside detune/spread — replace:

```cpp
    double det = pm[base + OSC_DETUNE];
    double spr = pm[base + OSC_SPREAD];
```

with:

```cpp
    double det = pm[base + OSC_DETUNE];
    double spr = pm[base + OSC_SPREAD];
    double blend = std::min(1.0, std::max(0.0, pm[base + OSC_BLEND]));
```

Then replace the gain + unison loop:

```cpp
    o.gain = (level * 0.32) / std::sqrt((double)uni);

    for (int u = 0; u < uni; u++) {
        double sprd = uni > 1 ? (double)u / (uni - 1) * 2 - 1 : 0;
        double cents = sprd * det * 50;
        double ratio = std::pow(2.0, cents / 1200);
        o.incs[u] = cps * ratio * table.size;
        double pan = std::max(-1.0, std::min(1.0, sprd * spr + basePan));
        double a = ((pan + 1) * PI) / 4;
        o.gl[u] = (float)std::cos(a);
        o.gr[u] = (float)std::sin(a);
    }
```

with:

```cpp
    double sumW2 = 0;
    for (int u = 0; u < uni; u++) {
        double sprd = uni > 1 ? (double)u / (uni - 1) * 2 - 1 : 0;
        double cents = sprd * det * 50;
        double ratio = std::pow(2.0, cents / 1200);
        o.incs[u] = cps * ratio * table.size;
        // BLEND: outer (most-detuned) voices fade relative to the center.
        // blend==1 -> weight==1 for all voices -> sumW2==uni -> gain identical
        // to the legacy /sqrt(uni) path (preset back-compat).
        double weight = 1 - (1 - blend) * std::fabs(sprd);
        sumW2 += weight * weight;
        double pan = std::max(-1.0, std::min(1.0, sprd * spr + basePan));
        double a = ((pan + 1) * PI) / 4;
        o.gl[u] = (float)(std::cos(a) * weight);
        o.gr[u] = (float)(std::sin(a) * weight);
    }
    // Guard sumW2==0 (uni=2, blend=0: both voices at |sprd|=1 -> weight 0).
    // Predicate matches the web's `sumW2 || 1` exactly (swaps to 1 only at 0),
    // so the two engines stay in lockstep at low blend.
    o.gain = (level * 0.32) / std::sqrt(sumW2 > 0.0 ? sumW2 : 1.0);
```

Note: `o.gain` moves to AFTER the loop because it now depends on `sumW2`. Leave the `o.uni = uni;` line above this block unchanged.

- [ ] **Step 5: Build and run to verify all checks pass**

Run: `cmake --build juce/build --parallel && juce/build/engine_test`
Expected: all section-12 checks PASS; final line `ALL CHECKS PASSED` (exit 0). Existing sections 1–11 stay green.

- [ ] **Step 6: Commit**

```bash
git add juce/source/dsp/Engine.h juce/source/dsp/Engine.cpp juce/test/engine_test.cpp
git commit -m "feat(juce): BLEND unison weighting + MAXUNI 16, with regression/guard tests"
```

---

## Task 3: JUCE Stepper integer-range mode

**Files:**
- Modify: `juce/source/ui/Controls.h` (add `ranged` + `lastValue` members)
- Modify: `juce/source/ui/Controls.cpp` (`Stepper` ctor / `displayName` / `timerCallback` / `step`)

**Interfaces:**
- Produces: a `Stepper` that, when bound to a non-choice `RangedAudioParameter` (e.g. `oscX.unison`: `AudioParameterFloat`, range 1–16, interval 1), displays the integer value and steps ±1 with clamping. Consumed by Task 4 (`unisonStep`). Existing choice-based steppers (table, sub shape, filter type, lfo) are byte-for-byte unchanged.

- [ ] **Step 1: Add the numeric-mode members**

In `juce/source/ui/Controls.h`, in the `Stepper` private section, replace:

```cpp
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::AudioParameterChoice* choice = nullptr;
    juce::Colour accent;
    juce::TextButton prev{"<"}, next{">"};
    int lastIndex = -1;
```

with:

```cpp
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::AudioParameterChoice* choice = nullptr;
    // Set when the param is not a choice (e.g. unison: AudioParameterFloat with
    // Int curve, range 1-16). Drives an integer-range stepping mode.
    juce::RangedAudioParameter* ranged = nullptr;
    juce::Colour accent;
    juce::TextButton prev{"<"}, next{">"};
    int lastIndex = -1;
    float lastValue = -1.0f;   // normalized value cache for the numeric mode
```

- [ ] **Step 2: Resolve `ranged` only when the choice cast fails**

In `juce/source/ui/Controls.cpp`, `Stepper::Stepper`, replace:

```cpp
    choice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id));
    auto styleBtn = [this](juce::TextButton& b, int dir) {
```

with:

```cpp
    choice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id));
    if (!choice)
        ranged = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
    auto styleBtn = [this](juce::TextButton& b, int dir) {
```

Order matters: `AudioParameterChoice` IS-A `RangedAudioParameter`, so the choice cast must come first or choice params would fall into the numeric path.

- [ ] **Step 3: Numeric display**

Replace:

```cpp
juce::String Stepper::displayName() const {
    if (!choice) return "-";
    if (nameProvider) return nameProvider(choice->getIndex());
    return choice->getCurrentChoiceName();
}
```

with:

```cpp
juce::String Stepper::displayName() const {
    if (choice) {
        if (nameProvider) return nameProvider(choice->getIndex());
        return choice->getCurrentChoiceName();
    }
    if (ranged)
        return juce::String((int)std::lround(ranged->convertFrom0to1(ranged->getValue())));
    return "-";
}
```

- [ ] **Step 4: Numeric repaint tracking**

Replace:

```cpp
void Stepper::timerCallback() { if (choice && choice->getIndex() != lastIndex) { lastIndex = choice->getIndex(); repaint(); } }
```

with:

```cpp
void Stepper::timerCallback() {
    if (choice) {
        if (choice->getIndex() != lastIndex) { lastIndex = choice->getIndex(); repaint(); }
        return;
    }
    if (ranged) {
        float v = ranged->getValue();
        if (v != lastValue) { lastValue = v; repaint(); }
    }
}
```

- [ ] **Step 5: Numeric step with clamping**

Replace:

```cpp
void Stepper::step(int d) {
    if (!choice) return;
    int n = choiceCount();
    int idx = ((choice->getIndex() % n) + d + n) % n;
    choice->setValueNotifyingHost(choice->convertTo0to1((float)idx));
    repaint();
}
```

with:

```cpp
void Stepper::step(int d) {
    if (choice) {
        int n = choiceCount();
        int idx = ((choice->getIndex() % n) + d + n) % n;
        choice->setValueNotifyingHost(choice->convertTo0to1((float)idx));
        repaint();
        return;
    }
    if (ranged) {
        const auto& r = ranged->getNormalisableRange();
        float stepBy = r.interval > 0.0f ? r.interval : 1.0f;
        float cur  = ranged->convertFrom0to1(ranged->getValue());
        float next = juce::jlimit(r.start, r.end, cur + (float)d * stepBy);
        ranged->setValueNotifyingHost(ranged->convertTo0to1(next));
        repaint();
    }
}
```

Numeric mode clamps (no modulo wrap), so 16 does not roll back to 1.

- [ ] **Step 6: Build to verify it compiles**

Run: `cmake --build juce/build --parallel`
Expected: builds clean. (The numeric path isn't exercised until Task 4 binds a Stepper to `oscX.unison`; choice steppers are unaffected.)

- [ ] **Step 7: Commit**

```bash
git add juce/source/ui/Controls.h juce/source/ui/Controls.cpp
git commit -m "feat(juce): Stepper integer-range mode for numeric params"
```

---

## Task 4: JUCE OscPanel — UNISON stepper + BLEND knob

**Files:**
- Modify: `juce/source/ui/Panels.h` (add `unisonStep` member)
- Modify: `juce/source/ui/Panels.cpp` (`OscPanel` ctor + `resized`)

**Interfaces:**
- Consumes: `Stepper` numeric mode (Task 3); `oscX.blend` + mod dests 26/27 (Task 1).
- Produces: per-osc header UNISON stepper (1–16); knob row `oct, semi, fine, detune, spread, blend, level, pan` with BLEND wired to mod dest 26/27.

- [ ] **Step 1: Declare the `unisonStep` member**

In `juce/source/ui/Panels.h`, replace:

```cpp
    PowerButton power; juce::TextButton editBtn{"E"}; Stepper tableStep; WavetableView wt; VSlider pos;
```

with:

```cpp
    PowerButton power; juce::TextButton editBtn{"E"}; Stepper tableStep, unisonStep; WavetableView wt; VSlider pos;
```

`unisonStep` is declared AFTER `tableStep` to keep member-init order consistent (avoids `-Wreorder`).

- [ ] **Step 2: Construct and register `unisonStep`**

In `juce/source/ui/Panels.cpp`, in the `OscPanel` ctor initializer list, replace:

```cpp
      power(s, pre + ".on", ac), tableStep(s, pre + ".table", ac),
```

with:

```cpp
      power(s, pre + ".on", ac), tableStep(s, pre + ".table", ac), unisonStep(s, pre + ".unison", ac),
```

Then in the ctor body, replace:

```cpp
    addAndMakeVisible(power); addAndMakeVisible(tableStep);
    addAndMakeVisible(wt); addAndMakeVisible(pos);
```

with:

```cpp
    addAndMakeVisible(power); addAndMakeVisible(tableStep); addAndMakeVisible(unisonStep);
    addAndMakeVisible(wt); addAndMakeVisible(pos);
```

- [ ] **Step 3: Rebuild the knob row (drop unison, add blend, re-index mod dests)**

Replace:

```cpp
    const char* ids[] = {".oct", ".semi", ".fine", ".unison", ".detune", ".spread", ".level", ".pan"};
    // Continuous knobs are mod targets (A/B per osc): DETUNE (4) → 11/14,
    // SPREAD (5) → 12/15, LEVEL (6) → 7/8, PAN (7) → 13/16. OCT/SEMI/FINE/UNISON
    // are discrete steppers → not modulatable (modDest 0).
    for (int i = 0; i < 8; ++i) {
        int modDest = 0;
        switch (i) {
            case 4: modDest = osc == 0 ? 11 : 14; break; // DETUNE → A/B DETUNE
            case 5: modDest = osc == 0 ? 12 : 15; break; // SPREAD → A/B SPREAD
            case 6: modDest = osc == 0 ?  7 :  8; break; // LEVEL  → A/B LVL
            case 7: modDest = osc == 0 ? 13 : 16; break; // PAN    → A/B PAN
        }
        auto* k = new Knob(s, pre + ids[i], i == 6 ? Knob::Md : Knob::Sm, ac, true, modDest);
        knobs.add(k); addAndMakeVisible(k);
    }
```

with:

```cpp
    const char* ids[] = {".oct", ".semi", ".fine", ".detune", ".spread", ".blend", ".level", ".pan"};
    // Continuous knobs are mod targets (A/B per osc): DETUNE (3) → 11/14,
    // SPREAD (4) → 12/15, BLEND (5) → 26/27, LEVEL (6) → 7/8, PAN (7) → 13/16.
    // OCT/SEMI/FINE are discrete steppers → not modulatable (modDest 0);
    // UNISON is now the header stepper (unisonStep), not a knob.
    for (int i = 0; i < 8; ++i) {
        int modDest = 0;
        switch (i) {
            case 3: modDest = osc == 0 ? 11 : 14; break; // DETUNE → A/B DETUNE
            case 4: modDest = osc == 0 ? 12 : 15; break; // SPREAD → A/B SPREAD
            case 5: modDest = osc == 0 ? 26 : 27; break; // BLEND  → A/B BLEND
            case 6: modDest = osc == 0 ?  7 :  8; break; // LEVEL  → A/B LVL
            case 7: modDest = osc == 0 ? 13 : 16; break; // PAN    → A/B PAN
        }
        auto* k = new Knob(s, pre + ids[i], i == 6 ? Knob::Md : Knob::Sm, ac, true, modDest);
        knobs.add(k); addAndMakeVisible(k);
    }
```

LEVEL stays at index 6 so the `i == 6 ? Knob::Md` sizing is preserved. The row is still 8 knobs, so `layoutKnobRow` distributes them exactly as before.

- [ ] **Step 4: Place `unisonStep` in the header**

In `OscPanel::resized()`, replace:

```cpp
    auto right = head.removeFromRight(138);
    tableStep.setBounds(right.removeFromRight(100).withSizeKeepingCentre(100, 18));
    right.removeFromRight(4);
    editBtn.setBounds(right.removeFromRight(18).withSizeKeepingCentre(18, 18));
    titleArea = head;
```

with:

```cpp
    auto right = head.removeFromRight(138);
    tableStep.setBounds(right.removeFromRight(100).withSizeKeepingCentre(100, 18));
    right.removeFromRight(4);
    editBtn.setBounds(right.removeFromRight(18).withSizeKeepingCentre(18, 18));
    head.removeFromRight(6);
    unisonStep.setBounds(head.removeFromRight(52).withSizeKeepingCentre(52, 18));
    titleArea = head;
```

- [ ] **Step 5: Build, install, and visually verify**

Run: `cmake --build juce/build --parallel`
Then render the editor for a visual check via the host test (per the build memory):
Run: `juce/build/plugin_host_test_artefacts/Release/plugin_host_test` (writes editor PNG(s) to its cwd).
Expected: each OSC header shows a `◂ N ▸` UNISON stepper (1–16) left of the table stepper; the knob row reads OCT SEMI FINE DETUNE SPREAD BLEND LEVEL PAN, evenly spaced; clicking the stepper increments 1→16 and stops (no wrap); a source chip dropped on BLEND creates a route (dest "A BLEND"/"B BLEND").

- [ ] **Step 6: Commit**

```bash
git add juce/source/ui/Panels.h juce/source/ui/Panels.cpp
git commit -m "feat(juce): UNISON stepper + BLEND knob in osc panel"
```

---

## Task 5: Web params — BLEND param, unison max, mod dests 26/27 (TDD)

**Files:**
- Test: `src/params.test.ts`
- Modify: `src/params.ts` (`MOD_DESTS`, `dstTarget`, `DEST_OF_PARAM`, `oscParams`)

**Interfaces:**
- Produces: `PARAMS['oscX.blend']` (def 1.0, 0..1, lin); `dstTarget(26)='oscA.blend'`, `dstTarget(27)='oscB.blend'`; `DEST_OF_PARAM['oscX.blend']=26/27`; `MOD_DESTS[26/27]='A BLEND'/'B BLEND'`; `PARAMS['oscX.unison'].max=16`. Consumed by Tasks 6 (worklet parity) and 7 (UI).

- [ ] **Step 1: Write the failing tests**

In `src/params.test.ts`, extend the `EXPECTED` table — replace:

```ts
  [24, 'SUB LVL', 'sub.level'],
  [25, 'NOISE LVL', 'noise.level'],
];
```

with:

```ts
  [24, 'SUB LVL', 'sub.level'],
  [25, 'NOISE LVL', 'noise.level'],
  [26, 'A BLEND', 'oscA.blend'],
  [27, 'B BLEND', 'oscB.blend'],
];
```

Update the length assertion — replace:

```ts
  it('has exactly 26 entries (0..25) with the contract labels', () => {
    expect(MOD_DESTS).toHaveLength(26);
```

with:

```ts
  it('has exactly 28 entries (0..27) with the contract labels', () => {
    expect(MOD_DESTS).toHaveLength(28);
```

Update the out-of-range probe — replace:

```ts
    expect(dstTarget(26)).toBe('none');
    expect(dstTarget(-1)).toBe('none');
```

with:

```ts
    expect(dstTarget(28)).toBe('none');
    expect(dstTarget(-1)).toBe('none');
```

Append a new describe block at the end of the file — replace:

```ts
    expect(log(440, 0)).toBeCloseTo(440, 12); // identity at x=0
  });
});
```

with:

```ts
    expect(log(440, 0)).toBeCloseTo(440, 12); // identity at x=0
  });
});

describe('unison + blend (Serum-style unison)', () => {
  it('raises the unison voice cap to 16 on both oscillators', () => {
    expect(PARAMS['oscA.unison'].max).toBe(16);
    expect(PARAMS['oscB.unison'].max).toBe(16);
    expect(PARAMS['oscA.unison'].min).toBe(1);
  });

  it('adds a per-osc BLEND param defaulting to 1.0 over 0..1 (lin)', () => {
    for (const id of ['oscA.blend', 'oscB.blend']) {
      const def = PARAMS[id];
      expect(def, id).toBeDefined();
      expect(def.def).toBe(1.0);
      expect(def.min).toBe(0);
      expect(def.max).toBe(1);
      expect(def.curve).toBe('lin');
    }
  });

  it('wires BLEND as mod dest 26 (A) / 27 (B), both directions', () => {
    expect(dstTarget(26)).toBe('oscA.blend');
    expect(dstTarget(27)).toBe('oscB.blend');
    expect(DEST_OF_PARAM['oscA.blend']).toBe(26);
    expect(DEST_OF_PARAM['oscB.blend']).toBe(27);
  });
});
```

`PARAMS` and `DEST_OF_PARAM` are already imported at the top of `src/params.test.ts`.

- [ ] **Step 2: Run to verify the new tests fail**

Run: `npx vitest run src/params.test.ts`
Expected: FAIL — `MOD_DESTS` length is 26 not 28, `dstTarget(26)` is `'none'`, `PARAMS['oscA.blend']` is undefined.

- [ ] **Step 3: Append `MOD_DESTS` labels**

In `src/params.ts`, replace:

```ts
  'SUB LVL', 'NOISE LVL',
];
```

with:

```ts
  'SUB LVL', 'NOISE LVL',
  'A BLEND', 'B BLEND',
];
```

- [ ] **Step 4: Append `dstTarget` cases**

Replace:

```ts
    case 24: return 'sub.level';
    case 25: return 'noise.level';
    default: return 'none';
```

with:

```ts
    case 24: return 'sub.level';
    case 25: return 'noise.level';
    case 26: return 'oscA.blend';
    case 27: return 'oscB.blend';
    default: return 'none';
```

- [ ] **Step 5: Extend `DEST_OF_PARAM`**

Replace:

```ts
  'sub.level': 24,
  'noise.level': 25,
};
```

with:

```ts
  'sub.level': 24,
  'noise.level': 25,
  'oscA.blend': 26,
  'oscB.blend': 27,
};
```

- [ ] **Step 6: Raise unison max and add the blend ParamDef**

In `oscParams()`, replace:

```ts
    { id: `${prefix}.unison`, label: 'UNI', min: 1, max: 7, def: 1, curve: 'int', fmt: (v) => String(Math.round(v)) },
    { id: `${prefix}.detune`, label: 'DETUNE', min: 0, max: 1, def: 0.2, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.spread`, label: 'SPREAD', min: 0, max: 1, def: 0.6, curve: 'lin', fmt: fmtPct },
```

with:

```ts
    { id: `${prefix}.unison`, label: 'UNI', min: 1, max: 16, def: 1, curve: 'int', fmt: (v) => String(Math.round(v)) },
    { id: `${prefix}.detune`, label: 'DETUNE', min: 0, max: 1, def: 0.2, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.spread`, label: 'SPREAD', min: 0, max: 1, def: 0.6, curve: 'lin', fmt: fmtPct },
    { id: `${prefix}.blend`, label: 'BLEND', min: 0, max: 1, def: 1.0, curve: 'lin', fmt: fmtPct },
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `npx vitest run src/params.test.ts`
Expected: PASS (all, including the existing forward/backward dest-parity checks now covering 26/27).

- [ ] **Step 8: Commit**

```bash
git add src/params.ts src/params.test.ts
git commit -m "feat(web): BLEND param, unison max 16, mod dests 26/27"
```

---

## Task 6: Web worklet — BLEND math + MAXUNI 16 (lockstep with JUCE)

**Files:**
- Modify: `src/engine/worklet.js` (`MAXUNI`, `DST_TARGET`, `MOD_PARAM_INFO`, `setupOsc`)
- Test: `src/engine/worklet.parity.test.ts` (entry count 22→24)

**Interfaces:**
- Consumes: `oscX.blend` def + dests from Task 5.
- Produces: audible BLEND in the worklet; mod routing to `oscX.blend`. Note: params reach the worklet keyed by string ID (no flat wire-format array), so the only params.ts-lockstep surfaces here are `DST_TARGET` and `MOD_PARAM_INFO`.

- [ ] **Step 1: Bump the parity-test entry count (failing test first)**

In `src/engine/worklet.parity.test.ts`, replace:

```ts
  it('parses all 22 modulatable param entries', () => {
    expect(Object.keys(info)).toHaveLength(22);
  });
```

with:

```ts
  it('parses all 24 modulatable param entries', () => {
    expect(Object.keys(info)).toHaveLength(24);
  });
```

- [ ] **Step 2: Run parity tests to verify failure**

Run: `npx vitest run src/engine/worklet.parity.test.ts`
Expected: FAIL — `MOD_PARAM_INFO` still has 22 entries; and the `DST_TARGET`↔`MOD_DESTS` length/index parity check fails (MOD_DESTS is now 28 from Task 5 but worklet `DST_TARGET` still ends at 25).

- [ ] **Step 3: Raise `MAXUNI`**

In `src/engine/worklet.js`, replace:

```js
const MAXUNI = 7;
```

with:

```js
const MAXUNI = 16;
```

`makeOscState()` (`Float64Array/Float32Array(MAXUNI)`) and `Voice.noteOn()` (`for (let i=0;i<MAXUNI;i++)` phase init) key off this constant, so the extra voices are allocated and phase-initialized automatically.

- [ ] **Step 4: Append `DST_TARGET` entries**

Replace:

```js
  'sub.level',     // 24 SUB LVL
  'noise.level',   // 25 NOISE LVL
];
```

with:

```js
  'sub.level',     // 24 SUB LVL
  'noise.level',   // 25 NOISE LVL
  'oscA.blend',    // 26 A BLEND
  'oscB.blend',    // 27 B BLEND
];
```

- [ ] **Step 5: Add `MOD_PARAM_INFO` entries**

Replace:

```js
  'oscA.spread':    { curve: 'lin', lo: 0, hi: 1 },
  'oscB.spread':    { curve: 'lin', lo: 0, hi: 1 },
  'oscA.pan':       { curve: 'lin', lo: -1, hi: 1 },
```

with:

```js
  'oscA.spread':    { curve: 'lin', lo: 0, hi: 1 },
  'oscB.spread':    { curve: 'lin', lo: 0, hi: 1 },
  'oscA.blend':     { curve: 'lin', lo: 0, hi: 1 },
  'oscB.blend':     { curve: 'lin', lo: 0, hi: 1 },
  'oscA.pan':       { curve: 'lin', lo: -1, hi: 1 },
```

This allowlist is what lets a blend mod route populate `pm['oscX.blend']` (the worklet's fold is allowlist-gated, unlike JUCE's generic fold).

- [ ] **Step 6: Read blend in `setupOsc`**

Replace:

```js
    const det = pm[k + 'detune'] ?? p[k + 'detune'];
    const spr = pm[k + 'spread'] ?? p[k + 'spread'];
    const basePan = Math.max(-1, Math.min(1, (pm[k + 'pan'] ?? p[k + 'pan']) + mPan));
```

with:

```js
    const det = pm[k + 'detune'] ?? p[k + 'detune'];
    const spr = pm[k + 'spread'] ?? p[k + 'spread'];
    const blend = pm[k + 'blend'] ?? p[k + 'blend'];
    const basePan = Math.max(-1, Math.min(1, (pm[k + 'pan'] ?? p[k + 'pan']) + mPan));
```

- [ ] **Step 7: Apply the blend weighting**

Replace:

```js
    o.uni = uni;
    o.gain = (level * 0.32) / Math.sqrt(uni);

    for (let u = 0; u < uni; u++) {
      const sprd = uni > 1 ? (u / (uni - 1)) * 2 - 1 : 0;
      const cents = sprd * det * 50;
      const ratio = Math.pow(2, cents / 1200);
      o.incs[u] = cps * ratio * table.size;
      const pan = Math.max(-1, Math.min(1, sprd * spr + basePan));
      const a = ((pan + 1) * Math.PI) / 4;
      o.gl[u] = Math.cos(a);
      o.gr[u] = Math.sin(a);
    }
    return true;
```

with:

```js
    o.uni = uni;

    // BLEND: weight outer (most-detuned) voices vs. the center. weight_u =
    // 1-(1-blend)*|sprd_u|. blend=1 => all weights 1 (identical to legacy).
    // Loudness held ~constant by normalising on sqrt of the sum of squared
    // weights instead of the raw voice count.
    let sumW2 = 0;
    for (let u = 0; u < uni; u++) {
      const sprd = uni > 1 ? (u / (uni - 1)) * 2 - 1 : 0;
      const cents = sprd * det * 50;
      const ratio = Math.pow(2, cents / 1200);
      o.incs[u] = cps * ratio * table.size;
      const weight = 1 - (1 - blend) * Math.abs(sprd);
      sumW2 += weight * weight;
      const pan = Math.max(-1, Math.min(1, sprd * spr + basePan));
      const a = ((pan + 1) * Math.PI) / 4;
      o.gl[u] = Math.cos(a) * weight;
      o.gr[u] = Math.sin(a) * weight;
    }
    // `|| 1` guards uni=2,blend=0 (both endpoints -> sumW2=0). Matches the JUCE
    // `sumW2 > 0 ? sumW2 : 1` predicate exactly, so the engines stay in lockstep.
    o.gain = (level * 0.32) / Math.sqrt(sumW2 || 1);
    return true;
```

`o.gain` moves below the loop (depends on `sumW2`). At blend=1 every weight is 1, `sumW2 = uni`, and `gl/gr` are unchanged — bit-identical to the old line.

- [ ] **Step 8: Run parity tests to verify they pass**

Run: `npx vitest run src/engine/worklet.parity.test.ts src/params.test.ts`
Expected: PASS — `MOD_PARAM_INFO` has 24 entries; `DST_TARGET.length === MOD_DESTS.length (28)` and each index matches `dstTarget(i)`.

- [ ] **Step 9: Commit**

```bash
git add src/engine/worklet.js src/engine/worklet.parity.test.ts
git commit -m "feat(web): BLEND unison weighting + MAXUNI 16 in worklet (lockstep with JUCE)"
```

---

## Task 7: Web UI — UNISON stepper + BLEND knob

**Files:**
- Modify: `src/components/Stepper.tsx` (numeric/int mode)
- Modify: `src/components/panels/OscPanel.tsx` (unison→stepper, add blend knob)
- Modify: `src/index.css` (compact stepper in the knob row)

**Interfaces:**
- Consumes: `oscX.blend` ParamDef + `DEST_OF_PARAM` 26/27 (Task 5); the `Knob` resolves its mod dest internally via `DEST_OF_PARAM[paramId]` (no dest prop).
- Produces: a numeric Stepper mode; the osc panel UNISON stepper + BLEND knob.

- [ ] **Step 1: Add numeric mode to `Stepper.tsx`**

In `src/components/Stepper.tsx`, replace:

```tsx
  // Table steppers extend their fixed procedural options with the live
  // user-table pool so imported/drawn tables are selectable.
  const userTables = useStore((s) => s.userTables);
  const options = paramId.endsWith('.table')
```

with:

```tsx
  // Table steppers extend their fixed procedural options with the live
  // user-table pool so imported/drawn tables are selectable.
  const userTables = useStore((s) => s.userTables);

  // Numeric (integer-range) mode: no option list, drive from the param's
  // numeric range, stepping by 1 with CLAMPING (not wrapping) at [min, max].
  if (def.curve === 'int' && !def.options) {
    const lo = def.min as number;
    const hi = def.max as number;
    const v = Math.min(hi, Math.max(lo, Math.round(value)));
    const stepN = (d: number) => setParam(paramId, Math.min(hi, Math.max(lo, v + d)));
    return (
      <div className="stepper" data-accent={accent}>
        {label ? <span className="st-label">{label}</span> : null}
        <button className="st-btn st-prev" aria-label="previous" onClick={() => stepN(-1)}>◂</button>
        <span className="st-value">{def.fmt ? def.fmt(v) : String(v)}</span>
        <button className="st-btn st-next" aria-label="next" onClick={() => stepN(1)}>▸</button>
      </div>
    );
  }

  const options = paramId.endsWith('.table')
```

The early return sits AFTER all `useStore` hooks (preserving hook order) and BEFORE `const options = …` (which would otherwise throw on unison's undefined `options`). The guard `def.curve === 'int' && !def.options` matches only unison; `.table` and LFO-division steppers carry `options` and keep the wrap path.

- [ ] **Step 2: Swap unison→stepper and add the blend knob in `OscPanel.tsx`**

In `src/components/panels/OscPanel.tsx`, replace:

```tsx
          <Knob paramId={`${prefix}.unison`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.detune`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.spread`} size="sm" accent={accentKey} />
```

with:

```tsx
          <Stepper paramId={`${prefix}.unison`} label="UNI" accent={accentKey} />
          <Knob paramId={`${prefix}.detune`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.spread`} size="sm" accent={accentKey} />
          <Knob paramId={`${prefix}.blend`} size="sm" accent={accentKey} />
```

`Stepper` is already imported in `OscPanel.tsx`.

- [ ] **Step 3: Keep the in-row stepper compact**

In `src/index.css`, after the `.osc-knobs` rule, replace:

```css
.osc-knobs {
  grid-column: 1 / 3;
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
}
```

with:

```css
.osc-knobs {
  grid-column: 1 / 3;
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
}
/* unison stepper lives in the knob row: stack label over a compact ◂ N ▸ so it
   reads like the neighboring knobs instead of blowing out the flex row width. */
.osc-knobs .stepper {
  display: grid;
  grid-template-columns: auto auto auto;
  justify-items: center;
  align-items: center;
  gap: 2px 3px;
}
.osc-knobs .st-label { grid-column: 1 / -1; margin: 0; }
.osc-knobs .st-value { min-width: 24px; padding: 3px 4px; }
.osc-knobs .st-btn { width: 16px; height: 16px; }
```

- [ ] **Step 4: Type-check / build and verify**

Run: `npm run build` (includes `tsc`), then `npm run dev` for a visual check.
Expected: build passes. In the osc panel the UNISON control is a `◂ N ▸` stepper (1–16, clamps at both ends), and a BLEND knob sits after SPREAD; dragging a mod source onto BLEND creates a route and shows a ring (resolved via `DEST_OF_PARAM['oscX.blend']`).

- [ ] **Step 5: Commit**

```bash
git add src/components/Stepper.tsx src/components/panels/OscPanel.tsx src/index.css
git commit -m "feat(web): UNISON stepper + BLEND knob in osc panel"
```

---

## Self-Review

**Spec coverage:** unison 1→16 (Tasks 1,2,5,6 params+MAXUNI) ✓; BLEND param def 1.0 (Tasks 1,5) ✓; blend DSP math + normalization (Tasks 2,6) ✓; UNISON stepper (Tasks 3,4,7) ✓; SPREAD kept (knob rows retain `.spread`) ✓; BLEND mod dest 26/27 end-to-end (Tasks 1,4,5,6 + Knob dest resolution) ✓; backward compat (default 1.0, string-keyed state — verified, no preset edits) ✓; tests (engine_test §12, params.test, worklet.parity) ✓; performance note (documented, not gated) ✓.

**Refinements from adversarial verification (folded in):** (a) divide-by-zero guard with an *identical* predicate in both engines — C++ `sumW2 > 0.0 ? sumW2 : 1.0`, JS `sumW2 || 1` (NOT `std::max`, which diverges at uni=4); (b) uni=2/blend=0 finite-output test + documented as a known near-silent corner; (c) `MOD_DESTS` itself extended (not just `dstTarget`) to avoid the `convertTo0to1` clamp trap; (d) web worklet needs BOTH `DST_TARGET` and `MOD_PARAM_INFO` entries or blend routes are silently dropped; (e) engine_test uses the real `setParams(p)` harness API (not `e.params()`), valid because the RNG is deterministically seeded.

**Type/name consistency:** `OSC_BLEND`, `oscX.blend`, dests 26/27, `MOD_DESTS`/`DST_TARGET`/`DEST_OF_PARAM`/`MOD_PARAM_INFO`, `sumW2`, `weight` used consistently across tasks. JUCE↔web dest indices and labels match.

**Known limitation (documented):** uni=2 + blend=0 is near-silent (both voices are endpoints); the guard keeps it finite. Acceptable for this release.
