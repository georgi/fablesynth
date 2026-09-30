# DR-1 POLY controls

Implemented September 22, 2026 for web DR-1, web SQ-4 hosted DR-1, and native DR-1 standalone/AU/VST3. Native SQ-4 support was added September 23, 2026, including POLY and micro-timing metadata in sessions, clip clipboard, hosted editing, and playback.

The disclosure opens a selected-lane inspector without enabling timing or auditioning a pad. It includes GRID/FIT, 1–16 steps, FIT cycle, fixed source bar, rotation, source-cell editing, validation, and undo/redo. Retained disabled configurations participate in source-bar validation. Standalone keeps four stored bars; hosted shortening rejects a referenced source and identifies its lane. GRID/FIT timing badges remain visible when the panel is closed.

Web sequence commits copy, validate, dirty, and publish `{patterns, chain, drumRhythm}` together. History contains the same complete state, with one entry per held stepper gesture. The SQ-4 bridge loads all metadata, clears history on target changes, and publishes through the session owner's pending/live clip target. Explicit rhythm removal reaches the worklet as `rhythm: null`. The remembered FIT cycle is editor state on web and the existing inactive cycle field on native; the portable rhythm schema remains version 1.

Native controls use JUCE components and `DrumUiModel::commitSequence`. The processor validates the batch and publishes its existing complete triple-buffered snapshot. Plugin state retains disabled settings and first-use information. Context revisions clear undo on kit/state replacement, including reloading the same program. Small native sequencers retain effective control sizes and scroll the lane area independently of the surrounding rack scale.

Per-lane telemetry encodes source bar and cell outside history and persistence. Enabled lanes never borrow the ordinary grid cursor. Live edits retain event cursors; changed FIT timing repositions only that lane. GRID changes retain the next global event, including after a swing edit. Native coincident ordinary/POLY hits share ascending pad-order choke dispatch.

## Verification

Passed:

- 157 focused Vitest tests across the drum store, controls, rhythm, worklet, kits, hosted bridge, session store, and exports.
- Native `drum_rhythm_test`, `drum_engine_test`, and `drum_host_test --poly-lifecycle`.
- Native SQ-4 host suite (the original capability gate has been replaced by hosted metadata checks).
- All DR-1 processor/sequencer checks within the full host suite, including complete audio-boundary publication, dirty marking, source cursors, first-enable defaults, undo/redo, and fresh/same-instance state restoration.
- Browser interaction scenarios in `tests/drum-poly-ui.js` and `tests/drum-poly-hosted-ui.js`: silent disclosure/selection, source editing from another displayed bar, numeric drafts/Enter/Escape, shortcut isolation, metadata-only hosted edits, focus isolation, and keyboard undo.
- Browser layout at 1280/600 px and touch emulation at 390/320 px: no page-wide horizontal overflow; touch inspector targets at least 44 px. Native snapshots at 1000/650 px exercise the wrapped inspector and scrolling lanes.
- Web production build and DR-1 standalone, VST3, and AU builds. Built artifacts remain under `juce/build/FableDrum_artefacts/Release/`; installed applications/plugins were not replaced.

Screenshots and check logs are in ignored `build/poly-controls/`.

Broader checks still fail outside POLY:

- `drum-fx.test.ts`: two drive-aliasing assertions.
- `worklet-fidelity.test.ts`: amp decay threshold assertion.
- These three web failures reproduce with the unchanged HEAD worklet.
- The full native drum host command exits nonzero in its shared FX-page check: GROUP FX echo telemetry is absent. The test now uses the current GROUP FX/EDIT tabs rather than obsolete FX CHAIN/SOUND names. The existing engine telemetry accessor clamps a group request to pad 0; POLY tests pass independently. No FX signal path was changed for this feature.

The native build was run with `SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.4.sdk` to match the installed linker; JUCE's helper otherwise picked the incompatible CommandLineTools 27 SDK. Browser/native snapshots and automated timing checks were performed; this is not a claim of listening approval or an external DAW session test.

## Review revision: stable grid and audible length edits

- Removed the duplicate source-step strip in both editors. Edit hits in the
  original lane; cells beyond its active source/length remain stored and dimmed.
- Reserved the inspector footprint so opening POLY does not move step targets.
- Editing length, rotation, source, or timing activates the lane in the same
  undo transaction. Explicit OFF still retains the configuration and notes.
- The native processor regression drives the actual STEPS input during playback
  and checks repeated three-step playback, source positions, note preservation,
  and undo. Browser geometry was checked before/after opening the inspector.

### FIT timing display

FIT now paints source steps at their exact fractional onsets across the lane's
full cycle. Each lane keeps its existing vertical position. Quarter-note marks
and bar numbers distinguish one- and two-bar cycles; source-step numbers and
rotation agree with the scheduler. Clicks and the playhead use the same mapped
source cells. Excluded notes stay stored and return when length is increased or
FIT is disabled. Native geometry tests compare directly with scheduler events.

## Native SQ-4 metadata

`ClipData` carries the optional v1 `drumRhythm` document. Session and clipboard
codecs preserve configured OFF lanes, GRID/FIT, source bars, rotation, and signed
lane/step delays. Unknown versions, invalid offsets, out-of-range sources, and
rhythm metadata on non-drum tracks are rejected before replacing the session.

Complete note/metadata edits travel in one audio command. Pending and live
metadata follow the same clip slot as their note bytes; launching a plain clip
clears previous timing. The shared lane scheduler reads the live clip, with
sample splits for early/late hits and scene/stop boundaries. Incoming early hits
clamp to the launch, and per-lane cursors feed the hosted inspector. Import and
playback support sixteen bars; hosted authoring retains its four-bar limit.

## Commit-time verification (September 30, 2026)

The current snapshot passes 184 focused web tests, the web production build,
`songs:check`, native compilation of the drum/SQ-4 test targets,
`drum_rhythm_test`, `drum_engine_test`, `sq4_engine_test`, and `sq4_host_test`.

The current `drum_host_test --poly-lifecycle` run fails two
`small native controls stay usable` assertions at 1000 and 650 pixels. Its
playback, sequence transactions, undo/redo, state restoration, and FIT geometry
checks pass. These layout failures remain unresolved and supersede the earlier
lifecycle pass recorded above. Logs are in ignored `build/commit-checks/`.
