# WT-1 on WebAssembly

This port compiles the native WT-1 engine (`juce/source/dsp`) to WebAssembly
and runs it in the web WT-1 AudioWorklet. Add `?engine=wasm` to the WT-1 page
URL to use it. Without the flag, the JavaScript worklet runs as before. The
pattern is the same as the BL-1 proof of concept (`docs/wasm-bl1.md`).

## Parts

| File | Purpose |
| --- | --- |
| `juce/source/dsp/wasm/wt_wasm.cpp` | A C API around one `Engine` and one `Fx`. It adds no DSP. |
| `scripts/build-wt-wasm.sh` (`npm run wasm:wt`) | The Emscripten build. |
| `src/engine/wasm/wt1.wasm` | The committed build output. `npm run build` does not need Emscripten. |
| `src/engine/worklet-wasm.js` | The worklet, registered as `fable-wt-wasm`. |
| `src/engine/synth.ts` | The `?engine=wasm` flag. It sends the module bytes in `init`. |
| `src/engine/wtWasmHarness.ts` | A Node harness, with the same contract as `workletHarness.ts`. |
| `src/engine/worklet-wasm.test.ts` | The parity test against the JavaScript worklet. |
| `src/engine/worklet-wasm.bench.test.ts` | The benchmark. It is skipped by default. |

## Build

1. Install Emscripten: `brew install emscripten` (version 7.0.0 was used).
2. Run `npm run wasm:wt`.
3. Commit the new `src/engine/wasm/wt1.wasm`.

The build compiles `Engine.cpp`, `Fx.cpp`, `Params.cpp` and `Wavetables.cpp`.
None of them uses JUCE. `Presets.cpp` and `UserTables.cpp` are not needed,
because the main thread sends the presets and the generated tables.

The flags are the same as for BL-1: `-O3 -flto`, `STANDALONE_WASM`, no threads,
no file system, no exceptions and no RTTI. The heap is fixed at 128 MB with
memory growth off, so the worklet's typed-array views never detach. The heap is
larger than BL-1's, because the factory table pool is 8.6 MB and the user can
add up to 16 user tables. The module has zero imports. It exports only the
`wt_*` functions.

## Design

- **Parameter ids:** The worklet reads the id table from `paramInfo()` in the
  module (`wt_param_count`, `wt_param_name`). `Params.cpp` is the single source.
- **Parameter writes:** `init` and `p` call `Engine::setParam`. That is a direct
  write, as `worklet.js` writes `this.p` directly.
- **Tempo:** The worklet keeps `this.bpm` as `worklet.js` does (`seq.bpm`, or
  the `bpm` message). It calls `Engine::setBpm` and gives the same value to
  `Fx::setParams`. The engine transport is set to playing once, so synced LFOs
  always follow the tempo, as in the web build.
- **No allocation in `process()`:** `wt_render` writes into static 128-frame L/R
  buffers. The FX parameters are read once per quantum. Only the table
  messages allocate.
- **Tables:** `tables`, `table` and `tablecount` are supported. The wrapper
  keeps the slot list and publishes the full set after each message with
  `Engine::setTables`. That copies only pointers. User tables work, because the
  main thread sends them as ordinary table slots.
- **Arp and comp:** `arp` fills static buffers for the notes, hits, accents,
  comp stacks, lengths and strum. The wrapper builds one `ArpPattern`.
- **Telemetry:** `viz` (`a`, `b`, `n`) and `mod` (the 28 route sums) come from
  `Engine::vizA`, `vizB`, `vizActive`, `vizMod` and `vizModAny`, at the same
  2048-sample cadence. `dynamics`, `echo` and `reverb` come from the native
  `FxTelemetry` snapshot at about 30 Hz.
- **Step messages:** The worklet sends `step` when the engine's current step or
  pattern changes after a render quantum. In arp or POLY mode `pat` is 0, as in
  `worklet.js`.

New entry points: none in `Engine` or `Fx`. The wrapper uses only existing
public methods.

## Measurements

Each preset plays the same note script for 2.5 s at 48 kHz, through both
engines in Node. Poly presets play a held chord of three notes and add a fourth
note. Mono presets (`master.mono` = 1) play an overlapped legato line. SEQ plays
a seeded random pattern on the internal sequencer with the default patch.

