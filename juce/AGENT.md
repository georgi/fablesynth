# Native parameter agent

WT-1, DR-1, BL-1, and SQ-4 include an **AGENT** button at the lower right of
their native editor. Open it, enter a request, and click **Send**. The agent
inspects parameter metadata, runs JavaScript calculations, and returns proposed
values. **Apply changes** commits the complete validated proposal. **Cancel**
discards an active turn. The conversation shows your prompts, replies, proposed
values, and whether changes were applied. Send follow-ups such as “a little
warmer” or “shorten those tails” in the same conversation. Each request captures
fresh instrument state and reports the previous proposal's application status.
The agent can make multiple JavaScript tool calls in one request, including a
batch of up to eight calls in one model response and further rounds of tool use.
Calls run sequentially and their changes are merged into one proposal; the most
recent successful value wins for each parameter. A turn allows up to twelve
model calls and must finish successfully before Apply becomes available.

Closing and reopening the panel or editor retains both conversation and selected
model for the lifetime of the processor instance. History is kept in memory,
not saved in presets or restored after restarting the app.

**New conversation** immediately clears the visible conversation and pending
proposal, and resets model context on the next Send. Failed or cancelled turns
discard their incomplete tools and JS memory while preserving earlier completed
model context. Sending with a different model or changed provider configuration
starts a new conversation. The visible
transcript keeps up to 32 recent turns within a bounded memory budget; the model
conversation has its own request-size limit and asks for a new conversation if full.

Examples:

- “Lower the filter cutoff to 800 Hz and reduce resonance.”
- “On SQ-4 track 3, shorten the delay feedback while keeping its timing.”
- “Make the closed hat on drum pad 5 softer, preserving its short attack.”
- “Measure the output level and check the compressor meters before suggesting changes.”

