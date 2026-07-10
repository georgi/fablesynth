# FableSynth DR-1 — Drum Machine (web) — Design Spec

Date: 2026-07-10
Status: approved (brainstorm with Matti, 2026-07-10)
Source design: Claude Design project "Synthesizer mockup design" → `FableSynth DR-1.dc.html`
(+ `FableKnob.dc.html`), imported 2026-07-10.

## Summary

DR-1 is a 16-pad wavetable drum machine shipped as a third surface of the
FableSynth web build. Every pad is a small synthesizer voice (2 wavetable
oscillators + noise, pitch envelope, one-shot AHD amp envelope, SVF filter,
4-slot mod matrix), driven by a sample-accurate 16-step sequencer with accents,
4 chainable patterns, choke groups, and a master FX rack. v1 is the **web app
only** — a full working instrument, not a mockup. The JUCE port is a separate
follow-up project.

Decisions made during brainstorming:

- **Target:** web app first (JUCE later, out of scope here).
- **Integration:** new Vite entry `drum/index.html` — its own SPA beside
  the WT-1 synth app; WT-1 code stays untouched.
- **Scope:** full instrument in v1. Aux-out routing is UI-only (browser has
  one stereo output).
- **Engine:** dedicated self-contained `worklet-drum.js` (not an extension of
  WT-1's `worklet.js`), borrowing its proven DSP primitives; sequencer clock
  lives in the audio thread.

## File layout

```
drum/index.html              third Vite entry ("FABLESYNTH DR-1")
src/drum/
  main.tsx                   entry, mounts DrumApp
  DrumApp.tsx                layout: header / pads+editor / step seq / FX rack
  params.ts                  canonical drum param defs (per-pad + global)
  kits.ts                    factory kits + localStorage user kits
  store.ts                   Zustand store, params-as-truth, setParam → engine
  engine/
    drum-synth.ts            main thread: AudioContext, worklet node, FX graph
    worklet-drum.js          self-contained worklet: 16 pad voices + seq clock
  components/                PadGrid, PadStrip, OscSection, NoiseSection,
                             PitchEnvPanel, AmpEnvPanel, FilterSection,
                             ModPanel, StepSeq, FxRack, OutPanel, Header
```

Reused from WT-1 (imported, not copied): `Knob.tsx`, `Stepper.tsx`, display
canvases (`WavetableView`, `EnvView`, `FilterView`, `ScopeView` — adapted via
props), `engine/wavetables.ts` generation pipeline, `engine/usertables.ts`
import pipeline, `PowerOverlay`, `index.css` design tokens.

Untouched: WT-1's `worklet.js`, `synth.ts`, `params.ts`, `store.ts`, app.

`vite.config.ts` gains the third input. The landing page links to DR-1.

## Parameter model

One flat `ParamValues` map, same discipline as WT-1 (`src/params.ts`): every
knob, kit, and engine message keys off one definition table.

Per-pad params are namespaced `pad<i>.<field>` (i = 0..15):

| Group | Params (curve, range, default per mockup model) |
| --- | --- |
| OSC A/B | `oscA/B.table` (int), `.pos` (lin 0–1), `.tune` (int ±48 st), `.fine` (int ±100 ct), `.phase` (lin 0–1), `.unison` (int 1–7), `.detune` (lin 0–1), `.level` (lin 0–1) |
| NOISE | `noise.color` (lin −1..1, tilt dark↔bright), `noise.level` |
| PITCH ENV | `penv.amt` (int ±48 st), `penv.dec` (log 5 ms–2 s) |
| AMP ENV | `aenv.att` (log 0.5 ms–0.5 s), `aenv.hold` (lin 0–250 ms), `aenv.dec` (log 5 ms–4 s), `aenv.curve` (lin 0–1, lin→exp decay morph) |
| FILTER | `flt.type` (int, LP12/LP24/BP12/HP12/NOTCH), `flt.cut` (log 20–20k), `flt.res` (lin), `flt.drive` (lin) |
| MOD | `mod1..4.src` (int), `.dst` (int), `.amt` (lin ±1); `modenv.dec` (log 5 ms–2 s) |
| PAD | `pad.lvl`, `pad.pan`, `pad.v2l` (velo→level), `pad.v2m` (velo→mod), `pad.choke` (int 0=off,1–4), `pad.out` (int 0=MAIN,1–4=AUX; UI-only routing) |

Globals: `master.volume`, `master.swing` (0–1 → up to ~66% off-16th delay),
`seq.bpm` (int 60–200).

Mod sources: `— / MOD ENV / VELO / RAND`. Mod destinations: `— / A POS /
B POS / LEVEL / CUTOFF / PITCH / A FINE / B FINE / NOISE LVL / RES`.
VELO and RAND are sampled at trigger; MOD ENV runs per block.

Non-param state (store + kit, not ParamValues): pad names, pattern data,
chain list, selected pad, transport.

## Engine

### Drum voice (worklet, one per pad, mono — retrigger restarts)

- **Oscillators:** band-limited wavetable playback with mip crossfade,
  borrowed from `worklet.js`. Fixed base pitch per pad (TUNE/FINE off a C3
  reference), phase reset on trigger (PHASE param sets start phase), unison
  1–7 with detune + stereo spread as in WT-1.
- **Noise:** white source through a one-pole tilt filter (COLOR).
- **Pitch env:** instant attack, exponential decay, adds `amt·e^(−t/dec)`
  semitones to both oscillators.
- **Amp env:** one-shot AHD; CURVE morphs decay shape linear→exponential;
  no gate/sustain.
- **Filter:** Simper SVF (LP12/LP24/BP12/HP12/NOTCH) + ADAA tanh drive,
  same math as `worklet.js`.
- **Choke:** trigger hard-chokes other pads in the same group (~5 ms fade).
- **Output:** all pads mix to one stereo bus (AUX assignment is displayed
  but not routed in the browser).

### Sequencer (in the worklet, sample-accurate)

- 16 steps of 16ths; BPM 60–200; swing delays every off-16th.
- 4 patterns (A–D) × 16 pads × 16 steps; step state off/on/accent
  (accent velocity 1.0, plain 0.72 — matches mockup's tap cycle
  on → accent → off).
- Chain: ordered pattern list (e.g. A→A→B), advanced at bar boundaries.
- Worklet → UI messages per step (`{step, pattern}`) drive the playhead and
  pad LED flashes. Live triggers (click/keys/MIDI) use the same trigger path
  with velocity.

### FX chain (main thread, `drum-synth.ts`, Web Audio graph like `synth.ts`)

worklet → **DRIVE** (WaveShaper; AMT, MIX) → **COMP**
(DynamicsCompressorNode; THRESH, MAKEUP) → **CHORUS** (dual modulated delays,
WT-1 topology; RATE, DEPTH, MIX) → **DELAY** (stereo; TIME, FDBK, MIX) →
**REVERB** (WT-1 scheme; SIZE, MIX) → master VOL → destination.
AnalyserNode taps the master for the header scope.

### Drum wavetables

Four new procedural tables — **THUD, CRACK, TINE, GRIT** — defined as harmonic
spectra and rendered through the existing FFT band-limit + 9-mip pipeline in
`wavetables.ts` (same anti-aliasing guarantees). Drum table list = these four
+ the existing six (PRIME, BLOOM, PULSE, VOX, CHIME, GLITCH). Drop-WAV onto a
pad imports through `usertables.ts` into that pad's OSC A.

## UI

Pixel-faithful to `FableSynth DR-1.dc.html` (colors/typography already match
DESIGN.md tokens; knobs are WT-1's `Knob`, which the FableKnob mockup mirrors).

- **Header:** wordmark, kit stepper + SAVE, STEP/PADS mode toggle, scope
  canvas, MIDI LED, BPM readout (drag to edit; SYNC label), SWING + VOL knobs.
- **Pad grid:** 4×4, pad 01 bottom-left; tiles show number, hit LED, name,
  choke/out tag; cyan selection ring. Click = select + audition. PADS mode:
  performance surface; keys `1234/qwer/asdf/zxcv` map to the grid; MIDI notes
  36–51 trigger pads 1–16. Drag-and-drop an audio file onto a pad → wavetable
  import.
- **Pad strip:** CHOKE + OUT steppers, LVL / PAN / V→LVL / V→MOD knobs.
- **Editor row:** OSC A (cyan) and OSC B (amber) panels — table stepper,
  terrain canvas with live POS, POS slider, 6 knobs; NOISE panel — type
  readout, noise canvas, COLOR + LVL. Below: PITCH ENV, AMP ENV, FILTER
  (type stepper + response canvas), MOD (4 src▸dst rows + amount knobs).
  All panels edit and visualize the selected pad, animating on hits.
- **Step sequencer:** play/stop, A–D buttons, CHAIN toggle (while on,
  clicking letters builds the chain; readout shows A→B→…), 16 step buttons
  grouped in 4s, tap cycles on→accent→off, amber playhead ring, editing-pad
  label.
- **FX rack:** DRIVE / COMP / CHORUS / DELAY / REVERB knob groups + OUT
  summary panel (informational routing list).
- **Power-on:** `PowerOverlay` gates audio start behind a user gesture.

## Kits

Kit = 16 pads' params + names + patterns + chain + FX + BPM/swing (+ any
imported user tables, serialized like WT-1 user tables). JSON in
localStorage, factory kits in `kits.ts` — ship exactly three: **TR-VOID**
(the mockup's kit, default), **ROOM ONE** (acoustic-leaning), **BITCRUSH**
(glitch). Same stepper + SAVE flow as WT-1 presets.

## Testing

- `src/drum/params.test.ts` — defaults, ranges, curve math, formatting.
- `src/drum/kits.test.ts` — kit serialize/load round-trip, factory kits valid.
- `src/drum/seq.test.ts` — step timing incl. swing, accent cycle, chain
  advance at bar boundaries (pure functions extracted for testability).
- `src/drum/engine/worklet-drum.test.ts` — offline-render smoke tests in the
  style of `worklet.parity.test.ts`: trigger produces audio; choke silences
  group peers; accent > plain level; pitch env sweeps; no NaN/denormal output.
- Manual: chrome-debug drive of the built app (power on, program a beat,
  tweak a pad, save/reload kit).

## Out of scope (follow-ups)

- JUCE/C++ port of DR-1 (own spec; the web worklet is written to be the
  lockstep reference, mirroring how WT-1 evolved).
- Real multi-out (AUX) routing — meaningful only in the plugin.
- Sample playback pads (DR-1 is synthesis-only by design).
- Song mode beyond the simple pattern chain.
