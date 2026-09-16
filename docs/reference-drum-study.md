# Deep Hypnotic Groove — SQ-4 drum study

Eight-bar study at 111 BPM. Isolated from the factory bank; existing songs have
not been changed. The first four bars contain kick and offbeat bass, the next
four add soft 808 clap and hats. Before and candidate use identical notes.

## Listen and inspect

Artifacts are in `build/auditions/reference-study/`:

- `reference-before-candidate.mp3`: reference → before → candidate, about 17.3 s each.
- `candidate-matched.mp3`: candidate alone.
- `after.json`: importable candidate session.
- `envelopes.png`: peak-normalized low-frequency pulse comparison.
- `spectrograms.png`: same-scale four-bar full-kit spectrograms.
- `measurements.json`: machine-readable measurements.

Reference source: `/Users/mg/Downloads/Deep Hypnotic Groove.mp3`.
Audio comparison uses 30.00–47.30 s; spectrogram uses 38.65–47.30 s.
These are time excerpts, not a transcription or guaranteed downbeat-aligned cut.
Envelope analysis instead uses the cleaner regular pulses in the opening 8.65 s.

## Candidate changes

Kick: oscillator tune -27 (nominal A1), oscillator level 0.9, 0.5 ms attack,
3 ms hold, 140 ms decay, curve 0.88. Pitch envelope: 22 semitones, 45 ms
parameter (the engine uses exponential decay; this is not a time constant).
Drive amount 0.22, blend 0.30; compressor threshold -8 dB, makeup 2 dB;
pad level 0.95. Clap/closed/open hat levels 0.75/0.90/0.80 restore their balance.
Existing samples, soft filters and hat choke remain.

## Results and limitations

| Metric | Reference | Before | Candidate |
|---|---:|---:|---:|
| Median low-pulse half-amplitude width | 25 ms | 65 ms | 30 ms |
| Nominal 35–140 Hz RMS relative to mono full-band RMS | -0.36 dB | -1.79 dB | -0.92 dB |
| Peak after level matching | -15.69 dBFS | -12.48 dBFS | -8.23 dBFS |

The candidate shortens the long low-frequency tail and brings the low-band
proportion closer. It still has higher peaks relative to average level than the
reference. This is not proof of a perceptual match or evidence that further
compression is necessarily desirable.

Reference spectrograms have considerably more sustained energy around the
lower mids. The reference contains a complete arrangement; our study contains
only drums and bass. That difference cannot be assigned to the kit alone.
High-frequency energy contains mixed clap, hat and other content; no individual
reference clap/hat envelope was extracted or claimed.

All audio excerpts use constant gain to match **stereo RMS to -25 dBFS**.
This is electrical level matching, not LUFS/perceived-loudness matching. No
normalizing limiter, compression or time stretching was added to the exports.
Render tails are excluded from measurements and comparison excerpts.

Envelope method: mono sum, smooth tapered nominal 35–140 Hz frequency mask,
5 ms RMS windows, peak alignment, per-hit peak normalization, median of 15
pulses. Reference is a full mix; SQ-4 envelopes use isolated kick renders.
Band filtering and RMS windows broaden transients, so widths are comparative,
not exact instrument envelope settings. The earlier rough 35 ms estimate used
a different window/filter and excerpt; this controlled comparison supersedes it.
Spectrograms use FFmpeg/Hann, identical 24 kHz rate, logarithmic frequency and
an identical 80 dB display range. Images inspected for legibility.

## Reproduce

1. `node scripts/reference-drum-study.mjs` exports validated portable sessions.
2. Render before, after, before-drums and after-drums using the existing
   `sq4_host_test --render-session` command (four bars per scene).
3. Run `scripts/analyze-drum-study.py` with Python providing NumPy and Pillow;
   FFmpeg must be on PATH. No external API or upload is used.

All four native renders passed finite-output, audibility and clipping checks.
The composition/export script validates session structure. No factory data or
playback-engine changes were necessary.
