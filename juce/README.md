# FableSynth WT-1 — JUCE / C++ port (VST3 · AU · Standalone)

A faithful C++/JUCE port of the FableSynth web wavetable synth. The DSP core
is reimplemented one-to-one from the AudioWorklet engine; the parameters,
presets and signal flow match the web build so a patch sounds the same.

![FableSynth plugin editor](docs/plugin_editor.png)

The live 3D wavetable terrain for both oscillators (the highlighted line is the
frame currently playing, tracked from the DSP thread):

![Wavetable views](docs/wavetable_view.png)

> Both images are rendered headlessly by `plugin_host_test` via JUCE's software
> renderer (`./plugin_host_test <output-dir>`), so they always reflect the
> current build.

## Architecture

The DSP is **JUCE-independent pure C++** so it can be unit-tested headless and
isn't coupled to the plugin framework. JUCE only provides the plugin shell
(APVTS parameters, MIDI, the editor).

```
source/dsp/Wavetables.{h,cpp}  FFT + 6 procedural tables + 9-level band-limited
                               mip pyramid + buildUserTable   (port of wavetables.ts)
source/dsp/Engine.{h,cpp}      8-voice engine: 2 morphing oscillators (unison),
                               sub + noise, dual SVF/comb/vowel filter with ADAA
                               drive, 2 ADSR, 2 LFO, 4-slot mod matrix, glide,
                               voice stealing            (port of worklet.js)
source/dsp/Fx.{h,cpp}          drive -> chorus -> ping-pong delay -> reverb ->
                               master gain -> DC block -> limiter (port of synth.ts)
source/dsp/Params.{h,cpp}      single source of truth for every parameter
                               (port of params.ts) — drives APVTS + defaults
source/dsp/Presets.{h,cpp}     20 factory presets        (port of presets.ts)
source/PluginProcessor.{h,cpp} JUCE AudioProcessor + APVTS <-> engine bridge
source/PluginEditor.{h,cpp}    the rack: scaled, pixel-faithful CSS-grid layout
source/WavetableView.{h,cpp}   live 3D wavetable terrain (port of WavetableView.tsx)
source/ui/Theme.h              palette + panel drawing (port of index.css :root)
source/ui/Controls.{h,cpp}     knob / stepper / power LED / vertical slider
source/ui/Displays.{h,cpp}     scope, spectrum, filter, env + LFO views
source/ui/Panels.{h,cpp}       osc / util / filter / env / lfo / matrix / fx / top bar
test/engine_test.cpp           headless DSP verification harness (no JUCE)
test/plugin_host_test.cpp      plugin-boundary test + PNG snapshot of the editor
```

## User interface

The editor is an exact replica of the web rack (`src/components`, `src/index.css`):
the same dark machined-hardware panels, cyan/amber/violet accents, custom rotary
knobs (−135°→135° arc, bipolar from centre), enum steppers, power LEDs, the
wavetable POS slider with modulated ghost, the mod-matrix selectors and the FX
rack. Every visualization is ported too — the live oscilloscope and spectrum
analyser (fed from a post-FX ring buffer), the filter-response curve, the ADSR
and LFO views, and the 3D wavetable terrain. The whole rack is laid out at a
fixed logical size matching the CSS grid and scaled to the plugin window, so it
stays pixel-faithful at any size.

### Mapping notes
- **Parameters** use a flat float array indexed by an enum (no string hashing in
  the audio thread). The APVTS `NormalisableRange` uses the *same* value<->norm
  curve functions as the web app, so log/int knobs map identically.
- **Oscillators / filters / envelopes / LFOs / mod matrix** are a direct port of
  `worklet.js`, including the Serum-style crossfaded mip selection, the Cytomic
  SVF, the tuned comb and the A-E-I-O-U vowel bank, and the ADAA `tanh` drive.
- **FX**: WebAudio native nodes don't exist outside the browser, so each stage is
  reimplemented. Drive is a 2x-oversampled `tanh` shaper; chorus and ping-pong
  delay are direct ports. The web build's **convolution reverb** (generated
  exponential-noise impulse) is approximated by a Freeverb network tuned by SIZE
  — a real-time-safe stand-in with an equivalent diffuse tail.

## Build

JUCE Linux deps (Debian/Ubuntu):
```sh
sudo apt-get install -y libasound2-dev libx11-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev
```

Configure + build (JUCE 8.0.4 is fetched automatically; pass
`-DJUCE_DIR=/path/to/JUCE` to use a local checkout / build offline):
```sh
cd juce
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Artifacts land in `build/FableSynth_artefacts/Release/` (`VST3/`, `Standalone/`).

## Verify the audio engine

The headless harness builds without JUCE and checks wavetable correctness, that
a note produces audio, the anti-aliasing floor, every filter type, the FX chain
and all 20 presets:
```sh
cd juce
g++ -std=c++17 -O2 -o engine_test test/engine_test.cpp \
  source/dsp/Engine.cpp source/dsp/Wavetables.cpp source/dsp/Fx.cpp \
  source/dsp/Params.cpp source/dsp/Presets.cpp
./engine_test
# or: cmake --build build --target engine_test && ctest --test-dir build
```

## Wavetable visualization
The editor shows the signature **live 3D wavetable terrain** for both
oscillators (`WavetableView`, a port of `WavetableView.tsx`): every frame drawn
in perspective with the currently-playing, modulated frame highlighted. The DSP
thread publishes the smoothed modulated frame position per oscillator via
atomics (`Engine::vizA/vizB` -> `FableAudioProcessor::getVizPos`), which the
view reads on a 30 Hz timer (falling back to the POS knob when idle). The
plugin-boundary test renders both views to a PNG headlessly to verify drawing.

## Not ported (web-only)
User-wavetable **audio import / draw** modal (the `buildUserTable` band-limit
pipeline *is* ported and ready) and the on-screen keyboard / power-on overlay —
these are browser-input concerns; the plugin receives notes from the host.
