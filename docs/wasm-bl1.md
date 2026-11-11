# BL-1 on WebAssembly (proof of concept)

This proof of concept compiles the native BL-1 engine (`juce/source/bass/dsp`)
to WebAssembly and runs it in the web BL-1 AudioWorklet. Open
`/bass/?engine=wasm` to use it. Without the flag, the JavaScript worklet runs
as before.

## Parts

| File | Purpose |
| --- | --- |
| `juce/source/bass/wasm/bass_wasm.cpp` | A C API around one `BassEngine` and one `BassFx`. It adds no DSP. |
| `scripts/build-bass-wasm.sh` (`npm run wasm:bass`) | The Emscripten build. |
| `src/bass/engine/wasm/bl1.wasm` | The committed build output. `npm run build` does not need Emscripten. |
| `src/bass/engine/worklet-bass-wasm.js` | The worklet, registered as `fable-bl-wasm`. |
| `src/bass/engine/bass-synth.ts` | The `?engine=wasm` flag. It sends the module bytes in `init`. |
| `src/bass/engine/bassWasmHarness.ts` | A Node harness, with the same contract as `bassHarness.ts`. |
| `src/bass/engine/worklet-bass-wasm.test.ts` | The parity test against the JavaScript worklet. |
| `src/bass/engine/worklet-bass-wasm.bench.test.ts` | The benchmark. It is skipped by default. |

## Build

1. Install Emscripten: `brew install emscripten` (version 7.0.0 was used).
2. Run `npm run wasm:bass`.
3. Commit the new `src/bass/engine/wasm/bl1.wasm`.

The build uses `-O3 -flto`, `STANDALONE_WASM`, no threads, no file system, no
exceptions and no RTTI. It has a fixed 64 MB heap with memory growth off, so the
worklet's typed-array views never detach. The module has zero imports. It exports
only the `bl_*` functions. A SIMD build (`-msimd128`) was not faster, so the
build does not use it.

## Design

- **Parameter ids:** The worklet reads the id table from `bassParamInfo()` in the
  module (`bl_param_count`, `bl_param_name`). `BassParams.cpp` is the single
  source, so there is no generated or copied id file.
- **No allocation in `process()`:** `bl_render` writes into static 128-frame L/R
  buffers. The worklet copies them into its outputs. Only message handlers
  allocate: table upload and the held-note list in `keyOn`.
- **Tables:** The main thread sends the same tables as before. The worklet copies
  them into module memory (`bl_table_alloc`, `bl_tables_commit`).
- **Telemetry:** The `dynamics`, `echo` and `reverb` messages come from the
  native `FxTelemetry` snapshot at the native rate of about 30 Hz.
- **Step messages:** The worklet sends `step` when the engine's current step or
  pattern changes after a render quantum. It reads `semi`, `acc` and `slide`
  with `BassEngine::readStep`.

New entry points: none in `BassEngine`. The wrapper uses only existing public
methods.

## Measurements

The three factory patches play pattern A for four bars at 48 kHz, through both
engines in Node:

| Patch | JS RMS | wasm RMS | Peak difference | RMS difference |
| --- | --- | --- | --- | --- |
| ACID LINE | -7.66 dB | -7.66 dB | -56.4 dB | -75.5 dB |
| RUBBER SUB | -6.04 dB | -6.04 dB | -91.3 dB | -107.3 dB |
| NEON SQUELCH | -8.84 dB | -8.84 dB | -19.3 dB | -58.0 dB |

Both engines send the same 65 step messages.

**Differences above -60 dB (ACID LINE and NEON SQUELCH peaks):** These are
differences between the JavaScript engine and the C++ port. The wasm build does
not cause them. A native clang build of the same wrapper and sources gives a
NEON SQUELCH render that differs from the wasm render by only -132.5 dB peak.
That native render also differs from the JavaScript render by the same -19.3 dB.
So the shipping plugin already has this difference. In NEON SQUELCH, bar 0
matches at -102 dB RMS. The difference starts in bar 1 at about step 27 and stays
at about -65 dB RMS. It does not grow after that. With the FX off, it is limited
to about 50 single samples on the steep edges of the resonant (0.78), driven
filter. OTT and the compressor make it larger. A change of the LFO, unison,
resonance, accent or slide does not remove it. A sample-level trace of
`BassEngine` against `worklet-bass.js` is the next diagnostic step. That is work
on the port and is outside this proof of concept.

Benchmark (Node 22 on this Mac, 10 s of audio, best of 3, one case per process):

| Render | JavaScript | wasm |
| --- | --- | --- |
| Heavy patch (7 voices), full FX | 36.4x realtime (73.3 µs/block) | 56.1x realtime (47.5 µs/block) |
| ACID LINE, pattern A | 71.0x realtime (37.5 µs/block) | 74.4x realtime (35.8 µs/block) |

Size of `bl1.wasm`: 204,098 bytes raw and 66,287 bytes gzipped (`gzip -9`).

Headless Chromium check of `/bass/?engine=wasm`: the processor was
`fable-bl-wasm`. After power-on and play, it sent 33 step messages in about
3.5 s (steps 0, 1, 2 …). The sequencer cursor moved from step 2 to step 9. The
peak output RMS was -4.4 dBFS. There were no console errors.

## Not implemented

- The hosted SQ-4 messages are implemented. See `docs/wasm-sq4.md` for the
  remaining hosted differences.
- The side-chain key input is not connected to `BassFx::process`.
- `comp.trace` in `dynamics` is always empty. `echo.input` is always -90 dB
  because the native meter does not measure the delay input.
- The arp accepts at most 192 notes (`kMaxArpCycle`). The JavaScript worklet
  accepts 1024.
- A rhythm with one step that loops on one pattern sends one `step` message
  only, because the worklet detects a change of the step.
- The worklet compiles the module synchronously when `init` arrives. That takes
  a few milliseconds for 204 kB.

## Recommendation

Port the other three engines, one at a time, in this order: DR-1, WT-1, then
SQ-4 last. The case for the port:

- One DSP source removes the parity work between two engines. The parity work is
  the largest maintenance cost in this repository.
- The wasm engine is as fast or faster, and it is small.
- The wrapper is thin, and the C++ needed no changes.

Before you start each engine:

1. Fix or accept the JS/C++ differences that the parity test finds. NEON
   SQUELCH shows that the two engines do not sound identical now.
2. Done: the wrapper implements the hosted SQ-4 protocol (`docs/wasm-sq4.md`).
3. Make wasm the default only after a listening comparison.
