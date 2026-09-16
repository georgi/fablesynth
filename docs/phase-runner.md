# PHASE RUNNER

Preset **42 · DUB TECHNO**, 126 BPM, F minor. A driving companion to TIDAL
MEMORY: the bass supplies the hook and two different echo lengths interlock.

- **CHASSIS:** steady kick, alternating hat accents, tight soft clap, and occasional
  tom pickups. The clap uses a shortened, darkened 808 sample. Closed and open
  hats use soft 808 one-shots, gently low-passed, with a shared choke group.
- **MOTOR:** short F–Eb–C phrases with an Ab lift and G pickups. Accents open
  the filter slightly; the low end stays dry and centered.
- **ECHO:** soft, rootless minor-ninth stabs. A 357 ms dotted-eighth ping-pong
  at 79% feedback makes the counter-rhythm; the trigger notes leave it space.
- **RELAY:** a low wooden response with 476 ms quarter-note echoes, avoiding
  the bright lead sound we removed from TIDAL MEMORY.

Suggested performance: eight bars each of **IGNITION → LOCKSTEP → CROSSCURRENT
→ SUSPENSION → SECOND WIND → COAST**. In SUSPENSION the kick leaves while the
bass keeps moving. SECOND WIND changes the bass ending and chord placement.
Scenes launch manually; the preview performs this sequence in about 1:39.

The source is `src/seq/songs/phaseRunner.ts`. The shared `score.ts` helpers only
encode explicitly written notes and hits. They do not choose musical content.

## Small iteration loop

1. Edit the score or patch overrides in one file.
2. Run `npm run songs:generate` to update native data and its parity fixture.
3. Run the authored-song tests and build the affected targets once.
4. Render through the existing native harness; inspect level results and
   audition the preview. Repeat only when a musical change or failure warrants it.

```sh
juce/build/sq4_host_test_artefacts/Release/sq4_host_test \
  --render-preset "PHASE RUNNER" "$PWD/build/auditions/phase-runner.wav"
```

For feedback, start with LOCKSTEP and CROSSCURRENT: do the bass hook and the
two echo rhythms stay distinct, and does the track feel sufficiently different
from TIDAL MEMORY?
