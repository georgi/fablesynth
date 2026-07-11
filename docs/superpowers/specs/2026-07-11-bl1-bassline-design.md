# FableSynth BL-1 — Bassline Synth (web) — Design Spec

Date: 2026-07-11
Status: draft (proposed)

## Summary

BL-1 is a monophonic wavetable bassline synthesizer with an acid-style pitched
step sequencer — the third instrument in the family after WT-1 (poly wavetable
synth) and DR-1 (drum machine). One morphing wavetable oscillator with up to
7-voice unison, a sub oscillator, a single Simper SVF with ADAA drive, 303-style
**accent** and **slide** per step, a tempo-synced wobble LFO, and a compact FX
chain. Like DR-1, v1 is the **web app only** — a full working instrument; the
JUCE port is a separate follow-up project.

Positioning: WT-1 covers pads and leads, DR-1 covers drums; BL-1 covers the
bassline. The design target is "program a moving, sliding, accented bass groove
within a minute" — the sequencer is the front door, not a side panel.

Scope decisions (mirroring the DR-1 precedent):

- **Target:** web app first (JUCE later, out of scope here).
- **Integration:** new Vite entry `bass/index.html` — its own SPA beside WT-1
  and DR-1; existing code stays untouched.
- **Engine:** dedicated self-contained `worklet-bass.js`, borrowing the proven
  DSP primitives from `worklet.js` (wavetable playback + mips, Simper SVF,
  ADAA drive, glide) and the sequencer clock scheme from `worklet-drum.js`
  (clock lives in the audio thread, sample-accurate).
- **Voice model:** strictly monophonic, last-note priority — mono is the
  instrument's identity (slides only make sense mono), not a limitation.

## File layout

```
bass/index.html              fourth Vite entry ("FABLESYNTH BL-1")
src/bass/
  main.tsx                   entry, mounts BassApp
  BassApp.tsx                layout: header / osc+filter+env row / pitch seq / FX
  params.ts                  canonical param defs
  patches.ts                 factory patches + localStorage user patches
  seq.ts                     pure sequencer data model + timing math
  store.ts                   Zustand store, params-as-truth, setParam → engine
  engine/
    bass-synth.ts            main thread: AudioContext, worklet node, FX graph
    worklet-bass.js          self-contained worklet: mono voice + seq clock
  components/                OscSection, SubSection, FilterSection, EnvSection,
                             LfoSection, PitchSeq, FxRack, Header
```

Reused from WT-1/DR-1 (imported, not copied): `Knob.tsx`, `Stepper.tsx`,
display canvases (`WavetableView`, `FilterView`, `ScopeView`, `EnvView` —
adapted via props), `engine/wavetables.ts` generation pipeline,
`engine/usertables.ts` import pipeline, `PowerOverlay`, `index.css` design
tokens. Timing constants and helpers in `src/bass/seq.ts` follow
`src/drum/seq.ts` (`stepDurSamples`, `swingDelaySamples`, chain advance) with
the step payload widened from a `StepVal` byte to a per-step struct.

Untouched: WT-1's and DR-1's worklets, params, stores, apps.

`vite.config.ts` gains the fourth input. The landing page links to BL-1.

## Parameter model

One flat `ParamValues` map, same discipline as WT-1 (`src/params.ts`). No
per-pad namespacing — BL-1 is one voice, so params are flat:

