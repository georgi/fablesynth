# SQ-4 Native Device Focus Implementation Plan

**Goal:** Replace the JUCE SQ-4 focus-mode `ClipEditView` with the real native
DR-1, BL-1, and WT-1 device panels, wired to SQ-4's existing hosted engines and
the selected session clip.

**Scope:** JUCE SQ-4 only. The standalone DR-1, BL-1, and WT-1 plugins must keep
their current behavior and state compatibility. The web implementation is the
interaction reference, but no web changes are required.

## Outcome and acceptance criteria

When a user focuses an SQ-4 track, the lower focus area shows that machine's
native sound-design panels and native sequencer UI instead of the generic grid.

- DR-1 shows pads, selected-pad synthesis panels, step sequencer, and effects.
- BL-1 shows oscillator, sub, filter, envelope, modulation/keys, pitch sequencer,
  and effects.
- WT-1 shows both oscillator panels, utility/filter/envelopes/LFO/matrix/effects,
  note sequencer, and keyboard where space permits.
- Parameter edits immediately affect only the focused SQ-4 track and persist as
  that track's inline `PatchRef` in the session.
- Pattern edits immediately update the selected `(scene, track)` clip and use
  the existing phase-preserving `Update` command when that clip is live.
- Switching scenes reloads the target clip without changing the track patch.
- Switching between the two WT-1 tracks keeps their parameter and UI state
  independent.
- An empty target still displays the sound-design panels; its sequencer area
  offers `CREATE CLIP`.
- Device-local transport and pattern chaining are hidden/disabled in hosted
  mode because SQ-4 owns transport and one clip is the hosted pattern.
- All standalone plugin tests and state round trips continue to pass.

## Architecture

Do not embed standalone `AudioProcessor` instances as shadow processors. Their
controls would initially bind to duplicate APVTS state, their audition commands
would reach engines that SQ-4 does not render, and every edit would need a
fragile polling bridge.

Instead, split the existing device UI from its processor by introducing narrow
UI model interfaces:

```text
standalone processor -> standalone UI-model adapter -> shared native panels
SQ-4 track           -> hosted UI-model adapter     -> shared native panels
```

The interfaces cover four concerns:

1. Parameter lookup and change notification.
2. Pattern read/write plus the displayed step/bar.
3. Visualization/table data and audition commands.
4. Hosted capabilities such as `canChain`, `canOwnTransport`, and empty clips.

Standalone adapters delegate to the existing processor/APVTS methods. Hosted
adapters delegate to `SeqAudioProcessor`, `Conductor`, and the selected session
clip. No DSP engine is duplicated.

## Model allocation and orchestration

Use **GPT-5.6 Sol** for decisions or changes where a mistake could create
audio-thread races, duplicate state, incompatible parameter APIs, or a broad
cross-product regression. Use **GPT-5.6 Terra** and **GPT-5.6 Luna** for
bounded implementation packages after Sol has fixed the interfaces and written
the relevant handoff contract.

Do not ask Terra or Luna to independently redesign an interface. If an assigned
task reveals that the agreed interface is insufficient, stop that work package
and return it to Sol for an API decision.

### Work-package ownership

