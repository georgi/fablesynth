# Repository guidance

## SQ-4 musical direction

- Build a curated collection of individually composed showcase tracks, one
  style and one track at a time. Prioritize musical identity over preset count.
- Start with dub techno. Develop complementary tracks with different grooves,
  instrumental roles, harmonic ideas, and arrangements; avoid cosmetic variants.
- The legacy generator repeats one bass, lead, and chord clip through a common
  scene template. Do not use that formula for new authored tracks.
- Compose the bass and drums together. Write distinctive motifs, intentional
  rests, phrase variants, departures, and changed returns. Design patches around
  their actual parts rather than choosing sounds after writing the notes.
- Each track should make a few synth capabilities sound compelling. Demonstrate
  the full instrument range across the collection, not every feature in one song.
- Prefer an 8–16-bar core-groove audition before developing the full arrangement
  when the task allows it. Listening feedback should drive revisions. This is
  not an extra approval requirement for explicitly requested work.
- Retain existing presets and host program positions while the authored bank
  grows. Do not delete the legacy collection or change the startup preset as a
  side effect of adding a song.

## Sound preferences established through feedback

- Leads should be warm and soft. Avoid piercing, bright, octave-up bell attacks.
  Address the source, oscillator octave, envelope, filter tracking, modulation,
  and resonance—not just the fader. Keep the softened part audible in the mix.
- Delay is a central rhythmic element of dub. Leave space for audible repeats,
  use tempo-related timings and ping-pong movement, and add spacious reverb.
- More delay is not always better: the user subsequently found TIDAL MEMORY's
  roughly 80% feedback tails too long. For requests to shorten tails, reduce
  feedback first and adjust mix while retaining the rhythmic subdivision.
  Do not automatically shorten the interval between echoes or remove the space.
- Avoid cowbell-like percussion in these dub tracks. The MINIMAL kit's pitched
  TINE rim on pad 4 was the offending sound. Replace its hits with a tight,
  soft clap on pad 3; merge coincident hits instead of doubling them.
- Use standard, soft 808 closed and open hats. The original synthesized TINE /
  ring-modulated hats sounded like a triangle to the user. Use actual one-shot
  samples, with the oscillator, noise, and ring layers disabled, modest levels,
  short envelopes, gentle low-pass filtering, and a shared choke group.
- Preserve earlier accepted edits when applying new feedback: both tracks now
  have soft claps and 808 hats; TIDAL MEMORY also has shortened delay tails.
- Apply sound changes only to the requested song(s). Check other presets remain
  unchanged when extracting or modifying shared helpers.

## Current reference tracks and sounds

- **TIDAL MEMORY:** program 41 (zero-based index 40), DUB TECHNO, 118 BPM,
  D minor, seven authored scenes. Soft chord and signal parts. Both delays use
  dotted eighths (`45 / 118` seconds). Current chord feedback/mix: 0.58/0.64;
  signal: 0.60/0.66. These are working references, not universal rules.
- **PHASE RUNNER:** program 42 (index 41), DUB TECHNO, 126 BPM, F minor,
  six scenes. A driving bass hook, dark chord stabs with dotted-eighth echoes,
  and a low wooden response with quarter-note echoes. Avoid a bright lead.
- `src/seq/songs/dubKit.ts` contains the shared sounds. `dubKit()` supplies the
  soft clap; `soft808DubKit()` adds the hats. Both songs use `soft808DubKit()`.
- DR-1 pad numbers and sample indices are different namespaces: pad 3 is CLAP,
  pad 5 is CH HAT, pad 6 is OH HAT. Their `oscB.table` one-shot sample indices
  are 1 (808CP), 2 (808CH), and 3 (808OH), respectively. These are not oscillator
  wavetable indices; confirm mappings in `src/drum/params.ts` when editing.
- Current shared punch revision: 0.5 ms drum attacks; clap has 18 ms hold,
  95 ms decay, 5.2 kHz LP12, and 4% room. Hats have 50/170 ms decay,
  6.5 kHz low-pass, and choke group 1. Kick has a faster 32 ms pitch drop,
  less drive blending, and no compressor or reverb. Await listening feedback.
- Warm tone should still have a firm transient. Do not soften attacks merely
  to avoid brightness. Compare revisions at matched loudness.
- Use supported parameters from each machine's definitions. DR-1's per-pad FX
  do not support the WT-1 `fx.eq.*` controls; use its filter for tone shaping.

## Authoring and compatibility

- Author music in `src/seq/songs/`. `score.ts` encodes explicit notes and hits;
  it must not choose progressions, melodies, fills, or arrangements procedurally.
