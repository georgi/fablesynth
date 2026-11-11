# DR-1 on WebAssembly (proof of concept)

This proof of concept compiles the native DR-1 engine (`juce/source/drum/dsp`)
to WebAssembly and runs it in the web DR-1 AudioWorklet. Open
`/drum/?engine=wasm` to use it. Without the flag, the JavaScript worklet runs
as before. It follows the BL-1 proof of concept (`docs/wasm-bl1.md`).

## Parts

| File | Purpose |
| --- | --- |
| `juce/source/drum/wasm/drum_wasm.cpp` | A C API around one `DrumEngine`. The engine owns its `DrumFx` chains, reverbs and bus outputs. The wrapper adds no DSP. |
| `scripts/build-drum-wasm.sh` (`npm run wasm:drum`) | The Emscripten build. |
| `src/drum/engine/wasm/dr1.wasm` | The committed build output. `npm run build` does not need Emscripten. |
| `src/drum/engine/worklet-drum-wasm.js` | The worklet, registered as `fable-dr-wasm`. |
| `src/drum/engine/drum-synth.ts` | The `?engine=wasm` flag. It sends the module bytes in `init`. |
| `src/drum/engine/drumWasmHarness.ts` | A Node harness, with the same contract as `workletHarness.ts`. |
| `src/drum/engine/worklet-drum-wasm.test.ts` | The parity test against the JavaScript worklet. |
| `src/drum/engine/worklet-drum-wasm.bench.test.ts` | The benchmark. It is skipped by default. |

## Build

1. Install Emscripten: `brew install emscripten` (version 7.0.0 was used).
2. Run `npm run wasm:drum`.
3. Commit the new `src/drum/engine/wasm/dr1.wasm`.

The build uses the BL-1 flags: `-O3 -flto`, `STANDALONE_WASM`, no threads, no
file system, no exceptions and no RTTI. The heap is fixed at 128 MB with memory
growth off, so the worklet's typed-array views never detach. The module has zero
imports. It exports only the `dr_*` functions.

The build links `OneShotSamples.gen.cpp`, because `DrumEngine` reads the
one-shot bank directly. It does not link `SampledTables.gen.cpp`: the main
thread sends those tables in the `tables` message, as before.

## Design

- **Parameter ids:** The worklet reads the id table from `drumParamInfo()` in
  the module (`dr_param_count`, `dr_param_name`). `DrumParams.cpp` is the single
  source.
- **No allocation in `process()`:** `dr_render` writes five stereo buses and the
  KICK key into static 128-frame buffers. The worklet copies them into its six
  outputs. Only message handlers allocate: table upload.
- **One-shot samples:** The module embeds the same bank as the plugin. The
  worklet ignores the `samples` message.
- **Rhythm:** The worklet normalizes the rhythm like `normalizeRhythm` in
  `worklet-drum.js`. It packs it into a static int buffer for `dr_set_rhythm`,
  which fills a `DrumRhythm` and calls `DrumEngine::setDrumRhythm`.
- **Sequence automation:** `seqauto` lanes are copied into a static `AutoBank`
  and sent to `DrumEngine::setSequenceAutomation`.
- **Side chain:** The worklet copies its input channels into static key buffers
  and passes them to `DrumEngine::setSidechainInput`.
- **Telemetry:** The `dynamics`, `echo` and `reverb` messages come from
  `DrumEngine::fxTelemetry(meterPad, bus)` at the native rate.
- **KIT MIX capture:** `padCapture` turns on the native pad taps. The worklet
  pulls 4096-frame `padAudio` blocks from each tap.
- **Step messages:** The worklet sends `step` when the engine's step or pattern
  changes after a render quantum. `hits` comes from `consumeHits()`.

New entry points: none in `DrumEngine`. The wrapper uses only existing public
methods.

## Measurements

Each factory kit plays its own patterns and chain for four bars at 48 kHz,
through both engines in Node. The JavaScript worklet gets the seed of the C++
`Rng` (`0x9e3779b9`).