The native catalogs are the authority: 178 WT-1, 78 BL-1, 1,587 DR-1, and
2,029 SQ-4 parameters. SQ-4 contains the eight master/mixer parameters plus
`track0.*` (DR-1), `track1.*` (BL-1), and `track2.*` / `track3.*` (WT-1).
These track IDs are zero-based; drum parameter IDs also use zero-based `pad0.*`
through `pad15.*` names. The model sees physical ranges, units, discrete steps,
and choice labels. The agent does not edit note/clip data, load wavetable/sample
assets, control another plugin, or listen to audio. The browser app has its own
session-only agent panel; see the root [README](../README.md#fable-agent-on-the-web).

Each turn keeps a complete in-memory **Activity log** of model-visible assistant
text, JavaScript tool code, tool results, and provider/application errors. It
uses the configured response/tool limits and has a 32 MiB per-turn cap. The log
is not saved in presets, DAW projects, or sessions, and it clears with **New
conversation** or when the processor is destroyed. Private model reasoning is
not exposed; reasoning sent as ordinary assistant text appears in the log.

## OpenRouter configuration

Open **AGENT**, paste your OpenRouter API key into the masked field, and click
**Save key**. The key is saved in JUCE per-user settings, shared by WT-1, BL-1,
DR-1, and SQ-4 in VST3, AU, and standalone hosts. It survives app/DAW restarts
without a repository or environment setup. The field never displays the stored
key. Paste another key to replace it, or click **Remove** to delete the saved key.
Changing credentials starts a fresh conversation on the next Send.

The settings file is `FableSynth/Agent.settings` under the user's Application
Support folder on macOS, application-data folder on Windows, or `.config` folder
on Linux. Storage is plain text in the user's settings directory, with owner-only
directory/file permissions on POSIX systems. It is not encrypted. The key is
never included in plugin state, DAW projects, presets, or exported sessions.

Saved settings take precedence over developer configuration. Developers can still
use `OPENROUTER_API_KEY` in the process environment or repository `.env`, with
`FABLE_AGENT_ENV_FILE` selecting another file. Removing the saved key restores
that fallback when present; the panel displays the active key source.
Configuration reloads when opening the panel, saving/removing a key, and sending.

An optional `OPENROUTER_MODEL` selects the initial model; the default is
`openrouter/auto`. The model dropdown offers curated tool-capable choices and
accepts a custom OpenRouter model ID. Use a
[tool-capable OpenRouter model](https://openrouter.ai/docs/guides/features/tool-calling).

When the Agent panel opens, it fetches OpenRouter's public live model catalog
in the background and refreshes the dropdown with models that declare tool-call
support. It does not send your API key while loading this list. If the catalog
is unavailable, the current/custom model ID remains usable and the panel shows
the fetch error.

Requests use OpenRouter's HTTPS Chat Completions endpoint with streaming and
tool-capability routing. Keys never enter JS, prompts, plugin state, session
exports, or diagnostic output. Model requests incur the selected provider's
normal charges.

## Architecture

`source/agent/FableAgent` captures parameter and document state on the message
thread. `codeact::Agent` owns a worker thread running a bounded model/tool loop
with one tool, `execute_js`. QuickJS exposes `host.snapshot()` and
`host.proposeParameters(...)`, with no filesystem, network, process, or module
access. Read-only `host.measureAudio()` and `host.readMeters()` also expose
captured audio measurements and instrument meters. The full snapshot stays inside JS; the model filters or pages results
before returning them to keep tool output small.

All proposals are staged until a successful final response. Apply revalidates
the entire batch, including ID, range, finite value, discrete step, original
value, and captured document state. A knob edit, preset/session load, or other
document change can make a proposal stale; send a fresh request in that case.
SQ-4 tempo changes require stopped clips, because its conductor locks tempo
while clips are active or queued. Other sound parameters can change during playback.
Standalone edits use APVTS host gestures. SQ-4 uses its existing session patch
and audio-command paths, preserves patch identity, and updates hosted editors.
SQ-4's existing session undo covers hosted changes; master gain uses host
gestures and is outside the existing session undo document.

Only allocation-free output metering runs in `processBlock`; model and JavaScript
work stay on the worker. Worker cancellation is
cooperative, including HTTP cancellation; processor destruction joins the worker.
As in the reference, an unresponsive platform networking backend could delay
plugin unload. Model calls, time, JS heap/stack, code, and outputs are bounded.
The snapshot catalog limit is 8,192, proposals accept at most 64 parameters per
JS proposal call, and the model can make multiple tool calls in one turn.

## Audio measurements

`host.measureAudio()` reports the latest completed 100 ms window of the actual
main output: channel and combined RMS, sample peaks, dBFS levels, DC offset,
stereo correlation, full-scale sample counts, and non-finite sample counts.
Mono measurements use the final mono output; DR-1 auxiliary outputs are excluded.
Sample peaks are not oversampled true peaks, and a full-scale sample count is an
indicator rather than proof that a limiter or clipping occurred.

`host.readMeters()` exposes available instrument FX readings and SQ-4 track
levels, with their tap points and availability. These meters have their own
measurement windows and are not a synchronized recording.

Both tools return copies frozen at the start of the user turn. Window metadata
identifies the captured audio interval; wall-clock age is explicitly unknown.
Before a complete window is available or after audio resources are released,
output measurements are unavailable. The tools do not play notes, record audio,
hear the instrument, or measure staged proposals. Play the instrument, send a
measurement request, and apply any desired proposal. Let the changed sound
render for at least a complete measurement window before sending a follow-up;
an immediate request may still capture audio from before the change.

## Build and verification

Configure and build with the existing JUCE commands. CMake fetches QuickJS-NG
`v0.10.1`, or accepts an existing checkout:

```sh
cmake -S juce -B juce/build -DFABLE_CODEACT_QUICKJS_SOURCE_DIR=/path/to/quickjs-ng
cmake --build juce/build --target fable_codeact_tests fable_codeact_parser_tests fable_agent_test fable_agent_config_test -j4
ctest --test-dir juce/build -R 'fable_codeact|fable_agent' --output-on-failure
```

The four existing host tests also exercise real processor parameter coverage,
application, invalid/stale rejection, and persistence. The SQ-4 host test checks
hosted parameter delivery to DSP and editor synchronization. Tests use scripted
transports and do not need credentials.

An **opt-in paid network** smoke test exercises the actual model → JavaScript →
proposal → final-response loop against a synthetic snapshot. It makes at most
four model calls, never changes a real instrument, and is not registered in CTest:

```sh
cmake --build juce/build --target fable_agent_live_smoke -j4
juce/build/fable_agent_live_smoke_artefacts/Release/fable_agent_live_smoke
```

`codeact/UPSTREAM.md` records provenance and differences from `../juce-codeact`.
The sibling reference is not a runtime or build dependency.