- Register new songs in `AUTHORED_SESSION_PRESETS` in
  `src/seq/sessionPresets.ts`, appending to preserve program ordering.
- `npm run songs:generate` compiles the authored scores into
  `juce/source/seq/dsp/AuthoredSessions.gen.h` and refreshes
  `juce/test/fixtures/web-session-presets.json`. Do not manually duplicate the
  composition in C++ or edit the generated header as the source of truth.
- Embed complete inline patch parameters using the machine's default/factory
  resolution helpers. Retain `base` for patch identification. Recalled sessions
  must deep-copy inline patch data so edits cannot mutate factory presets.
- Keep clips within the current four-bar hosted-editor limit. The session
  protocol supports longer clips, but the hosted editors have a smaller limit.
- Scenes are launched manually. The rendered performance is an audition plan,
  not stored scene-follow actions or parameter automation. Timbre can develop
  through envelopes, LFOs, effects, and different written parts.
- Scope legacy generator tests to legacy presets. Authored songs must not be
  forced into whole-bar triads, fixed lead/pad registers, mandatory fills,
  fixed scene masks, or unique-byte requirements as a substitute for musicality.

## Efficient iteration and validation

- The user explicitly wants token-efficient execution and no bloated tool calls.
  Reuse the existing score helpers, exporter, renderer, and tests. Do not create
  a new framework or external music-generation workflow for routine revisions.
- Read focused file spans, batch independent inspection, save verbose build/test
  output to logs, and report summaries or failures. Avoid repeatedly reading
  whole files, large generated headers, or fixture payloads.
- Sequence dependent work: edit → generate → validate/build → render → inspect.
  Stop on failures; do not let a later successful command hide an earlier error.
  Never run overlapping builds against the same native build directory.
- Use bounded waits for long builds instead of frequent polling. A native link
  can take time without printing progress. Keep user updates short and useful.
- For patch-only edits, use existing authored-session/export checks and native
  parity checks. Broaden testing only for a relevant code change or unresolved
  failure. Do not add tests that merely assert chosen knob values.
- When sharing helpers or making a narrow sound edit, compare the generated
  sessions before/after: verify unrelated tracks, notes, scenes, and presets are
  unchanged. Existing tests should cover valid parameter keys and portable
  inline-patch round trips, including drums.
- Run `npm run songs:check` to detect stale native data. Build the web output and
  affected native targets when delivering an updated preset.
- Render through the actual SQ-4 processor, including its device FX, scene
  launches, faders, and limiter. Start with one full render; render isolated
  parts or repeat only when a balance issue, failure, or revision warrants it.
- Inspect finite output, audibility, and clipping. RMS, peaks, and spectral
  measurements help diagnose problems but do not establish that music sounds
  good. Do not claim to have listened when only numerical checks were performed.

Useful commands:

```sh
npm run songs:generate
npm test -- src/seq/songs/tidalMemory.test.ts src/seq/sessionExport.test.ts
npm run build
cmake --build juce/build --target sq4_host_test FableSeq_Standalone -j4
juce/build/sq4_host_test_artefacts/Release/sq4_host_test
npm run songs:check
```

The historically named `tidalMemory.test.ts` checks all authored songs. Include
`sessionPresets.test.ts` and `sq4_engine_test` when changing library registration
or legacy compatibility.

## Delivering auditions

- Use the existing renderer:
  `sq4_host_test --render-preset "SONG NAME" /absolute/path.wav [solo-track]`.
  Its current performance plan uses 16 bars for PRESSURE/RETURN and eight for
  other scenes; do not assume these durations are session metadata.
- For portable groove drafts, use `sq4_host_test --render-session
  /absolute/session.json /absolute/output.wav`. This plays each scene for four
  bars. LATE CHECKOUT is a 122 BPM deep-house draft in
  `src/seq/songs/lateCheckout.ts`, pending listening feedback and full arrangement.
- Store generated WAV/MP3 previews and portable JSON exports in ignored
  `build/auditions/`. Export through `exportSessionJson` to embed all sounds.
- Give revisions descriptive filenames so earlier previews remain comparable
  and cached players do not replay an older mix. Keep the canonical session JSON
  current as well. Prefer a compact MP3 for the inline preview.
- Present the audio directly, briefly state the audible change and verification,
  and tell the user to reload the preset when needed. Avoid long implementation
  reports during musical iteration. Rebuild the native standalone when updating
  it, and distinguish a built artifact from an installed or running app.
