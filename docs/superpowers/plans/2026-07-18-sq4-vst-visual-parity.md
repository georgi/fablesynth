# SQ-4 VST Visual Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the visual gaps between the JUCE FableSeq (SQ-4) editor and the web SQ-4: embedded Michroma + IBM Plex Mono fonts, vector icons instead of ASCII glyph stand-ins, hover polish, the hint line, an animated focus collapse, and a restyled DeviceFocusView toolbar.

**Architecture:** Fonts are vendored TTFs embedded via a `juce_add_binary_data` target linked into all four plugins and all host tests; `fui::monoFont`/`dispFont` in `source/ui/Theme.h` load them with system-font fallback. Icons are `juce::Path` helpers in `Theme.h`. All motion is a pure function of time driven by existing/new lightweight timers.

**Tech Stack:** JUCE 8.0.4, C++17, CMake (build dir already configured at `juce/build`).

**Spec:** `docs/superpowers/specs/2026-07-18-sq4-vst-visual-parity-design.md`

**Spec §3 correction:** the grid's live-cell breathing glow, 3-bar EQ animation, queued pulse, and stopping shutter ALREADY exist (`SceneGridView.cpp:19-31, 646-652, 663-673, 739-755` — `qpulse()`/`stopPulse()` on a 30 Hz timer). The only motion work remaining is the focus collapse (Task 8). Do not re-implement the cell animations.

## Global Constraints

- No engine/DSP/state-model/parameter changes. UI + build files only.
- All existing tests must stay green: `plugin_host_test`, `drum_host_test`, `bass_host_test`, `sq4_host_test` (run via `ctest --test-dir juce/build` or the binaries directly).
- Tests run headless — nothing may require a display or installed fonts.
- Working dir for all commands: `/Users/mg/dev/fablesynth`.
- The repo has unrelated uncommitted changes (`juce/source/drum/dsp/DrumKits.cpp`, `juce/source/seq/dsp/SeqFactory.cpp`, `juce/test/drum_engine_test.cpp`, `juce/test/fixtures/web-session-presets.json`, `src/drum/kits.ts`, `src/seq/sessionPresets.ts`). NEVER `git add -A`; stage only the files each task touches.
- Palette/geometry constants are already web-accurate — do not change values in `fui::col` or any layout rectangle except where a task says so.
- Comment style: explain constraints, reference web counterparts (`src/seq/seq.css`, `SceneRow.tsx`) the way surrounding code does.

---

### Task 1: Vendor fonts + FableFonts BinaryData target

**Files:**
- Create: `juce/assets/fonts/Michroma-Regular.ttf`, `juce/assets/fonts/IBMPlexMono-Regular.ttf`, `juce/assets/fonts/IBMPlexMono-Medium.ttf`, `juce/assets/fonts/IBMPlexMono-SemiBold.ttf`, `juce/assets/fonts/OFL-Michroma.txt`, `juce/assets/fonts/OFL-IBMPlexMono.txt`
- Modify: `juce/CMakeLists.txt`

**Interfaces:**
- Produces: CMake static-lib target `FableFonts`; header `FableFonts.h` with namespace `fablefonts` exposing `MichromaRegular_ttf` / `MichromaRegular_ttfSize`, `IBMPlexMonoRegular_ttf(Size)`, `IBMPlexMonoMedium_ttf(Size)`, `IBMPlexMonoSemiBold_ttf(Size)`. Linked into FableSynth, FableDrum, FableBass, FableSeq, and all four host tests, so any TU including `Theme.h` in those targets can `#include "FableFonts.h"`.

- [ ] **Step 1: Download the fonts (both SIL OFL 1.1)**

```bash
mkdir -p juce/assets/fonts && cd juce/assets/fonts
curl -fLO https://raw.githubusercontent.com/google/fonts/main/ofl/michroma/Michroma-Regular.ttf
curl -fLO https://raw.githubusercontent.com/google/fonts/main/ofl/ibmplexmono/IBMPlexMono-Regular.ttf
curl -fLO https://raw.githubusercontent.com/google/fonts/main/ofl/ibmplexmono/IBMPlexMono-Medium.ttf
curl -fLO https://raw.githubusercontent.com/google/fonts/main/ofl/ibmplexmono/IBMPlexMono-SemiBold.ttf
curl -fL -o OFL-Michroma.txt   https://raw.githubusercontent.com/google/fonts/main/ofl/michroma/OFL.txt
curl -fL -o OFL-IBMPlexMono.txt https://raw.githubusercontent.com/google/fonts/main/ofl/ibmplexmono/OFL.txt
file *.ttf   # every line must say "TrueType Font data"
```

If a `google/fonts` path 404s (files occasionally move to a `static/` subdir), find the file with the GitHub API (`curl -s https://api.github.com/repos/google/fonts/contents/ofl/ibmplexmono`) and download from the path listed there. Do not substitute a variable-font `[wght].ttf` — JUCE needs static weights.

- [ ] **Step 2: Add the BinaryData target to CMake**

In `juce/CMakeLists.txt`, immediately after the `if(APPLE AND CMAKE_CXX_COMPILER_ID ...)` warning-suppression block (i.e. just before `juce_add_plugin(FableSynth`), insert:

```cmake
    # Embedded UI fonts (SIL OFL, vendored in assets/fonts): Michroma display +
    # IBM Plex Mono — the web app's typefaces (index.html loads them from Google
    # Fonts). One static lib shared by every editor target and host test so the
    # headless tests exercise the exact shipping typefaces.
    juce_add_binary_data(FableFonts
        HEADER_NAME FableFonts.h
        NAMESPACE fablefonts
        SOURCES
            assets/fonts/Michroma-Regular.ttf
            assets/fonts/IBMPlexMono-Regular.ttf
            assets/fonts/IBMPlexMono-Medium.ttf
            assets/fonts/IBMPlexMono-SemiBold.ttf)
    set_target_properties(FableFonts PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
```

- [ ] **Step 3: Link FableFonts into all eight targets**

Add `FableFonts` as the first entry in the `PRIVATE` list of each `target_link_libraries` for: `FableSynth` (line ~85), `FableDrum` (~144), `FableBass` (~197), `FableSeq` (~261), `plugin_host_test` (~308), `drum_host_test` (~322), `bass_host_test` (~336), `sq4_host_test` (~350). Example (FableSeq):

```cmake
    target_link_libraries(FableSeq PRIVATE FableFonts juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
```

- [ ] **Step 4: Reconfigure + build one target to verify generation**

Run: `cmake -B juce/build -S juce && cmake --build juce/build --target sq4_host_test -j8`
Expected: configures and builds cleanly; `juce/build/juce_binarydata_FableFonts/JuceLibraryCode/FableFonts.h` exists and declares `MichromaRegular_ttf`.

- [ ] **Step 5: Run the sq4 host test**

Run: `./juce/build/sq4_host_test_artefacts/sq4_host_test` (find the exact artefact path with `find juce/build -name "sq4_host_test" -type f -perm +111` if it differs)
Expected: `ALL PASS`

- [ ] **Step 6: Commit**

```bash
git add juce/assets/fonts juce/CMakeLists.txt
git commit -m "build: embed Michroma + IBM Plex Mono as FableFonts binary data"
```

---

### Task 2: Theme.h loads the embedded typefaces (+ real middle dots)

**Files:**
- Modify: `juce/source/ui/Theme.h:100-106` (font accessors), `juce/test/sq4_host_test.cpp` (new checks)
- Modify (middle-dot cleanup, same commit): `juce/source/seq/ui/SceneGridView.cpp:516-517`, `juce/source/seq/ui/SeqHeader.cpp:351`, `juce/source/seq/ui/SeqFooterView.cpp` (ASCII dot stand-ins)

**Interfaces:**
- Consumes: `fablefonts::*` symbols from Task 1.
- Produces: `fui::monoFont(float h, bool bold=false)` (unchanged signature, now Plex Regular/SemiBold), `fui::monoFontMedium(float h)` (new, Plex Medium), `fui::dispFont(float h)` (unchanged signature, now Michroma). All return `juce::Font`; later tasks rely on these exact names.

- [ ] **Step 1: Write the failing test**

In `juce/test/sq4_host_test.cpp`, add `#include "../source/ui/Theme.h"` to the include block, and add these checks inside `main` right after the first existing `check(...)` block (harness: `static void check(bool, const char*, double=0)`, global `failures` counter):

```cpp
    // ---- Embedded fonts (visual-parity spec §1): the shared Theme must serve
    // the web's real typefaces, not the default-font stand-ins. ----
    check(fui::dispFont(10.0f).getTypefaceName() == "Michroma", "dispFont is embedded Michroma");
    check(fui::monoFont(10.0f).getTypefaceName().startsWith("IBM Plex Mono"), "monoFont is embedded IBM Plex Mono");
    check(fui::monoFont(10.0f, true).getTypefaceName().startsWith("IBM Plex Mono"), "bold monoFont is embedded IBM Plex Mono");
    check(fui::monoFontMedium(10.0f).getTypefaceName().startsWith("IBM Plex Mono"), "monoFontMedium is embedded IBM Plex Mono");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build juce/build --target sq4_host_test -j8 2>&1 | tail -5`
Expected: compile FAILURE — `monoFontMedium` is not a member of `fui` (the other three would fail at runtime; the compile error is the failing signal).

- [ ] **Step 3: Implement the font loading in Theme.h**

Replace `Theme.h:100-106` (the current `monoFont`/`dispFont` pair) with:

```cpp
// Embedded typefaces (FableFonts binary data, visual-parity spec §1): the
// web app's Michroma display face and IBM Plex Mono 400/500/600. Guarded by
// __has_include so a target that doesn't link FableFonts still compiles and
// falls back to the old system-font stand-ins.
#if __has_include("FableFonts.h")
} // namespace fui  (BinaryData header must live outside our namespace)
#include "FableFonts.h"
namespace fui {
namespace detail {
inline juce::Typeface::Ptr embedded(const void* data, int size) {
    return juce::Typeface::createSystemTypefaceFor(data, (size_t) size);
}
inline juce::Typeface::Ptr michroma()      { static auto t = embedded(fablefonts::MichromaRegular_ttf,     fablefonts::MichromaRegular_ttfSize);     return t; }
inline juce::Typeface::Ptr plexRegular()   { static auto t = embedded(fablefonts::IBMPlexMonoRegular_ttf,  fablefonts::IBMPlexMonoRegular_ttfSize);  return t; }
inline juce::Typeface::Ptr plexMedium()    { static auto t = embedded(fablefonts::IBMPlexMonoMedium_ttf,   fablefonts::IBMPlexMonoMedium_ttfSize);   return t; }
inline juce::Typeface::Ptr plexSemiBold()  { static auto t = embedded(fablefonts::IBMPlexMonoSemiBold_ttf, fablefonts::IBMPlexMonoSemiBold_ttfSize); return t; }
} // namespace detail
#else
namespace detail {
inline juce::Typeface::Ptr michroma()     { return nullptr; }
inline juce::Typeface::Ptr plexRegular()  { return nullptr; }
inline juce::Typeface::Ptr plexMedium()   { return nullptr; }
inline juce::Typeface::Ptr plexSemiBold() { return nullptr; }
} // namespace detail
#endif

inline juce::Font monoFont(float h, bool bold = false) {
    if (auto tf = bold ? detail::plexSemiBold() : detail::plexRegular())
        return juce::Font(juce::FontOptions(tf).withHeight(h));
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h,
                                        bold ? juce::Font::bold : juce::Font::plain));
}
inline juce::Font monoFontMedium(float h) {
    if (auto tf = detail::plexMedium())
        return juce::Font(juce::FontOptions(tf).withHeight(h));
    return monoFont(h);
}
inline juce::Font dispFont(float h) {
    if (auto tf = detail::michroma())
        return juce::Font(juce::FontOptions(tf).withHeight(h));
    return juce::Font(juce::FontOptions(h, juce::Font::bold));
}
```

Note the namespace close/reopen around the include — `FableFonts.h` must not be swallowed into `fui`. If the `#if` placement fights the surrounding file structure, an equivalent fix is to put the `#include "FableFonts.h"` guard at the very top of `Theme.h` (after `#pragma once`) and keep everything else inside `fui` — choose whichever compiles cleanly; the accessor signatures are what matters.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build juce/build --target sq4_host_test -j8 && ./juce/build/sq4_host_test_artefacts/sq4_host_test | head -20`
Expected: the four new checks print `[PASS]`, final line `ALL PASS`.

- [ ] **Step 5: Replace ASCII middle-dot stand-ins with real "·"**

IBM Plex Mono covers Latin-1, so `·` (U+00B7) is now safe. Find every stand-in:

```bash
grep -rn '" - "\|LIVE - MUTED\|middle dot\|middle-dot' juce/source/seq/ui/ juce/source/seq/SeqEditor.cpp
```

Apply at minimum:
- `SceneGridView.cpp:517`: `status = "LIVE - MUTED"` → `status = juce::String::fromUTF8("LIVE \xc2\xb7 MUTED")` and delete the stale `-- ASCII middle dot substitute` comment on line 516.
- `SeqHeader.cpp:351`: `g.drawText(bar + " - " + bpm, ...)` → `g.drawText(bar + juce::String::fromUTF8(" \xc2\xb7 ") + bpm, ...)`; update the comment at `SeqHeader.cpp:28` that explains the old substitution.
- Any hit in `SeqFooterView.cpp` (the NOW readout separator): same `fromUTF8(" \xc2\xb7 ")` treatment.

- [ ] **Step 6: Build everything, run all tests**

Run: `cmake --build juce/build -j8 && ctest --test-dir juce/build --output-on-failure`
Expected: all targets build; all tests pass.

- [ ] **Step 7: Commit**

```bash
git add juce/source/ui/Theme.h juce/test/sq4_host_test.cpp juce/source/seq/ui/SceneGridView.cpp juce/source/seq/ui/SeqHeader.cpp juce/source/seq/ui/SeqFooterView.cpp
git commit -m "feat(ui): load embedded Michroma/Plex Mono in Theme, real middle dots"
```

---

### Task 3: Vector icon helpers in Theme.h

**Files:**
- Modify: `juce/source/ui/Theme.h` (append before the closing `} // namespace fui`)

**Interfaces:**
- Produces (all `inline`, namespace `fui`, all take a `juce::Rectangle<float>` bounding box):
  - `juce::Path iconPlay(juce::Rectangle<float>)` — fill
  - `juce::Path iconStop(juce::Rectangle<float>)` — fill
  - `juce::Path iconChevron(juce::Rectangle<float>, bool pointsRight)` — STROKE with `PathStrokeType(1.6f)`
  - `juce::Path iconPencil(juce::Rectangle<float>)` — fill
  - `juce::Path iconTrash(juce::Rectangle<float>)` — fill
  - `juce::Path iconPlus(juce::Rectangle<float>)` — fill
  Later tasks call exactly these names.

- [ ] **Step 1: Add the helpers**

Append to `Theme.h` (inside `namespace fui`, after `dispFont`):

