# LFO Controls Expansion — Design

**Goal:** Turn each LFO from a single-RATE control into a usable modulation source by adding **tempo sync**, **rise / fade-in**, and **phase + retrigger mode** — in both the web app and the JUCE plugin, kept as a 1:1 port.

## Motivation

Today each LFO exposes only `shape` + `rate` (free-running Hz, retriggered per voice). The panel reads empty (one knob under a short curve) and the LFO lacks the controls a modulation LFO normally has. Depth is deliberately **not** added — it already lives in the mod matrix (`mat*.amt`).

## New parameters (per LFO, `lfo1` / `lfo2`)

Added after the existing `lfoN.rate`. All defaults chosen so existing presets and behaviour are **unchanged** (sync off, retrig on, rise 0, phase 0).

| id | kind | range / options | default | notes |
|----|------|-----------------|---------|-------|
| `lfoN.sync` | bool | 0/1 | 0 | when on, rate follows tempo divisions instead of Hz |
| `lfoN.syncrate` | enum | `LFO_DIVS` | `2` (`1/4`) | note division |
| `lfoN.rise` | float lin | 0–5 s (`fmtSec`) | 0 | fade-in time after note-on |
| `lfoN.phase` | float lin | 0–1 (`fmtPct`) | 0 | start-phase offset |
| `lfoN.retrig` | bool | 0/1 | 1 | 1 = retrigger per note (legacy); 0 = free-running (global phase) |

`LFO_DIVS = ['1/1','1/2','1/4','1/4.','1/4T','1/8','1/8.','1/8T','1/16','1/16T','1/32']`
`LFO_DIV_F = [0.25, 0.5, 1, 2/3, 1.5, 2, 4/3, 3, 4, 6, 8]` — **cycles per beat** (beat = quarter note).

## Tempo / BPM source

- **JUCE:** host tempo from the playhead each block (`getPlayHead()->getPosition()->getBpm()`), fallback **120** when absent/invalid. Pushed into the engine via `Engine::setBpm`.
- **Web:** standalone has no transport; the worklet holds an internal `bpm = 120` (settable via a `{t:'bpm'}` port message for future wiring). SYNC therefore locks to musical divisions of 120 BPM. Same DSP path as JUCE — only the BPM source differs.

## DSP model

Synced rate: `hz = sync ? (bpm/60) * LFO_DIV_F[syncrate] : rate`. Shared helper (`lfoHz`) used for both per-voice and global advance.

**Rise** is per-voice and always keyed off note-on (even in free-run): the LFO tracks `elapsed` samples since reset; `riseGain = rise<=0 ? 1 : min(1, elapsed/(rise*sr))`; the LFO's contribution to the mod sources is multiplied by it.

**Phase** is applied at read time as a wrapped offset: `valueOff(shape, off)` reads `frac(phase + off)`. Works uniformly for retrig and free-run.

**Retrig mode:**
- `retrig = 1` (default): each voice reads its own per-voice LFO phase, reset on note-on — current behaviour.
- `retrig = 0` (free-run): voices read a **single global LFO phase** (`gLfo1` / `gLfo2`) advanced once per block in the engine, independent of notes. Rise stays per-voice.

The per-voice LFO is still reset and advanced (for `elapsed`/rise and for retrig phase) regardless of mode; only the phase *read* is switched. The global LFO advances every block even when idle, so a free-running LFO keeps moving during silence.

S&H `hold` randomises on phase wrap, per-source (global hold shared across voices in free-run, per-voice hold in retrig) — acceptable and matches each mode's intent.

## UI (both platforms, per LFO block)

```
LFO 1                 ◂ SINE ▸     header: title + shape stepper (unchanged)
  ~~~~ waveform view ~~~~          LFOView (unchanged; decorative)
  [ SYNC ]   [ TRIG ]             two labelled toggles (sync, retrig)
  RATE|DIV    RISE    PHASE        knob row; first slot swaps knob↔division stepper on SYNC
```

- When `sync` is **off** the first slot is the **RATE** knob; when **on** it is the **DIV** stepper (`syncrate`). The other two slots are always the **RISE** and **PHASE** knobs.
- `TRIG` lit = retrigger; unlit = free-running.
- **Web:** conditional render driven by the `sync` store value; small labelled pill toggles; three `sm` knobs / compact stepper.
- **JUCE:** `LfoPanel` runs a `Timer` that swaps `rate`/`syncRate` visibility (and re-lays-out) when a block's `sync` value changes. Toggles are `juce::TextButton` (clicking-toggles) bound via `APVTS::ButtonAttachment`; knobs use a small size to fit the half-panel.

The decorative `LFOView` continues to animate from `lfoN.rate` (Hz); it does not reflect the synced rate. Acceptable cosmetic gap, noted here.

## Parameter index space (JUCE)

The flat `Pid` enum gains an `LfoField` group so each LFO block is 7 fields:

```
enum LfoField { LFO_SHAPE, LFO_RATE, LFO_SYNC, LFO_SYNCRATE, LFO_RISE, LFO_PHASE, LFO_RETRIG, LFO_NFIELDS };
LFO2_BASE = LFO1_BASE + LFO_NFIELDS;
MAT1_BASE = LFO2_BASE + LFO_NFIELDS;   // downstream bases shift automatically
```

`NUM_PARAMS` and the flat indices of MAT/FX/master shift, but APVTS keys by **string id** and the engine descriptor table is placed **by id**, so presets and state stay valid; new params fall back to their defaults. The web list is keyed by string id, so insertion order is irrelevant there.

## Testing

`juce/test/engine_test.cpp` gains an LFO section:
- `lfoDivFactor` mapping (`1/4`→1, `1/8`→2, `1/1`→0.25).
- defaults preserve legacy behaviour (`retrig`=1, `sync`=0, `syncrate`=`1/4`).
- `riseGain` ramps 0→1 (0 at note-on, ~0.5 mid-ramp, 1 when `rise`=0).
- engine renders finite/bounded audio with sync + free-run LFOs routed to a destination.

Web: `tsc && vite build` must pass; chrome-debug smoke check of the LFO panel.

## Out of scope

Per-LFO depth (lives in the mod matrix), host-tempo transport UI for the web app, smoothing for S&H, unipolar/bipolar polarity, syncing the decorative waveform view to the division.
