# SQ-4 VST Visual Parity Pass — Design

**Date:** 2026-07-18
**Scope:** Bring the JUCE FableSeq (SQ-4) editor's design in line with the web version. Palette, layout, and grid geometry already match 1:1 (`juce/source/ui/Theme.h` transcribes the web CSS tokens; `SeqEditor.cpp` mirrors the 218px + 4-column grid). This pass closes the remaining gaps: typography, iconography, motion, hover polish, and the focus-toolbar styling. No engine, state-model, or parameter changes.

## 1. Typography (shared — affects all four plugins)

The web uses Michroma (display) and IBM Plex Mono 400/500/600 (body), loaded from Google Fonts. The JUCE side currently fakes both with default bold / default monospaced (`Theme.h` `dispFont`/`monoFont`).

- Vendor `Michroma-Regular.ttf` and `IBMPlexMono-{Regular,Medium,SemiBold}.ttf` (both SIL OFL) into `juce/assets/fonts/`, with the OFL license files alongside.
- Add a `juce_add_binary_data(FableFonts ...)` target in `juce/CMakeLists.txt`, linked into FableSynth, FableDrum, FableBass, FableSeq, and the headless test targets.
- `Theme.h`: `dispFont()` and `monoFont(h, bold)` load embedded typefaces once via static `juce::Typeface::Ptr` (`Typeface::createSystemTypefaceFor`), keeping the current system-font fallbacks if loading fails. `bold` maps to SemiBold; add a medium-weight accessor (`monoFontMedium`) for web weight-500 call sites.
- `drawSpaced` (manual letter-tracking) continues to work unchanged on top of the real fonts.

All four plugins pick up the fonts by construction (shared Theme). This is intentional and approved.

## 2. Vector glyphs replace ASCII stand-ins

Neither font reliably covers ▶ ■ ✎ 🗑, so icons are drawn, not typed: add `juce::Path`-based icon helpers to `Theme.h` — `iconPlay`, `iconStop`, `iconPassThrough` (hollow circle), `iconPencil`, `iconTrash`, `iconChevronL`, `iconChevronR`. Replace every ASCII stand-in call site:

- `SeqHeader.cpp` — PLAY/STOP text, `<`/`>` steppers.
- `SceneGridView.cpp` — `>`/`S`/`~`/`E` cell glyphs, `- M` muted tag separator.
- `TrackHeadsView.cpp` — `E` edit affordance, patch stepper chevrons.
- `SeqFooterView.cpp` — middle-dot stand-ins.
- `DeviceFocusView.cpp` — stepper chevrons.

Text labels stay font-rendered.

## 3. Motion

All effects are pure functions of `juce::Time::getMillisecondCounterHiRes()`; `SceneGridView`'s existing repaint timer becomes the shared ~30 Hz animation clock, running only while something is live/queued/stopping/animating. Idle editor = zero repaint cost.

- **Live cell** — breathing border glow (sinusoidal, ~2 s period) and an animated 3-bar EQ replacing the play icon (web `sq-live-pulse`, `sq-eq`).
- **Queued cell** — pulsing ring, ~1 s period (web `sq-qpulse`).
- **Stopping cell** — shutter overlay sweeping closed across the quantize window (web `sq-stop-close`).
- **Focus enter/exit** — `SeqRack` gains an eased 0→1 interpolation parameter (~180 ms, timer-driven relayout) replacing the instant snap; the JUCE analogue of the web's FLIP collapse. Sub-views must tolerate intermediate bounds during the transition.

## 4. Hover polish + hint line

- `SceneGridView` tracks the hovered cell via `mouseMove`/`mouseExit`. Edit/delete chips render only on the hovered filled cell; empty cells show the `+` add affordance on hover (matching web `.sq-cell` hover reveals).
- `SeqRack::hint` (currently an empty placeholder component) paints the web's contextual keyboard/mouse legend: session-mode string by default, focus-mode string in focus, swapping when a selection or drag is active.

## 5. DeviceFocusView toolbar restyle

The focus toolbar currently uses stock `ComboBox`/`Label`/`TextButton`, visually off-brand next to the hand-drawn chrome elsewhere. Restyle to `SeqHeader`'s language:

- Combos stay stock widgets but restyled via `DarkLNF` where sufficient.
- Buttons/steppers/labels become hand-drawn panel chips with `drawSpaced` labels; CREATE CLIP gets the accent-tinted treatment.
- `DeviceFocusView` gains a `paint()` that fills its toolbar strip with the standard panel treatment instead of relying on child widgets alone.

## Testing & verification

- All four plugin targets build; existing headless tests stay green (`sq4_host_test`, `plugin_host_test`, `drum_host_test`, `bass_host_test`) — embedded in-memory typefaces must not break headless runs (they should in fact remove the current CI font-coverage workaround motivation, but the ASCII fallback paths are gone, so tests must pass with the embedded fonts).
- Manual visual pass: build, install to `~/Library/Audio/Plug-Ins`, screenshot the editor in session and focus modes, compare side-by-side against the web SQ-4.

## Risks

- Font BinaryData adds ~600 KB per binary — acceptable.
- Animation timers must provably stop when idle — gate the timer on an "anything animating" predicate.
- The focus-collapse interpolation touches `SeqRack::resized()`; sub-views already handle arbitrary bounds via the scaled-rack scheme, so risk is low, but verify no view caches its bounds.

## Out of scope

- Quant automation back-wiring (host→header), onboarding tour, clip-library floating browser parity, reduced-motion preference handling, any DSP/engine work.