| Group | Params (curve, range, default) |
| --- | --- |
| OSC | `osc.table` (int, 10 tables: PRIME, BLOOM, PULSE, VOX, CHIME, GLITCH + THUD, CRACK, TINE, GRIT), `osc.pos` (lin 0–1), `osc.tune` (int ±24 st), `osc.fine` (int ±100 ct), `osc.unison` (int 1–7), `osc.detune` (lin 0–1), `osc.spread` (lin 0–1), `osc.level` (lin 0–1) |
| SUB | `sub.shape` (int, SINE/SQUARE), `sub.oct` (int −1/−2), `sub.level` (lin 0–1) |
| FILTER | `flt.type` (int, LP12/LP24/BP12/HP12), `flt.cut` (log 20–20k), `flt.res` (lin 0–1), `flt.drive` (lin 0–1), `flt.env` (lin ±1), `flt.track` (lin 0–1) |
| FILTER ENV | `fenv.att` (log 0.5 ms–0.5 s, def ~1 ms), `fenv.dec` (log 5 ms–4 s) |
| AMP ENV | `aenv.att` (log 0.5 ms–0.5 s), `aenv.dec` (log 5 ms–4 s), `aenv.sus` (lin 0–1), `aenv.rel` (log 5 ms–2 s) |
| ACCENT | `acc.amt` (lin 0–1) — one macro: scales level boost, filter-env boost and decay snap on accented steps |
| SLIDE | `slide.time` (log 10–500 ms, def 60 ms) — glide time for slid steps |
| LFO | `lfo.rate` (synced stepper: 1/1 … 1/32, incl. dotted/triplet), `lfo.depth` (lin 0–1, → cutoff), `lfo.shape` (int, SINE/TRI/SAW/SQUARE/S&H) |
| FX | `drive.amt/.mix`, `chorus.rate/.depth/.mix`, `delay.time/.fdbk/.mix`, `reverb.size/.mix` |
| MASTER | `master.volume`, `master.swing` (0–1, same SWING_MAX = 0.667 math as DR-1), `seq.bpm` (int 60–200) |

Non-param state (store + patch, not `ParamValues`): pattern data, chain list,
transport, selected step, imported user table.

## Engine

### Mono voice (worklet)

- **Oscillator:** band-limited wavetable playback with 9-mip crossfade,
  borrowed from `worklet.js` — this is what keeps slides clean, the existing
  mip-crossfade guard band means glides never step in brightness. Unison 1–7
  with detune + stereo spread (reese basses). Table list = all 10 procedural
  tables; drop-WAV / draw imports through `usertables.ts` replace the table.
- **Sub:** sine or polyblep square at −1/−2 oct, pre-filter, same as WT-1's
  sub path.
- **Filter:** one Simper SVF + ADAA tanh drive, same math as `worklet.js`.
  Filter env (AD, exponential decay) with bipolar amount + key tracking.
- **Envelopes:** filter env is AD (attack near-instant by default); amp env is
  full ADSR so held/tied steps sustain. Retrigger behavior: a new *non-slid*
  step retriggers both envs; a *slid* step retriggers neither (envelopes ride
  through — the 303 rule).
- **Accent (the 303 macro):** an accented step, scaled by `acc.amt`, gets
  +level (up to +6 dB into the drive), +filter-env amount (up to +40%), and a
  shortened filter decay (down to ×0.6). One knob, three coupled effects —
  matches the "no manual" product principle.
- **Slide:** a step flagged SLIDE glides pitch from the previous step over
  `slide.time` using the existing glide implementation (`worklet.js` lastPitch
  scheme) and suppresses env retrigger. Live playing uses the same path:
  overlapping (legato) notes slide, detached notes retrigger.
- **LFO:** tempo-synced from the sequencer clock, hard-wired to cutoff
  (`lfo.depth` bipolar around the knob position) — the wobble control. Phase
  resets at pattern start so wobbles lock to the bar.

### Sequencer (in the worklet, sample-accurate)

- 16 steps of 16ths; BPM 60–200; swing on off-16ths (constants shared with
  `drum/seq.ts` — extract the common timing helpers only if trivially clean,
  otherwise mirror + parity-test like the drum worklet does).
- Per step: `{ on, note (0–12 within octave), oct (−1/0/+1), accent, slide }`.
  Slide on step *n* means "slide **into** step n from step n−1" and also ties
  n−1 through (no gap) — one flag, matching TB-303 semantics.
- 4 patterns (A–D) + chain, advanced at bar boundaries — same model and UI
  contract as DR-1.
- Worklet → UI messages per step drive the playhead; live keyboard/MIDI input
  plays the voice directly (transport stopped) or is ignored while running
  (v1 keeps it simple).

### FX chain (main thread, `bass-synth.ts`, Web Audio graph like `synth.ts`)

