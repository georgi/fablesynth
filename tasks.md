# Fable agent loop

## Goal and scope

Integrate the native JUCE CodeAct loop from `../juce-codeact` so an agent can
discover and change every declared Fable parameter: WT-1, DR-1 (all pads), BL-1,
and SQ-4 master/mixer parameters plus every hosted instrument parameter.
Use OpenRouter with `OPENROUTER_API_KEY` supplied by the environment or local
`.env`; never serialize credentials into plugin state, prompts, or logs.
This implementation targets native plugins/standalones; the browser synth and
non-parameter editing (notes, clips, wavetable assets) are outside this task.

## Steps

- [x] Inspect repository guidance, existing edits, and reference architecture.
- [x] Complete focused reference-core and Fable parameter-path investigations.
- [x] Integrate a local copy of the reusable CodeAct core and pinned QuickJS-NG
  dependency; preserve upstream provenance and deterministic tests.
- [x] Adapt the bounded snapshot API for the full parameter catalog, with
  discoverable ranges, discrete choices, and physical parameter values.
- [x] Implement a shared processor-owned controller and parameter adapter.
  Capture on the message thread, run model/JS work on a worker, validate the
  complete proposal before mutation, detect stale values, and use existing
  APVTS gestures / SQ-4 inline-patch and undo paths to apply changes.
- [x] Add a reusable native agent panel to all four editors: prompt, model,
  streaming status, cancellation, change review, Apply, and session reset.
  Keep the loop alive across editor closure and credentials out of saved state.
- [x] Add safe OpenRouter configuration loading and actionable errors for
  missing credentials; keep the endpoint fixed to OpenRouter for `.env` keys.
- [x] Verify core loop behavior with scripted transports and real QuickJS;
  verify parameter coverage, invalid/stale proposals, persistence, and hosted
  DSP application with deterministic native tests.
- [x] Build affected native standalones and host tests, run relevant tests and
  the existing web build, and perform a bounded live OpenRouter smoke test if
  credentials and connectivity allow it.
- [x] Document configuration, usage, build steps, limitations, and results.

## Delegation and integration

Use GPT-5.6 Sol for bounded reference/parameter investigations and the isolated
core/build integration. Use GPT-6 Astra for the shared parameter controller,
where threading, stale-state detection, and multiple processor types require
more reasoning. The main session owns integration, UI, documentation, and final
validation. Assign disjoint files; run native builds serially. Preserve all
pre-existing worktree changes, especially current SQ-4 and drum UI changes.

## Acceptance criteria

- Every canonical parameter is discoverable without hand-maintained allowlists.
- Unknown IDs, non-finite/out-of-range/disallowed discrete values, stale state,
  failed turns, and cancelled turns cannot partially apply a proposal.
- No networking, JS, locks, or agent allocations enter `processBlock`.
- Agent changes are visible in editors, reach DSP, and survive normal state
  export/reload; SQ-4 changes retain unrelated session data.
- Native UI exposes a complete prompt → tools → proposal → apply workflow.
- Tests and build results below distinguish offline verification from an actual
  live model request. No preset or musical-content changes are intended.

## Validation results

### Conversation follow-ups

- [x] Keep a bounded processor-owned transcript, including user prompts, agent
  replies, proposed values, and applied/rejected outcomes.
- [x] Preserve model selection and conversation across panel/editor reopening;
  make New conversation clear both visible history and the next model session.
- [x] Include actual application outcomes and a fresh snapshot in follow-ups;
  retain completed context when a later turn fails or is cancelled.
- [x] Rebuild native targets and verify multi-turn, reset, cancellation, and
  reopened-panel behavior with deterministic tests.
- [x] Explicitly support multiple JavaScript tool calls per request, including
  batched calls and later tool rounds, merged into one validated proposal.

### Audio measurements and meter tools

- [x] Add allocation-free output metering with bounded, coherent atomic reads.
- [x] Capture actual post-output RMS, peaks, full-scale/non-finite indicators,
  stereo correlation, measurement windows, and explicit availability/freshness.
