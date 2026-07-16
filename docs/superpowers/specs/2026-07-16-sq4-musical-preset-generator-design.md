# SQ-4 factory preset generator: musical arrangement, register split, per-song drums

**Date:** 2026-07-16
**Status:** Approved (design)
**Files:** `src/seq/sessionPresets.ts`, `src/seq/factory.ts`, `juce/source/seq/dsp/SeqFactory.cpp`

## Problem

The SQ-4 factory ships 24 session presets. Three problems:

1. **Lead/pad register overlap.** The WT-1 pad triad spans ~0…+18 semitones (rel. seq root C3) and the lead spans ~+5…+23. They share the +5…+18 band, so lead and pad occupy the same notes and mask each other. This is true in the hand-authored NEON TALE (`FOG_STABS`/`FOG_SWELL` reach +12 into the lead) and in every generated preset.

2. **Web/native have diverged.** `src/seq/sessionPresets.ts` already generates each preset musically from one shared chord progression (`harmonyFor` → bass/lead/pad). `juce/.../SeqFactory.cpp::factorySessionLibrary` still uses the **old random modulo clip-pick** from a shared WT-1/DR-1 pool. The two no longer match, despite the header comment claiming they do.

3. **Repeated drums.** Both platforms' drum selection reuses a small set of library clips. `dr1-distant-ticks` is wired as the INTRO drum for NEON, ACID and AMBIENT (and again mid-song in AMBIENT), so the same sparse "ticks" loop appears across most songs. Drums do not fit each song's style, and repeat heavily.

## Goals

- Lead and pad never occupy the same note, in every preset.
- One procedural generator, shared note-for-note between web and native (end the divergence).
- Every song gets unique drum patterns that fit its family style, its energy, and each scene's role. No two songs share a drum pattern; no scene pulls a shared library clip.

## Non-goals

