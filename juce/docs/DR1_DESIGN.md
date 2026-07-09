# FableSynth DR-1 — Wavetable Drum Machine (design)

> **The 808 and 909 were never drum machines. They were synthesizers with
> sixteen buttons.** Every classic there — the boomy kick, the snappy snare,
> the sizzling hat — is a pitch-swept oscillator, filtered noise and a fast
> envelope. That is *exactly* what the WT-1 engine already does, per voice, in
> real time. DR-1 puts sixteen of those voices behind pads and a step row, and
> adds the one axis neither an 808 nor a sampler ever had: **the drum morphs
> through a wavetable while it plays.**

DR-1 is a parallel JUCE plugin (VST3 · AU · Standalone) that ships alongside
WT-1 from the same repository, the same DSP core, the same design language.
Where WT-1 is *sculpt a sound*, DR-1 is *sculpt a kit*.

---

## 1. The idea

Three instruments collapsed into one engine:

**808/909 — synthesis.** A kick is a sine with a fast pitch envelope. A snare
is two detuned tones plus band-passed noise. A hat is a dense inharmonic
buzz through a highpass. The WT-1 voice already contains every ingredient:
two wavetable oscillators, a sub, white/pink noise, a dual SVF/comb/vowel
filter with ADAA drive, two envelopes and a mod matrix that *already routes
MOD ENV → PITCH* — the literal 808-kick patch. DR-1 doesn't need a new
synthesis engine; it needs drum-shaped defaults on the one we have.

**Sampler — resynthesis, not playback.** WT-1's `buildUserTable` pipeline
already converts arbitrary audio into a band-limited 16-frame wavetable.
DR-1's sampler story: **drop a one-shot on a pad and it becomes a wavetable**
— 16 single-cycle frames sliced along the sample's length, band-limited
through the same 11-level mip pyramid. Scanning POS front-to-back with the
mod envelope *replays the sample's spectral evolution*; slowing the scan
stretches the body; reversing it plays the tail first; freezing POS turns a
snare into a drone. Pitch it four octaves either way and it never aliases —
something no classic sampler could claim. DR-1 deliberately does **not** do
raw PCM playback in v1: resynthesis *is* the identity. It's what makes this a
DR-1 and not the ten-thousandth drum sampler.

**Drum machine — the workflow.** 16 pads, choke groups, accent, a TR-style
16-step sequencer synced to the host transport (the engine already consumes
`ppq`/`bpm` for LFO sync — the sequencer clock rides the same rails).

The new axis that justifies the whole product: **POS is a per-hit dimension.**
An analog machine gives you tune and decay. A sampler gives you a frozen
recording. DR-1 sweeps table position across every hit — transient frame →
body frame → tail frame — so one knob (`POS ENV`) morphs a kick from clicky
techno thump to saturated 808 boom *continuously*, and the live terrain view
shows the sweep happen. Show the signal, as always.

---

## 2. What a pad is

Each of the 16 pads is one **DrumVoice** — a trimmed, one-shot-shaped
specialization of WT-1's `Voice`:

```
[OSC A: wavetable + POS]──┐
[OSC B: wavetable + POS]──┤
[NOISE: white/pink+color]─┼─▶ [FILTER: SVF/comb/vowel + ADAA drive] ─▶ [AMP ENV] ─▶ pad LEVEL/PAN ─▶ bus
                          │
        [PITCH ENV]───────┘ (▲ semitones, fast decay — the 808 knob)
        [MOD ENV]──▶ mod matrix (4 slots: POS, cutoff, level, pan, …)
        [LFO]──────▶ (optional wobble/S&H per pad)
```

Reused verbatim from `Engine.{h,cpp}`: `OscState` (incl. unison — 5-voice
detuned unison on a noise-ish table *is* the 909 clap), `FilterState` (comb =
metallic toms, vowel = talking percussion), `Env`, `Lfo`, the ADAA drive, the
steal-fade machinery (state 5) which becomes the **retrigger/choke fade** so
chokes never click.