| Package | Model | Work | Completion gate |
|---|---|---|---|
| A. Baseline | GPT-5.6 Luna | Build/test all four products, capture current render/test baselines, and document existing failures. No production-code edits. | Reproducible baseline commands and results are recorded. |
| B. Shared architecture | GPT-5.6 Sol | Design and implement `ParameterSource`, `DeviceParameterBank`, the device UI-model interfaces, callback/threading rules, and one representative shared-control migration. Write the adapter contract used by later packages. | Architecture tests pass; code review confirms no shadow processors or unsafe message/audio-thread access. |
| C1. DR-1 extraction | GPT-5.6 Terra | Migrate remaining DR-1 controls/panels to the fixed interfaces and extract `DrumDeviceBody`. Preserve the standalone editor exactly. | DR-1 standalone build, tests, state round trip, and render comparison pass. |
| C2. BL-1 extraction | GPT-5.6 Terra | Extract `BassDeviceBody` using the same fixed interfaces. | BL-1 standalone build, tests, state round trip, and render comparison pass. |
| C3. WT-1 extraction | GPT-5.6 Luna | Extract `WtDeviceBody`; keep user-table persistence behavior unchanged in standalone mode. | WT-1 standalone build, tests, state round trip, and render comparison pass. |
| D. Hosted core | GPT-5.6 Sol | Implement the common hosted-model lifecycle, parameter-bank/session synchronization, command-FIFO patch and audition paths, visualization atomics, clip targeting rules, save/exit flushing, and the DR-1 hosted model as the reference vertical slice. | A focused DR-1 control changes the real SQ-4 engine; live pattern editing is phase-preserving; persistence tests pass. |
| E1. Hosted BL-1 | GPT-5.6 Terra | Implement `HostedBassModel` against Sol's hosted contract and add bass-specific model tests. | BL-1 parameter, clip, playhead, audition, and persistence tests pass. |
| E2. Hosted WT-1 | GPT-5.6 Luna | Implement two independent `HostedWtModel` instances and WT-specific model tests. | Both WT-1 tracks pass isolation, clip, parameter, and persistence tests. |
| F. Focus UI integration | GPT-5.6 Luna | Implement `DeviceFocusView`, focus switching, hosted layout/viewport behavior, CMake source wiring, and empty-clip presentation using the completed models. | All four tracks show the correct native body; navigation and resizing tests pass. |
| G. Legacy removal and test migration | GPT-5.6 Terra | Move remaining `ClipEditView` byte-edit coverage to hosted-model/plugin-boundary tests, remove the legacy component and stale handles, and update comments/docs. | No production or test reference to `ClipEditView`; all targeted tests pass. |
| H. Final integration audit | GPT-5.6 Sol | Review the complete diff for lifetime/threading hazards, parameter/session authority, flush behavior, state compatibility, and cross-machine leakage; fix architecture-level issues. | Sol signs off after the full verification matrix, AU validation, and VST3 scan. |

### Scheduling

Run packages in this order:

```text
A -> B -> (C1, C2, C3) -> D -> (E1, E2) -> F -> G -> H
```

The parenthesized packages may run in parallel because their production-file
ownership is machine-specific. Merge and rerun the complete standalone suite
after C1-C3 before starting D. Merge and rerun hosted model tests after E1-E2
before starting F.

### File-ownership rule

- Sol owns shared interface files, `SeqProcessor` threading/command paths, and
  any change to session/patch authority.
- Terra owns DR-1 and BL-1 panel implementation during extraction, then the
  corresponding hosted models and legacy-test migration.
- Luna owns WT-1 panel implementation during extraction, then the WT-1 hosted
  models and final `DeviceFocusView` composition.
- `juce/CMakeLists.txt` is edited only by Luna in package F unless an earlier
  package cannot compile without source registration; in that case Sol makes
  the minimal temporary registration and documents it in the handoff.
- No two active packages may edit the same file. Shared-header changes go back
  to Sol instead of being patched independently by Terra or Luna.

### Required handoff format

Every package must finish with:

1. Commit hash and exact files changed.
2. Commands run and their results.
3. Public interfaces added or consumed.
4. Known limitations or deferred behavior.
5. A short next-agent note describing assumptions that must remain true.

Sol's package B handoff must additionally include construction/lifetime rules
for `ParameterSource`, callback thread guarantees, parameter-ID mapping, and a
minimal standalone-adapter example. Sol's package D handoff must include the
hosted-model lifecycle, focus retargeting rules, flush contract, and the DR-1
reference implementation Terra and Luna should copy rather than reinterpret.

For hosted parameters, use an editor-owned `DeviceParameterBank` containing
JUCE `RangedAudioParameter` objects built from the existing machine parameter
metadata. Controls bind to those parameters through a lookup interface rather
than directly through APVTS. The bank is loaded from the track `PatchRef`; a
parameter change writes an inline patch and queues a patch update to the real
SQ-4 engine. It is UI state, not an additional audio engine and not part of
SQ-4's host-automatable APVTS surface.

## Phase 1: Add reusable UI model and parameter bindings

### 1.1 Parameter source abstraction

Create:

- `juce/source/ui/ParameterSource.h`
- `juce/source/ui/ParameterSource.cpp`
- `juce/source/ui/DeviceParameterBank.h`
- `juce/source/ui/DeviceParameterBank.cpp`

`ParameterSource` resolves a parameter ID to `juce::RangedAudioParameter*` and
provides parameter metadata. Add an APVTS-backed implementation for standalone
plugins and an owned-bank implementation for hosted tracks.

