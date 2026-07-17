# WT-1 Mono/Poly Switch — Design

Date: 2026-07-17
Scope: **web app only** for this cycle. JUCE parity (Engine.cpp, Params, Presets.cpp, hosted WT UI) is an explicit follow-up.

## Goal

Add a mono/poly voice mode to WT-1. Mono is classic last-note-priority with legato:
notes played while another is held glide (per `master.glide`) without retriggering
envelopes, and releasing the top note falls back to the most recent held note.
Lead factory presets default to mono.

## Param

- New bool param `master.mono` in `src/params.ts` PARAM_DEFS. Default 0 (poly).
- No new plumbing: it flows through presets, serialization, and param sync like
  any other bool param.

## Engine (`src/engine/worklet.js`)

- Maintain a held-note stack in press order (live keyboard/MIDI path).
- Mono note-on while a voice is sounding: retune that voice to the new pitch with
  a glide from the current pitch (instant when `master.glide` ≈ 0). Envelopes do
  **not** retrigger (legato).
- Mono note-on from silence: normal voice trigger (existing glide-from-lastPitch
  rule applies).
- Mono note-off of the sounding note while other keys are held: glide back to the
  most recently pressed held note, no envelope retrigger.
- Mono note-off with nothing else held: normal release.
- Poly path unchanged. Toggling the param mid-notes releases notes via the normal
  path; no special-case state migration.

### Sequencer interaction

WT-1 clip steps can hold 8-lane chords. In mono:

- A chord step collapses to its **first active lane** (lane 0 is the melody lane).
- Overlapping seq notes route through the same mono legato logic as live input.

## Presets (`src/presets.ts`)

Set `'master.mono': 1` on: ACID LINE, SCREECH LEAD, 8-BIT LEAD, GLIDE LEAD,
MINI LEAD, FUNKY WORM, TAURUS PEDAL, FOG LIGHT, GLASS RIBBON, NORTH WIRE,
TEMPLE BREATH.

## UI

MONO toggle next to the glide knob in `src/components/panels/KeyboardBar.tsx`
(mono and glide are conceptually paired).

## Testing

- Worklet-level tests: legato note-on does not retrigger envelopes and glides;
  release with held notes glides back without retrigger; release with nothing
  held releases normally; chord step collapses to first active lane in mono;
  poly behavior unchanged when `master.mono` is 0.
- Preset test: the lead presets listed above have `master.mono` = 1.

## Rejected alternative

Implementing mono as a UI-level note filter in front of the engine: simpler, but
cannot express glide-back-on-release and would not cover the sequencer path.