Drum-specific deltas (small, additive):

- **One-shot envelopes.** `Env` gains an AHD mode: attack, hold, decay to
  zero, no sustain/release; gate-off is ignored. A `CURVE` param morphs the
  decay between exponential (acoustic) and linear (LinnDrum gate). ~30 lines.
- **First-class pitch envelope.** `P.AMT` (±48 st) and `P.DEC` as pad params —
  internally it's just a hardwired mod-matrix route to `DST_PITCH`, but a
  drum machine must have it on the panel, not buried in a matrix.
- **Deterministic phase.** `OscState` already randomizes start phase; pads add
  a `PHASE` param (0..1 or FREE) because kicks need cycle-locked attacks to
  stack identically every hit.
- **Choke groups.** 4 groups; `noteOn` on a pad kills (steal-fades) any
  sounding pad in the same group. Open/closed hat out of the box.
- **No glide, no poly.** Each pad is monophonic with retrigger fade. The
  16-voice allocator is replaced by a 16-pad map — *simpler* than WT-1.

### Per-pad parameter block

Same flat-`Pid` discipline as `Params.h` — `PAD1_BASE + field`, one repeated
block, no string hashing on the audio thread:

| group | params | count |
|---|---|---|
| OSC A | ON, TABLE, POS, TUNE(±48), FINE, PHASE, UNI, DET, LVL | 9 |
| OSC B | same | 9 |
| NOISE | ON, TYPE, COLOR (tilt filter), LVL | 4 |
| PITCH ENV | AMT, DECAY | 2 |
| AMP ENV | ATT, HOLD, DEC, CURVE | 4 |
| MOD ENV | DEC (att = 0) | 1 |
| FILTER | ON, TYPE, CUT, RES, DRIVE | 5 |
| MOD ×4 | SRC, DST, AMT (src: MODENV/VELO/LFO/RAND; dst: pad-local) | 12 |
| PAD | LVL, PAN, VEL→LVL, VEL→MOD, CHOKE, OUT BUS | 6 |

**52 params × 16 pads ≈ 832**, plus master/FX/sequencer ≈ **~880 APVTS
parameters**. Serum-scale but routine for hosts; every one automatable. The
descriptor table is generated by a loop over pads exactly like WT-1 generates
its matrix slots. Sequencer *pattern data is not parameters* — patterns are
state-blob data like user tables (chunk in `getStateInformation`), only
SWING, pattern select and accent amount are automatable params.

### New procedural tables

`Wavetables.cpp` grows a drum-tuned bank beside PRIME…GLITCH, same
FFT/mip pipeline:

- **THUD** — fundamental-heavy frames morphing sine → bridged-T-style droop
  with growing 2nd/3rd harmonics and soft-clip folds (kicks, toms).
- **CRACK** — mid-heavy frames with shaped odd-harmonic combs (snare bodies).
- **SHATTER** — dense high-harmonic pseudo-inharmonic frames (hats,
  cymbals). Honest physics note: a single-cycle table is harmonic by
  construction — true cymbal inharmonicity comes from *density + noise + comb
  filtering*, which is exactly how GLITCH already reads and why the comb
  filter matters here.
- **BODY** — vowel-ish resonant frames (congas, glitch perc).

All 6 WT-1 tables remain selectable — VOX snares and CHIME toms are free
content.

---

## 3. Sequencer

TR-style, host-locked, deliberately minimal in v1:

- 16 steps × 16 pads, patterns A–D with chaining; per-step **on / accent /
  probability**; global **swing**.
- Clock derives from the existing `setTransport(ppq)/setBpm` path. The engine
  already renders in 128-sample chunks; the sequencer checks step boundaries
  per chunk and fires `noteOn` sample-accurately inside the chunk — same
  cadence discipline as block-rate modulation, no new threading.
- The sequencer is a *layer*: incoming host MIDI always plays pads directly
  (GM-ish map from C1, remappable), so DR-1 works as a pure sound module in
  a DAW drum lane on day one. STEP mode is where the 808 lives.