Refactor these shared controls to accept `ParameterSource&` while retaining
small APVTS convenience constructors during migration:

- `juce/source/ui/Controls.{h,cpp}`
- `juce/source/ui/Displays.{h,cpp}`
- `juce/source/ui/Modulation.{h,cpp}`

The matrix-ring lookups in `Knob` and `VSlider` must use the same source, not
reach back into APVTS directly.

### 1.2 Device UI model interfaces

Create:

- `juce/source/ui/DeviceUiModel.h`
- `juce/source/drum/ui/DrumUiModel.h`
- `juce/source/bass/ui/BassUiModel.h`
- `juce/source/ui/WtUiModel.h`

Keep the base interface small. Machine-specific interfaces expose only methods
already used by their panels: selected pad, table lookup, visualization values,
sequencer cells, playhead, audition, and machine-specific names.

Add standalone adapters next to each processor. Initially they should be thin
delegates, making this phase behavior-neutral.

### 1.3 Verify the neutral refactor

- Build all three standalone plugin targets.
- Run existing WT-1, DR-1, and BL-1 host/UI tests.
- Compare existing headless editor renders before and after the refactor.
- Do not proceed while state, mouse interaction, or render snapshots regress.

## Phase 2: Make each device body reusable

Refactor the full editor racks into reusable body components that omit
standalone-only chrome when hosted.

### 2.1 DR-1

Update:

- `juce/source/drum/DrumEditor.{h,cpp}`
- `juce/source/drum/ui/PadGrid.{h,cpp}`
- `juce/source/drum/ui/PadStrip.{h,cpp}`
- `juce/source/drum/ui/DrumPanels.{h,cpp}`
- `juce/source/drum/ui/StepSeqView.{h,cpp}`
- `juce/source/drum/ui/DrumFxRack.{h,cpp}`

Extract `DrumDeviceBody`. `DrumRack` composes its existing header plus this
body; SQ-4 composes only the body. In hosted mode, replace pattern-bank/chain
controls with clip bar selection and disable local transport.

### 2.2 BL-1

Update the equivalent bass editor/panel files and extract `BassDeviceBody`.
`PitchSeqView` must read/write through `BassUiModel`, and hosted mode must hide
pattern-bank chaining and transport while keeping RAND, note, octave, accent,
and slide editing.

### 2.3 WT-1

Update `PluginEditor`, `Panels`, `NoteSeqView`, `WavetableEditor`, and related
display components to extract `WtDeviceBody`. Keep user-wavetable destructive
operations out of the first hosted version unless the session format is also
extended to persist those tables; factory table selection and all ordinary
sound parameters remain available.

At the end of this phase, standalone editors must still assemble the same
visual hierarchy and geometry they have today.

## Phase 3: Add SQ-4 hosted device models

Create:

- `juce/source/seq/ui/HostedDeviceModel.h`
- `juce/source/seq/ui/HostedDeviceModel.cpp`
- `juce/source/seq/ui/HostedDrumModel.{h,cpp}`
- `juce/source/seq/ui/HostedBassModel.{h,cpp}`
- `juce/source/seq/ui/HostedWtModel.{h,cpp}`

Each model is constructed with a `SeqAudioProcessor&` and track index. The
focused scene is a mutable target, not part of construction, so switching the
scene rail does not rebuild the entire device UI.

### 3.1 Patch flow

Add a message-thread API to `SeqAudioProcessor`/`Conductor` that atomically:

1. Updates `session.tracks[t].patch` to an inline parameter map.
2. Converts it with the existing `computeTrackParams` path.
3. Queues one `Cmd::Patch` for the real hosted engine.

Use a short coalescing timer for continuous drags, but flush it on focus exit,
track switch, editor destruction, state save, and session export. The audible
engine may be updated on every UI tick; session serialization can be coalesced
if profiling shows it is necessary.

Loading a model must suppress callbacks while applying the selected track's
factory or inline patch to its `DeviceParameterBank`.

### 3.2 Clip flow

The hosted sequencer adapter reads and writes the session clip directly using
the existing byte layouts:

- DR-1: `sqDr1Idx` and `SQ_DR1_BYTES_PER_BAR`.
- BL-1/WT-1: `sqNoteIdx` and `SQ_NOTE_BYTES_PER_BAR`.

