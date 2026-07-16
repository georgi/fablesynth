# SQ-4 native device-focus baseline

Date: 2026-07-13  
Package: A (GPT-5.6 Luna)  
Repository revision: `570c6631145c9b98d1309b76da50e29aeebd8c87` (`main`)  
Host: macOS 26.4.1 (25E253), arm64  
Toolchain: CMake 4.0.1, Apple clang 21.0.0

## Scope and working-tree guard

This package made no production-code changes. The pre-existing changes below
were present before baseline work and were left untouched:

```text
 M package.json
?? docs/superpowers/plans/2026-07-13-sq4-native-device-focus.md
?? scripts/install-vst.sh
```

The only repository file added by Package A is this report. Render artifacts
were written outside the repository under `/tmp/fablesynth-sq4-baseline`.

## Configure baseline

The existing `juce/build` directory is usable and is configured as Release with
Unix Makefiles and `FABLE_BUILD_PLUGIN=ON`.

The README's fresh-build Ninja command cannot be applied to that existing build
directory because CMake does not permit changing generators in place:

```sh
cmake -S juce -B juce/build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Result: expected configuration-command failure, exit 1:

```text
CMake Error: generator : Ninja
Does not match the generator used previously: Unix Makefiles
```

Reconfiguring with the build directory's existing generator succeeded:

```sh
cmake -S juce -B juce/build -DCMAKE_BUILD_TYPE=Release
```

Result: exit 0; configure and generation completed successfully. The generator
mismatch is a local build-directory condition, not a source/build failure.

## JUCE-free DSP tests

Commands:

```sh
cmake --build juce/build \
  --target engine_test drum_engine_test bass_engine_test sq4_engine_test -j4

ctest --test-dir juce/build --output-on-failure \
  -R '^(engine_test|drum_engine_test|bass_engine_test|sq4_engine_test)$'
```

Results:

| Product | Test | Result |
|---|---|---|
| WT-1 | `engine_test` | PASS (0.72 s) |
| DR-1 | `drum_engine_test` | PASS (0.60 s) |
| BL-1 | `bass_engine_test` | PASS (0.36 s) |
| SQ-4 | `sq4_engine_test` | PASS (0.60 s) |

CTest summary: 4/4 passed, 0 failed, 2.28 s total.

## Plugin-boundary tests

These tests instantiate the real JUCE processors and cover audio, transport,
state round trips, editor construction/painting, and machine-specific UI
interactions.

Commands:

```sh
cmake --build juce/build \
  --target plugin_host_test drum_host_test bass_host_test sq4_host_test -j4

ctest --test-dir juce/build --output-on-failure \
  -R '^(plugin_host_test|drum_host_test|bass_host_test|sq4_host_test)$'
```

Results:

| Product | Test | Result |
|---|---|---|
| WT-1 | `plugin_host_test` | PASS (2.01 s) |
| DR-1 | `drum_host_test` | PASS (1.22 s) |
| BL-1 | `bass_host_test` | PASS (1.14 s) |
| SQ-4 | `sq4_host_test` | PASS (2.86 s) |

CTest summary: 4/4 passed, 0 failed, 7.25 s total.

The SQ-4 boundary test currently verifies the legacy focus editor's byte-level
DR-1 and note-clip edits, phase-preserving live updates, scene/track focus
rules, locked clips, empty-clip creation, session JSON compatibility, stale-ack
invalidation, and over-large host buffers. These checks are the coverage that
must be migrated before `ClipEditView` is removed.

## AU, VST3, and Standalone build matrix

Command:

```sh
cmake --build juce/build \
  --target FableSynth_All FableDrum_All FableBass_All FableSeq_All -j4
```

Result: exit 0. All twelve Release artifacts were produced:

| Product | AU | VST3 | Standalone |
|---|---|---|---|
| WT-1 | `FableSynth WT-1.component` | `FableSynth WT-1.vst3` | `FableSynth WT-1.app` |
| DR-1 | `FableSynth DR-1.component` | `FableSynth DR-1.vst3` | `FableSynth DR-1.app` |
| BL-1 | `FableSynth BL-1.component` | `FableSynth BL-1.vst3` | `FableSynth BL-1.app` |
| SQ-4 | `FableSynth SQ-4.component` | `FableSynth SQ-4.vst3` | `FableSynth SQ-4.app` |

`codesign --verify --deep --strict` passed for all twelve bundles. AU host
validation and an external VST3 scanner were not run in Package A; those remain
part of the plan's final integration audit.

## Existing headless renders

The repository already supports software-rendered PNG snapshots in the four
plugin-boundary executables. No new render harness was added.

Commands:

```sh
mkdir -p /tmp/fablesynth-sq4-baseline/wt \
  /tmp/fablesynth-sq4-baseline/drum \
  /tmp/fablesynth-sq4-baseline/bass

