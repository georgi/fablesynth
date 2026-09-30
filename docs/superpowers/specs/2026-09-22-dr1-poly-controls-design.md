# DR-1 POLY Controls Design

Status: proposed  
Scope: Web DR-1, native DR-1 standalone/plugin, and Web SQ-4 hosted DR-1  
Authoring note: this proposal is based on source inspection; it does not include implementation changes.

## Summary

Add a compact selected-lane POLY inspector to the existing drum sequencer. Keep the 16-lane grid as the primary editing surface and expose detailed rhythm controls for one selected lane at a time.

Opening the inspector must not enable POLY or alter playback. Enabled lanes retain a compact timing badge when the inspector is closed.

Native DR-1 uses JUCE components, not a WebView. Web and native should provide equivalent controls backed by their respective complete sequence models.

## MVP layout

```text
STEP SEQ   BAR [1][2][3][4]   LENGTH [- 1 BAR +]   [POLY v]
16 CLAVE   FIT 3/1B   . # . . . . . . . . . . . . . .
15 TOM     GRID 15    # . . . # . . . # . . . # . . .
... existing sixteen-lane grid ...

POLY - PAD 16 CLAVE                              [ON]
MODE [GRID | FIT]   STEPS [- 3 +]   CYCLE [1 BAR | 2 BARS]
SOURCE [BAR 1 v]    ROTATE [< 0 >]               [VIEW SOURCE]
SOURCE CELLS        [#][.][#][.][.] ... [.]
3 equal slots per bar - Straight timing
```

Use the existing dark panels, mono readouts, cyan selection, and amber accents. Reuse the visual treatment of existing steppers, but bind the new controls to sequence state rather than synthesis parameters.

Native DR-1 has a dedicated Sequencer page with roughly 605 logical pixels of workspace. Reserve approximately 130-150 logical pixels for the inspector and let the lane grid use the remaining space. On the web, place the inspector immediately below the grid.

## Controls and interactions

### POLY disclosure

- Opens or closes the inspector without changing playback.
- Enabled lanes display `GRID 15`, `FIT 3/1B`, or the corresponding current configuration as a compact badge.
- Clicking a lane name or badge selects that lane for editing. Selection must be silent and must not audition its pad.

### ON / OFF

- Enables custom timing for the selected lane.
- OFF restores ordinary chain or clip playback while retaining the lane's POLY settings for later re-enabling.
- OFF is not a mute control.
- First enable defaults to GRID, 16 steps, rotation 0, and the currently edited source bar.

### MODE: GRID / FIT

- GRID advances on the global sixteenth-note clock and follows global swing.
- FIT divides its cycle into the configured number of equal slots and remains straight.
- Switching modes preserves compatible lane fields.
- The editor remembers the last FIT cycle while the lane is in GRID mode.

### STEPS

- Integer range: 1-16.
- Both modes read the first N source cells.
- In FIT mode, N is the number of equal subdivisions, including rests; it is not a hit-count control.
- Changing the step count normalizes rotation to `rotation % steps` in the same transaction.

### CYCLE

- Visible in FIT mode only.
- Values: 1 BAR or 2 BARS, represented as four or eight quarter-note beats.
- In GRID mode, replace it with the explanatory text `1/16 GRID - REPEAT EVERY N STEPS`; GRID has no separate cycle field.
- First switch to FIT defaults to 1 BAR.

### SOURCE

- Standalone DR-1: selects one of the four stored pattern bars, independently of the current sequence length.
- Hosted DR-1: selects one of the bars available in the clip.
- The selected source remains fixed while playback advances.
- `VIEW SOURCE` navigates the main grid to that bar without changing rhythm state.

### ROTATE

- Integer range: 0 through `steps - 1`.
- The right arrow increments rotation and visually moves the phrase right; arrows wrap.
- Rotation changes cell lookup without rewriting note data or restarting the lane clock.

### SOURCE CELLS

- Shows and edits the selected lane's actual stored cells in source order.
- Uses the existing OFF -> ON -> ACCENT -> OFF interaction.
- Remains available while the main grid is showing another bar.
- Cells beyond the active step count are dimmed but preserved and remain editable for later expansion.

In the main grid, highlight the active source range only while viewing that lane's source bar. Other bars remain editable, but must not appear to be the currently sounding source.

## Validation and transactions

- Numeric fields support arrow buttons and direct entry.
- Enter or blur commits a valid value; Escape restores the previous value.
- Invalid drafts display an inline error and never reach the playback engine.
- Every committed mutation validates, marks the program or clip dirty, and publishes one complete `{patterns, chain, drumRhythm}` sequence state.
- A stepper press-and-hold gesture produces one undo entry.
- Undo restores notes and rhythm metadata together.

Hosted clip shrinking must be rejected if it removes a source bar referenced by any retained lane configuration, including disabled lanes. The UI should identify the conflict, for example:

> BAR 4 is used by CLAVE. Change its source before shortening.

Standalone sequence-length changes do not remove the four stored pattern bars.

## Live-edit semantics

All committed edits apply at the next audio processing boundary while preserving musical phase:

- Note, rotation, and source changes preserve event cursors.
- Timing, step-count, and FIT-cycle changes reposition only affected lanes.
- Enabling or disabling schedules the next valid event without replaying historical hits.
- Tempo and swing changes preserve the current beat position.
- Ordinary lanes retain the existing grid cursor.
- POLY lanes receive a source-cell cursor derived from per-lane telemetry.
- Overall bar progress remains separate from lane source position.

Do not reuse the ordinary `curStep` highlight for enabled POLY lanes.

## Accessibility and responsive behavior