- Pattern edits happen on the message thread into a double-buffered pattern
  snapshot swapped under the same try-lock pattern as `setTables` — the
  audio thread never blocks.

---

## 4. FX and busses

- **Per pad:** filter DRIVE (ADAA tanh, already in the voice) is the per-pad
  saturator. No per-pad FX chains in v1 — that's what busses are for.
- **Master bus:** the existing `Fx` chain (drive → chorus → ping-pong delay →
  reverb → limiter) reused as-is, plus **one new stage: a bus compressor**
  (simple feedback comp, ~80 lines) — the 909-glue that makes a kit read as a
  kit. Chorus earns its keep on claps/hats.
- **Aux outs:** 4 additional stereo output busses; each pad routes MAIN or
  AUX 1–4 (`OUT BUS` param) so the kick can hit its own channel strip.
  Standard JUCE multi-bus layout; the FX chain applies to MAIN only.

---

## 5. UI — the same machined rack, a different instrument

Same `Theme.h` palette, Michroma/Plex Mono, panel drawing, knobs, steppers,
LEDs — DR-1 must look like WT-1's sibling on the shelf.

```
┌──────────────────────────────────────────────────────────────────────┐
│ FABLESYNTH DR-1        KIT: ⟨ TR-VOID ⟩        SWING · VOL · [STEP]  │
├──────────────────┬───────────────────────────────────────────────────┤
│  PAD GRID 4×4    │  SELECTED PAD: 03 · SNARE                         │
│  ┌──┬──┬──┬──┐   │  ┌ OSC A ─────┐ ┌ OSC B ─────┐ ┌ NOISE ┐          │
│  │▉ │  │  │  │   │  │ 3D terrain │ │ 3D terrain │ │ COLOR │          │
│  ├──┼──┼──┼──┤   │  │ + POS/TUNE │ │ + POS/TUNE │ │ LVL   │          │
│  │  │▓ │  │  │   │  └────────────┘ └────────────┘ └───────┘          │
│  ├──┼──┼──┼──┤   │  ┌ PITCH ENV ┐ ┌ AMP ENV ┐ ┌ FILTER ┐ ┌ MOD ┐     │
│  │  │  │  │  │   │  │ AMT · DEC │ │ A·H·D·C │ │ CUT··· │ │ 4×  │     │
│  ├──┼──┼──┼──┤   │  └───────────┘ └─────────┘ └────────┘ └─────┘     │
│  └──┴──┴──┴──┘   │                                                   │
├──────────────────┴───────────────────────────────────────────────────┤
│ STEP  ●○○○ ●○○○ ●○○○ ●○○○   ·  ACCENT ROW  ·  A B C D  ·  ▶          │
├──────────────────────────────────────────────────────────────────────┤
│ FX: DRIVE · COMP · CHORUS · DELAY · REVERB          OUT: MAIN/AUX    │
└──────────────────────────────────────────────────────────────────────┘
```

- **Pad grid** (left): velocity-sensitive click pads, per-pad LED flash on
  trigger, drag-and-drop WAV onto a pad → `buildUserTable` import. Selecting
  a pad focuses the editor; colors stay functional per WT-1's rules — cyan =
  osc A, amber = osc B/noise, violet = filter; pads are neutral with a
  signal-cyan trigger glow.
- **Pad editor** (right): reuses `Controls`/`Panels`/`Displays` wholesale.
  The **live 3D terrain per oscillator** is the hero again — you *watch* the
  POS envelope traverse the table on every hit (`vizA/vizB` atomics already
  publish exactly this).
- **Step row** (bottom): the 808 signature. LED chase from the audio-thread
  step counter via one atomic, same 30 Hz timer discipline as the terrain.
- Fixed logical size, scale-to-window, pixel-faithful — identical approach
  to `PluginEditor`.

---

## 6. Reuse map (the engineering case)

