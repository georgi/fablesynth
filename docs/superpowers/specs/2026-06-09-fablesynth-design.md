# FableSynth — Wavetable Synthesizer Web App

**Date:** 2026-06-09 · **Status:** Approved (autonomous run — decisions made by Claude per user's open brief: "build a website with an amazing wavetable synthesizer, beautiful UI and capable synth engine like Serum")

## Goal

A Serum-class wavetable synthesizer that runs entirely in the browser: band-limited wavetable oscillators with morphing, unison, multimode filter, envelopes, LFOs, a mod matrix, an FX chain, presets, and a striking pro-audio UI with live 3D wavetable visualization.

## Constraints & stack

- **Zero dependencies, no build step.** Vanilla JS ES modules + Web Audio API. Served as a static site (AudioWorklet requires http://, not file://).
- Target: modern Chrome/Firefox/Safari on desktop. Degrades gracefully (Web MIDI optional).
- Performance budget: 8-voice polyphony × 2 oscillators × up to 7 unison voices in a single AudioWorkletProcessor.

## Architecture

Three layers, communicating through narrow interfaces:

1. **DSP core — `js/engine/worklet.js`** (AudioWorkletProcessor, self-contained, no imports).
   Renders all voices sample-accurately: 2 wavetable oscillators (frame-morphing, unison w/ detune + stereo spread), polyblep sub osc, white/pink noise, per-voice trapezoidal SVF filter (LP12/LP24/BP/HP/Notch) with drive, 2 ADSR envelopes (amp + mod), 2 LFOs, 4-slot mod matrix, glide, voice stealing. Receives params/notes/wavetable data via `port` messages; posts back modulated wavetable positions for live visualization (~every 2048 samples).

2. **Wavetable generation — `js/engine/wavetables.js`** (pure, main thread).
   Six procedural tables (Prime, Bloom, Pulse, Vox, Chime, Glitch), each 16 frames × 2048 samples × 9 mip levels. Frames defined as harmonic spectra (or time-domain → FFT); each mip zeroes harmonics above 1024>>m and inverse-FFTs — proper anti-aliasing across the keyboard. Full data is transferred to the worklet; 128-point-per-frame copies stay on the main thread for visualization.

3. **Main-thread engine — `js/engine/synth.js`**.
   Owns AudioContext, worklet node, and the FX graph built from native nodes: Drive (WaveShaper) → Chorus (modulated delays) → ping-pong Delay → Reverb (generated impulse Convolver), each with dry/wet bypass; master gain → safety compressor → analysers (scope + spectrum). Routes params: `fx.*`/`master.volume` to the graph, everything else to the worklet.

**Single source of truth for parameters:** `js/params.js` defines every param (id, range, curve, default, formatter). UI controls, presets, and the engine all key off it.

## UI (`index.html`, `css/style.css`, `js/ui/*`, `js/main.js`)

- **Aesthetic:** dark machined-hardware rack. Near-black base, panel gradients with noise texture, Michroma display font + IBM Plex Mono labels. Accents: cyan (Osc A), amber (Osc B), violet (filter). Power-on overlay doubles as the AudioContext-resume gesture.
- **Layout (CSS grid):** header (brand, presets, scope, spectrum, master, MIDI/voices); row 1: OSC A, OSC B, SUB/NOISE; row 2: FILTER (with response curve), AMP ENV, MOD ENV, LFO 1+2; row 3: MOD MATRIX, FX rack; footer: 4-octave keyboard + octave/glide controls.
- **Signature visual:** per-oscillator 3D wavetable terrain (canvas) — all frames drawn in perspective, the *currently playing, modulated* frame highlighted live from worklet feedback. Position slider shows a ghost marker of the modulated position.
- **Controls:** custom SVG knobs (drag/wheel/double-click-reset/arrow keys, ARIA slider), steppers for enums, LED power toggles per section.
- **Input:** on-screen keyboard (mouse/touch, Y-position velocity), computer keys (AWSEDF…, Z/X octave), Web MIDI (notes, pitch bend), Esc = panic.

## Presets

8 built-ins (Init, Velvet Pad, Acid Line, Crystal Pluck, Hyper Saw, Vowel Talk, Cathedral Bell, Neuro Wobble) + user presets in localStorage.

## Error handling

- file:// or no-AudioWorklet → overlay with instructions instead of silent failure.
- Worklet message protocol tolerant of unknown keys; param values clamped in DSP.
- Voice stealing (oldest, prefer-released) prevents unbounded CPU.

## Testing

- Node smoke test of wavetable generation (FFT correctness, no NaNs, peak normalization).
- Browser verification via CDP: console clean, screenshot review, programmatic note-on → analyser RMS > 0 confirms audio path.

## Out of scope (YAGNI)

Custom wavetable import, FM/RM between oscillators, MPE, preset file export, mobile-first layout (usable but desktop-optimized).
