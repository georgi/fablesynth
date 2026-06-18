# FABLESYNTH WT‑1

A Serum-style wavetable synthesizer that runs entirely in the browser. Built
with TypeScript, React and Zustand on top of Web Audio and an AudioWorklet DSP
core, with custom (dependency-free) UI components.

[![](docs/screenshot.png)](https://github.com/georgi/fablesynth/raw/main/docs/fablesynth.mp4)

https://github.com/user-attachments/assets/0855c756-2155-4dd3-b353-fe80abd48db9

## Run it

```sh
cd fablesynth
npm install
npm run dev
# open the printed http://localhost:5173
```

`npm run build` produces a static bundle in `dist/` (`npm run preview` serves it).
AudioWorklet requires a real HTTP origin (not `file://`), which the dev/preview
servers provide.

Click the power button (this is also the browser audio-unlock gesture) and play.

## Engine

- **2 wavetable oscillators** — 6 procedural tables (PRIME, BLOOM, PULSE, VOX,
  CHIME, GLITCH), each 16 morphable frames × 2048 samples. Tables are built from
  harmonic spectra and rendered into **9 band-limited mip levels** via inverse
  FFT, so high notes never alias (the same trick Serum uses). Mip transitions
  are **crossfaded** inside the 0.475·sr guard band, so glides and pitch bends
  never step in brightness — worst measured non-harmonic content is −92 dB:

  ![](docs/spectrum-saw.png)
- **User wavetables** — the ✎ button on either oscillator opens the wavetable
  editor. Import an audio file (single-cycle, autocorrelation pitch
  **auto-detect**, or fixed cycle length — sliced into 2048-sample frames with
  resampling) or **draw** a single cycle on a canvas. Every imported/drawn frame
  runs through the *same* FFT band-limit + 9-level mip pipeline as the procedural
  tables (`buildUserTable` in `wavetables.ts`), so they anti-alias identically.
  User tables join the registry, appear in the OSC A/B table selector, and the 3D
  view renders them unchanged. They persist in `localStorage` and are embedded in
  saved presets, so a preset is self-contained.
- **Unison** up to 7 voices per oscillator with detune + stereo spread,
  octave/semi/fine tuning, level and pan per oscillator.
- **Sub oscillator** (sine / polyblep square, −1/−2 oct) and **noise** (white/pink).
- **Dual per-voice filter** — two independent filters, each offering the Simper
  (Cytomic) zero-delay state-variable types (LP12, LP24, BP, HP, Notch) plus a
  tuned **comb** (CUTOFF sets pitch, RES sets feedback) and a **vowel/formant**
  filter (CUTOFF morphs A-E-I-O-U), all with envelope amount and key tracking.
  They route **serial** (F1→F2), **parallel** (summed) or **split** (osc A→F1,
  osc B→F2). The `tanh` drive is **anti-aliased** (first-order antiderivative
  anti-aliasing, ADAA), so pushing DRIVE on bright material doesn't fold
  harmonics back down the way a naive per-sample saturator would.
- **2 ADSR envelopes** (amp + mod), **2 LFOs** (5 shapes), **4-slot mod matrix**
  (LFOs / mod env / velocity / note → wavetable position, F1/F2 cutoff, F2
  resonance, pitch, amp, pan, osc levels).
- **8-voice polyphony** with smart voice stealing, glide, pitch-bend, and a
  per-voice DC blocker so saturation stages never see offset.
- **FX chain** — tanh drive, stereo chorus, ping-pong delay, convolution reverb
  (generated impulse), safety limiter.
- Live **3D wavetable views** show the actual modulated frame position streamed
  back from the DSP thread, plus oscilloscope, spectrum analyser and filter
  response displays.

## JUCE / VST port

A faithful C++/JUCE port of the engine lives in [`juce/`](juce/) — builds as a
**VST3 · AU · Standalone** plugin. The DSP core (oscillators, dual filter,
envelopes, LFOs, mod matrix, FX) is reimplemented one-to-one from the
AudioWorklet engine as JUCE-independent pure C++, with the same parameters and
20 factory presets. It ships a headless verification harness (wavetable
correctness, anti-aliasing floor, every filter type, FX, all presets) and a
plugin-boundary test. See [`juce/README.md`](juce/README.md) to build and verify.

## Controls

| Input | Action |
| --- | --- |
| Knobs | drag vertically · `shift` = fine · double-click = reset · scroll wheel |
| Computer keys | `A W S E D F T G Y H U J K O L P ; '` play notes · `Z`/`X` octave |
| `Esc` | panic (all notes off) |
| MIDI | plug in a controller — notes + pitch bend (Chrome/Edge) |
| On-screen keys | click/touch, vertical position = velocity, drag for glissando |

Presets: 8 factory patches via the header selector, `SAVE` stores your own in
localStorage.

## Code layout

```
src/engine/worklet.js      DSP core (AudioWorklet thread): voices, oscillators,
                           filter, envelopes, LFOs, mod matrix
src/engine/wavetables.ts   FFT + procedural wavetable/mipmap generation
                           (+ buildUserTable: shared band-limit for user tables)
src/engine/usertables.ts   user wavetables: audio import (resample, autocorr
                           cycle detect), draw mode, base64-Float32 (de)serialize
src/engine/synth.ts        AudioContext, FX graph, param routing
src/components/WavetableEditor.tsx  import / draw modal
src/params.ts              single source of truth for every parameter
src/presets.ts             factory + localStorage user presets
src/store.ts               Zustand store: param state + transport, engine glue
src/components/            knobs, steppers, sliders, keyboard, canvas displays
src/components/panels/     the rack layout (oscillators, filter, env, fx, …)
src/hooks/                 computer-keyboard + MIDI input
src/App.tsx                top-level composition
```
