# Generated drum kit library

The current batch lives in `build/auditions/kit-library/` (ignored generated audio).
Each numbered kit contains Sources/, Samples/, and Audition.mp3. Delete unwanted
WAVs directly in Samples/. Rebuild audition reels and the CSV index after editing:

```sh
python3 scripts/drum-kit-library.py index build/auditions/kit-library
```

Python requires NumPy, and ffmpeg must be available. On this workstation the
bundled Python is `/Users/mg/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3`.

## Tools

- `scripts/drum-kit-prompts.json`: numbered Dry Machine-inspired source prompts.
- `scripts/kie-drum-stems.mjs generate|poll|separate|download KIT`: resumable KIE
  music → split_stem workflow. Exactly one selected take per prompt is separated.
  It saves task IDs before further operations and does not retry paid submissions.
- `scripts/assemble-drum-kit-library.py`: accepts optional kit IDs to process only new kits; copies downloaded stems to descriptive
  filenames, cuts the drum/percussion stems, then indexes the library.
- `scripts/drum-kit-library.py cut SOURCE OUTPUT --kit NAME --limit 20`: onset
  detector, quiet-start ranking, waveform duplicate rejection, source timestamps,
  and tapered mono 48 kHz PCM16 export with -3 dBFS peak normalization.
- `tests/drum-kit-library-test.py`: silence and known-pulse checks, peak limits,
  spectral labeling, deduplication, and preservation of a deleted sample on rerun.

Names include kit, broad spectral group, source stem, and source time in ms.
LOW/MID/HIGH/MIXED are sorting hints, not verified instrument identifications.
Auditions use alphabetical sample order. CSV includes duration and source time.
The cutter skips an existing manifest, so rerunning does not resurrect deletions.
For a new extraction, choose a new output folder. Indexing reads only surviving
WAVs. Source MP3s and task provenance remain separate from audition samples.

A separated drum stem may still contain simultaneous drums or artifacts. These
are user-curated candidates; no factory bank is changed by these tools.