| Preset | Mode | JS RMS | wasm RMS | Peak difference | RMS difference |
| --- | --- | --- | --- | --- | --- |
| INIT | poly | -19.71 dB | -19.71 dB | -98.0 dB | -110.3 dB |
| VELVET PAD | poly | -26.31 dB | -26.31 dB | -94.1 dB | -111.7 dB |
| CRYSTAL PLUCK | poly | -28.47 dB | -28.47 dB | -39.3 dB | -74.5 dB |
| HYPER SAW | poly | -16.65 dB | -16.65 dB | -59.3 dB | -90.1 dB |
| VOWEL TALK | poly | -12.95 dB | -12.95 dB | -65.2 dB | -97.6 dB |
| CATHEDRAL BELL | poly | -20.66 dB | -20.66 dB | -46.8 dB | -69.5 dB |
| NEURO WOBBLE | poly | -11.71 dB | -11.71 dB | -61.1 dB | -88.4 dB |
| REESE BASS | poly | -13.91 dB | -13.91 dB | -68.6 dB | -98.2 dB |
| FUTURE CHORD | poly | -18.90 dB | -18.90 dB | -68.3 dB | -95.3 dB |
| HARPSI COMB | poly | -34.35 dB | -34.35 dB | -52.9 dB | -84.7 dB |
| PUMP PAD | poly | -21.55 dB | -21.55 dB | -93.7 dB | -111.5 dB |
| GLASS MORPH | poly | -21.91 dB | -21.91 dB | -92.6 dB | -107.0 dB |
| ACID LINE | mono | -19.55 dB | -19.55 dB | -68.5 dB | -100.6 dB |
| SCREECH LEAD | mono | -31.54 dB | -31.54 dB | -93.8 dB | -119.4 dB |
| GLIDE LEAD | mono | -31.04 dB | -31.04 dB | -80.7 dB | -113.2 dB |
| MINI LEAD | mono | -18.20 dB | -18.20 dB | -64.4 dB | -98.5 dB |
| FUNKY WORM | mono | -19.78 dB | -19.78 dB | -56.6 dB | -92.2 dB |
| SEQ | poly | -20.15 dB | -20.15 dB | -99.1 dB | -110.3 dB |

Both engines send the same `step` messages in the SEQ case. The test requires
an RMS difference below -60 dB for every preset (-50 dB for SEQ). Run it with
`WASM_REPORT=1` to print the numbers.

The two largest peak differences (CRYSTAL PLUCK and CATHEDRAL BELL) are
isolated samples. Their RMS differences stay below -69 dB. These are JS/C++
port differences. The wasm build does not add DSP, so the native plugin has
the same difference. This port did not trace them further.

Benchmark (Node 22 on this Mac, 10 s of audio, best of 3, one case per process):

| Render | JavaScript | wasm |
| --- | --- | --- |
| Heavy patch (8 voices, 2 x 7 unison), full FX | 8.3x realtime (321.6 µs/block) | 11.3x realtime (235.5 µs/block) |
| Light patch (1 voice) | 146.7x realtime (18.2 µs/block) | 253.7x realtime (10.5 µs/block) |

Size of `wt1.wasm`: 243,183 bytes raw and 86,426 bytes gzipped (`gzip -9`).

No browser check was made for this port.

## Not implemented

- The hosted SQ-4 messages and `seqauto` are implemented. See
  `docs/wasm-sq4.md` for the remaining hosted differences.
- The side-chain key input is not connected to `Fx::process`.
- `comp.trace` in `dynamics` is always empty. `echo.input` is always -90 dB
  because the native meter does not measure the delay input.
- The arp accepts at most 192 notes (`kMaxArpCycle`). The JavaScript worklet
  accepts 1024.
- A POLY `step` message has no note-event fields. The JavaScript worklet adds
  the fields that `polyEmit` returns.
- A rhythm with one step that loops sends one `step` message only, because the
  worklet detects a change of the step.
- While the internal sequencer plays, the native engine phase-locks synced LFOs
  to the sequencer start. The JavaScript worklet uses a beat clock that starts
  with the audio. This is an existing JS/C++ difference.
- The worklet compiles the module synchronously when `init` arrives.