- Use labeled buttons, radio groups for mode and cycle, and named numeric inputs.
- Keyboard interaction inside the inspector must not trigger pad shortcuts or sequencer deletion.
- Provide visible focus and non-color indicators for selected and enabled states.
- Secondary labels must remain legible against the dark panel.
- Desktop controls need at least 24-pixel effective targets; touch layouts need 44-pixel targets.
- Do not announce playback cursor ticks to screen readers.
- Below 700 pixels, wrap the inspector into two columns and allow only the grid and source strip to scroll horizontally.
- At small native window sizes, use a wrapped inspector and scrollable lane region instead of shrinking controls below usable size.

## Implementation map

### Web DR-1 controls

- Extend `src/drum/components/StepSeq.tsx`.
- Add `src/drum/components/PolyLanePanel.tsx`.
- Add a controlled sequence-number input under `src/drum/components/`.
- Update `src/drum/drum.css`.
- Update `src/drum/hooks/useDrumKeys.ts` to isolate focused controls from drum shortcuts.

### Rhythm helpers and web state

- Extend `src/drum/rhythm.ts` with default-lane and immutable update helpers.
- Validate against the actual available source-bar count.
- No rhythm schema expansion is required for the MVP.
- In `src/drum/store.ts`, replace byte-only history with complete `{patterns, chain, drumRhythm}` snapshots.
- Add `commitSequence`, `setLaneEnabled`, `updateLaneRhythm`, source-cell editing, and silent lane selection operations.
- Publish standalone changes through `drum-synth.ts` using `setSequence`.

### Web SQ-4

- Update `src/seq/components/DeviceView.tsx` to load and observe complete drum rhythm state, preserve echo guards, and clear complete history when the focused clip changes.
- Add `updateDrumClipSequence` to `src/seq/store.ts` for atomic byte, bar-count, and rhythm replacement with resize validation.
- Route hosted edits through the session owner and device `updateClip` path.
- Preserve pending/live target identity and explicit `rhythm: null` replacement semantics.
- Share the React inspector only after this complete-state bridge exists.

### Native DR-1 controls

- Extend `juce/source/drum/ui/StepSeqView.h` and its implementation.
- Add a native `PolyLanePanel` JUCE component.
- Update `juce/source/drum/DrumDeviceBody.cpp` for layout.
- Adjust `StepSeqView` mouse interception so child controls can receive input.

### Native model and publication

- Add complete sequence read/write operations and POLY capability reporting to `juce/source/drum/ui/DrumUiModel.h`.
- Implement those operations in `juce/source/drum/DrumEditor.cpp`.
- Extend `DrStepSnapshot` with rhythm metadata.
- In `juce/source/drum/DrumProcessor.cpp`, validate and apply batch edits on the message thread, mark the program dirty, and publish through the existing complete triple-buffered sequence snapshot.
- Existing rhythm setters must mark state dirty when invoked by user edits.

The native data path is:

```text
JUCE controls -> DrumUiModel -> processor sequence transaction
              -> existing atomic sequence snapshot -> audio engine
```

There is no reason to introduce a WebView for this feature. If one is introduced later, it should submit the same complete transaction with explicit target identity and revision.

### Telemetry

- Extend the web worklet adapter and native processor/model with bounded per-lane position snapshots.
- Keep cursor subscriptions isolated so playback telemetry does not cause broad UI rerenders.
- Telemetry must remain outside undo and persistence state.

### Native SQ-4 capability gate

Native SQ-4 remains outside the MVP because its hosted drum model does not yet expose the complete POLY authoring contract. Keep authoring unavailable there and retain unsupported-import rejection until `HostedDrumModel` and the clip/transport model support complete rhythm updates.

## Shipping scope

The MVP includes:

- All controls described above.
- Source-cell editing.
- Complete sequence undo/redo.
- Kit, session, and plugin-state persistence.
- Correct ordinary and POLY lane feedback.
- Web DR-1 and native DR-1 parity.
- Web SQ-4 support after its atomic clip transaction bridge is implemented.

Optional follow-ups:

- Euclidean `HITS / APPLY` generation.
- Multi-lane editing.
- Rhythm presets.
- Time-proportional cycle preview.
- `RESET CYCLES NEXT BAR` performance action.

## Verification

### Focused automated tests

State tests:

- First-enable defaults.
- Disabled-setting retention.
- Rotation normalization.
- Immutable copies.
- Source-bar bounds.
- Rejected hosted clip shrink.
- One-step undo restoring notes and rhythm together.

UI tests:

- Opening the panel without enabling POLY.
- Silent lane selection.
- Source editing while another bar is displayed.
- GRID/FIT-specific controls.
- Direct-entry validation.
- Keyboard focus and shortcut isolation.

Hosted bridge tests:

- Metadata-only edits.
- Focus changes without cross-clip leakage.
- Pending versus live targeting.
- Explicit rhythm removal.

Native tests:

- Complete batch publication.
- Program dirty indication.
- Undo/redo.
- Fresh-instance and same-instance state restoration.
- SQ-4 capability gating.

Playback tests:

- Control-driven rotation and source changes.
- Disabling POLY while running.
- Live tempo edits using actual trigger traces.
- Lane cursor mapping.
- Coincident choke-group hits.

### Manual acceptance scenario

1. Create a 15-step GRID closed-hat lane over an ordinary kick.
2. Add independent FIT-3 and FIT-5 lanes.
3. Rotate a lane and change its source while playback continues.
4. Change tempo and swing while playback continues.
5. Disable and re-enable lanes.
6. Confirm uninterrupted phase and coherent source cursors.
7. Undo and redo changes.
8. Save and reload the kit, session, and native plugin state.
9. Verify the controls at minimum/default native sizes and narrow web widths.

No action should cause historical hits to replay, unrelated lanes to restart, or ordinary playback to fire an early step.
