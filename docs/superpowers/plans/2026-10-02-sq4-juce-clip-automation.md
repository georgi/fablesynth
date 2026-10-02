# SQ-4 JUCE Clip Automation — Implementation Plan

Status: implemented. Native playback, codecs, authored lane export, compiler
goldens, and worklet trace parity are covered by the engine and host tests.
The web trace fixture is refreshed with `UPDATE_AUTO_FIXTURES=1 npm test --
src/engine/native-automation-fixtures.test.ts`. The subsequent native UI pass
adds the web-style automation panel to SQ-4 device focus: lane chips, target
picker/LEARN, DRAW/LINE/POINT, snap, CLIP/GRID/FIT/PAD timing, and lane actions.
Editing helpers are checked against web-generated fixtures. Native host tests
cover gestures, undo/redo, cancellation, selected drum pads, persistence, and
rendered UI snapshots. DR-1 standalone automation remains outside this pass.

Spec: `docs/superpowers/specs/2026-09-30-sq4-clip-automation-design.md`
Web reference: `src/seq/clipAutomation.ts`, `autoTick`/`autoRead` in
`src/engine/worklet.js`, `src/bass/engine/worklet-bass.js`,
`src/drum/engine/worklet-drum.js`.

## Goal

The native SQ-4 plays `ClipDoc.automation` with the same values and timing
as the web. Authored songs carry their lanes into `AuthoredSessions.gen.h`, so
`sq4_host_test --render-preset` renders automation.

## Scope

In scope:

- The session model, the JSON codecs, and the clip clipboard/library carry lanes.
- Lane compilation in C++ (port of `compileAutomation`).
- Audio-thread playback in hosted mode for WT-1, BL-1, and DR-1, including FX targets.
- The authored-song generator emits lanes.
- Web/native parity tests.
- Native SQ-4 automation editor, matching the web panel and editing operations.

Out of scope for this pass:

- DR-1 standalone `seqauto` playback (the web standalone drum sequencer).
- Knob rings that show the live automated value (also out of scope on the web).

## Design decisions

1. **Compile on the message thread.** The conductor IO compiles lanes into
   absolute-value tables, as `ioScheduleArpClip` compiles the arp today.
   The audio thread only interpolates. A C++ port of `compileAutomation` keeps
   portable JSON sessions, clipboard pastes, and saved state correct. The
   generator therefore emits lanes (points), not tables.
2. **One shared player.** A JUCE-free header `juce/source/dsp/ClipAutomation.h`
   holds the model, compiler, and an `AutoPlayer`. Each engine embeds one
   player, as each engine embeds one `ClipHost`.
3. **Fixed capacity, no audio-thread allocation.** A compiled bank holds at most
   8 lanes × `SQ_MAX_BARS * 16` steps × 16 samples (4096 floats per lane).
   `AutoPlayer` owns two preallocated banks (pending, playing) and swaps them
   with the `ClipHost` pending→playing handoff. The `Cmd` carries a
   `shared_ptr<const AutoBank>`; the audio thread copies it into the
   preallocated slot. The command FIFO already releases the `shared_ptr`s of
   `bytes` and `drumRhythm` off the audio thread; use that same path.
4. **Web-identical clock.** Lane phase = `(frame - hostAnchor) / stepFrames`,
   with the bpm clamped to 60–200, as in the web `autoTick`. Evaluate once per
   sub-chunk of at most 128 samples. The web evaluates once per render quantum
   (128). Native host blocks can be 512+, so split long blocks. Otherwise a
   cutoff ramp steps audibly and parity fails.
5. **Held values.** The player keeps `heldValue[paramIndex]` for each automated
   param. A knob edit or patch load on an automated param writes the held
   value, not the live value. When the playing bank loses a lane (clip stop,
   new clip without that lane, lane removed), the held value returns. This
   matches the web `autoHeld` map. Use a fixed array plus a bitset per engine,
   not a map.
