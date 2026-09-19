# JUCE CodeAct provenance

The files under `src/` and the two deterministic tests under `tests/` were
copied from the local `../juce-codeact` reference implementation on
2026-09-19. That reference directory did not contain Git metadata, so no source
commit was available to record.

FableSynth changes raise the bounded snapshot catalog from 64 to 8192 entries,
expose physical-value `step` and discrete `choices` metadata to JavaScript, and
reject proposals that do not lie on a declared step. QuickJS-NG remains pinned
to the reference version, `v0.10.1`.

The system/tool prompts explain filtering large catalogs inside JavaScript.
HTTP failures include a bounded provider error message with the configured key
redacted, which makes OpenRouter routing errors actionable in the native panel.
Fable's OpenRouter configuration omits `parallel_tool_calls`: the runtime already
serializes tools, and requiring support for this optional field excluded routes
during the live smoke test.

Completed conversations now survive failure/cancellation of a later turn:
unfinished tools/proposals roll back, and the next turn is told that JavaScript
memory was reset. Host application outcomes are carried as a bounded user-role
observation, keeping quoted user/provider text out of system instructions.
Multi-call turns explicitly support batched and subsequent JavaScript calls,
with step/tool progress and a single merged final proposal.
Completion is accepted against cancellation while the session rollback
checkpoint is still active. Completed tool-call IDs may be reused by providers;
duplicate IDs within a single response remain invalid. Core and controller
share the same endpoint-identity comparison for conversation resets.

Snapshots also carry bounded, JSON-validated audio and meter observations.
`host.measureAudio()` and `host.readMeters()` return separate copies of the
turn-start observations; processor telemetry never runs on the JS worker.