juce/build/plugin_host_test_artefacts/Release/plugin_host_test \
  /tmp/fablesynth-sq4-baseline/wt

juce/build/drum_host_test_artefacts/Release/drum_host_test \
  /tmp/fablesynth-sq4-baseline/drum

juce/build/bass_host_test_artefacts/Release/bass_host_test \
  /tmp/fablesynth-sq4-baseline/bass

juce/build/sq4_host_test_artefacts/Release/sq4_host_test \
  /tmp/fablesynth-sq4-baseline/sq4-session.png \
  /tmp/fablesynth-sq4-baseline/sq4-drum-focus.png \
  /tmp/fablesynth-sq4-baseline/sq4-bass-focus.png
```

Every command exited 0 and its embedded render checks passed.

| Render | Dimensions | SHA-256 |
|---|---:|---|
| WT-1 `wavetable_view.png` | 560x380 | `f5db435b5729b0074623149d91088968d8eae07a40718dc2b5fdc5fe99e56441` |
| WT-1 `plugin_editor.png` | 1400x1332 | `5574acb722725679ff8341404f6b232ae45ef1ad4222a8b550dcc6792ce4e7e1` |
| WT-1 `plugin_editor_mat5.png` | 1400x1332 | `2802e03cd706ff2f981a5e77e4c5586925f72aa6f17a159a6ff0187e0aa2471c` |
| WT-1 `plugin_editor_newdest.png` | 1400x1332 | `45ddc30e2e54e34380107f176a6e0df6257b976b9e1f79d2941935568bbe5116` |
| WT-1 `plugin_editor_matrixfull.png` | 1400x1332 | `3aca46a39e5fdeaa6f6bb3f814c9421beec1d93469a5bb1301a1c7162ab81a86` |
| WT-1 `wavetable_editor.png` | 900x700 | `79238703dec806a8aca6aa559f6a6cd1a197cbab534b5f82ea53bea038d2bb83` |
| DR-1 `drum_editor.png` | 1460x880 | `27f9f8ae58b9d2d58a156449e1f79bacc69c4d55539f9e4a7bba3db9d569b876` |
| BL-1 `bass_editor.png` | 1460x931 | `6e24b0a676e970e233a0c6f6df833527685b2861759dcc12cde1eb46800f9024` |
| SQ-4 session | 1460x920 | `9d99654b1d2557f88acbf17a040e253378e42c518158f6ec09226d3b59cc0de0` |
| SQ-4 DR-1 focus | 1460x920 | `dee94f47e4e4e9618c643b60b5c31f5c2fb6e81ef790e8fc373bf9f579ad5780` |
| SQ-4 BL-1 focus | 1460x920 | `d1cc549b27785679fbca8ca07cbb1358b5db2e7a02c5b5fd8ba01d1f46ca0238` |

Visual inspection confirms the reported starting condition: SQ-4 DR-1 focus
shows a generic 16-pad-by-16-step grid and SQ-4 BL-1 focus shows a generic
12-lane pitch grid. Neither shows the corresponding native device panel. The
existing SQ-4 harness has no WT-1 focus snapshot, so adding WT-1 focus render
coverage is a later-package requirement rather than an existing baseline.

## Existing failures and baseline conclusion

- Source builds: no failures.
- DSP tests: no failures.
- Plugin-boundary/state tests: no failures.
- Existing render assertions: no failures.
- Bundle strict code-signature verification: no failures.
- Local setup note: specifying `-G Ninja` against the existing Unix Makefiles
  build directory fails until a fresh build directory is used.
- Functional defect reproduced: SQ-4 focus mode paints the legacy generic note
  grids instead of the native DR-1/BL-1 device interfaces.

This is a clean green baseline for beginning the behavior-neutral shared UI
architecture work. Later packages should keep the eight CTest targets green
and compare their standalone render output against the hashes above where the
render is expected to remain pixel-identical.
