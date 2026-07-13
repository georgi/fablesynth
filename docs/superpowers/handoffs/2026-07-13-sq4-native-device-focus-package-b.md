# Package B handoff — shared native-device UI architecture

## Scope completed

- Added a copyable `fui::ParameterSource` lookup that can wrap an APVTS or an
  editor-owned parameter bank.
- Added `fui::DeviceParameterBank`, built from the canonical `ParamInfo`
  catalogs, with real-value load/snapshot and an atomic dirty flag.
- Migrated shared knobs, steppers, power buttons, vertical sliders, WT display
  parameter lookups, and modulation slot helpers to accept `ParameterSource`.
  Existing APVTS constructors remain as compatibility adapters.
- Added frozen UI-model contracts for DR-1, BL-1, and WT-1.
- Added hosted-bank boundary coverage to `sq4_host_test`.

## Construction and lifetime rules

`ParameterSource` is a small value containing lookup callbacks. It does not own
the APVTS or `DeviceParameterBank` captured by those callbacks. The backing
processor/model/bank must therefore be constructed before its child panels and
must outlive them.

For standalone adapters, return an APVTS source whose metadata catalog is the
matching process-lifetime table:

```cpp
const auto& info = fable::bassParamInfo();
return fui::ParameterSource::fromApvts(proc.apvts, info.data(), info.size());
```

For hosted adapters, keep a `DeviceParameterBank` as a model member and return
`bank.source()`. Do not construct a temporary bank in a component constructor.

## Threading and callback contract

- Parameter lookups and metadata lookups are UI/message-thread operations.
- JUCE parameter gestures may notify listeners synchronously on their caller's
  thread.
- `DeviceParameterBank`'s listener performs only `atomic<bool>::store(true)`.
- Hosted models must call `consumeDirty()` from their message-thread timer and
  perform session mutation / command-FIFO writes there.
- Loading a patch calls `DeviceParameterBank::load`, which resets unspecified
  values to catalog defaults and clears dirty after the notifications finish.
- No UI model may call a DSP engine directly.

## Parameter-ID mapping

Parameter IDs remain the existing canonical `ParamInfo.pid` strings. There is
no SQ-4 prefix and no synthetic APVTS registration. The machine-specific model
chooses exactly one catalog (`drumParamInfo`, `bassParamInfo`, or `paramInfo`),
and `SeqAudioProcessor::computeTrackParams` remains the conversion authority
before values reach an engine.

## Model adapter rules

- Standalone adapters are thin delegates to the existing processor API.
- Hosted adapters implement the same interface against a track index and a
  mutable focused-scene target.
- Capability flags, rather than type checks inside panels, control hosted-only
  restrictions (transport, chaining, user-table editing, audition).
- If extraction discovers a genuinely missing method, change the shared model
  interfaces centrally before continuing; do not add processor-specific escape
  hatches to panels.

## Validation

Commands run:

```text
cmake --build build --target plugin_host_test drum_host_test bass_host_test sq4_host_test -j4
ctest --test-dir build -R '^(plugin_host_test|drum_host_test|bass_host_test|sq4_host_test)$' --output-on-failure
```

All four targets built and 4/4 tests passed. Existing compiler warnings remain;
no new build failure was introduced.

## Next-agent assumptions

- Terra may edit DR-1 and BL-1 editor/panel files, but not shared
  `source/ui/ParameterSource*`, `DeviceParameterBank*`, `Controls*`,
  `Displays*`, or `Modulation*` without returning the API question to Sol.
- Luna may edit WT-1 editor/panel files under the same restriction.
- Keep standalone constructors and layouts pixel-identical. The first goal is
  adapter/extraction parity, not hosted behavior.
- `juce/CMakeLists.txt` already contains the temporary shared-source
  registration required for every current target.
