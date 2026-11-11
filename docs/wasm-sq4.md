# SQ-4 on the WebAssembly engines

`/seq/?engine=wasm` runs all four SQ-4 tracks on the C++ engines compiled to
WebAssembly: DR-1 (`fable-dr-wasm`), BL-1 (`fable-bl-wasm`) and both WT-1
tracks (`fable-wt-wasm`). The conductor, the master bus and the limiter stay
JavaScript. Without the flag nothing changes.

The device ports are described in `docs/wasm-bl1.md`, `docs/wasm-dr1.md` and
`docs/wasm-wt1.md`. This page covers the hosted clip protocol only.

## Parts

| File | Purpose |
| --- | --- |
| `juce/source/dsp/wasm/HostedWasm.h` | Shared static buffers (clip bytes, automation tables, events) and the event drain. |
| `bass_wasm.cpp`, `drum_wasm.cpp`, `wt_wasm.cpp` | `*_host`, `*_host_tempo`, `*_host_clip`, `*_host_stop`, `*_host_clear`, `*_events_take`. |
| `worklet-bass-wasm.js`, `worklet-drum-wasm.js`, `worklet-wasm.js` | The messages `host`, `tempo`, `clip`, `clipupdate`, `auto`, `clipstop`, `panic` (`preserveTransport`). |
| `src/seq/hostedParityHarness.ts` | Drives a worklet through a scripted hosted session. |
| `worklet-*-wasm-hosted.test.ts` | Hosted parity tests, one per device. |
| `src/engine/hosted-reset.test.ts` | The panic and preserved-transport contract, now also for the wasm worklets. |

## Design

- The bridges call the hosted engine methods that `SeqProcessor` uses:
  `setHostClipMode`, `hostTempo`, `hostClip`, `hostClipUpdate`, `hostClipStop`,
  `hostSetFrame` and `takeHostEvents`. The engine's `ClipHost` and `AutoPlayer`
  do the work. There is no new DSP and no change to the C++ engines.
- Each render passes `currentFrame` to `hostSetFrame`. After the render the
  worklet drains the engine events and sends `clipstart`, `clipstop` and `pos`.
  DR-1 `pos` also carries `hits` and `frame`.
- Automation arrives as the precompiled tables that the JS worklets read. The
  worklet copies them into one staging `AutoBank`; the engine copies the bank.
- The JS protocol sends `auto` after `clip` or `clipupdate`; the engine takes
  both in one call. The worklet holds a clip command until the next message or
  render, and adds the `auto` lanes to it. The worklet mirrors the pending and
  playing slots (the engine acks a launch with its tag), so a later `auto`, or
  a `clipupdate` without `auto`, goes to the slot that the JS worklet uses.
- A full `panic` clears the clip host with `setHostClipMode(false)` and
  re-enables it. `panic` with `preserveTransport` only silences the voices.
- DR-1 `tempo` keeps the worklet-drum.js rule: a bpm change on the same anchor
  moves the anchor so the beat position stays. The worklet computes the new
  anchor and passes it to `hostTempo`.
- WT-1 also implements `seqauto` (`setSequenceAutomation`). Hosted, its LAB
  grid follows the shared timebase, as in worklet.js.

## Measurements

Each test plays one session through the JS worklet and the wasm worklet:
`host`, `tempo`, a one-bar clip at a future frame with an automation lane, a
two-bar `clipupdate` with a new lane, and a `clipstop` (2300 blocks). The
`clipstart` and `clipstop` messages are equal, frame for frame, in every case.
The `pos` messages have the same order and steps (31 each).

At 125 BPM a sixteenth is 5760 samples, 45 render blocks, so every step falls on
a block start in both engines:

| Device | Sound | JS RMS | wasm RMS | Peak difference | RMS difference | `pos` block skew |
| --- | --- | --- | --- | --- | --- | --- |
| BL-1 | RUBBER SUB | -9.27 dB | -9.27 dB | -89.2 dB | -108.6 dB | 0 |
| DR-1 | ROOM ONE | -19.08 dB | -19.06 dB | -12.5 dB | -32.5 dB | 0 |
| DR-1 | 808 CLASSIC | -15.24 dB | -15.27 dB | -6.1 dB | -26.6 dB | 0 |
| WT-1 | INIT | -21.28 dB | -21.28 dB | -95.5 dB | -111.3 dB | 0 |
| WT-1 | VELVET PAD | -29.69 dB | -29.69 dB | -97.4 dB | -115.7 dB | 0 |
| WT-1 | ACID LINE | -18.47 dB | -18.47 dB | -81.4 dB | -105.4 dB | 0 |

The DR-1 differences match its standalone parity (-22 to -36 dB RMS,
`docs/wasm-dr1.md`). They come from the engine, not from the hosted path.

At 124 BPM with 0.3 swing and a tempo change to 130 BPM:

| Device | Sound | JS RMS | wasm RMS | RMS difference | `pos` block skew |
| --- | --- | --- | --- | --- | --- |
| BL-1 | RUBBER SUB | -9.21 dB | -9.24 dB | -18.7 dB | 1 block (5 of 31) |
| DR-1 | ROOM ONE | -19.33 dB | -19.32 dB | -33.2 dB | 0 |
| WT-1 | VELVET PAD | -29.60 dB | -29.60 dB | -115.7 dB | 2 blocks (4 of 31) |

Two native behaviors cause the BL-1 difference. The JUCE SQ-4 has them too:

1. The BL-1 engine splits a block at a gate-off and fires a due step inside the
   block. The JS worklet fires it at the next block start. At a fractional step
   length the note onsets then differ by a few samples.
2. On a tempo change, the native BL-1 bar-locked LFO takes its phase from the
   anchor again. The JS LFO keeps adding to its phase.

The `pos` skew is the same split: the native engine reports some steps one or
two blocks (up to 5 ms) earlier.

## Browser check

Headless Chrome (`--mute-audio`, `--autoplay-policy=no-user-gesture-required`)
on `/seq/?engine=wasm`, with vite from this worktree. The default session was
powered on, and the third scene (index 2, a clip on every track) was launched:

- The processors were `fable-sq-master`, `fable-bl-wasm`, `fable-wt-wasm` (two)
  and `fable-dr-wasm`.
- `owner` became `{0: 2, 1: 2, 2: 2, 3: 2}`.
- The post-fader track RMS was -38.9, -15.2, -24.5 and -25.4 dBFS.
- There were no console errors.

These are numerical checks. Nobody has listened to the result.

## Not implemented

- DR-1 sends no `poly` messages. Its `viz` message carries the lane positions.
- The side-chain key input is still not read by BL-1 and WT-1.
- The arp accepts at most 192 notes (`kMaxArpCycle`), also in clips. The JS
  worklets accept 1024. A longer arp clip plays without its arp.
- The hosted differences above (sub-block onsets, BL-1 LFO on a tempo change)
  remain. They are native engine behavior, so the wrappers keep them.