| module | reuse | delta |
|---|---|---|
| `dsp/Wavetables` | 100% | + 4 drum tables in `generateTables()` |
| `dsp/Engine` voice internals (`OscState`, `FilterState`, `Env`, `Lfo`, ADAA, mip blend) | ~90% | Env AHD mode, PHASE param, DrumVoice wrapper |
| `dsp/Engine` orchestration | replaced | pad map + choke instead of poly allocator; **simpler** |
| `dsp/Fx` | 100% | + bus compressor stage |
| `dsp/Params` pattern | 100% | new descriptor table (pad-block loop) |
| `dsp/UserTables` (import pipeline) | 100% | + slice-to-16-frames from one-shot |
| sequencer | new | ~400 lines, clock math shared with LFO sync |
| `ui/Theme, Controls, Displays, LookAndFeel` | 100% | — |
| `ui/Panels` | ~60% | pad grid + step row are new |
| `WavetableView` | 100% | reused in pad editor |
| `PluginProcessor` shell | ~80% | multi-bus outs, pattern state chunk |
| tests (`engine_test` pattern) | pattern | kit renders, choke correctness, seq timing, aliasing floor per pad |

Roughly **70% of DR-1 exists today**. The build system gains a second
`juce_add_plugin` target in the same CMake tree sharing the `dsp/` static
library — one repo, two products, one engine to maintain.

### Repository layout

```
juce/source/dsp/        shared, unchanged home (Engine, Wavetables, Fx, …)
juce/source/dr1/dsp/    DrumVoice, DrumEngine, Sequencer, DrumParams, Kits
juce/source/dr1/ui/     PadGrid, StepRow, PadEditor, DR1Editor
juce/source/dr1/DR1Processor.{h,cpp}
juce/test/dr1_engine_test.cpp      headless, JUCE-free, like engine_test
```

---

## 7. Factory content

20 kits, same curation bar as WT-1's 20 presets:

- **TR-VOID / TR-BLOOM** — the 808/909 archetypes, pure synthesis, proof the
  engine covers the canon.
- **GLASS PERC / VOX KIT** — CHIME and VOX tables as drums; only DR-1 can
  make these.
- **MORPH KIT** — every pad rides a deep POS envelope; the demo kit that
  sells the concept in ten seconds.
- Each kit ships a pattern A–D so STEP mode plays music immediately —
  *inspiring without a manual*.

---

## 8. Milestones

1. **M1 — DrumEngine headless.** DrumVoice, pad params, choke, drum tables,
   `dr1_engine_test` green (kick fundamental sweep, choke fade, aliasing
   floor at +24 st, kit render non-silent). No JUCE.
2. **M2 — Plugin shell + pad editor.** APVTS from the pad descriptor table,
   MIDI pad map, drag-drop sample import, terrain views live. Playable from
   a DAW drum lane end to end.
3. **M3 — Sequencer + busses.** STEP mode, swing, accent, probability, aux
   outs, bus compressor. `plugin_host_test`-style PNG snapshot.
4. **M4 — Content + polish.** 20 kits with patterns, choke/velocity feel
   pass, manual-free onboarding pass.

## 9. Risks & positions taken

- **~880 parameters.** Big but bounded; hosts handle Serum-scale lists. If a
  host chokes, the fallback is halving the mod slots per pad (−192). Pattern
  data stays out of APVTS by design.
- **No raw sample playback.** Deliberate. Resynthesis-only is the sharp edge
  of the product; a PCM layer per pad is an obvious v2 if users demand it,
  and the pad architecture leaves room for it (`OSC C: PCM`).
- **Sequencer timing.** Step firing is chunk-quantized to ≤128 samples then
  offset-corrected inside the chunk — same guarantee WT-1 gives block-rate
  modulation; verified in the headless test against ppq math.
- **Cymbal realism.** Single-cycle tables are harmonic; DR-1 leans into
  *machine* cymbals (606/808 character) via SHATTER + comb + noise rather
  than chasing acoustic rides. That's the genre-correct call for this
  instrument.

---

*WT-1 proved the engine. DR-1 gives it a backbeat.*