```cpp
// ---- Vector icons (visual-parity spec §2) -----------------------------------
// The web uses raw glyphs (▶ ■ ✎ …); no embedded font reliably covers them,
// so the icons are Paths — resolution-independent and identical headless.
// Caller sets the colour; fill every path except iconChevron (stroke 1.6f).

inline juce::Path iconPlay(juce::Rectangle<float> r) {
    juce::Path p;
    p.addTriangle(r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
    return p;
}

inline juce::Path iconStop(juce::Rectangle<float> r) {
    juce::Path p;
    p.addRectangle(r.reduced(r.getWidth() * 0.08f));
    return p;
}

inline juce::Path iconChevron(juce::Rectangle<float> r, bool pointsRight) {
    juce::Path p;
    const float xBack = pointsRight ? r.getX() : r.getRight();
    const float xTip  = pointsRight ? r.getRight() : r.getX();
    p.startNewSubPath(xBack, r.getY());
    p.lineTo(xTip, r.getCentreY());
    p.lineTo(xBack, r.getBottom());
    return p; // stroke with PathStrokeType(1.6f), do not fill
}

inline juce::Path iconPencil(juce::Rectangle<float> r) {
    juce::Path p;
    const float w = r.getWidth() * 0.30f, h = r.getHeight();
    p.addRectangle(-w * 0.5f, -h * 0.5f, w, h * 0.68f);                       // shaft
    p.addTriangle(-w * 0.5f, h * 0.24f, w * 0.5f, h * 0.24f, 0.0f, h * 0.5f); // tip
    p.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi * 0.25f)
                         .translated(r.getCentreX(), r.getCentreY()));
    return p;
}

inline juce::Path iconTrash(juce::Rectangle<float> r) {
    juce::Path p;
    p.addRoundedRectangle(r.withTrimmedTop(r.getHeight() * 0.30f)
                              .reduced(r.getWidth() * 0.14f, 0.0f), 1.0f);     // bin
    p.addRectangle(r.getX(), r.getY() + r.getHeight() * 0.16f,
                   r.getWidth(), r.getHeight() * 0.09f);                       // lid
    p.addRectangle(r.getCentreX() - r.getWidth() * 0.16f, r.getY() + r.getHeight() * 0.02f,
                   r.getWidth() * 0.32f, r.getHeight() * 0.10f);               // handle
    return p;
}

inline juce::Path iconPlus(juce::Rectangle<float> r) {
    juce::Path p;
    const float t = juce::jmax(1.5f, r.getWidth() * 0.18f);
    p.addRectangle(r.getCentreX() - t * 0.5f, r.getY(), t, r.getHeight());
    p.addRectangle(r.getX(), r.getCentreY() - t * 0.5f, r.getWidth(), t);
    return p;
}
```

- [ ] **Step 2: Build to verify it compiles everywhere**

Run: `cmake --build juce/build -j8 2>&1 | tail -3`
Expected: clean build (helpers are unused so far; if `-Wunused` complains about inline functions it won't — they're inline).

- [ ] **Step 3: Commit**

```bash
git add juce/source/ui/Theme.h
git commit -m "feat(ui): vector icon path helpers in Theme"
```

---

### Task 4: Replace ASCII glyph stand-ins with icons (header, grid, heads, focus steppers)

**Files:**
- Modify: `juce/source/seq/ui/SeqHeader.cpp` (~30-31, ~297-299, stepper draw sites), `juce/source/seq/ui/SceneGridView.cpp` (12-17, 500-502, 539, 674-682), `juce/source/seq/ui/TrackHeadsView.cpp` (48, 267-270, chevron draw sites), `juce/source/seq/ui/DeviceFocusView.cpp` (ctor: `previousPatchButton_`/`nextPatchButton_` labels)

**Interfaces:**
- Consumes: `fui::iconPlay/iconStop/iconChevron/iconPencil` from Task 3.
- Produces: nothing new for later tasks; `kEditGlyph` drawing in `SceneGridView.cpp:735-737` becomes `iconPencil` (Task 5 will gate it on hover).

- [ ] **Step 1: SeqHeader transport + chevrons**

At `SeqHeader.cpp:30-31`, delete `kPlayGlyph`/`kStopGlyph`/`kPrevGlyph`/`kNextGlyph` and the stale comment above them. At the transport button (line ~299, `drawBtn(playBtn, playing ? kStopGlyph : kPlayGlyph, playing)`): change `drawBtn` (defined just above, ends `g.drawText(txt, r, ...)` at ~297) so the play button draws an icon; the web shows ▶ / ■ with a text label beside it only in the tooltip, so icon-only is correct:

```cpp
    // drawBtn keeps drawing text for other buttons; the transport gets an icon.
    drawBtn(playBtn, "", playing);
    {
        auto ir = playBtn.toFloat().withSizeKeepingCentre(11.0f, 11.0f);
        g.setColour(playing ? col::bg : accentA()); // same fg colours drawBtn used for its text
        g.fillPath(playing ? iconStop(ir) : iconPlay(ir.reduced(1.0f, 0.0f)));
    }
```

Match the exact fg colour logic from the existing `drawBtn` body (read it first — if it uses different on/off colours, mirror those, not the ones above). Replace every `kPrevGlyph`/`kNextGlyph` `drawText` in the quant/patch steppers with:

```cpp
    g.strokePath(iconChevron(area.toFloat().withSizeKeepingCentre(5.0f, 9.0f), /*pointsRight*/ true),
                 juce::PathStrokeType(1.6f));
```

(`false` for prev; keep whatever colour the drawText used.)

- [ ] **Step 2: SceneGridView glyphs**

Delete the `kPlayGlyph/kStopGlyph/kIdleGlyph/kPassGlyph/kEditGlyph` constants and their stale comment (`SceneGridView.cpp:9-17`; `kPassGlyph` is already unused — verify with grep). Replace the call sites, keeping each site's existing colour lines:

Scene-card launch button (~500-502):
```cpp
    g.setColour(queued ? col::text.withAlpha(qpulse()) : anyOwner ? juce::Colour(0xff4dff9e) : col::acN);
    g.fillPath(iconPlay(launchBtn[s].toFloat().withSizeKeepingCentre(8.0f, 10.0f)));
```

Scene-card stop toggle (~539): `drawToggle(stopBtnR[s], kStopGlyph, false, col::acB);` — `drawToggle` draws text, so give it an icon variant. Replace the call with:

```cpp
    drawToggle(muteBtnR[s], "M", muted, col::acB);
    { // stop button: same chrome as drawToggle, square icon instead of a letter
        auto rf2 = stopBtnR[s].toFloat();
        g.setColour(juce::Colour(0xff11141c));
        g.fillRoundedRectangle(rf2, 5.0f);
        g.setColour(col::line);
        g.drawRoundedRectangle(rf2.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(col::textDim);
        g.fillPath(iconStop(stopBtnR[s].toFloat().withSizeKeepingCentre(7.0f, 7.0f)));
    }
```

(The stop toggle is always drawn `on=false`, so the constant-off chrome above is equivalent.)

Filled-cell stopping icon (~674-677): replace the `kStopGlyph` drawText with
```cpp
        g.setColour(col::acB.withAlpha(0.65f + stopPulse() * 0.35f));
        g.fillPath(iconStop(iconArea.toFloat().withSizeKeepingCentre(8.0f, 8.0f)));
```

Filled-cell idle icon (~678-682): replace the `kIdleGlyph` drawText with
```cpp
        g.setColour(juce::Colour(0xff4a5266).withAlpha(bodyAlpha));
        g.fillPath(iconPlay(iconArea.toFloat().withSizeKeepingCentre(7.0f, 9.0f)));
```

Edit glyph (~733-737): replace the drawText with a pencil (Task 5 adds hover gating; keep steady for now):
```cpp
    g.setColour(tc.withAlpha(0.85f));
    g.fillPath(iconPencil(editGlyph[s][t].toFloat().withSizeKeepingCentre(9.0f, 9.0f)));
```

Clip name font (~693): `g.setFont(monoFont(9.5f));` → `g.setFont(monoFontMedium(9.5f));` (web `.sq-cell-name` is weight 500).

- [ ] **Step 3: TrackHeadsView + DeviceFocusView**

`TrackHeadsView.cpp:48`: delete `kPrevGlyph`/`kNextGlyph`; replace their drawText sites with the same `iconChevron` stroke pattern as Step 1. `TrackHeadsView.cpp:270`: replace `g.drawText("E", editSlot, ...)` (and its `// ✎ stand-in` comment) with:

```cpp
        g.fillPath(iconPencil(editSlot.toFloat().withSizeKeepingCentre(9.0f, 9.0f)));
```

`DeviceFocusView.h:91-93`: the `previousPatchButton_ { "<" }` / `nextPatchButton_ { ">" }` TextButtons keep their ASCII labels for now — Task 7 replaces them as part of the toolbar restyle. No change here.

- [ ] **Step 4: Build + run all tests**

Run: `cmake --build juce/build -j8 && ctest --test-dir juce/build --output-on-failure`
Expected: all pass. If `sq4_host_test` asserts on removed constants (grep `kPlayGlyph\|kEditGlyph` in `juce/test/` first), update those references to the new drawing — they are paint-side only, so no behavioral checks should break.

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/ui/SeqHeader.cpp juce/source/seq/ui/SceneGridView.cpp juce/source/seq/ui/TrackHeadsView.cpp
git commit -m "feat(ux): vector transport/grid/stepper icons replace ASCII stand-ins"
```

---

### Task 5: Grid hover polish — hover-revealed edit/delete chips, empty-cell +

**Files:**
- Modify: `juce/source/seq/ui/SceneGridView.h` (hover state + regions + mouse overrides), `juce/source/seq/ui/SceneGridView.cpp` (mouseMove/mouseExit, layout, paint, mouseDown hit tests)

**Interfaces:**
- Consumes: `iconPencil`, `iconTrash`, `iconPlus` (Task 3); existing `selectCell(s,t)` / `selDelete()` / `onEditClip` (already declared in `SceneGridView.h`).
- Produces: test handle `void hoverCell(int s, int t)` (public, sets hover state exactly as mouseMove would; `(-1,-1)` clears) — declared so headless tests can drive hover.

- [ ] **Step 1: Add hover state and handles**

`SceneGridView.h`: next to the existing mouse overrides add `void mouseMove(const juce::MouseEvent&) override;` and `void mouseExit(const juce::MouseEvent&) override;`; in the public test-handle section add `void hoverCell(int s, int t);`; in the private data add `int hoverCellS_ = -1, hoverCellT_ = -1;` and a region array `juce::Rectangle<int> trashGlyph[kScenes][kTracks];` next to `editGlyph`.

`SceneGridView.cpp`:

```cpp
void SceneGridView::hoverCell(int s, int t) {
    if (s == hoverCellS_ && t == hoverCellT_) return;
    hoverCellS_ = s; hoverCellT_ = t;
    repaint();
}
void SceneGridView::mouseMove(const juce::MouseEvent& e) {
    int t = -1;
    const int s = cellAt(e.getPosition(), t);
    hoverCell(s, t);
}
void SceneGridView::mouseExit(const juce::MouseEvent&) { hoverCell(-1, -1); }
```

In `layoutRow` (find where `editGlyph[s][t]` is assigned), place `trashGlyph[s][t]` as the same-size rectangle immediately left of `editGlyph[s][t]` with a 4px gap.

- [ ] **Step 2: Gate the chips on hover in paintFilledCell**

Replace the Task-4 steady pencil block (~733-737) with web-parity hover reveal (`seq.css` `.sq-cell-tools`, hover-only):

```cpp
    // edit / delete chips — hover-revealed, like the web's .sq-cell-tools.
    if (hoverCellS_ == s && hoverCellT_ == t) {
        g.setColour(tc.withAlpha(0.90f));
        g.fillPath(iconPencil(editGlyph[s][t].toFloat().withSizeKeepingCentre(9.0f, 9.0f)));
        g.setColour(col::textDim);
        g.fillPath(iconTrash(trashGlyph[s][t].toFloat().withSizeKeepingCentre(8.0f, 9.0f)));
    }