worklet → **DRIVE** (WaveShaper; AMT, MIX) → **CHORUS** (WT-1 topology) →
**DELAY** (stereo ping-pong; TIME synced to BPM divisions, FDBK, MIX) →
**REVERB** (WT-1 scheme; SIZE, MIX) → master VOL → destination. AnalyserNode
taps the master for the header scope. No compressor — accent dynamics are the
point; squashing them defeats the instrument.

## UI

Same design language as WT-1/DR-1 (DESIGN.md tokens, `Knob`, uppercase labels,
units shown). Accent color for BL-1 surfaces: **green** (cyan = osc A, amber =
osc B, violet = filter are taken; the bassline lane needs its own identity).

- **Header:** wordmark ("FABLESYNTH BL-1"), patch stepper + SAVE, scope canvas,
  MIDI LED, BPM readout (drag to edit), SWING + VOL knobs — the DR-1 header
  layout with the kit stepper swapped for patches.
- **Editor row:** OSC panel (table stepper, terrain canvas with live POS, POS
  slider, tune/fine/unison/detune/spread/level knobs, ✎ import/draw button) ·
  SUB panel · FILTER panel (type stepper, response canvas, CUT/RES/DRIVE/ENV/
  TRACK) · ENV panel (filter AD + amp ADSR, EnvView canvas) · LFO panel
  (rate stepper, shape, depth) · ACCENT/SLIDE panel (AMT + TIME knobs).
- **Pitch sequencer (the centerpiece, full width):** 16 columns. Each column:
  a one-octave mini pitch lane (12 semitone cells, drag/click to set note),
  OCT −/0/+ tap, and ACCENT + SLIDE toggle cells under it. Empty column =
  rest. Playhead ring in green; accented steps render brighter; slid steps
  draw a connecting line to the previous column so the pitch contour reads at
  a glance ("show the signal"). A–D pattern buttons + CHAIN toggle exactly as
  DR-1. RAND button seeds a musical pattern (root-biased note pool, ~60%
  density, occasional accents/slides) — the fast path to "inspiring without a
  manual".
- **Keyboard:** WT-1's on-screen keyboard strip (2 octaves) for auditioning
  the patch when the transport is stopped; computer-key + MIDI input as WT-1.
- **FX rack:** DRIVE / CHORUS / DELAY / REVERB knob groups.
- **Power-on:** `PowerOverlay` gates audio start behind a user gesture.

## Patches

Patch = all params + patterns + chain + BPM/swing (+ serialized user table).
JSON in localStorage, factory patches in `patches.ts` — ship four: **ACID
LINE** (default: squelchy LP24 + accents/slides), **REESE** (7-voice unison
detune, slow LFO), **SUB ROLLER** (sine sub-heavy, tight decay), **WOBBLE**
(1/4-dotted S&H LFO showcase). Same stepper + SAVE flow as WT-1/DR-1.

## Testing

- `src/bass/params.test.ts` — defaults, ranges, curve math, formatting.
- `src/bass/patches.test.ts` — patch serialize/load round-trip, factory
  patches valid.
- `src/bass/seq.test.ts` — step timing incl. swing, slide/tie semantics
  (slid step suppresses retrigger + ties previous), chain advance, RAND
  output validity (pure functions extracted for testability).
- `src/bass/engine/worklet-bass.test.ts` — offline-render smoke tests in the
  style of `worklet.parity.test.ts`: note produces audio; accent > plain
  peak; slid step glides pitch without amp discontinuity (no retrigger
  click); LFO modulates cutoff at the synced rate; no NaN/denormal output.
- Manual: chrome-debug drive of the built app (power on, program a line with
  accents + slides, wobble it, save/reload patch).

## Out of scope (follow-ups)

- JUCE/C++ port of BL-1 (own spec; the web worklet is the lockstep reference,
  same playbook as WT-1 → plugin and DR-1 → plugin).
- Poly or duo mode — mono is the product.
- MIDI input while the sequencer runs (transpose-follow is a natural v2).
- Pattern export / host-sync — meaningful only in the plugin.
- A second oscillator — unison + sub covers the width; a second osc would
  push BL-1 back toward WT-1.