- No change to the DR-1/BL-1/WT-1 **clip library** itself (the manual clip browser keeps its clips).
- No change to bass voicing beyond what already exists (bass sharing the pad's low end is normal orchestration; the concern is lead vs pad).
- No refactor of unrelated code, patch/gain calibration, or the session codec.

## Coordinate system

Semitones are relative to the seq root (C3 = 0). The packed WT-1/BL-1 note byte stores `n` (pitch class 0..11) and `o` (octave, clamped −1..+1 by `putNote`), so representable pitches are −12..+23. DR-1 stores one byte per pad-step: `0` off, `1` hit, `2` accent; layout `dr1Idx(bar,pad,step) = (bar*16 + pad)*16 + step`, 16 pads × 16 steps/bar.

DR-1 pad map: `0 KICK · 2 SNARE · 3 CLAP · 4 RIM · 5 CH HAT · 6 OH HAT · 8/9/10 TOMS · 12/13 PERC`.

## Design

### 1. Register split (web + native)

Three fixed octave bands, split on octave boundaries so no lead note can equal a pad note:

```
LEAD   +12 … +23   (C4–B4)   melody, one octave up
──────────────────── split at +12 ────────────────────
PAD     +0 … +11   (C3–B3)   close-voiced chord bed
BASS   −12 …  −1   (C2–B2)   roots + fifths (unchanged)
```

- **`padProgression`** — voice each of the three chord tones as its pitch class in octave 0: `absolute = ((root + interval) % 12 + 12) % 12`, giving 0..11. Same three pitch classes (same chord, close/inverted voicing), guaranteed ≤ +11.
- **`leadProgression`** — voice the authored melody strictly in +12..+23. Replace the nearest-octave `voicedLeadPitch` search (which allowed −12..+23) with `absolute = pitchClass + 12` (pitch class 0..11 → 12..23). The band is one octave, so the melody follows the authored `LEAD_LINES` contour; no octave choice remains. `voicedLeadPitch` is removed.
- **`bassProgression`** — unchanged.

Invariant to assert in tests: for every generated preset and every scene, `max(pad pitch) < min(lead pitch)` whenever both parts are present.

### 2. NEON TALE (index 0, hand-authored)

NEON TALE stays the hand-authored `factorySession()` on both platforms (not the generator). Apply the register fix directly: drop `FOG_STABS` and `FOG_SWELL` one octave (the only pad clips that share a scene with a lead — DROP A/B use `FOG_STABS` with `GLASS_HOOK/II`; BREAK uses `FOG_SWELL` with `GLASS_SOLO`). The `AIR_*` pads never coincide with a lead and are left untouched.

- `FOG_STABS`: subtract 1 from each root's octave (`o`), shifting all three voiced lanes −12. Range −7…+12 → −19…0.
- `FOG_SWELL`: subtract 1 from each `chordHeld` octave argument. Range −2…+12 → −14…0.

Mirror identically in `SeqFactory.cpp` (`fogStabs`, `fogSwell`).

### 3. Procedural drum generator (web + native)

Replace `drumClip(spec, scene)` (which picks a shared library clip) with `drumProgression(spec, scene)` that builds a 4-bar DR-1 clip from three deterministic layers. No RNG — everything is a pure function of `spec.family`, `spec.variationIndex` (0..3), `spec.energy` (1..5), and `scene` (0..5).

#### 3a. Family archetype (base 16-step bar)

Each family defines base hit positions per pad. `A` marks an accent (byte `2`), plain marks a hit (byte `1`).

| Family | KICK (0) | SNARE(2)/CLAP(3)/RIM(4) | CH HAT (5) | OH HAT (6) | PERC/TOM |
|--------|----------|--------------------------|------------|------------|----------|
| NEON | 0ᴬ,4,8,12 | CLAP 4,12 | 2,6,10,14 | 2,10 | PERC 12: 15 |
| ACID | 0ᴬ,4,8,12,14 | SNARE 4ᴬ,12ᴬ | 0..15 (16ths, offbeats ᴬ) | 7 | — |
| HOUSE | 0,4,8,12 | CLAP 4,12 | 2,6,10,14 (light) | 2,6,10,14 | — |
| LO-FI | 0ᴬ,7,10 | SNARE 4,12 | 0,3,6,8,11,14 | 14 | PERC 13: 6 |
| CINEMATIC | 0ᴬ,10 | SNARE 8ᴬ | — | — | TOM 8/9/10 fills |
| AMBIENT | 0,8 | RIM 8 | 4,12 (airy) | — | PERC 12: 2 |

Swing is already applied globally per family (`swing = HOUSE 0.12 / LO-FI 0.18 / else 0`); the generator does not re-encode swing into steps.

#### 3b. Per-song mutation (uniqueness)

Derive a small deterministic integer `seed = variationIndex * 5 + energy` and apply rule-based mutations so the four songs in a family differ and energy scales intensity:

- **Hat density** scales with energy: energy ≥ 4 promotes CH HAT to 16ths (fill the gaps); energy ≤ 2 thins to every 4th step.
- **Accent rotation:** rotate the CH/OH accent positions by `variationIndex` steps (mod pattern length), so each variation lands its accents differently.
- **Ghost notes:** `variationIndex` selects which ghost hits are added — e.g. ghost KICK on 14, ghost SNARE on 11, ghost PERC on 3 — one added per variation index > 0.
- **Bar-4 fill:** the fourth bar always differs from bars 1–3 — a family-appropriate fill (TOM run 8→9→10 for CINEMATIC/NEON, snare roll 13/14/15 for ACID/HOUSE, dropped-beat + perc for LO-FI, cymbal/perc swell for AMBIENT). This guarantees intra-clip variation and end-of-phrase motion.

The combination `(family archetype) × (variationIndex accent/ghost) × (energy density) × (bar-4 fill)` yields a distinct pattern per song. A generation-time check (dev assertion / test) confirms no two of the 24 songs produce byte-identical drum clips.

#### 3c. Per-scene density mask (arrangement role)

The scene mask multiplies/gates the generated pattern to fit each part. Scene → drum track mapping is unchanged from `buildSession`:

| Scene | Role | Drum treatment |
|-------|------|----------------|
| 0 INTRO | sparse entry | KICK (downbeats only) + CH HAT; no snare/clap |
| 1 BUILD | rising | add SNARE/CLAP backbeat + full hats; add riser PERC on later steps |
| 2 DROP A | full | full archetype + mutation |
| 3 DROP B | full variant | full archetype + mutation, **plus** an extra fill/hat shift so A≠B |
| 4 BREAK | drumless | no drum clip (arrangement already `[null, bass, lead, pads]`) |
| 5 OUTRO | tail | sparse KICK + PERC tail; no busy hats |

INTRO and OUTRO are therefore generated from the same archetype at low density — not a shared `dr1-distant-ticks` clip. Each song's intro is its own sparse groove.

### 4. Native port

Port the whole generator into `SeqFactory.cpp::factorySessionLibrary`, replacing the modulo clip-pick loop:

- `harmonyFor`, `bassProgression`, `leadProgression` (with register split), `padProgression` (with register split), `drumProgression`, the `LEAD_LINES` / `LEAD_RHYTHMS` tables, and the per-scene density map.
- Preserve existing patch/program assignment and `calibratedGain`.
- Index 0 (NEON TALE) keeps `factorySession()` exactly, with the FOG octave fix.
- Keep the C++ a note-for-note transcription of the TS (existing porting discipline in this file).

## Data flow

```
spec (family, variationIndex, energy, programs)
        │
        ├─ harmonyFor ──────────────► roots[4], minor[4]
        │                                   │
        │        ┌──────────────────────────┼───────────────────────────┐
        │        ▼                           ▼                           ▼
        │  bassProgression           leadProgression (+12 band)   padProgression (0..11 band)
        │        │                           │                           │
        └─ drumProgression(scene) ──────────┼───────────────────────────┤
                 │                            │                           │
                 ▼                            ▼                           ▼
        per-scene density mask        scene.clips[0..3] assembled per density map
```

## Testing

- **Register invariant:** for all 24 presets × all scenes, assert `max(pad note) < min(lead note)` when both present. (New unit test, web; parity test native.)
- **Drum uniqueness:** assert the 24 generated drum clips (per scene) contain no byte-identical duplicates; assert no scene references a library clip pattern.
- **Bar-4 variation:** assert bar 4 differs from bar 1 in every generated drum clip.
- **Web/native parity:** existing factory-session parity test extended to cover the generated presets (byte-for-byte equality of session payloads across platforms).
- **Determinism:** generating twice yields identical bytes.

## Risks / tradeoffs

- **Close-voiced pads** are tighter than the old open triads; acceptable and standard for pad beds, and required to keep the band.
- **One-octave lead** removes the old nearest-octave smoothing; the authored `LEAD_LINES` are melodic within an octave, so contour is preserved.
- **Porting surface is large** (~150 lines TS → C++). Mitigated by the existing note-for-note porting discipline and the parity test.
