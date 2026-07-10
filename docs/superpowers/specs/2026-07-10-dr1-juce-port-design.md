# FableSynth DR-1 — JUCE / C++ port — Design Spec

Date: 2026-07-10
Status: approved (brainstorm with Matti, 2026-07-10)
Predecessors: `2026-07-10-dr1-drum-machine-design.md` (the web instrument, the
lockstep reference) and the WT-1 JUCE port (`juce/`, the structural template).

## Summary

Port the DR-1 drum machine to a second JUCE plugin — **"FableSynth DR-1"**
(AU · VST3 · Standalone) — in the existing `juce/` CMake project, following the
WT-1 port's architecture: a JUCE-free pure-C++ DSP core reimplemented
one-to-one from `src/drum/engine/worklet-drum.js`, a thin APVTS/processor
shell, and a pixel-faithful editor built from the shared `ui/` components.

Decisions made during brainstorming:

- **Multi-out is real in v1:** 5 stereo output buses (MAIN + AUX 1–4), per-pad
  routing via `pad.out`; master FX applies to MAIN only, AUX buses are dry.
- **Host sync = tempo only:** BPM follows the host `AudioPlayHead` when
  available (readout shows SYNC and locks); play/stop remains DR-1's own
  transport button. No bar-position locking in v1. Standalone uses the
  internal clock.
- **Drop-WAV onto pads is in scope:** JUCE file drag-and-drop into the
  already-ported `UserTables::buildUserTable` pipeline, assigned to that
  pad's OSC A and serialized in host state.
- **Packaging:** second `juce_add_plugin` target in `juce/CMakeLists.txt`,
  sharing DSP/UI sources with WT-1 by direct source inclusion (no library
  extraction). Plugin code `Fdr1`, manufacturer `Fabl`.
- **Kits follow the WT-1 preset precedent:** 3 factory kits in code + full
  session state via `get/setStateInformation`; the web's SAVE-to-localStorage
  is replaced by the DAW's preset system. No in-plugin user-kit files.

## File layout

```
juce/source/drum/
  DrumProcessor.{h,cpp}    JUCE AudioProcessor: APVTS bridge, 5 stereo buses,
                           MIDI 36–51 → pad triggers, host-tempo sync, state
  DrumEditor.{h,cpp}       the rack: fixed logical size scaled to the window,
                           pixel-faithful to DrumApp.tsx / drum.css
  ui/PadGrid.{h,cpp}       4×4 grid (pad 01 bottom-left), hit LEDs, selection
                           ring, click = select + audition, QWERTY map, WAV drop
  ui/PadStrip.{h,cpp}      CHOKE/OUT steppers, LVL/PAN/V→LVL/V→MOD knobs
  ui/DrumPanels.{h,cpp}    OSC A/B, NOISE, PITCH ENV, AMP ENV, FILTER, MOD —
                           bound to the selected pad (attachment swapping)
  ui/StepSeqView.{h,cpp}   play/stop, A–D, CHAIN builder + readout, 16 step
                           buttons (tap cycles on→accent→off), amber playhead
  ui/DrumFxRack.{h,cpp}    DRIVE/COMP/CHORUS/DELAY/REVERB groups + OUT panel
                           (real routing summary)
  dsp/DrumEngine.{h,cpp}   1:1 port of worklet-drum.js — JUCE-free
  dsp/DrumFx.{h,cpp}       drive→comp→chorus→delay→reverb — JUCE-free
  dsp/DrumParams.{h,cpp}   port of src/drum/params.ts — single source of truth
  dsp/DrumTables.{h,cpp}   THUD/CRACK/TINE/GRIT spectra (port of drumtables.ts)
  dsp/DrumKits.{h,cpp}     TR-VOID / ROOM ONE / BITCRUSH (port of kits.ts)
juce/test/drum_engine_test.cpp   headless DSP harness (no JUCE)
juce/test/drum_host_test.cpp     plugin-boundary test + editor PNG snapshot
```