```

In `paintEmptyCell`, after the centre square/dot, add the hover-revealed add affordance:

```cpp
    if (hoverCellS_ == s && hoverCellT_ == t) {
        // + add chip (web .sq-cell-add): opens the device focused on this cell
        // so CREATE CLIP lands exactly here.
        g.setColour(tc.withAlpha(0.75f));
        g.fillPath(iconPlus(juce::Rectangle<float>(9.0f, 9.0f)
            .withCentre({ rf.getRight() - 12.0f, rf.getY() + 12.0f })));
    }
```

- [ ] **Step 3: Wire the hit targets in mouseDown**

Find the `mouseDown` cell hit-test (it already special-cases `editGlyph[s][t]` → `cellEditClick`). Add, for filled cells, a `trashGlyph[s][t].contains(pos)` branch BEFORE the general cell-press handling:

```cpp
        if (trashGlyph[s][t].contains(e.getPosition())
            && proc.conductor().session().scenes[(size_t)s].hasClip[(size_t)t]) {
            selectCell(s, t);   // route through the selection verb: one undo
            selDelete();        // snapshot + machine-safe clearing, already tested
            return;
        }
```

For empty cells, add a hit test on the same rectangle used to paint the `+` (top-right 24×24 corner of the cell) that calls `onEditClip(s, t)` and returns (before the stop/launch press logic).

- [ ] **Step 4: Add a headless regression check**

In `juce/test/sq4_host_test.cpp`, where the grid test handles are exercised (grep `cellEditClick` or `selDelete` for the right section), add:

```cpp
    // hover handle exists and is inert on state (visual-parity spec §4)
    ed->grid().hoverCell(0, 0);
    ed->grid().hoverCell(-1, -1);
    check(true, "grid hover handles callable headless");
```

(Adapt the editor accessor to the local variable the section already uses — the test file constructs the editor for grid checks.) If no section constructs an editor, add the check right after the first existing editor construction.

- [ ] **Step 5: Build + run all tests**

Run: `cmake --build juce/build -j8 && ctest --test-dir juce/build --output-on-failure`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add juce/source/seq/ui/SceneGridView.h juce/source/seq/ui/SceneGridView.cpp juce/test/sq4_host_test.cpp
git commit -m "feat(ux): hover-revealed edit/delete/add chips in the SQ-4 grid"
```

---

### Task 6: Real hint line

**Files:**
- Modify: `juce/source/seq/SeqEditor.h` (replace `juce::Component hint` with a `HintBar`), `juce/source/seq/SeqEditor.cpp` (wire provider), `juce/source/seq/ui/SeqHeader.h` + `SeqHeader.cpp` (expose `quantLabel()`)

**Interfaces:**
- Consumes: `fui::monoFont`, `fui::drawSpaced`, `fui::col::textHint`.
- Produces: `juce::String SeqHeader::quantLabel() const` — the exact string painted in the quant value area (refactored out of the paint code at `SeqHeader.cpp:~329`); `class HintBar : public juce::Component, private juce::Timer` with `void setProvider(std::function<juce::String()>)`.

- [ ] **Step 1: Expose the quant label**

In `SeqHeader.cpp`, the quant value paint (~line 329, `g.drawText(label, quantValArea, ...)`) computes a label string. Extract that computation into a public const method (declaration in `SeqHeader.h`):

```cpp
juce::String SeqHeader::quantLabel() const {
    // ...the exact expression paint() used to build `label`, unchanged...
}
```