6. **Write path per machine.**
   - WT-1: write `p_`, `ps_`, `pt_`, `rampTarget_` (as `Engine::setParam` does).
     The table is already smooth at 16 samples/step, so do not ramp twice.
   - BL-1: write `target_` and the smoothed value, so the smoother does not lag
     the table.
   - DR-1: write `p_` without `snapSmoother` for pad params (read at trigger,
     as on the web). Pad/group FX params move continuously.
   - FX targets (`fx.*`, `pad<i>.fx.*`, `master.volume`): set a dirty flag.
     `SeqAudioProcessor` then calls `bassFx_/wtFx_[i].setParams(params)` before
     `process` for that chunk. Today the FX parameters change only in
     `loadTrackParams`.

## Tasks

### Task 1 — Model and codecs

Files: `juce/source/seq/dsp/SeqModel.h`, `juce/source/dsp/ClipAutomation.h` (new),
`juce/source/seq/SessionCodec.cpp`, `juce/source/seq/ClipClipboardCodec.cpp`,
`juce/source/seq/ClipLibraryStorage.cpp`.

1. Define `AutoTime { Clip, Grid, Fit, Pad; steps; cycleBeats }`,
   `AutoPoint { t, v, c, hold }`, and `AutoLane { target, enabled, time, points }`.
2. Add `bool hasAutomation; std::vector<AutoLane> automation;` to `ClipData`.
3. Port `validateAutomation` to C++: at most 8 lanes, `AUTO_MAX_STEPS`, and
   `AUTO_MAX_POINTS`. Accept only continuous targets that `idFromString` of
   the machine resolves to Lin/Log, excluding `seq.bpm` and `master.swing`.
   `pad` mode is valid on DR-1 only.
4. Read and write `automation` in `SessionCodec` next to `drumRhythm`.
   Omit the field when it is empty, as the web does.
5. Make the clipboard and clip-library codecs carry the lanes.

Test (`sq4_engine_test`): round-trip a session with one lane per time mode.
Reject an enum target, a 9th lane, and `pad` mode on WT-1.

### Task 2 — Compiler port

File: `juce/source/dsp/ClipAutomation.h`.

1. Port `laneCycle`, `bend`, `evalLane`, `compileAutomation` (first lane per
   target wins; skip disabled/empty lanes).
2. Map points with the machine's `normToValue` (WT `Params.h`, `BassParams.h`,
   `DrumParams.h`).
3. Output an `AutoBank { int count; AutoLaneTable lanes[8]; }` with
   `{ paramIndex, len, fit, rot, isFx, float table[4096] }`.

Test: a golden fixture. Add `juce/test/fixtures/web-automation-tables.json`,
generated by a small vitest helper from `compileAutomation`, one case per
mode and curve. Include a log cutoff, a bent segment, a hold, a wrap, and a
DR-1 PAD lane with rotation. The native test compares the values within 1e-5.

### Task 3 — Player and engine integration

Files: `ClipAutomation.h` (`AutoPlayer`), `juce/source/dsp/ClipHost.h`,
`juce/source/dsp/Engine.h/.cpp`, `juce/source/bass/dsp/BassEngine.h/.cpp`,
`juce/source/drum/dsp/DrumEngine.h/.cpp`.

1. Add `AutoPlayer::prepare()`, `schedule(bank)` (into the pending slot),
   `update(bank)`, `swap()`, `clear()`, and
   `tick(frame, anchor, bpm, writeFn, fxDirty&)`.
2. Call `swap()` from the existing `ClipHost::tick` `onSwap` hook. On
   stop/clear, release all held values.
3. Extend `hostClip`/`hostClipUpdate` with an optional `const AutoBank*`.
   On an update with a pending clip, attach to pending, else to playing.
   This is the same rule as the web `case 'auto'`.
4. Split `render` into at most 128-sample chunks while lanes are active.
   Call `tick` at each chunk start. Reuse the existing boundary split loop.
5. Route knob edits and `loadTrackParams` through the held value for
   automated params. Find the native knob → engine write path first
   (device editor → `SeqProcessor`) and add the check there once.

Tests (`sq4_engine_test`):

- A WT-1 GRID lane on `filter.cutoff`: the param at known frames equals the
  table value.