Reused directly (compiled into both plugin targets, not copied):
`dsp/Wavetables` (FFT + 9-mip pipeline + the six existing tables),
`dsp/UserTables` (WAV import), `dsp/Fx.h` primitives (Smooth, DelayLine,
Biquad, Freeverb comb/allpass), `ui/Theme.h`, `ui/Controls`, `ui/Displays`,
`WavetableView`. WT-1 sources are untouched except where a helper needs to
become shared-visible (header include only, no behavior change).

## Build

`juce/CMakeLists.txt` gains:

- `juce_add_plugin(FableDrum PRODUCT_NAME "FableSynth DR-1" PLUGIN_CODE Fdr1
  FORMATS AU VST3 Standalone IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE …)` listing
  the `source/drum/` sources plus the shared sources above, with the same
  compile definitions and the same macOS ad-hoc re-sign post-build step.
- `drum_engine_test` as a plain (JUCE-free) executable + ctest entry,
  buildable with bare `g++` like `engine_test`.
- `drum_host_test` as a `juce_add_console_app` compiling the drum plugin
  sources, + ctest entry.

## Parameter model

`DrumParams` ports `src/drum/params.ts`: one definition table drives the flat
float array (enum-indexed, no string hashing on the audio thread), the APVTS
layout, defaults, and formatting. `NormalisableRange` uses the same
value↔norm curve functions as the web app so log/int knobs map identically.

All per-pad params are real automatable APVTS parameters: 16 pads × 48
params (`pad<i>.oscA.*` ×8, `oscB.*` ×8, `noise.*` ×2, `penv.*` ×2,
`aenv.*` ×4, `flt.*` ×5 incl. `flt.on`, `mod1..4.*` ×12, `modenv.dec`,
`lvl/pan/v2l/v2m/choke/out` ×6) + 3 globals (`master.volume`,
`master.swing`, `seq.bpm`) + 17 `fx.*` = **788 parameters**, organized in
one APVTS group per pad so hosts display a sane tree.

`pad.out` values: 0 = MAIN, 1–4 = AUX 1–4 (now real routing).
`seq.bpm` is host-overridden while a host playhead reports tempo.

Non-param state, serialized in `get/setStateInformation` alongside the APVTS
tree: patterns (4 patterns × 16 pads × 16 steps, byte-packed), chain list,
pad names, selected pad, and imported user tables (same serialization scheme
as WT-1 user tables).

## Engine (`DrumEngine`, JUCE-free)

One-to-one port of `worklet-drum.js`, keeping its constants and block
structure so kits sound identical:

- **16 PadVoices**, mono, retrigger restarts. Trigger resets phases (PHASE
  param sets start phase), filter state, DC blockers, noise state.
- **Oscillators:** band-limited wavetable playback with Serum-style
  crossfaded mip selection (the `W = 0.07` blend window), fixed base pitch
  (note 60 + TUNE + FINE), unison 1–7 with ±50 ct detune spread and 0.6
  stereo spread, level squared with 0.32/√uni normalization. Params
  re-evaluated every 16-sample subblock.
- **Noise:** white → one-pole tilt (COLOR −1..1 → coefficient
  0.02 + (c+1)·0.49), level squared × 0.35.
- **Pitch env:** `amt · e^(−4.5·t/dec)` semitones added to both oscillators.
- **Amp env:** one-shot AHD; DECAY morphs linear→exponential (e^(−4.5·td/dec))
  by CURVE; voice auto-kills past the envelope end below 1e−4.
- **Filter:** Cytomic SVF (LP12; LP24 = second cascaded pass; BP12; HP12;
  NOTCH) with 0.5-coefficient cutoff smoothing, `k = 2 − 1.93·res`, preceded
  by the ADAA `lcosh` tanh drive (gain 1 + drive·7, 0.55-power compensation).
- **Mod matrix:** 4 slots; sources — / MOD ENV (exp decay, `modenv.dec`/4.5) /
  VELO·v2m / RAND (sampled at trigger); destinations A/B POS, LEVEL, CUTOFF
  (±5 octaves log), PITCH (±24 st), A/B FINE (±200 ct), NOISE LVL, RES.
  RAND uses a seeded xorshift PRNG (not `rand()`) so tests are deterministic.