- [x] Expose `host.measureAudio()` and `host.readMeters()` inside JavaScript,
  including available SQ-4 track and instrument FX meters.
- [x] Test known signals, silence, mono, reset, invalid samples, immutable tool
  reads, and real processor output; rebuild affected native apps.

### Follow-up validation

- Final rebuild of all four native standalones succeeded after conversation and
  audio-tool changes; the complete ten-suite native regression run passed again.
  Built artifacts are in `juce/build`; apps were not installed or launched.

- All ten relevant native suites passed, including exact rendered-output meter
  comparisons for all four processors, concurrent meter publication, frozen
  JavaScript observations, multi-turn history, and multiple tool calls.
- Added and passed regressions for late-cancellation rollback, completed tool-call
  ID reuse, provider-configuration conversation resets, and honest JS-memory
  reporting after local capture failures.
- Rendered and inspected the updated native panel with dropdown, transcript,
  follow-up composer, and audio-measurement guidance.

### Initial integration

- Web production build passed (existing bundle-size warning only).
- All nine relevant native suites passed: four processor host suites, SQ-4 DSP,
  CodeAct parser/runtime, Fable controller, and runtime-config parsing.
- Built all four native standalones with the final dropdown UI.
- Live OpenRouter smoke passed in three model calls using the configured key:
  snapshot inspection → JavaScript proposal → final response. No real instrument
  was changed by the smoke test.
- Fixed OpenRouter routing by omitting optional `parallel_tool_calls`; the local
  runtime still serializes tool calls. Added sanitized HTTP routing errors.
- Processor tests cover 2,029 SQ-4 parameters, hosted DSP delivery, preservation
  of unrelated notes/values, state/base roundtrips, undo/redo synchronization,
  stale proposals, invalid batches, cancellation, and all hosted editor banks.
- Agent panel rendered headlessly and inspected; corrected native text encoding.
  User follow-up verified in the rendered panel and SQ-4 host regression: model selection is now an editable dropdown of tool-capable
  models, including the configured model and custom-ID support.
- SQ-4 tempo proposals require stopped clips. Master gain is outside SQ-4's
  existing session undo format, but applies through host gestures.
- The initial sandboxed drum test could not create its temporary WAV fixture;
  the entire final suite passed with normal temporary-file access.
- Existing unrelated worktree edits retained. Built apps are not installed.

### End-user API-key settings

- [x] Add masked key entry, Save, and Remove controls inside the native Agent panel.
- [x] Persist a shared key using JUCE per-user settings across plugin formats and
  standalone apps; saved settings override developer env configuration.
- [x] Validate persistence, replacement/removal, failure handling, and secret-free
  plugin state; verify the UI does not expose the saved key.
- [x] Build and install updated VST3/AU plugins and standalone apps.

### Live OpenRouter model catalog

- [x] Fetch `https://openrouter.ai/api/v1/models` in the Agent panel's background
  worker and list only models advertising the `tools` parameter.
- [x] Test response filtering and UI refresh, then rebuild and install all formats.

Live request, 2026-09-19: OpenRouter's public model endpoint returned current
catalog entries with `supported_parameters`, including tool-capable models. The
native picker filters this metadata locally and keeps custom IDs usable if a
refresh fails. All four VST3, AU, and standalone bundles were installed after
the ten-suite native regression run passed.

### Complete agent activity logging

- [x] Retain the full model-visible assistant transcript, tool input/output,
  provider errors, and application errors for each turn in the Agent panel.
- [x] Validate long/error/tool transcripts and rebuild the native bundles.

### Browser agent port

- [x] Port the staged OpenRouter tool loop to the web surfaces with live model
  discovery, a model dropdown, follow-up history, full model-visible activity
  logs, multi-tool turns, and output/meter reads.
- [x] Mount the agent on WT-1, DR-1, BL-1, and SQ-4. SQ-4 exposes master,
  track faders, and every hosted device parameter through one transaction.
- [x] Keep the browser key session-only: it is never written to browser storage
  or exported sessions.