- A knob edit during automation changes the restored value only.
- After a clip stop, the stored value returns.
- A FIT lane (3 slots over 2 bars) and a DR-1 PAD lane with POLY rotation
  hold their phase.
- Allocation-free: the capacity is unchanged after launch/update/stop
  (same pattern as the `ClipHost` capacity hooks).

### Task 4 — Processor wiring and FX

File: `juce/source/seq/SeqProcessor.cpp/.h`.

1. Add `std::shared_ptr<const AutoBank> autoBank` to `Cmd`. Compile it in
   `ioScheduleArpClip` and `ioUpdateArpClip` when `clip.hasAutomation`.
   Confirm that the conductor routes clips with automation through the
   `Arp` IO variants, or extend `ioScheduleClip`.
2. Pass it to `drum_/bass_/wt_[i].hostClip(..., autoBank.get())`.
3. In `renderBass`/`renderWt` (and the DR-1 FX path), call
   `fx.setParams(params)` before `process` when the engine reports `fxDirty`.
4. Make the conductor push an update when only the automation of a clip
   changes (the web store already does this).

Test (`sq4_host_test`): render a session with a WT-1 `fx.delay.mix` lane.
The wet energy follows the lane over two cycles. A session without lanes
renders bit-identically to the build before the change.

### Task 5 — Generator and fixtures

Files: `scripts/generate-authored-sessions.mjs`,
`juce/source/seq/dsp/AuthoredSessions.gen.h` (regenerated).

1. Emit `scene.clips[t].hasAutomation = true;` and one `automation.push_back`
   per lane. Emit the points sparsely, in a stable order.
2. Reject a lane whose target `nativeSession` strips (web-only FX such as
   `fx.comp.*` on BL-1), as the script already rejects enabled web-only FX.
3. `web-session-presets.json` already passes `automation` through
   `nativeSession`. Make the native fixture test compare lanes too.
4. Run `npm run songs:generate`, then `npm run songs:check`. With no authored
   lanes yet, the header diff must be empty.

### Task 6 — Parity check

1. Add a render-parity case: one portable session with a lane on each
   machine (WT cutoff, BL-1 cutoff p-locks with holds, DR-1 PAD-mode hat decay,
   DR-1 group FX send).
2. Render it through `sq4_host_test --render-session`. Compare the per-chunk
   param trace against the web worklet harness
   (`src/engine/worklet-automation.test.ts`) at the same frames.
3. Compare param traces, not audio. The voice DSP differs in float detail.
   The traces must agree within 1e-4 at 128-sample resolution.

## Validation sequence

Run each step only after the previous one passes:

```sh
npm test -- src/seq/clipAutomation.test.ts src/engine/worklet-automation.test.ts src/seq/sessionExport.test.ts src/seq/songs/tidalMemory.test.ts
npm run songs:generate && npm run songs:check
cmake --build juce/build --target sq4_engine_test sq4_host_test FableSeq_Standalone -j4 > build/logs/juce-auto.log 2>&1
juce/build/sq4_engine_test
juce/build/sq4_host_test_artefacts/Release/sq4_host_test
```

Then render TIDAL MEMORY and PHASE RUNNER and compare them with the current
renders. With no lanes, the output must be unchanged.

## Risks

- **Block size.** Without the 128-sample split, native ramps step at the host
  block size. The parity test catches this at a 512-sample block.
- **Smoother interaction.** BL-1 and DR-1 smoothers can lag or snap. Write the
  smoothed value directly for automated params and verify with the trace test.
- **FX setParams cost.** Calling `setParams` each chunk can recompute filter
  coefficients. Call it only when the dirty flag is set. Measure the CPU of one
  render before and after.
- **Held value on a patch swap.** A patch load during automation must update
  the held values. If it does not, the old patch value returns at clip stop.
  Task 3 covers this.

## After this plan

1. Author the first lanes on TIDAL MEMORY: the REFLECTION cutoff drift
   (GRID 28) and one chord delay throw in LOW WATER.
2. Render before/after at matched loudness for listening feedback.
