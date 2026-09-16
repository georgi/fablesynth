# TIDAL MEMORY

An authored SQ-4 pilot: dub techno, 118 BPM, D minor, light sixteenth swing.
Select **DUB TECHNO · TIDAL MEMORY** at the end of the preset list (program 41).

## The composition

The hook is a rootless minor-ninth chord, F–A–C–E, with E/F movement in its
top voice. The bass supplies D and plays around the chord attacks. REFRACTION
moves through B-flat before returning to D; RETURN remembers that change
instead of copying the first groove. The soft SIGNAL answers in the gaps,
with restrained ping-pong repeats forming part of the rhythm.

- **ROOM / DR-1:** custom MINIMAL kit, a steady kick, alternate hat accents, tight soft clap
  and occasional low-tom pickups. Both hats use the same soft 808 one-shots
  as PHASE RUNNER, with short envelopes and a shared choke group. Fills are
  written at specific moments.
- **UNDERTOW / BL-1:** a custom TECHNO SUB variant with a shorter envelope,
  audible oscillator body, and accent-sensitive filter movement.
- **REFLECTION / WT-1:** a custom DUB SKANK variant, voiced across octaves,
  a dark 24 dB low-pass, slow filter modulation, and a prominent dotted-eighth
  echo (381.36 ms; 58% feedback, 64% mix) feeding a spacious reverb.
- **SIGNAL / WT-1:** a softened FANTA BELLS variant with a warm fundamental
  in the written octave, a trace of bell, and a gentler attack. Dotted-eighth
  ping-pong delay (381.36 ms; 60% feedback, 66% mix) and 52% reverb mix let
  the echoes answer each note. Filter tracking and envelope lift are restrained
  so higher notes stay soft.

## Suggested performance

Scenes loop and are launched manually in SQ-4. These lengths describe the
audition render, not stored scene-follow actions or automation.

| Scene | Bars | Purpose |
| --- | ---: | --- |
| SOUNDINGS | 8 | Sparse room, first chord reflections, distant signal |
| UNDERTOW | 8 | Bass enters; the chord rhythm starts to take shape |
| PRESSURE | 16 | Main groove, complete motif, two-note responses |
| REFRACTION | 8 | Harmonic departure and a kick withdrawal before the middle |
| LOW WATER | 8 | Bass and kick disappear; chord and signal become exposed |
| RETURN | 16 | New bass ending, changed chord voicings, answering signal |
| DISSOLVE | 8 | Drums retreat over four bars; isolated reflections remain |

All clips stay within the four-bar hosted-editor limit. Long-form development
comes from the seven distinct scenes. Patches remain fixed across scenes;
their envelopes, LFOs and effects provide the timbral motion.

## Editing and auditioning

The score and patch overrides live in `src/seq/songs/tidalMemory.ts`. The
encoding helpers do not select notes, progressions, rhythms or fills.

After editing:

```sh
npm run songs:generate
npm run songs:check
npm test -- src/seq/songs/tidalMemory.test.ts src/seq/sessionPresets.test.ts
cmake --build juce/build --target sq4_engine_test sq4_host_test -j4
juce/build/sq4_engine_test
juce/build/sq4_host_test_artefacts/Release/sq4_host_test
```

`songs:generate` compiles the authored scores to
`juce/source/seq/dsp/AuthoredSessions.gen.h` and refreshes the complete
web/native parity fixture. Musical edits are made only in the TypeScript
score. Legacy program positions and the startup preset remain stable.

Render the suggested performance through the real native SQ-4 processor,
including its device effects, scene launches, track faders and master limiter:

```sh
mkdir -p build/auditions
juce/build/sq4_host_test_artefacts/Release/sq4_host_test \
  --render-preset "TIDAL MEMORY" "$PWD/build/auditions/tidal-memory.wav"
```

The renderer writes 48 kHz / 24-bit stereo, reports scene levels, and rejects
silent, non-finite or clipped output. An optional final argument `0`–`3`
solos one track for balance checks. The performance lasts approximately 2:34,
including the final stop/tail window. Generated audio lives in ignored
`build/auditions/`.