and have the paint site call it. (Read the existing code first; move, don't rewrite.)

- [ ] **Step 2: HintBar component**

In `SeqEditor.h`, above `class SeqRack`, add:

```cpp
// The web's .sq-hint legend line (SeqApp.tsx): tiny contextual key/mouse help
// under the rack. Text comes from a provider so the quant value stays current;
// 2 Hz repaint is plenty for copy changes.
class HintBar : public juce::Component, private juce::Timer {
public:
    HintBar() { startTimerHz(2); setInterceptsMouseClicks(false, false); }
    ~HintBar() override { stopTimer(); }
    void setProvider(std::function<juce::String()> p) { provider_ = std::move(p); repaint(); }
    void paint(juce::Graphics& g) override {
        if (!provider_) return;
        g.setColour(fui::col::textHint.withAlpha(0.85f));
        g.setFont(fui::monoFont(8.0f));
        fui::drawSpaced(g, provider_(), getLocalBounds(), 1.1f, juce::Justification::centred);
    }
private:
    void timerCallback() override {
        auto now = provider_ ? provider_() : juce::String();
        if (now != last_) { last_ = now; repaint(); }
    }
    std::function<juce::String()> provider_;
    juce::String last_;
};
```

Change `juce::Component hint;` to `HintBar hint;` in `SeqRack`, and add a public `HintBar& getHint() { return hint; }`.

- [ ] **Step 3: Wire the copy in SeqEditor's constructor**

After the existing focus wiring in `SeqEditor::SeqEditor` (~line 92), add:

```cpp
    // Hint copy mirrors SeqApp.tsx's two .sq-hint strings; ✎ has no glyph
    // coverage, so the word EDIT stands in.
    rack.getHint().setProvider([this]() -> juce::String {
        const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
        if (focusTrack_ >= 0)
            return "MINI STRIP STAYS LIVE - TAP CELLS TO LAUNCH" + dot
                 + "EDIT RETARGETS THE EDITOR" + dot + "ESC BACK TO SESSION";
        return "TAP CLIP TO LAUNCH" + dot + "TAP AGAIN TO STOP" + dot
             + "LAUNCHES QUANTIZE TO " + header().quantLabel() + dot
             + "CMD-CLICK SELECTS" + dot + "DRAG MOVES (ALT COPIES)" + dot
             + "CMD-C/X/V/D/Z EDIT" + dot + "RIGHT-CLICK EMPTY CELL TO TOGGLE PASS-THROUGH";
    });
```

- [ ] **Step 4: Build + run all tests**

Run: `cmake --build juce/build -j8 && ctest --test-dir juce/build --output-on-failure`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/SeqEditor.h juce/source/seq/SeqEditor.cpp juce/source/seq/ui/SeqHeader.h juce/source/seq/ui/SeqHeader.cpp
git commit -m "feat(ux): contextual hint legend line under the SQ-4 rack"
```

---

### Task 7: DeviceFocusView toolbar restyle

**Files:**
- Modify: `juce/source/seq/ui/DeviceFocusView.cpp` (ctor styling + new `paint()`), `juce/source/seq/ui/DeviceFocusView.h` (add `void paint(juce::Graphics&) override;`)

**Interfaces:**
- Consumes: `fui::drawPanel`, `fui::monoFont`, `fui::monoFontMedium`, `fui::col::*`, `fui::accentA()`.
- Produces: nothing new; all `*ForTest` accessors and widget behavior stay identical (tests depend on them).

- [ ] **Step 1: Panel background**

Add to `DeviceFocusView.cpp`:

```cpp
void DeviceFocusView::paint(juce::Graphics& g) {
    // The toolbar strip gets the standard machined panel so it stops reading
    // as bare stock widgets floating over the editor background (spec §5).
    // The hosted device body below paints itself.
    auto toolbar = getLocalBounds().removeFromTop(/* toolbar height from resized() */ 0);
    // Read resized(): use the exact y-extent it gives the toolbar row(s).
    if (!toolbar.isEmpty()) drawPanel(g, toolbar.toFloat(), 10.0f);
}
```

First read `DeviceFocusView::resized()` and replace the `0` with the real toolbar strip height it lays out (the rows holding `patchLabel_ … createClipButton_`), including its padding. If `resized()` interleaves toolbar and body, compute the strip as `getLocalBounds().withHeight(bodyTop)` where `bodyTop` is the y `layoutBody` starts at.

- [ ] **Step 2: Widget styling in the constructor**

In the constructor where the labels/combos/buttons are configured, add:

```cpp
    // Toolbar typography + colours — match SeqHeader's hand-drawn chrome.
    for (auto* l : { &patchLabel_, &clipTargetLabel_, &clipMetadataLabel_ }) {
        l->setFont(monoFont(8.0f));
        l->setColour(juce::Label::textColourId, col::textDim);
    }
    clipTargetLabel_.setFont(monoFontMedium(9.0f));
    clipTargetLabel_.setColour(juce::Label::textColourId, col::text);
    createClipButton_.setColour(juce::TextButton::buttonColourId, accentA().withAlpha(0.14f));
    createClipButton_.setColour(juce::TextButton::textColourOffId, accentA());
    previousPatchButton_.setColour(juce::TextButton::textColourOffId, col::textDim);
    nextPatchButton_.setColour(juce::TextButton::textColourOffId, col::textDim);
```

Keep `previousPatchButton_`/`nextPatchButton_` as TextButtons (their `<` / `>` labels now render in Plex Mono, which is fine for a stepper); the DarkLNF ComboBox styling already matches the house look.

- [ ] **Step 3: Build + run all tests**

Run: `cmake --build juce/build -j8 && ctest --test-dir juce/build --output-on-failure`
Expected: all pass (styling only; every `*ForTest` accessor untouched).

- [ ] **Step 4: Commit**

```bash
git add juce/source/seq/ui/DeviceFocusView.h juce/source/seq/ui/DeviceFocusView.cpp
git commit -m "feat(ux): device-focus toolbar matches the hand-drawn chrome"
```

---

### Task 8: Animated focus collapse

**Files:**
- Modify: `juce/source/seq/SeqEditor.h` (SeqRack gains Timer + interpolation), `juce/source/seq/SeqEditor.cpp` (enterFocus/exitFocus/resized → layout)

**Interfaces:**
- Consumes: existing `SeqRack` child views; `SceneGridView::setSingleRow/clearSingleRow`.
- Produces: unchanged public API. Headless behavior guarantee: when the editor `!isShowing()` (all host tests), enter/exit focus snaps to the final layout synchronously — tests that assert bounds right after `enterFocus` keep passing.

- [ ] **Step 1: Add the interpolated layout**

`SeqEditor.h`: `class SeqRack : public juce::Component, private juce::Timer {`; add privates:

```cpp
    void timerCallback() override;
    void applyLayout();
    float focusT_ = 0.0f, focusTarget_ = 0.0f; // 0 = session, 1 = focus
```

`SeqEditor.cpp`: replace the body of `SeqRack::resized()` with `applyLayout();` and add:

```cpp
// Eased focus collapse — the JUCE analogue of the web's FLIP animation
// (seq.css sq-focus-in): the grid shrinks 630→96 while the device surface
// grows into the freed space. ~180ms at 30Hz; headless (never showing)
// snaps instantly so the host tests see final geometry synchronously.
void SeqRack::applyLayout() {
    const float t = focusT_ * focusT_ * (3.0f - 2.0f * focusT_); // smoothstep
    header.setBounds(18, 14, 1424, 66);
    trackHeads.setBounds(18, 89, 1424, 54);
    const int gridH = juce::roundToInt(630.0f + (96.0f - 630.0f) * t);
    sceneGrid.setBounds(18, 152, 1424, gridH);
    const int devY = 152 + gridH + 8;
    deviceFocus.setBounds(18, devY, 1424, 906 - devY);
    footer.setBounds(18, 782, 1424, 68);
    hint.setBounds(18, 858, 1424, 20);
}

void SeqRack::timerCallback() {
    const float step = 1.0f / (0.18f * 30.0f); // full sweep in ~180ms
    focusT_ = focusTarget_ > focusT_ ? juce::jmin(focusTarget_, focusT_ + step)
                                     : juce::jmax(focusTarget_, focusT_ - step);
    applyLayout();
    if (juce::approximatelyEqual(focusT_, focusTarget_)) stopTimer();
}
```

- [ ] **Step 2: Drive it from enterFocus/exitFocus**

In `SeqRack::enterFocus` replace the trailing `resized();` with:

```cpp
    focusTarget_ = 1.0f;
    if (!isShowing()) { focusT_ = 1.0f; applyLayout(); }
    else startTimerHz(30);
```

In `SeqRack::exitFocus` likewise with `focusTarget_ = 0.0f; if (!isShowing()) { focusT_ = 0.0f; applyLayout(); } else startTimerHz(30);`. Keep all the existing visibility toggles, but move `footer.setVisible(...)`/`hint.setVisible(...)` rules so they don't pop mid-animation: on enter, hide footer/hint immediately (they're covered by the growing device anyway); on exit, show them immediately (they're revealed as the grid grows). Verify in the visual pass. Delete the "There is no FLIP animation" paragraph from the `SeqRack` header comment (`SeqEditor.h:19-24`) and the matching note at `SeqEditor.cpp:19`, replacing them with a one-liner pointing at `applyLayout()`.

- [ ] **Step 3: Build + run all tests**

Run: `cmake --build juce/build -j8 && ctest --test-dir juce/build --output-on-failure`
Expected: all pass — `sq4_host_test` drives focus mode headlessly and must see snapped final bounds.

- [ ] **Step 4: Commit**

```bash
git add juce/source/seq/SeqEditor.h juce/source/seq/SeqEditor.cpp
git commit -m "feat(ux): eased focus-mode collapse animation in the SQ-4 rack"
```

---

### Task 9: Full verification pass

**Files:** none created; screenshots land in the session scratchpad, not the repo.

- [ ] **Step 1: Clean build of everything + full test suite**

Run: `cmake --build juce/build -j8 && ctest --test-dir juce/build --output-on-failure`
Expected: every target builds, every test passes.

- [ ] **Step 2: Install and eyeball the standalone**

```bash
open "$(find juce/build/FableSeq_artefacts -name '*.app' -path '*Standalone*' | head -1)"
```

Manually verify against the running web app (`npm run dev`, open the seq page) side by side:
- Michroma on titles/logo, Plex Mono everywhere else; letter-spacing intact.
- Transport/launch/stop/idle icons are crisp triangles/squares; chevrons on steppers.
- Hovering a filled cell reveals pencil + trash; hovering an empty cell reveals +; trash deletes with undo (Cmd+Z restores).
- Hint line shows the session legend with the live quant value; switches copy in focus mode.
- Entering/leaving focus animates the grid collapse (~180ms), no flicker of footer/hint.
- Focus toolbar sits on a panel, labels in Plex Mono, CREATE CLIP accent-tinted.
- WT-1/DR-1/BL-1 standalones: spot-check one of them — fonts upgraded, nothing clipped (Michroma is wider than the old bold default; if a title overflows its area, reduce that call site's font size by 1pt rather than changing layout rects).

If VST3-in-DAW verification is wanted: copy `juce/build/FableSeq_artefacts/*/VST3/*.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and rescan in the host.

- [ ] **Step 3: Fix anything found, re-run tests, commit fixes**

Each fix: smallest change, rebuild, retest, commit with a `fix(ui):` message.