| Kit | JS RMS | wasm RMS | Peak difference | RMS difference |
| --- | --- | --- | --- | --- |
| TR-VOID | -17.43 dB | -17.43 dB | -37.9 dB | -72.2 dB |
| ROOM ONE | -16.27 dB | -16.28 dB | -33.8 dB | -64.1 dB |
| BITCRUSH | -14.19 dB | -14.19 dB | -37.4 dB | -66.9 dB |
| 808 CLASSIC | -11.58 dB | -11.58 dB | -37.5 dB | -68.4 dB |
| DEEP DUB | -16.33 dB | -16.33 dB | -45.9 dB | -73.8 dB |
| DUST HOUSE | -14.67 dB | -14.67 dB | -36.1 dB | -66.0 dB |
| WAREHOUSE | -11.59 dB | -11.59 dB | -34.5 dB | -63.7 dB |
| METAL WORK | -15.72 dB | -15.72 dB | -41.1 dB | -70.1 dB |
| TAPE KIT | -15.97 dB | -15.97 dB | -49.1 dB | -78.3 dB |
| MINIMAL | -13.97 dB | -13.97 dB | -40.5 dB | -69.8 dB |
| BROKEN TOYS | -14.65 dB | -14.65 dB | -63.5 dB | -93.0 dB |
| LIVE ROOM | -11.71 dB | -11.71 dB | -44.6 dB | -75.3 dB |
| UZU | -12.07 dB | -12.07 dB | -43.7 dB | -71.2 dB |
| 808+UZU HYBRID | -15.43 dB | -15.43 dB | -48.5 dB | -75.5 dB |
| NEON GRID | -13.57 dB | -13.57 dB | -35.9 dB | -70.3 dB |
| ACID CAVE | -11.67 dB | -11.67 dB | -40.4 dB | -70.4 dB |
| BOOM BAP | -14.55 dB | -14.55 dB | -39.3 dB | -71.4 dB |
| PIRATE RADIO | -12.98 dB | -12.98 dB | -41.7 dB | -69.5 dB |
| CC0 IMPACT | -11.68 dB | -11.67 dB | -43.5 dB | -67.8 dB |
| CC0 BOUNCE | -17.77 dB | -17.77 dB | -36.7 dB | -66.8 dB |
| CC0 WAREHOUSE | -11.51 dB | -11.51 dB | -40.3 dB | -68.0 dB |
| CC0 DEEP HOUSE | -12.14 dB | -12.15 dB | -41.9 dB | -72.0 dB |
| CC0 BASS RUSH | -9.03 dB | -9.03 dB | -37.8 dB | -69.4 dB |

The loudness agrees within 0.01 dB for every kit. Both engines send the same
number of step messages for every kit (64 or 65). The parity test asserts a
per-kit maximum RMS difference, about 3 dB above these values. Every kit now
matches at -63 dB RMS or better.

A pad-by-pad trace found five causes of the earlier -12 to -36 dB differences.
All five are fixed:

1. `DrumEngine::renderPad` gave the oscillator and sample layers the pitch
   envelope at the START of each 16-sample sub-block as the ramp target, so the
   pitch ran one sub-block late. It now ramps from the first-sample to the
   last-sample value, like `worklet-drum.js`. This was the main cause: TR-VOID
   went from -28.9 to -72.2 dB.
2. `padMod` ran once per block. It now runs at both block edges, and cutoff,
   resonance, level, pitch, fine and position are interpolated per sub-block.
   The filter takes 32-sample coefficient chunks.
3. The web bus gain started at 0 and did not ramp while the bus was gated, so
   the first hit after a silent start faded in. Both engines now snap the
   master gain on the first parameters.
4. The native echo time (`DrumFx::dlTime_`) glided in float. The 80 ms glide
   stalls up to about 0.7 samples off its target and drifts with rounding, so
   the echoes of a later hit read the line a fraction of a sample off. It now
   glides in double, like the web worklet. BROKEN TOYS: -34.8 to -93.0 dB;
   PIRATE RADIO, DEEP DUB, ACID CAVE and BITCRUSH: about -45 to -70 dB.
5. All noise layers drew from one engine generator. A voice draws noise up to
   the end of the render chunk in which it is killed, and the two engines split
   chunks differently, so every later noise hit could desync. Each hit now
   seeds its own noise generator from the engine generator at the trigger, in
   both engines. This changes the noise samples of the web engine as well (the
   noise statistics are the same). NEON GRID: -47.6 to -70.3 dB. The native
   engine also renders a pad's main voice before its tail and fade voices now,
   in the web order.

A native clang build of the same wrapper and sources matches the wasm renders
at -129 dB RMS or better, so the wasm build adds no difference.

Benchmark (Node 24 on this Mac, 10 s of audio, best of 3, one case per process,
`BENCH=1`):

| Render | JavaScript | wasm |
| --- | --- | --- |
| WAREHOUSE (synthesized), chain | 18.3x realtime (145.8 µs/block) | 20.9x realtime (127.4 µs/block) |
| CC0 WAREHOUSE (samples), chain | 24.4x realtime (109.3 µs/block) | 25.4x realtime (104.9 µs/block) |

Size of `dr1.wasm`: 4,099,187 bytes raw and 2,459,955 bytes gzipped
(`gzip -9`). The embedded one-shot bank is most of it. The JavaScript worklet
loads the same bank as separate WAV files, so the page does not download more
sample data in total. A smaller module is possible if the wrapper receives the
bank in the `samples` message, but `DrumEngine` reads `drumOneShots()` directly,
so that needs a change in the engine.

## Not implemented

- The hosted SQ-4 messages are implemented. See `docs/wasm-sq4.md` for the
  remaining hosted differences.
- POLY rhythm lanes play, but the worklet sends no `poly` messages. The `viz`
  message carries the lane positions.
- `seed` is ignored. The C++ `Rng` always starts at its default seed.
- `samples` is ignored. User samples are not supported; the bank is fixed at
  build time.
- `comp.trace` in `dynamics` is always empty. `echo.input` is always -90 dB.
- `step.hits` lists every pad triggered since the last step message. That
  includes pads played by hand. `padAudio.frames` counts the frames after the
  block, one more than the JavaScript worklet.
- A one-step loop on one pattern sends one `step` message only, because the
  worklet detects a change of the step.
- The worklet compiles the 4 MB module synchronously when `init` arrives.
- The headless browser check ran in SQ-4 only (`docs/wasm-sq4.md`), not on
  `/drum/`.