Every edit calls `Conductor::updateClipBytes`; do not add another clip cache.
Bar selection is UI-only. Bars 1-4 remain editable under the existing hosted
limit. Empty clips route to `Conductor::createClip`.

### 3.3 Visualization and audition

Expose read-only SQ-4 feeds required by the shared panels:

- Table lookup/name and table-generation counters.
- Per-track oscillator position, envelopes/filter/gate where supported.
- Existing step/bar/RMS/scope values.
- Per-track tempo/playing state derived from the conductor.

Publish DSP visualization values through atomics in `SeqProcessor::renderDrum`,
`renderBass`, and `renderWt`, matching standalone processor practice.

Extend the SQ-4 command FIFO with UI audition commands:

- DR-1 pad trigger.
- BL-1 note on/off.
- WT-1 note on/off.

These commands target the selected hosted engine only and do not alter SQ-4
transport ownership. If audition cannot safely coexist with a playing hosted
clip for a machine, its model reports audition unavailable and the relevant UI
is display-only while playback runs.

## Phase 4: Replace `ClipEditView` with the device host

Create `juce/source/seq/ui/DeviceFocusView.{h,cpp}`. It owns one reusable DR-1
body, one BL-1 body, and two independent WT-1 bodies/models, or constructs them
lazily on first focus. Prefer lazy construction if editor-open profiling shows
a meaningful cost.

Update:

- `juce/source/seq/SeqEditor.{h,cpp}`
- `juce/source/seq/ui/TrackHeadsView.{h,cpp}` as needed for focus indication.
- `juce/CMakeLists.txt` to compile the shared panels into `FableSeq`.

`SeqRack::enterFocus(track, scene)` selects the matching body and calls
`setTarget(scene)`. `setFocusScene(scene)` only retargets the current model.
`exitFocus()` flushes pending patch writes and hides the device host.

Preserve the current header, track tabs, single-row scene strip, scene rail,
keyboard shortcuts, and back behavior. Give the device body its own scaled or
scrollable viewport so the WT-1 rack does not force the entire SQ-4 editor to
grow beyond its current aspect ratio.

Once parity tests pass, remove `ClipEditView` from production and CMake. Keep
its low-level byte-edit tests only after moving them to hosted model tests;
then delete the component and stale test handles.

## Phase 5: Tests and verification

### Unit/model tests

Add coverage for:

- Hosted parameter-bank factory and inline-patch loading.
- Parameter edit -> session inline patch -> audio-thread engine params.
- DR-1 and note-machine byte edits for every editable bar.
- Live, queued, and idle clip-update routing.
- Empty clip creation.
- Scene retargeting without patch reset.
- Independent state for WT-1 tracks 2 and 3.
- Pending parameter-write flush on focus exit and save.

### Plugin-boundary tests

Extend `juce/test/sq4_host_test.cpp` to:

- Focus each track and assert the correct device body is visible.
- Exercise one real panel control per machine and verify
  `debugTrackParams(track)` changes only for that track.
- Exercise the real machine sequencer view and verify session bytes.
- Switch between both WT-1 tracks and prove no state leakage.
- Create a clip in an empty focused slot.
- Save/reload the session and verify patch plus pattern edits survive.

### Regression and manual QA

Run:

1. JUCE-free DSP tests.
2. WT-1, DR-1, and BL-1 plugin-boundary tests.
3. SQ-4 host tests.
4. AU validation and VST3 scan.
5. Standalone applications for all four products.

Manually verify focus entry by head, edit-pencil entry, scene rail navigation,
Esc/back, live phase-preserving edits, resizing, high-DPI scaling, and reopening
saved sessions in both SQ-4 and the web app.

## Delivery sequence

Keep the work bisectable in these commits:

1. `refactor(ui): add processor-neutral parameter and device models`
2. `refactor(dr1): extract reusable native device body`
3. `refactor(bl1): extract reusable native device body`
4. `refactor(wt1): extract reusable native device body`
5. `feat(seq): add hosted device models and parameter flow`
6. `feat(seq): render native device bodies in focus mode`
7. `test(seq): cover native hosted device focus and persistence`
8. `chore(seq): remove legacy clip edit view`

The standalone-neutral refactor is the first gate. The SQ-4 integration should
not begin until all three standalone editors pass unchanged, which keeps any
regression attributable to a small phase rather than the final integration.