- **Choke:** trigger hard-chokes same-group voices (multiplicative 0.12 fade).
- **Per-voice DC blocker** (R = 0.9998), velocity gain `1 − v2l·(1 − vel)`,
  pad level squared, equal-power pan.

### Multi-out routing

`DrumEngine::process` renders into **five stereo pairs**; each pad
accumulates into the pair selected by `pad.out`. The processor maps these to
the declared buses: MAIN (bus 0, default layout stereo) + AUX 1–4 (stereo,
disableable). Master FX and the scope tap apply to MAIN only; AUX output is
post pad level/pan, dry. Hosts that connect only MAIN hear every factory kit
unchanged (all factory pads default to MAIN).

### Sequencer (inside DrumEngine, sample-accurate)

Port of the worklet clock: 16 steps of 16ths, `dur = 60/bpm/4 · sr`, swing
delays every odd 16th by up to `0.667 · swing · dur` (the web's
`dur − offNow + offNext` formula), accent = 1.0 / plain = 0.72 velocity,
chain advances at bar wrap (with the web's OOB clamp fix), steps fire
mid-block by splitting the render loop exactly as `worklet-drum.js` does.
Engine publishes current step / pattern / per-pad hit flashes and the
selected pad's `posA/posB/env` viz via atomics (WT-1 `vizA/vizB` pattern).

**Host tempo:** each `processBlock` reads `AudioPlayHead::getPosition()`;
when BPM is reported it overrides `seq.bpm` (clamped 60–200) and the UI shows
SYNC with the readout locked. Play/stop is DR-1's own button; host transport
start/stop and PPQ position are ignored in v1 (follow-up). Standalone always
runs the internal clock.

**MIDI:** notes 36–51 → pads 1–16 with note velocity, through the same
trigger path (choke, v2l/v2m) as the sequencer. No other MIDI handling.

## FX chain (`DrumFx`, JUCE-free)

Stage-by-stage port of `drum-synth.ts`'s Web Audio graph, reusing `Fx.h`
primitives; every stage keeps the per-stage ON switch and the equal-power
sin/cos wet/dry mix:

1. **DRIVE** — 2×-oversampled tanh shaper, `k = 1 + amt·24`, pre-gain
   `1 + amt·2`, tanh(k)-normalized (same curve as WT-1's drive). AMT, MIX.
2. **COMP** — new C++ stage matching `DynamicsCompressorNode` semantics:
   ratio 4:1, knee 9 dB, attack 3 ms, release 250 ms, params THRESH
   (−60..0 dB) and MAKEUP (dB → linear). Feedforward peak detector with
   smooth knee; mix stage fixed at 100 % wet when ON (as in the web build).
3. **CHORUS** — dual modulated delays 12/17 ms, one LFO, depth
   `0.0008 + depth·0.0045` with the second path at −0.8×, RATE/DEPTH/MIX
   (mix scaled ×0.8).
4. **DELAY** — ping-pong, cross-feedback with 4.5 kHz lowpass damping in the
   L→R path, TIME (both lines), FDBK, MIX (×0.85).
5. **REVERB** — Freeverb network tuned by SIZE (the WT-1 stand-in for the
   web's rendered exponential-noise impulse; SIZE maps to room size + tail
   length), MIX (×0.9).
6. **Master** — volume (squared ×1.6), DC-block highpass @ 8 Hz, limiter
   (−8 dB threshold, knee 4, ratio 14:1, 2 ms/220 ms), post-FX ring buffer
   feeding the header scope.

## Drum wavetables & kits

- `DrumTables` ports the four `drumtables.ts` harmonic spectra — THUD, CRACK,
  TINE, GRIT — rendered through the shared `Wavetables` FFT band-limit +
  9-mip pipeline. Drum table list = these 4 + the existing 6 (PRIME, BLOOM,
  PULSE, VOX, CHIME, GLITCH) **in the same order as the web** so kit table
  indices line up.
- `DrumKits` ports `kits.ts` verbatim: **TR-VOID** (default), **ROOM ONE**,
  **BITCRUSH** — each a full snapshot of pad params, names, patterns, chain,
  FX and BPM/swing. Kit stepper in the header applies them onto the APVTS
  (WT-1 `applyPreset` pattern).

## UI (`DrumEditor` + `source/drum/ui/`)

Pixel-faithful to the web `DrumApp` (drum.css tokens are already Theme.h's
palette), fixed logical size scaled to the plugin window:

- **Header:** wordmark, kit stepper, scope canvas (post-FX ring buffer),
  MIDI-activity LED, BPM readout (drag to edit; shows SYNC and locks when
  host tempo is present), SWING + VOL knobs. The web's PADS/STEP mode toggle
  and power overlay are dropped (host provides MIDI and audio start — WT-1
  precedent).
- **PadGrid:** 4×4, pad 01 bottom-left; tiles show number, hit LED, name,
  choke/out tag, cyan selection ring. Click = select + audition.
  QWERTY map `1234/qwer/asdf/zxcv` when the editor has keyboard focus.
  `FileDragAndDropTarget`: dropping a WAV on a pad imports it via
  `UserTables::buildUserTable` into that pad's OSC A.
- **PadStrip:** CHOKE + OUT steppers, LVL / PAN / V→LVL / V→MOD knobs.
- **Editor panels:** OSC A (cyan) / OSC B (amber) — table stepper,
  `WavetableView` terrain with live POS, POS slider, 6 knobs; NOISE — color
  response view, COLOR + LVL; PITCH ENV, AMP ENV (env views animate on
  hits via the env viz atomic), FILTER (type stepper + response curve),
  MOD (4 src▸dst selector rows + amount knobs). All panels edit the
  **selected pad** by swapping APVTS attachments on selection change.
- **StepSeqView:** play/stop, A–D pattern buttons, CHAIN toggle (letters
  build the chain while on; readout shows A→B→…), 16 step buttons in groups
  of 4, tap cycles on→accent→off, amber playhead ring, editing-pad label.
- **DrumFxRack:** DRIVE / COMP / CHORUS / DELAY / REVERB knob groups + OUT
  summary panel showing the real pad→bus routing.

## Testing

Two harnesses, mirroring WT-1:

- **`drum_engine_test.cpp`** (headless, no JUCE, plain `g++`): drum-table
  spectra correctness + aliasing floor; trigger produces audio; accent >
  plain output level; choke silences group peers within the fade window;
  pitch-env sweep measurable; AHD envelope timing and curve morph; every
  filter type; mod-matrix routing (each destination moves the right thing);
  swing/step timing sample-exact against the `dur − offNow + offNext`
  formula; chain advance at bar wrap; per-bus routing (pad→AUX renders to
  the right pair, silent on MAIN); all 3 factory kits render a bar with no
  NaN/denormal; FX chain stages (comp reduces crest factor, delay/reverb
  tails decay).
- **`drum_host_test.cpp`** (real processor): APVTS parameter count/ranges
  match `DrumParams`; state round-trip incl. patterns, chain, names and an
  imported user table; multi-bus layout accepted (5 stereo out) and a pad
  assigned to AUX 2 is audible on bus 2 and absent from MAIN; host tempo
  from a mock `AudioPlayHead` drives the step clock; MIDI note 36 triggers
  pad 1; editor renders headlessly to a PNG snapshot for the README.
- **Parity:** engine tests assert the web worklet's constants and formulas
  (velocities, swing math, choke fade, env shapes) — the web build is the
  lockstep reference, as with WT-1.

Manual verification: build, install per the usual flow
(`~/Library/Audio/Plug-Ins`), load in a host, program a beat on TR-VOID,
check SYNC against host tempo, route a pad to AUX in a multi-out-capable
host, drop a WAV on a pad, save/reload the session.

## Docs

`juce/README.md` gains a DR-1 section (architecture table, build/test
commands, editor snapshot from `drum_host_test`); the root `README.md` links
the plugin next to the web DR-1.

## Out of scope (follow-ups)

- Full host-transport lock (play/stop + PPQ bar-position sync, loop/jump
  handling).
- MIDI clock / MIDI start-stop, pattern switching via program change.
- Song mode beyond the 4-pattern chain (unchanged from web).
- Sample playback pads (DR-1 stays synthesis-only).
- Convolution reverb parity (Freeverb stand-in accepted, as in WT-1).
