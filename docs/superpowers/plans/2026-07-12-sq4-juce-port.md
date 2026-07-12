# SQ-4 JUCE Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the SQ-4 web session launcher (`src/seq/`) to a fourth JUCE plugin target, `FableSeq`, that hosts the three already-ported engines (DR-1, BL-1, WT-1 ×2) in one processor with the web's phase-locked clip transport, conductor state machine, launcher UI, and web-compatible session files.

**Architecture:** Three layers, exactly as `docs/sq4-clips.md` §2: a serializable **session document** (same JSON schema `v:1` as the web, so files interchange), a **conductor** on the message thread (owner/queue state machine + boundary math, port of `src/seq/store.ts`), and **hosted-clip transport inside each engine** (port of the worklets' five hosted messages: pending/playing slots, frame-stamped commands, start/stop/pos acks). All four engines render in one `processBlock` against one `int64` sample counter — the shared timebase is free.

**Tech Stack:** JUCE 8.0.4 (FetchContent), C++17, CMake. DSP additions are JUCE-free pure C++ in `namespace fable` (hard repo rule, `juce/README.md:25-27`). UI reuses `source/ui/Theme.h` + `fui` controls.

## Global Constraints

- C++17, `set(CMAKE_CXX_STANDARD 17)` — already set in `juce/CMakeLists.txt:4`.
- Everything under `source/*/dsp/` and `source/dsp/` compiles **without JUCE** (bare `add_executable` test proves it).
- Clip byte layouts are byte-for-byte the web's (`src/seq/protocol.ts:11-29`): DR1 = 256 B/bar, index `(bar*16+pad)*16+step`, values 0/1/2 = off/on/accent; BL1/WT1 = 48 B/bar, index `(bar*16+step)*3`, byte0 flags (bit0 on, bit1 acc, bit2 tie/slide), byte1 note 0..11, byte2 oct+1 (neutral rest = 1).
- Session JSON schema is the web's `SessionDoc v:1` verbatim (`src/seq/protocol.ts:42-78`): bpm 60..200, swing 0..1, quant `'1 BAR'|'1/4'|'OFF'`, base64 `pattern`, `pass` arrays. A file exported from the web SQ-4 must load in FableSeq and vice versa.
- New plugin: `PLUGIN_CODE Fsq4`, `PRODUCT_NAME "FableSynth SQ-4"`, `COMPANY_NAME "FableSynth"`, `PLUGIN_MANUFACTURER_CODE Fabl`, `FORMATS AU VST3 Standalone`. Add `FableSeq` to the macOS re-sign `foreach(tgt ...)` at `juce/CMakeLists.txt:178`.
- Velocities: accent 1.0, plain 0.72. Swing: odd 16ths delayed by `swing * 0.667` of a step. Gain law `gainCurve(v) = v*v*1.4`, ramps ~15 ms.
- Timing contract (`docs/sq4-clips.md` §3): all positions are **derived arithmetic from an anchor frame** — never an accumulating musical counter. Commands carry absolute `atFrame` doubles; a device executes a command in the render quantum containing it. Clips enter **phase-locked**: entry step = `round((frame − anchor)/stepDur) mod clipSteps`, never their own step 0.
- Owner flips **only on the audio thread's clipstart/clipstop ack**, never on the UI action.
- Audio thread never blocks: UI→audio via lock-free FIFO, audio→UI via lock-free ack FIFO + atomics (the `BassProcessor.h:19-27` threading model).
- v1 non-goals (mirror web v1): tempo automation, host-transport sync, MIDI clock, follow actions, per-clip patches, audio export, full device patch-editing panels (patch = factory selection per track; `inline` patches load but have no editor).
- Verify each task with `cmake --build juce/build --target <t>` + `ctest --test-dir juce/build --output-on-failure`. Commit after every green task.

## File Structure

```
juce/source/dsp/ClipHost.h                 NEW  header-only, JUCE-free: two-slot clip transport shared by all 3 engines
juce/source/dsp/Engine.h/.cpp              MOD  WT-1: hosted-clip mode (clip fire = seq tie/gate machinery)
juce/source/bass/dsp/BassEngine.h/.cpp     MOD  BL-1: hosted-clip mode (slide/accent/gate 0.55)
juce/source/drum/dsp/DrumEngine.h/.cpp     MOD  DR-1: hosted-clip mode (pad triggers, ring-out stop)
juce/source/seq/dsp/SeqProtocol.h          NEW  JUCE-free: constants, byte indices, boundary/phase math (protocol.ts port)
juce/source/seq/dsp/SeqModel.h             NEW  JUCE-free: SessionData structs, validation, mute/solo gates (model.ts port)
juce/source/seq/dsp/SeqFactory.h/.cpp      NEW  JUCE-free: NEON TALE factory session (factory.ts port)
juce/source/seq/dsp/Conductor.h/.cpp       NEW  JUCE-free: owner/queue conductor (store.ts port, UI-free)
juce/source/seq/SeqProcessor.h/.cpp        NEW  juce::AudioProcessor: 4 engines + FX, FIFOs, gains, limiter, state
juce/source/seq/SeqEditor.h/.cpp           NEW  editor shell: Rack 1460×920, scale/letterbox, background
juce/source/seq/ui/SeqHeader.h/.cpp        NEW  transport, QUANT stepper, beat dots, clock, scope, SWING/VOL knobs
juce/source/seq/ui/TrackHeadsView.h/.cpp   NEW  SCENES card + 4 track heads (LED, chips, M/S, vol, patch stepper)
juce/source/seq/ui/SceneGridView.h/.cpp    NEW  6 scene rows × (scene card + 4 clip cells), launch interactions
juce/source/seq/ui/SeqFooterView.h/.cpp    NEW  STOP ALL, NOW cells, per-track VU
juce/source/seq/ui/ClipEditView.h/.cpp     NEW  focus mode: per-machine clip step editor + bars/scene strip
juce/test/sq4_engine_test.cpp              NEW  no-JUCE harness: protocol, ClipHost, hosted engines, conductor, factory
juce/test/sq4_host_test.cpp                NEW  plugin-boundary harness: real processor, RMS, state round-trip, PNG
juce/CMakeLists.txt                        MOD  FableSeq target, FABLE_SEQ_SOURCES, 2 test targets, codesign list
```

Source references for implementers (read before the task that touches them):
web logic — `src/seq/protocol.ts`, `src/seq/model.ts`, `src/seq/store.ts`, `src/seq/factory.ts`, `src/seq/rig.ts`, `src/seq/devices.ts`, `src/seq/store.test.ts`, `src/seq/protocol.test.ts`; worklet hosted transport — `src/engine/worklet.js:478-517` (and the drum/bass worklet equivalents); design docs — `docs/sq4-clips.md`, `docs/superpowers/specs/2026-07-12-sq4-device-focus-design.md`; JUCE conventions — `juce/source/bass/BassProcessor.{h,cpp}`, `juce/test/bass_host_test.cpp`, `juce/source/ui/Theme.h`.

---

### Task 1: SeqProtocol.h + sq4_engine_test target

**Files:**
- Create: `juce/source/seq/dsp/SeqProtocol.h`
- Create: `juce/test/sq4_engine_test.cpp`
- Modify: `juce/CMakeLists.txt` (append after the `bass_engine_test` block, `:286`)

**Interfaces:**
- Produces: `fable::Machine`, `fable::Quant`, `sqBytesPerBar(Machine)`, `sqDr1Idx(bar,pad,step)`, `sqNoteIdx(bar,step)`, `sqEmptyClip(Machine,bars)`, `sqSamplesPerBeat/Step`, `sqBarFrames`, `sqBoundaryFrame(Quant,now,anchor,bpm,sr) -> double`, `sqSongPosition(now,anchor,bpm,sr) -> SqSongPos{beat,bar}` — used by every later task.

- [ ] **Step 1: Write the failing test**

Create `juce/test/sq4_engine_test.cpp` with a tiny assert harness (same style as `test/bass_engine_test.cpp`) and the protocol section:

```cpp
#include "../source/seq/dsp/SeqProtocol.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void testProtocol() {
    using namespace fable;
    CHECK(sqBytesPerBar(Machine::DR1) == 256);
    CHECK(sqBytesPerBar(Machine::BL1) == 48);
    CHECK(sqBytesPerBar(Machine::WT1) == 48);
    CHECK(sqDr1Idx(1, 2, 3) == (1 * 16 + 2) * 16 + 3);
    CHECK(sqNoteIdx(2, 5) == (2 * 16 + 5) * 3);

    auto dr = sqEmptyClip(Machine::DR1, 2);
    CHECK(dr.size() == 512);
    for (auto b : dr) CHECK(b == 0);
    auto bl = sqEmptyClip(Machine::BL1, 1);
    CHECK(bl.size() == 48);
    for (int i = 0; i < 48; i++) CHECK(bl[(size_t)i] == (i % 3 == 2 ? 1 : 0));

    const double sr = 48000, bpm = 122;
    const double spb = sr * 60 / bpm;
    CHECK(std::abs(sqSamplesPerBeat(bpm, sr) - spb) < 1e-9);
    CHECK(std::abs(sqSamplesPerStep(bpm, sr) - spb / 4) < 1e-9);
    CHECK(std::abs(sqBarFrames(bpm, sr) - spb * 4) < 1e-9);

    // OFF -> 0 ("this block"); 1/4 -> next beat; 1 BAR -> next bar; pure fn.
    CHECK(sqBoundaryFrame(Quant::Off, 99999, 256, bpm, sr) == 0.0);
    double b1 = sqBoundaryFrame(Quant::Quarter, 256 + spb * 1.5, 256, bpm, sr);
    CHECK(std::abs(b1 - (256 + spb * 2)) < 1e-6);
    double b2 = sqBoundaryFrame(Quant::Bar, 256 + spb * 1.5, 256, bpm, sr);
    CHECK(std::abs(b2 - (256 + spb * 4)) < 1e-6);
    // at-or-after: now exactly on a boundary returns now
    CHECK(std::abs(sqBoundaryFrame(Quant::Bar, 256 + spb * 4, 256, bpm, sr) - (256 + spb * 4)) < 1e-6);
    // now before anchor clamps to anchor
    CHECK(std::abs(sqBoundaryFrame(Quant::Bar, 0, 256, bpm, sr) - 256) < 1e-6);

    auto p = sqSongPosition(256 + spb * 5.2, 256, bpm, sr);
    CHECK(p.beat == 1 && p.bar == 2);
}

int main() {
    testProtocol();
    if (failures) { std::printf("%d FAILURES\n", failures); return 1; }
    std::printf("ALL PASS\n");
    return 0;
}
```

- [ ] **Step 2: Add the CMake target and run to verify it fails**

Append to `juce/CMakeLists.txt` after the `bass_engine_test` block:

```cmake
# ---- SQ-4 headless DSP verification harness (no JUCE) ----
add_executable(sq4_engine_test
    test/sq4_engine_test.cpp)
target_compile_features(sq4_engine_test PRIVATE cxx_std_17)
add_test(NAME sq4_engine_test COMMAND sq4_engine_test)
```

(Engine/conductor sources join this list in Tasks 2–7.)

Run: `cmake --build juce/build --target sq4_engine_test`
Expected: FAIL — `SeqProtocol.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `juce/source/seq/dsp/SeqProtocol.h` — a direct port of `src/seq/protocol.ts:11-134` (frames stay `double`, exactly like the web):

```cpp
// SQ-4 protocol constants and shared-timebase math — C++ port of
// src/seq/protocol.ts (docs/sq4-clips.md §3-§5). Header-only, JUCE-free.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace fable {

enum class Machine { DR1, BL1, WT1 };
// Cycle order matches the web QUANTS = ['1 BAR', '1/4', 'OFF'].
enum class Quant { Bar, Quarter, Off };

constexpr int SQ_STEPS_PER_BAR = 16;
constexpr int SQ_DR1_PADS      = 16;
constexpr int SQ_NOTE_STRIDE   = 3;   // BL1/WT1: flags, note, oct+1 per step
constexpr int SQ_MAX_BARS      = 16;
constexpr int SQ_HOSTED_MAX_BARS = 4; // hosted editor cap (= device NPATTERNS)
constexpr int SQ_STOP          = -1;  // queue sentinel: "stop the track"

inline int sqBytesPerBar(Machine m) {
    return m == Machine::DR1 ? SQ_DR1_PADS * SQ_STEPS_PER_BAR
                             : SQ_STEPS_PER_BAR * SQ_NOTE_STRIDE;
}
inline int sqDr1Idx(int bar, int pad, int step) {
    return (bar * SQ_DR1_PADS + pad) * SQ_STEPS_PER_BAR + step;
}
inline int sqNoteIdx(int bar, int step) {
    return (bar * SQ_STEPS_PER_BAR + step) * SQ_NOTE_STRIDE;
}

// A silent clip payload; note machines get the neutral oct byte (=1).
inline std::vector<uint8_t> sqEmptyClip(Machine m, int bars) {
    std::vector<uint8_t> out((size_t)(bars * sqBytesPerBar(m)), 0);
    if (m != Machine::DR1)
        for (size_t i = 2; i < out.size(); i += SQ_NOTE_STRIDE) out[i] = 1;
    return out;
}

inline double sqSamplesPerBeat(double bpm, double sr) { return sr * 60.0 / bpm; }
inline double sqSamplesPerStep(double bpm, double sr) { return sqSamplesPerBeat(bpm, sr) / 4.0; }
inline double sqBarFrames(double bpm, double sr)      { return sqSamplesPerBeat(bpm, sr) * 4.0; }

// Next quantize boundary at-or-after `now`, in context frames. Quant OFF
// returns 0 — devices treat atFrame <= currentFrame as "this block".
inline double sqBoundaryFrame(Quant q, double now, double anchor, double bpm, double sr) {
    if (q == Quant::Off) return 0.0;
    const double step = q == Quant::Bar ? sqBarFrames(bpm, sr) : sqSamplesPerBeat(bpm, sr);
    const double delta = std::max(0.0, now - anchor);
    return anchor + std::ceil(delta / step) * step;
}

struct SqSongPos { int beat = 0; int bar = 1; };

inline SqSongPos sqSongPosition(double now, double anchor, double bpm, double sr) {
    const double beats = std::max(0.0, now - anchor) / sqSamplesPerBeat(bpm, sr);
    return { (int)std::floor(beats) % 4, (int)std::floor(beats / 4.0) + 1 };
}

inline double sqBarSeconds(double bpm) { return 240.0 / bpm; }

} // namespace fable
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build juce/build --target sq4_engine_test && juce/build/sq4_engine_test`
Expected: `ALL PASS`. Also `ctest --test-dir juce/build --output-on-failure` — all 7 tests green.

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/dsp/SeqProtocol.h juce/test/sq4_engine_test.cpp juce/CMakeLists.txt
git commit -m "feat(seq-juce): SQ-4 protocol constants and timebase math"
```

---

### Task 2: ClipHost — the two-slot hosted clip transport

**Files:**
- Create: `juce/source/dsp/ClipHost.h` (in `source/dsp/` because all three engines include it, like `NoteSeq.h`)
- Test: `juce/test/sq4_engine_test.cpp` (extend)

**Interfaces:**
- Produces: `fable::ClipHost` with `setTempo(bpm, swing, sr, anchor)`, `scheduleClip(data, n, bars, atFrame)`, `scheduleStop(atFrame)`, `updateClip(data, n, bars)`, `clear()`, and `template<class FireFn> void tick(double frame, int n, FireFn&& fire)` where `fire(int absStep)` triggers a step. Events: `fable::HostEvent { enum class T { Start, Stop, Pos } t; double frame; int step, bar; }` accumulated in `std::vector<HostEvent> events` (caller drains after tick).
- This is the exact port of the worklet contract in `docs/sq4-clips.md` §6 rules 1–4 and `src/engine/worklet.js:478-517` (`hostTick`, `clipPhase`, `clipFire`). Read those 40 lines of worklet source before implementing.

- [ ] **Step 1: Write the failing tests** (append `testClipHost()` to `sq4_engine_test.cpp`, call from `main`)

```cpp
#include "../source/dsp/ClipHost.h"

// Drive a ClipHost through consecutive 128-sample quanta from frame f0,
// recording fires; returns fired absolute steps.
static std::vector<int> run(fable::ClipHost& h, double f0, int blocks,
                            std::vector<fable::HostEvent>& evs) {
    std::vector<int> fired;
    for (int b = 0; b < blocks; b++)
        h.tick(f0 + b * 128.0, 128, [&](int abs) { fired.push_back(abs); });
    evs = h.events; // accumulated; caller clears
    return fired;
}

static void testClipHost() {
    using namespace fable;
    const double sr = 48000, bpm = 125;           // stepDur = 5760 exactly
    const double stepDur = sqSamplesPerStep(bpm, sr);

    // 1. Pending swaps in at atFrame, Start event posted, first fire is the
    //    phase-locked entry step (anchor-derived), not step 0.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, /*anchor*/ 256);
        auto clip = sqEmptyClip(Machine::DR1, 1);
        const double at = 256 + 16 * stepDur;      // exactly one bar after anchor
        h.scheduleClip(clip.data(), clip.size(), 1, at);
        std::vector<HostEvent> evs;
        auto fired = run(h, at - 128, 4, evs);     // spans the swap frame
        CHECK(!evs.empty() && evs[0].t == HostEvent::T::Start);
        CHECK(!fired.empty() && fired[0] == 0);    // 16 steps in = step 0 of a 1-bar clip
        // steps advance by stepDur
        if (fired.size() > 1) CHECK(fired[1] == 1);
    }
    // 2. Entry mid-clip: launch a 2-bar clip 5 steps after the anchor -> enters at step 5.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto clip = sqEmptyClip(Machine::BL1, 2);
        const double at = 5 * stepDur;
        h.scheduleClip(clip.data(), clip.size(), 2, at);
        std::vector<HostEvent> evs;
        auto fired = run(h, at - 64, 2, evs);
        CHECK(!fired.empty() && fired[0] == 5);
    }
    // 3. Re-schedule before the boundary replaces pending (one pending slot).
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::DR1, 1), b = sqEmptyClip(Machine::DR1, 2);
        h.scheduleClip(a.data(), a.size(), 1, 16 * stepDur);
        h.scheduleClip(b.data(), b.size(), 2, 16 * stepDur);
        std::vector<HostEvent> evs;
        run(h, 16 * stepDur - 128, 2, evs);
        CHECK(h.playingBars() == 2);
    }
    // 4. scheduleStop cancels a pending clip; Stop event still acks.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::DR1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 16 * stepDur);
        h.scheduleStop(16 * stepDur);
        std::vector<HostEvent> evs;
        auto fired = run(h, 16 * stepDur - 128, 3, evs);
        CHECK(fired.empty());
        bool sawStop = false;
        for (auto& e : evs) if (e.t == HostEvent::T::Stop) sawStop = true;
        CHECK(sawStop && !h.isPlaying());
    }
    // 5. Stop on a playing clip stops it at atFrame; no fires after.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::DR1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 0);   // quant OFF: at<=frame fires now
        std::vector<HostEvent> evs;
        run(h, 0, 4, evs);
        CHECK(h.isPlaying());
        h.scheduleStop(8 * stepDur);
        evs.clear(); h.events.clear();
        auto fired = run(h, 4 * stepDur, 8 * (int)(stepDur / 128) + 4, evs);
        for (int s : fired) CHECK(s < 8);
        CHECK(!h.isPlaying());
    }
    // 6. updateClip on the live slot preserves phase (playhead doesn't move):
    //    grow 1 bar -> 2 bars mid-flight, next fire continues the global grid.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::WT1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 0);
        std::vector<HostEvent> evs;
        run(h, 0, 3 * (int)(stepDur / 128), evs);   // ~3 steps in
        auto bigger = sqEmptyClip(Machine::WT1, 2);
        h.updateClip(bigger.data(), bigger.size(), 2);
        evs.clear(); h.events.clear();
        auto fired = run(h, 3 * stepDur + 128, (int)(stepDur / 128), evs);
        CHECK(!fired.empty() && fired[0] >= 3 && fired[0] <= 4);
        CHECK(h.playingBars() == 2);
    }
    // 7. updateClip targets the PENDING slot when one exists.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::WT1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 32 * stepDur);
        auto b = sqEmptyClip(Machine::WT1, 4);
        h.updateClip(b.data(), b.size(), 4);
        std::vector<HostEvent> evs;
        run(h, 32 * stepDur - 128, 2, evs);
        CHECK(h.playingBars() == 4);
    }
    // 8. Swing delays odd 16ths by swing*0.667 of a step.
    {
        ClipHost h; h.setTempo(bpm, 0.5, sr, 0);
        auto a = sqEmptyClip(Machine::DR1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 0);
        std::vector<double> fireFrames;
        double f = 0;
        for (int b = 0; b < 200; b++) {
            h.tick(f, 128, [&](int) { fireFrames.push_back(f); });
            f += 128;
        }
        // step1 fires ~ stepDur*(1 + 0.5*0.667) after step0 (within a quantum)
        CHECK(fireFrames.size() >= 3);
        double d01 = fireFrames[1] - fireFrames[0];
        double d12 = fireFrames[2] - fireFrames[1];
        CHECK(std::abs(d01 - stepDur * (1 + 0.5 * 0.667)) <= 128.0);
        CHECK(std::abs(d12 - stepDur * (1 - 0.5 * 0.667)) <= 128.0);
    }
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build juce/build --target sq4_engine_test`
Expected: FAIL — `ClipHost.h: No such file or directory`.

- [ ] **Step 3: Implement `juce/source/dsp/ClipHost.h`**

```cpp
// Hosted clip transport — the SQ-4 two-slot state machine each engine embeds
// in hosted mode. C++ port of the worklet hostTick/clipPhase/clipFire
// (src/engine/worklet.js:478-517, contract in docs/sq4-clips.md §6):
// one playing slot, one pending slot, frame-stamped commands, phase-locked
// entry derived from the shared anchor. Header-only, JUCE-free.
#pragma once

#include "../seq/dsp/SeqProtocol.h"
#include <cstring>
#include <functional>
#include <vector>

namespace fable {

struct HostEvent {
    enum class T { Start, Stop, Pos };
    T t; double frame; int step = 0, bar = 0;
};

class ClipHost {
public:
    std::vector<HostEvent> events; // drained by the embedding engine per block

    void setTempo(double bpm, double swing, double sr, double anchor) {
        bpm_ = bpm; swing_ = swing; sr_ = sr; anchor_ = anchor;
    }

    // Replaces any pending clip and disarms a pending stop (a re-launch
    // before the boundary re-targets; docs §6 rule 1).
    void scheduleClip(const uint8_t* d, size_t n, int bars, double at) {
        pend_.assign(d, d + n); pendBars_ = bars; pendAt_ = at; hasPend_ = true;
        hasStop_ = false;
    }

    // Cancels a pending clip if one waits; otherwise stops the playing clip.
    // Stop still acks so a pending-only launch clears in the conductor.
    void scheduleStop(double at) { hasPend_ = false; stopAt_ = at; hasStop_ = true; }

    // Hot-swap bytes: pending slot when one exists, else the live clip.
    // Position is derived arithmetic, so the playhead never moves; a bars
    // change re-derives the entry under floor() (mid-flight resize).
    void updateClip(const uint8_t* d, size_t n, int bars) {
        if (hasPend_) { pend_.assign(d, d + n); pendBars_ = bars; return; }
        if (!playing_) return;
        clip_.assign(d, d + n);
        if (bars != clipBars_) {
            clipBars_ = bars;
            clipStep_ = phaseStep(lastFrame_, bars * SQ_STEPS_PER_BAR, /*roundNearest*/ false);
        }
    }

    void clear() { playing_ = false; hasPend_ = false; hasStop_ = false; clipStep_ = -1; }

    bool isPlaying() const { return playing_; }
    int  playingBars() const { return hasPend_ ? pendBars_ : clipBars_; }
    const uint8_t* clipData() const { return clip_.data(); }
    int  clipStep() const { return clipStep_; } // last fired absolute step

    // One render quantum: [frame, frame+n). fire(absStep) triggers the step's
    // voices in the embedding engine. Order matters: stop, swap, fire.
    template <typename FireFn>
    void tick(double frame, int n, FireFn&& fire) {
        lastFrame_ = frame;
        if (hasStop_ && stopAt_ < frame + n) {
            hasStop_ = false;
            playing_ = false; clipStep_ = -1;
            events.push_back({ HostEvent::T::Stop, frame });
        }
        if (hasPend_ && pendAt_ < frame + n) {
            hasPend_ = false;
            clip_ = std::move(pend_); clipBars_ = pendBars_;
            playing_ = true;
            // Phase-locked entry: enter at the global grid position, -1 so
            // the immediate fire below lands ON that step (worklet clipPhase).
            clipStep_ = phaseStep(frame, clipBars_ * SQ_STEPS_PER_BAR, true) - 1;
            toNext_ = 0;
            events.push_back({ HostEvent::T::Start, frame });
        }
        if (!playing_) return;
        toNext_ -= n;
        while (toNext_ <= 0) {
            const int total = clipBars_ * SQ_STEPS_PER_BAR;
            const int abs = ((clipStep_ + 1) % total + total) % total;
            clipStep_ = abs;
            fire(abs);
            events.push_back({ HostEvent::T::Pos, frame,
                               abs % SQ_STEPS_PER_BAR, abs / SQ_STEPS_PER_BAR });
            toNext_ += intervalAfter(abs);
        }
    }

private:
    // round: activation snaps to the boundary step; floor: mid-flight resize.
    int phaseStep(double frame, int total, bool roundNearest) const {
        const double dur = sqSamplesPerStep(bpm_, sr_);
        const double pos = std::max(0.0, frame - anchor_) / dur;
        const long idx = roundNearest ? (long)std::llround(pos) : (long)std::floor(pos);
        return (int)(((idx % total) + total) % total);
    }
    // Swing: odd 16ths are delayed by swing*0.667 of a step, so the interval
    // leaving step s stretches into an odd step and shrinks out of it.
    double intervalAfter(int abs) const {
        const double dur = sqSamplesPerStep(bpm_, sr_);
        const double sw = swing_ * 0.667;
        const bool curOdd = (abs & 1) != 0, nextOdd = !curOdd;
        return dur * (1.0 + (nextOdd ? sw : 0.0) - (curOdd ? sw : 0.0));
    }

    double bpm_ = 120, swing_ = 0, sr_ = 48000, anchor_ = 0;
    std::vector<uint8_t> clip_, pend_;
    int clipBars_ = 0, pendBars_ = 0;
    double pendAt_ = -1, stopAt_ = -1, toNext_ = 0, lastFrame_ = 0;
    bool hasPend_ = false, hasStop_ = false, playing_ = false;
    int clipStep_ = -1;
};

} // namespace fable
```

Before finalizing, diff the tick/phase logic against `src/engine/worklet.js:478-517` line by line (the drum and bass worklets carry the same block) and adjust any divergence in ordering or rounding — the web is the reference implementation.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build juce/build --target sq4_engine_test && juce/build/sq4_engine_test`
Expected: `ALL PASS`.

- [ ] **Step 5: Commit**

```bash
git add juce/source/dsp/ClipHost.h juce/test/sq4_engine_test.cpp
git commit -m "feat(seq-juce): ClipHost two-slot hosted clip transport"
```

---

### Task 3: WT-1 Engine hosted-clip mode

**Files:**
- Modify: `juce/source/dsp/Engine.h` (public API + members), `juce/source/dsp/Engine.cpp` (render integration)
- Test: `juce/test/sq4_engine_test.cpp` (extend); Modify `juce/CMakeLists.txt` to add the WT-1 dsp sources to `sq4_engine_test`

**Interfaces:**
- Consumes: `ClipHost`, `SeqProtocol.h`; Engine's existing private seq machinery `seqFireAt(int s, int pat, int patNext, double dur)`, `seqGateOff()`, `seqTie(int n, double vel)` (`Engine.h:199-207`).
- Produces (identical names on all three engines — Tasks 4/5 repeat them):
```cpp
void setHostClipMode(bool on);                       // enters hosted mode; seqPlay/seqStop ignored while on
void hostTempo(double bpm, double swing, double anchorFrame);
void hostClip(const uint8_t* data, int bytes, int bars, double atFrame);
void hostClipStop(double atFrame);
void hostClipUpdate(const uint8_t* data, int bytes, int bars);
void hostSetFrame(double blockStartFrame);           // SQ-4 processor calls before render() each block
int  takeHostEvents(HostEvent* out, int max);        // drains ClipHost::events after render()
```
All are audio-thread-only; the SQ-4 processor is the sole caller.

- [ ] **Step 1: Write the failing test** (append to `sq4_engine_test.cpp`; add WT-1 sources to the target)

In `juce/CMakeLists.txt`, extend the `sq4_engine_test` sources with exactly the `engine_test` DSP list (`:249-256`): `source/dsp/Engine.cpp source/dsp/Wavetables.cpp source/dsp/Fx.cpp source/dsp/Params.cpp source/dsp/Presets.cpp source/dsp/UserTables.cpp`.

```cpp
#include "../source/dsp/Engine.h"
#include "../source/dsp/Presets.h"

static double rmsOf(fable::Engine& e, double& frame, int blocks) {
    double sumSq = 0; long cnt = 0;
    float L[128], R[128];
    for (int b = 0; b < blocks; b++) {
        e.hostSetFrame(frame);
        e.render(L, R, 128);
        frame += 128;
        for (int i = 0; i < 128; i++) { sumSq += (double)L[i]*L[i] + (double)R[i]*R[i]; cnt += 2; }
    }
    return std::sqrt(sumSq / (double)cnt);
}

static void testWt1Hosted() {
    using namespace fable;
    Engine e;
    e.prepare(48000);
    // load factory preset 3 (CRYSTAL PLUCK — the LEAD track's patch) exactly
    // as PluginProcessor::setCurrentProgram does; see Presets.h for the apply API.
    applyFactoryPreset(e, 3);            // <- use the actual helper found in Presets.h/PluginProcessor.cpp
    e.setHostClipMode(true);
    e.hostTempo(122, 0, 256);

    // one-bar clip: quarter-note C lane hits on steps 0,4,8,12
    auto clip = sqEmptyClip(Machine::WT1, 1);
    for (int s : {0, 4, 8, 12}) { clip[(size_t)sqNoteIdx(0, s)] = 1; clip[(size_t)sqNoteIdx(0, s) + 1] = 0; }
    e.hostClip(clip.data(), (int)clip.size(), 1, 0.0);   // quant OFF

    double frame = 256;
    CHECK(rmsOf(e, frame, 400) > 1e-5);                  // audible

    HostEvent evs[64];
    int n = e.takeHostEvents(evs, 64);
    bool sawStart = false, sawPos = false;
    for (int i = 0; i < n; i++) {
        if (evs[i].t == HostEvent::T::Start) sawStart = true;
        if (evs[i].t == HostEvent::T::Pos) sawPos = true;
    }
    CHECK(sawStart && sawPos);

    // stop gates seq notes off; audio decays to silence
    e.hostClipStop(frame);
    (void)rmsOf(e, frame, 100);
    CHECK(rmsOf(e, frame, 200) < 1e-4);                  // silent tail after release
}
```

Call `testWt1Hosted()` from `main`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build juce/build --target sq4_engine_test`
Expected: FAIL — `setHostClipMode` not a member of `fable::Engine`.

- [ ] **Step 3: Implement**

In `Engine.h`: add `#include "ClipHost.h"`, the seven methods above, and members `bool hostClipMode_ = false; double hostFrame_ = 0; ClipHost clipHost_;`. Method bodies:

```cpp
void setHostClipMode(bool on) { hostClipMode_ = on; if (!on) clipHost_.clear(); }
void hostTempo(double bpm, double swing, double anchorFrame) {
    setBpm(bpm); seqSwingOverride_ = swing;  // reuse the existing swing param path if one exists; else store
    clipHost_.setTempo(bpm, swing, sr_, anchorFrame);
}
void hostClip(const uint8_t* d, int n, int bars, double at) { clipHost_.scheduleClip(d, (size_t)n, bars, at); }
void hostClipStop(double at)                                { clipHost_.scheduleStop(at); }
void hostClipUpdate(const uint8_t* d, int n, int bars)      { clipHost_.updateClip(d, (size_t)n, bars); }
void hostSetFrame(double f)                                 { hostFrame_ = f; }
int takeHostEvents(HostEvent* out, int max) {
    int n = std::min((int)clipHost_.events.size(), max);
    std::copy(clipHost_.events.begin(), clipHost_.events.begin() + n, out);
    clipHost_.events.clear();
    return n;
}
```

In `Engine.cpp`, inside the render loop that already splits into `renderBlock(L, R, n, ppqChunk)` sub-blocks (`Engine.h:195`), add before each sub-block when `hostClipMode_`:

```cpp
clipHost_.tick(hostFrame_, n, [&](int abs) { clipFireAt(abs); });
hostFrame_ += n;
if (/* a Stop event was just appended by tick */ stopFired) seqGateOff();
```

Add private `void clipFireAt(int abs)` — the hosted twin of `seqFireAt` (`Engine.cpp`, port it): decode the step from `clipHost_.clipData()` at `sqNoteIdx(abs / 16, abs % 16)` with the same flags/note/oct decode as `getNoteSeqStep` (`NoteSeq.h`), then drive the identical seq voice path: off-step → `seqGateOff()`; on + previous step tied into this one → `seqTie(note, vel)` legato retune; else `seqGateOff()` + noteOn through the seq voice slot (`seqNote_`), velocity `acc ? 1.0f : 0.72f`, pitch `seqRoot_ + semi` where `semi = note + 12*oct`. The tie *lookahead* (does the NEXT step tie in?) reads the next clip step (abs+1 mod total) instead of the pattern bank. Follow `seqFireAt`'s structure exactly — same gate-off ordering, same accent velocities — only the byte source changes from `seqPatterns_` to the clip.

Stop semantics (docs §6 rule 3): WT-1 gates off **sequencer notes only** — `seqGateOff()`, never `panic()` — live MIDI notes survive.

While `hostClipMode_` is on, `seqPlay()`/`seqStop()`/host-transport seq firing are suppressed (guard the internal seq clock with `!hostClipMode_`) so the standalone sequencer and the hosted clip can't double-fire.

- [ ] **Step 4: Run tests**

Run: `cmake --build juce/build --target sq4_engine_test && juce/build/sq4_engine_test`
Expected: `ALL PASS`. Then the full suite: `ctest --test-dir juce/build --output-on-failure` — `engine_test` and `plugin_host_test` must stay green (hosted mode is additive; standalone behavior byte-identical).

- [ ] **Step 5: Commit**

```bash
git add juce/source/dsp/Engine.h juce/source/dsp/Engine.cpp juce/test/sq4_engine_test.cpp juce/CMakeLists.txt
git commit -m "feat(seq-juce): WT-1 hosted clip mode"
```

---

### Task 4: BL-1 BassEngine hosted-clip mode

**Files:**
- Modify: `juce/source/bass/dsp/BassEngine.h`, `juce/source/bass/dsp/BassEngine.cpp`
- Test: `juce/test/sq4_engine_test.cpp` (extend); add the `bass_engine_test` DSP source list (`CMakeLists.txt:276-286`) to `sq4_engine_test`

**Interfaces:**
- Consumes: `ClipHost`; BassEngine's private `noteOn(semi, acc, vel)`, `glideTo(semi, acc)` (`BassEngine.h:92-93`), gate-off scheduling with `BL_GATE_FRAC = 0.55`, `readStep` decode (`BassEngine.h:88`).
- Produces: the same seven `setHostClipMode/hostTempo/hostClip/hostClipStop/hostClipUpdate/hostSetFrame/takeHostEvents` methods as Task 3, on `fable::BassEngine`.

- [ ] **Step 1: Write the failing test** (append `testBl1Hosted()`; same `rmsOf` pattern with `render(L, R, 128)`)

```cpp
static void testBl1Hosted() {
    using namespace fable;
    BassEngine e;
    e.prepare(48000);
    applyFactoryBassPatch(e, 0);   // ACID LINE — use the actual apply path from BassProcessor::setCurrentProgram
    e.setHostClipMode(true);
    e.hostTempo(122, 0, 256);

    // steps 0..3: 0 on, 1 slide->2, 2 on, 3 off; slide bit = flags bit2
    auto clip = sqEmptyClip(Machine::BL1, 1);
    auto set = [&](int s, int note, bool slide) {
        clip[(size_t)sqNoteIdx(0, s)] = (uint8_t)(1 | (slide ? 4 : 0));
        clip[(size_t)sqNoteIdx(0, s) + 1] = (uint8_t)note;
    };
    set(0, 0, false); set(1, 0, true); set(2, 3, false);
    e.hostClip(clip.data(), (int)clip.size(), 1, 0.0);

    double frame = 256;
    CHECK(rmsBass(e, frame, 400) > 1e-5);

    // stop releases the mono voice
    e.hostClipStop(frame);
    (void)rmsBass(e, frame, 100);
    CHECK(rmsBass(e, frame, 300) < 1e-4);
}
```

(`rmsBass` = `rmsOf` with `BassEngine&`; write it next to `rmsOf`.)

- [ ] **Step 2: Run to verify failure** — `cmake --build juce/build --target sq4_engine_test`; expected: `setHostClipMode` not a member of `fable::BassEngine`.

- [ ] **Step 3: Implement**

Same seven methods and `ClipHost clipHost_` member as Task 3. In `BassEngine::render`'s 128-sample sub-block loop, when `hostClipMode_`: `clipHost_.tick(hostFrame_, n, [&](int abs){ clipFireAt(abs); }); hostFrame_ += n;`. Guard the internal clock (`fireStep`/`samplesToNext_`) and the host-transport path (`fireHostStep`) with `!hostClipMode_`.

`clipFireAt(int abs)` ports the worklet BL-1 clip fire, reusing `fireStep`'s body (`BassEngine.cpp`) with the byte source switched to the clip: decode `BassStep{on, acc, slide, semi}` from the clip bytes at `sqNoteIdx(abs/16, abs%16)` (semi = `min(11,note) + 12*(oct-1)`), then: off → schedule gate-off at the step edge; on with previous step's slide set → `glideTo(semi, acc)`; else `noteOn(semi, acc, acc ? 1.0f : 0.72f)`; non-tied gates close at `BL_GATE_FRAC = 0.55` of the step (`samplesToGateOff_`), exactly as `fireStep` already computes. Stop semantics: release the voice (the existing gate-off/release path), not a hard kill.

- [ ] **Step 4: Run tests** — `sq4_engine_test` `ALL PASS`; full `ctest` green (`bass_engine_test`, `bass_host_test` unchanged).

- [ ] **Step 5: Commit**

```bash
git add juce/source/bass/dsp/BassEngine.h juce/source/bass/dsp/BassEngine.cpp juce/test/sq4_engine_test.cpp juce/CMakeLists.txt
git commit -m "feat(seq-juce): BL-1 hosted clip mode"
```

---

### Task 5: DR-1 DrumEngine hosted-clip mode

**Files:**
- Modify: `juce/source/drum/dsp/DrumEngine.h`, `juce/source/drum/dsp/DrumEngine.cpp`
- Test: `juce/test/sq4_engine_test.cpp` (extend); add the `drum_engine_test` DSP source list (`CMakeLists.txt:260-272`) to `sq4_engine_test`

**Interfaces:**
- Consumes: `ClipHost`; `DrumEngine::trigger(int pad, float vel)` (`DrumEngine.h:49`) — already public, includes choke groups + phase reset.
- Produces: same seven hosted methods on `fable::DrumEngine`.

- [ ] **Step 1: Write the failing test**

```cpp
static void testDr1Hosted() {
    using namespace fable;
    DrumEngine e;
    e.prepare(48000);
    applyFactoryKit(e, 0);   // TR-VOID — use the actual apply path from DrumProcessor::setCurrentProgram
    e.setHostClipMode(true);
    e.hostTempo(122, 0, 256);

    // four-on-the-floor kick (pad 0), accent on step 0
    auto clip = sqEmptyClip(Machine::DR1, 1);
    for (int s : {0, 4, 8, 12}) clip[(size_t)sqDr1Idx(0, 0, s)] = (uint8_t)(s == 0 ? 2 : 1);
    e.hostClip(clip.data(), (int)clip.size(), 1, 0.0);

    double frame = 256;
    CHECK(rmsDrum(e, frame, 400) > 1e-5);   // renders the MAIN bus (bus 0)

    // stop: pads ring out — audio continues briefly, then decays
    e.hostClipStop(frame);
    double tail = rmsDrum(e, frame, 40);    // right after stop: the last hit still rings
    (void)tail;
    CHECK(rmsDrum(e, frame + 48000, 200) < 1e-4);
}
```

(`rmsDrum` renders via `float* outs[DR_NBUSES][2]` per `DrumEngine::render` (`DrumEngine.h:75`) and measures bus 0.)

- [ ] **Step 2: Run to verify failure** — expected: `setHostClipMode` not a member of `fable::DrumEngine`.

- [ ] **Step 3: Implement**

Same seven methods + `ClipHost clipHost_`. In the render sub-block loop, when hosted: `clipHost_.tick(...)` with

```cpp
[&](int abs) {
    const int bar = abs / SQ_STEPS_PER_BAR, step = abs % SQ_STEPS_PER_BAR;
    for (int pad = 0; pad < SQ_DR1_PADS; pad++) {
        const uint8_t v = clipHost_.clipData()[(size_t)sqDr1Idx(bar, pad, step)];
        if (v) trigger(pad, v == 2 ? 1.0f : 0.72f);
    }
}
```

`trigger` already applies choke groups. Stop semantics: do **nothing** to voices — pads ring out (docs §6 rule 3). Guard the internal `play()` clock and host-transport firing with `!hostClipMode_`.

- [ ] **Step 4: Run tests** — `sq4_engine_test` `ALL PASS`; full `ctest` green.

- [ ] **Step 5: Commit**

```bash
git add juce/source/drum/dsp/DrumEngine.h juce/source/drum/dsp/DrumEngine.cpp juce/test/sq4_engine_test.cpp juce/CMakeLists.txt
git commit -m "feat(seq-juce): DR-1 hosted clip mode"
```

---

### Task 6: SeqModel + factory session

**Files:**
- Create: `juce/source/seq/dsp/SeqModel.h`
- Create: `juce/source/seq/dsp/SeqFactory.h`, `juce/source/seq/dsp/SeqFactory.cpp`
- Test: `juce/test/sq4_engine_test.cpp` (extend); add `source/seq/dsp/SeqFactory.cpp` to `sq4_engine_test`

**Interfaces:**
- Produces (`SeqModel.h`):
```cpp
struct ClipData    { std::string name; int bars = 1; std::vector<uint8_t> bytes; };
struct PatchRef    { bool factory = true; int index = 0; std::map<std::string, float> params; }; // params used when !factory
struct TrackData   { Machine machine; std::string name; uint32_t color; float gain; PatchRef patch; };
struct SceneData   { std::string name; std::vector<ClipData> clips; std::vector<bool> hasClip; std::vector<int> pass; };
struct SessionData { std::string name; double bpm = 122, swing = 0; Quant quant = Quant::Bar;
                     std::vector<TrackData> tracks; std::vector<SceneData> scenes; };
std::string validateSession(const SessionData&);   // "" = valid, else reason
bool isTrackAudible(int t, const std::unordered_map<int,int>& owner,
                    const std::vector<bool>& trackMute, const std::vector<bool>& sceneMute,
                    const std::vector<bool>& solo);
bool isTrackOpen(...same args...);
```
(`clips[t]` is meaningful only where `hasClip[t]`; the pair models the web's `(ClipDoc | null)[]`.)
- Produces (`SeqFactory.h`): `SessionData factorySession();`
- Base64 is NOT here — JSON encode/decode lives in the JUCE layer (Task 14) via `juce::Base64`; the in-memory model carries decoded bytes.

- [ ] **Step 1: Write the failing tests** (port of `src/seq/protocol.test.ts:` factory + validation blocks)

```cpp
#include "../source/seq/dsp/SeqModel.h"
#include "../source/seq/dsp/SeqFactory.h"

static void testModelAndFactory() {
    using namespace fable;
    SessionData s = factorySession();
    CHECK(validateSession(s).empty());
    CHECK(s.name == "NEON TALE" && s.bpm == 122 && s.quant == Quant::Bar);
    CHECK(s.tracks.size() == 4 && s.scenes.size() == 6);
    CHECK(s.tracks[0].machine == Machine::DR1 && s.tracks[1].machine == Machine::BL1);
    CHECK(s.tracks[2].machine == Machine::WT1 && s.tracks[3].machine == Machine::WT1);
    CHECK(s.tracks[0].color == 0xff4de8ffu);
    // INTRO: clips on tracks 0 and 3 only
    CHECK(s.scenes[0].hasClip[0] && !s.scenes[0].hasClip[1] && !s.scenes[0].hasClip[2] && s.scenes[0].hasClip[3]);
    // DROP A drum clip: four-on-the-floor kick, accent step 0, 2 bars
    const ClipData& kit = s.scenes[2].clips[0];
    CHECK(kit.bars == 2 && kit.bytes.size() == 512);
    CHECK(kit.bytes[(size_t)sqDr1Idx(0, 0, 0)] == 2);
    for (int st : {4, 8, 12}) CHECK(kit.bytes[(size_t)sqDr1Idx(0, 0, st)] == 1);
    // every clip's byte length matches bars * bytesPerBar
    for (auto& sc : s.scenes)
        for (size_t t = 0; t < sc.clips.size(); t++)
            if (sc.hasClip[t])
                CHECK((int)sc.clips[t].bytes.size() == sc.clips[t].bars * sqBytesPerBar(s.tracks[t].machine));
    // validation rejects bad docs
    SessionData bad = factorySession(); bad.bpm = 300;
    CHECK(!validateSession(bad).empty());
    SessionData bad2 = factorySession(); bad2.scenes[1].clips[1].bytes.pop_back();
    CHECK(!validateSession(bad2).empty());

    // mute/solo gates (model.ts:20-51)
    std::unordered_map<int,int> owner{{0, 2}};
    std::vector<bool> tm(4,false), sm(6,false), solo(4,false);
    CHECK(isTrackAudible(0, owner, tm, sm, solo));
    CHECK(!isTrackAudible(1, owner, tm, sm, solo));   // unowned
    CHECK(isTrackOpen(1, owner, tm, sm, solo));       // open even between clips
    solo[2] = true;
    CHECK(!isTrackAudible(0, owner, tm, sm, solo));   // another track soloed
    solo[2] = false; sm[2] = true;
    CHECK(!isTrackAudible(0, owner, tm, sm, solo));   // owning scene muted
}
```

- [ ] **Step 2: Run to verify failure** — missing headers.

- [ ] **Step 3: Implement**

`SeqModel.h`: the structs above plus straight ports of `isTrackAudible`/`isTrackOpen` (`src/seq/model.ts:20-51` — audible = owned ∧ ¬trackMute ∧ (¬anySolo ∨ solo[t]) ∧ ¬sceneMute[owner]; open = same without the owned requirement) and `validateSession` (`protocol.ts:139-160`: bpm 60..200, non-empty tracks, per-scene `clips.size()==tracks.size()`, clip bars 1..16, byte length check).

`SeqFactory.cpp`: port `src/seq/factory.ts` **verbatim** — the builders:

```cpp
// One entry per bar; each bar is a list of {pad, steps, accents}.
static ClipData drumClip(std::string name,
                         std::vector<std::vector<std::tuple<int, std::vector<int>, std::vector<int>>>> barsSpec);
struct NoteStep { int s; int n; int o = 0; bool a = false, t = false; };
static ClipData noteClip(std::string name, int bars, std::vector<NoteStep> steps);
static std::vector<NoteStep> held(int s0, int span, int n, int o = 0); // attack + tie on EVERY following step
```

then transcribe every clip table from `factory.ts:60-168` (SPARSE_KICK, HAT_RISE, FULL_KIT_A/B, TAIL_KICK, ACID_CRAWL, ACID_303, ACID_SHIFT, SUB_HOLD, GLASS_HOOK, GLASS_HOOK_II, GLASS_SOLO, AIR_BED, AIR_BED_II, FOG_STABS, FOG_SWELL, AIR_OUT — pad constants KICK=0, SNARE=2, CLAP=3, RIM=4, CH=5, OH=6, TOM_LO=8, TOM_HI=10, PERC=12) and the session table `factory.ts:172-194` (4 tracks with colors `0xff4de8ff/0xff4dff9e/0xffffa14d/0xffb18cff`, gains .8/.75/.85/1.0, factory patch indices 0/0/3/11; 6 scenes INTRO/BUILD/DROP A/DROP B/BREAK/OUTRO with the exact clip placement). Copy step-by-step from the TypeScript — the data must be identical so both builds ship the same NEON TALE.

- [ ] **Step 4: Run tests** — `ALL PASS`.

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/dsp/SeqModel.h juce/source/seq/dsp/SeqFactory.h juce/source/seq/dsp/SeqFactory.cpp juce/test/sq4_engine_test.cpp juce/CMakeLists.txt
git commit -m "feat(seq-juce): session model, validation and NEON TALE factory session"
```

---

### Task 7: Conductor — the owner/queue state machine

**Files:**
- Create: `juce/source/seq/dsp/Conductor.h`, `juce/source/seq/dsp/Conductor.cpp`
- Test: `juce/test/sq4_engine_test.cpp` (extend); add `source/seq/dsp/Conductor.cpp` to `sq4_engine_test`

**Interfaces:**
- Consumes: `SessionData`, `SeqProtocol.h`.
- Produces:
```cpp
// The conductor's outbound side — implemented by SeqProcessor (Task 8) as a
// FIFO into the audio thread, and by tests as a recording fake.
struct ConductorIO {
    virtual double now() = 0;                                                     // current shared frame
    virtual void ioScheduleClip(int t, const std::vector<uint8_t>& bytes, int bars, double atFrame) = 0;
    virtual void ioScheduleStop(int t, double atFrame) = 0;
    virtual void ioUpdateClip(int t, const std::vector<uint8_t>& bytes, int bars) = 0;
    virtual void ioSetTrackGain(int t, float gain) = 0;                           // post-curve, 0 when closed
    virtual void ioSendTempo(double bpm, double swing, double anchorFrame) = 0;
    virtual ~ConductorIO() = default;
};

class Conductor {
public:
    Conductor(SessionData session, ConductorIO& io, double sampleRate);
    void powerOn();                       // anchor = now()+256, tempo out, gains out
    // UI actions (message thread only):
    void launch(int t, int s);  void stopTrack(int t);
    void launchScene(int s);    void stopScene(int s);   void stopAll();
    void togglePassThrough(int s, int t);
    void updateClipBytes(int s, int t, std::vector<uint8_t> bytes, int bars);
    void createClip(int s, int t);
    void setTrackPatch(int t, PatchRef patch);
    void toggleSceneMute(int s); void toggleTrackMute(int t); void toggleSolo(int t);
    void cycleQuant(int d);      void setTrackVol(int t, float v); void setSwing(double v);
    void setBpm(double bpm);     // guarded: only while no track owned/queued; re-anchors
    // audio-thread acks, delivered on the message thread by the editor timer:
    void onClipStart(int t);     void onClipStop(int t);
    // views for the UI:
    const SessionData& session() const;
    int ownerOf(int t) const;    // -2 = none
    int queueOf(int t) const;    // -2 = none, SQ_STOP = stop queued
    bool sceneMuted(int s) const; bool trackMuted(int t) const; bool soloed(int t) const;
    Quant quant() const;  double swing() const;  float trackVol(int t) const;
    double anchor() const;
    SqSongPos songPos() const;   // derived from io.now()
    static float gainCurve(float v) { return v * v * 1.4f; }
};
```

- [ ] **Step 1: Write the failing tests** — port the launcher cases of `src/seq/store.test.ts` (read it first; it is the spec). Fake IO:

```cpp
struct FakeIO : fable::ConductorIO {
    double frame = 0;
    struct Sched { int t; int bars; double at; size_t bytes; };
    std::vector<Sched> clips; std::vector<std::pair<int,double>> stops;
    std::vector<std::tuple<int, std::vector<uint8_t>, int>> updates;
    std::map<int,float> gains; double bpm = 0, swing = -1, anchor = -1;
    double now() override { return frame; }
    void ioScheduleClip(int t, const std::vector<uint8_t>& b, int bars, double at) override { clips.push_back({t,bars,at,b.size()}); }
    void ioScheduleStop(int t, double at) override { stops.push_back({t,at}); }
    void ioUpdateClip(int t, const std::vector<uint8_t>& b, int bars) override { updates.push_back({t,b,bars}); }
    void ioSetTrackGain(int t, float g) override { gains[t] = g; }
    void ioSendTempo(double b, double s, double a) override { bpm = b; swing = s; anchor = a; }
};
```

Cases (each a small block in `testConductor()`):
1. **powerOn**: `anchor == 256` when `frame == 0`; tempo sent with bpm 122; all four gains set to `gainCurve(track.gain)`.
2. **Quantized launch**: `frame = anchor + 10`, `launch(0, 2)` → one `ioScheduleClip` with `t==0, bars==2, bytes==512`, `at ≈ anchor + barFrames(122, 48000)` (1 BAR quant); `queueOf(0)==2`, `ownerOf(0)==-2`.
3. **Owner flips on ack only**: after `onClipStart(0)` → `ownerOf(0)==2`, `queueOf(0)==-2`.
4. **Re-launch re-targets**: `launch(0,2); launch(0,3); onClipStart(0)` → owner 3 (lastScheduled wins).
5. **Quant OFF** → `at == 0.0`.
6. **Empty-slot launch is a no-op** (INTRO has no BASS clip: `launch(1, 0)` schedules nothing).
7. **stopTrack** on owned track → `ioScheduleStop` at the boundary, `queueOf==SQ_STOP`; `onClipStop` clears owner+queue. On idle track → no-op.
8. **stopTrack on pending-only** (queued, never started) → stop scheduled; `onClipStop` clears the queue.
9. **launchScene(2)** (DROP A, all 4 clips) → 4 `ioScheduleClip`s, one shared `at`. **launchScene(0)** (INTRO) → clips on 0,3 + `ioScheduleStop` on 1,2 (empty cells are stop buttons) unless `togglePassThrough(0, 1)` first → then track 1 gets nothing.
10. **stopScene(2)** stops only tracks owned by/queued for scene 2. **stopAll** stops every owned/queued track.
11. **Mute/solo/gains**: `toggleTrackMute(0)` → gain 0; unmute → restored. `toggleSolo(1)` → all other owned tracks' gains 0. `toggleSceneMute(2)` while owner(0)==2 → gain 0 but `isTrackOpen` keeps unowned soloing logic intact (assert against the FakeIO gains map).
12. **setSwing live**: tempo re-sent with the SAME anchor.
13. **setBpm guarded**: with a track owned, `setBpm(140)` is ignored; after stopAll+acks it applies and re-sends tempo with a fresh anchor `now()+256`.
14. **updateClipBytes routing** (`store.ts:264-284`): editing the owned scene → `ioUpdateClip`; editing a *queued* scene while another is live → routes to the queued target (still one `ioUpdateClip`); editing an unrelated scene → no engine traffic; doc bytes changed in all cases.
15. **createClip** on empty cell only: 1-bar silent clip, BL1 → 48 bytes with neutral oct.
16. **cycleQuant** wraps 1 BAR → 1/4 → OFF → 1 BAR.

- [ ] **Step 2: Run to verify failure** — missing `Conductor.h`.

- [ ] **Step 3: Implement `Conductor.cpp`** as a mechanical port of `src/seq/store.ts:90-378` minus React/persistence: members `SessionData session_; std::unordered_map<int,int> owner_, queue_; std::map<int,int> lastScheduled_; std::vector<bool> sceneMute_, trackMute_, solo_; Quant quant_; std::vector<float> trackVol_; double swing_, anchor_ = 0; ConductorIO& io_; double sr_;`. Key bodies:

```cpp
double Conductor::boundary() const {
    return sqBoundaryFrame(quant_, io_.now(), anchor_, session_.bpm, sr_);
}
void Conductor::applyGains() {
    for (int t = 0; t < (int)session_.tracks.size(); t++) {
        const bool open = isTrackOpen(t, owner_, trackMute_, sceneMute_, solo_);
        io_.ioSetTrackGain(t, open ? gainCurve(trackVol_[(size_t)t]) : 0.0f);
    }
}
void Conductor::launch(int t, int s) {
    auto& sc = session_.scenes[(size_t)s];
    if (!sc.hasClip[(size_t)t]) return;
    lastScheduled_[t] = s;
    io_.ioScheduleClip(t, sc.clips[(size_t)t].bytes, sc.clips[(size_t)t].bars, boundary());
    queue_[t] = s;
}
void Conductor::onClipStart(int t) {
    owner_[t] = lastScheduled_[t];
    if (queue_.count(t) && queue_[t] == lastScheduled_[t]) queue_.erase(t);
    applyGains();
}
void Conductor::onClipStop(int t) {
    owner_.erase(t);
    if (queue_.count(t) && queue_[t] == SQ_STOP) queue_.erase(t);
}
```

`launchScene`/`stopScene`/`stopAll`/`togglePassThrough`/`updateClipBytes` (with the queued-beats-owner target rule)/`createClip`/mutes/solo/`cycleQuant`/`setTrackVol`/`setSwing` follow `store.ts` line for line; `powerOn` sets `anchor_ = io_.now() + 256`, sends tempo, applies gains. No watchdog: acks travel an in-process FIFO sized so they cannot be dropped (deviation from the web's 250 ms watchdog, noted in the header comment).

- [ ] **Step 4: Run tests** — `ALL PASS`.

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/dsp/Conductor.h juce/source/seq/dsp/Conductor.cpp juce/test/sq4_engine_test.cpp juce/CMakeLists.txt
git commit -m "feat(seq-juce): conductor owner/queue state machine"
```

---

### Task 8: SeqProcessor + FableSeq plugin target + sq4_host_test

**Files:**
- Create: `juce/source/seq/SeqProcessor.h`, `juce/source/seq/SeqProcessor.cpp`
- Create: `juce/test/sq4_host_test.cpp`
- Modify: `juce/CMakeLists.txt` (FableSeq target + `FABLE_SEQ_SOURCES` + `sq4_host_test` + codesign list at `:178`)

**Interfaces:**
- Consumes: all four engines' hosted APIs (Tasks 3–5), `Conductor` (Task 7), each standalone processor's render recipe (read `PluginProcessor.cpp`, `BassProcessor.cpp`, `DrumProcessor.cpp` `processBlock` before writing this).
- Produces:
```cpp
class SeqAudioProcessor : public juce::AudioProcessor {
public:
    fable::Conductor& conductor();            // message-thread half
    void drainAcks();                         // editor timer calls at 30 Hz -> conductor.onClipStart/Stop + pos atomics
    // published by the audio thread:
    std::atomic<double> currentFrame { 0 };   // shared timebase, block-start frame
    std::atomic<int>    trackStep[4], trackBar[4];   // -1 = no pos
    std::atomic<float>  trackRms[4];                 // post-gain, for VU
    float readScope(float* dest, int n);      // master-mix ring buffer for the scope
    void  setPaused(bool p);  bool paused() const;   // web pause = frames freeze
    juce::AudioProcessorValueTreeState apvts; // master, swing, bpm, quant, vol0..vol3
};
```
- APVTS ids: `"master"` (0..1 def 0.75), `"swing"` (0..1 def 0), `"bpm"` (60..200 def 122), `"quant"` (choice `1 BAR|1/4|OFF`), `"vol0".."vol3"`. Defined through a `ParamInfo` table `seqParamInfo()` in `SeqProcessor.cpp` using the exact idiom of `BassParams.cpp` `build()` (`v.push_back({id, "pid", "LABEL", min, max, def, Curve::Lin, Kind::Float, nullptr})`).

- [ ] **Step 1: Write the failing host test** (`juce/test/sq4_host_test.cpp`, modeled on `test/bass_host_test.cpp` — read it first; reuse its RMS helper shape, `ScopedJuceInitialiser_GUI`, PNG snapshot via `PNGImageFormat` to `argv[1]`)

```cpp
// Renders `blocks` blocks of 128 through the processor, returns overall RMS
// and per-track RMS from the published atomics.
static double renderRms(SeqAudioProcessor& p, juce::AudioBuffer<float>& buf, int blocks);

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    SeqAudioProcessor p;
    p.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buf(2, 128);
    juce::MidiBuffer midi;

    // 1. Silent before any launch, but the clock runs.
    CHECK(renderRms(p, buf, 50) < 1e-6);
    CHECK(p.currentFrame.load() >= 50 * 128);

    // 2. Launch DROP A (scene 2, all four tracks), quant 1 BAR -> starts at
    //    the next bar boundary, then all four tracks are audible.
    p.conductor().launchScene(2);
    renderRms(p, buf, /* > one bar at 122bpm/48k: 94488 frames */ 800);
    p.drainAcks();
    CHECK(p.conductor().ownerOf(0) == 2 && p.conductor().ownerOf(3) == 2);
    double rms = renderRms(p, buf, 400);
    CHECK(rms > 1e-4);
    for (int t = 0; t < 4; t++) CHECK(p.trackRms[t].load() > 1e-6);
    for (int t = 0; t < 4; t++) CHECK(p.trackStep[t].load() >= 0);

    // 3. Every sample finite.
    // (assert inside renderRms: std::isfinite on all samples)

    // 4. Mute track 0 -> its RMS collapses, others keep playing.
    p.conductor().toggleTrackMute(0);
    renderRms(p, buf, 400);
    CHECK(p.trackRms[0].load() < 1e-5 && p.trackRms[1].load() > 1e-6);
    p.conductor().toggleTrackMute(0);

    // 5. Pause freezes the frame counter and outputs silence.
    p.setPaused(true);
    double f0 = p.currentFrame.load();
    CHECK(renderRms(p, buf, 50) < 1e-6);
    CHECK(p.currentFrame.load() == f0);
    p.setPaused(false);

    // 6. stopAll + acks -> owners clear, audio decays to silence.
    p.conductor().stopAll();
    renderRms(p, buf, 1200); p.drainAcks();
    CHECK(p.conductor().ownerOf(0) == -2);
    CHECK(renderRms(p, buf, 400) < 1e-4);

    // 7. State round-trip into a fresh processor: session (edited clip) and
    //    params survive; garbage state doesn't crash.
    auto bytes = fable::sqEmptyClip(fable::Machine::BL1, 1);
    bytes[0] = 1; // step 0 on
    p.conductor().updateClipBytes(1, 1, bytes, 1);
    juce::MemoryBlock state; p.getStateInformation(state);
    SeqAudioProcessor q; q.prepareToPlay(48000.0, 128);
    q.setStateInformation(state.getData(), (int)state.getSize());
    CHECK(q.conductor().session().scenes[1].clips[1].bytes[0] == 1);
    q.setStateInformation("garbage", 7); // must not crash

    std::printf(failures ? "%d FAILURES\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Add CMake targets, run to verify failure**

In `juce/CMakeLists.txt`, after the FableBass block:

```cmake
# ---- FableSynth SQ-4 (session launcher) — fourth plugin target. Hosts the
# DR-1/BL-1/WT-1 engines in one processor (docs/sq4-clips.md, JUCE surface).
juce_add_plugin(FableSeq
    PRODUCT_NAME "FableSynth SQ-4"
    COMPANY_NAME "FableSynth"
    PLUGIN_MANUFACTURER_CODE Fabl
    PLUGIN_CODE Fsq4
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(FABLE_SEQ_SOURCES
    source/seq/SeqProcessor.cpp
    source/seq/SeqEditor.cpp
    source/seq/ui/SeqHeader.cpp
    source/seq/ui/TrackHeadsView.cpp
    source/seq/ui/SceneGridView.cpp
    source/seq/ui/SeqFooterView.cpp
    source/seq/ui/ClipEditView.cpp
    source/seq/dsp/SeqFactory.cpp
    source/seq/dsp/Conductor.cpp
    source/ui/Controls.cpp
    # all three machines' DSP (each list already exists above):
    source/dsp/Engine.cpp source/dsp/Presets.cpp
    source/drum/dsp/DrumParams.cpp source/drum/dsp/DrumTables.cpp
    source/drum/dsp/SampledTables.gen.cpp source/drum/dsp/DrumEngine.cpp
    source/drum/dsp/DrumFx.cpp source/drum/dsp/DrumKits.cpp source/drum/dsp/DrumPatches.cpp
    source/bass/dsp/BassParams.cpp source/bass/dsp/BassEngine.cpp
    source/bass/dsp/BassFx.cpp source/bass/dsp/BassPatches.cpp
    source/dsp/Wavetables.cpp source/dsp/Params.cpp source/dsp/UserTables.cpp source/dsp/Fx.cpp)

target_sources(FableSeq PRIVATE ${FABLE_SEQ_SOURCES})
target_compile_definitions(FableSeq PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
juce_generate_juce_header(FableSeq)
target_link_libraries(FableSeq PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# SQ-4 plugin-boundary harness (instantiates the real SeqAudioProcessor).
juce_add_console_app(sq4_host_test PRODUCT_NAME "sq4_host_test")
target_sources(sq4_host_test PRIVATE test/sq4_host_test.cpp ${FABLE_SEQ_SOURCES})
target_compile_definitions(sq4_host_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(sq4_host_test PRIVATE juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME sq4_host_test COMMAND sq4_host_test)
```

Change line 178 to `foreach(tgt FableSynth FableDrum FableBass FableSeq)`.

(Until Task 9 lands, create `SeqEditor.cpp` + the four `ui/*.cpp` as minimal stubs — `SeqEditor` returning a plain resizable component — so this task links; Task 9 replaces them. `createEditor()` may return `nullptr`-safe generic editor until then: `juce::GenericAudioProcessorEditor`.)

Run: `cmake --build juce/build --target sq4_host_test`
Expected: FAIL — `SeqProcessor.h` missing.

- [ ] **Step 3: Implement `SeqProcessor`**

Members:

```cpp
fable::DrumEngine drum_;   fable::DrumFx drumFx_;
fable::BassEngine bass_;   fable::BassFx bassFx_;
fable::Engine wt_[2];      /* + the WT-1 FX member exactly as PluginProcessor owns it */
fable::SessionData session_;                 // message-thread copy (conductor owns the truth)
std::unique_ptr<fable::Conductor> conductor_;
struct Cmd { enum class K { Clip, Stop, Update, Gain, Tempo, Panic } k;
             int t; int bars; double at, bpm, swing, anchor; float gain;
             std::shared_ptr<std::vector<uint8_t>> bytes; };  // built on message thread
juce::AbstractFifo cmdFifo_ { 256 };  std::array<Cmd, 256> cmdSlots_;
struct Ack { int t; fable::HostEvent ev; };
juce::AbstractFifo ackFifo_ { 512 };  std::array<Ack, 512> ackSlots_;
double frame_ = 0;  std::atomic<bool> paused_ { false };
juce::SmoothedValue<float> trackGain_[4], masterGain_;   // 15 ms ramps
/* limiter: the existing compressor from source/dsp/Fx.h configured
   threshold -6 dB, knee 4, ratio 12, attack 0.002, release 0.25 —
   the same component the three plugins already use for their web
   DynamicsCompressorNode ports (find its class name in Fx.h). */
float scopeRing_[2048]; std::atomic<int> scopeWrite_ { 0 };
```

`ConductorIO` implementation (a small inner struct holding `SeqAudioProcessor&`): `now()` returns `currentFrame.load()`; each `io*` method builds a `Cmd` on the message thread (bytes into a `shared_ptr` so no allocation happens on the audio thread) and pushes through `cmdFifo_`.

`prepareToPlay(sr, block)`: `prepare(sr)` all engines + FX; apply each track's patch — factory index via the same code path the standalone processors' `setCurrentProgram` uses (`DrumKits`/`BassPatches`/`Presets`), inline via param-name map; `frame_ = 0; currentFrame = 0;` construct conductor with the session and call `conductor_->powerOn()` (anchor 256), then `setHostClipMode(true)` + `hostTempo` on all four engines directly (audio not yet running — safe).

`processBlock(buffer, midi)`:
1. If `paused_`: clear buffer, return (frames freeze — web `ctx.suspend()` semantics).
2. Publish `currentFrame = frame_`; drain `cmdFifo_` → call `hostClip/hostClipStop/hostClipUpdate/hostTempo` on engine `t`, or set gain/panic targets.
3. Copy APVTS atomics (master) into `masterGain_` target.
4. For each engine: `hostSetFrame(frame_)`, render into its own stereo scratch (`drum_` renders its 5 buses + `drumFx_`, mix MAIN bus; `bass_` + `bassFx_`; each `wt_[i]` + its FX — replicate each standalone processor's `processBlock` render sequence, same call order, same scratch layout).
5. Per track: apply `trackGain_[t]` ramp, accumulate into the master bus, update `trackRms[t]` (running RMS of the post-gain scratch).
6. Master: gain ramp → limiter → output buffer; write the post-limiter mix into `scopeRing_`.
7. `takeHostEvents` from each engine → push `Ack{t, ev}` through `ackFifo_`; `Pos` events also update `trackStep[t]/trackBar[t]` atomics directly.
8. `frame_ += buffer.getNumSamples()`.

`drainAcks()` (message thread): pop `ackFifo_`; `Start` → `conductor_->onClipStart(t)`, `Stop` → `onClipStop(t)` + `trackStep[t] = -1`.

`getStateInformation`: root ValueTree `"SQ4STATE"` wrapping `apvts.copyState()` plus a child `"SESSION"` with the session serialized to the web JSON schema (Task 14 factors this codec out; here it can live in the processor) — follow the `BL1STATE` scheme (`BassProcessor.cpp:312-365`) including tolerant restore (wrapped tree, bare APVTS tree, garbage → keep factory).

Params: `createLayout()` from the `seqParamInfo()` table, grouped `MIX`/`TRANSPORT`; cache raw pointers in the ctor. `vol0..3` and `swing`/`quant`/`bpm` changes are observed on the message thread (editor timer compares and calls the conductor) so all musical decisions stay in one place.

- [ ] **Step 4: Run tests**

Run: `cmake --build juce/build --target sq4_host_test && juce/build/sq4_host_test_artefacts/sq4_host_test`
Expected: `ALL PASS`. Full `ctest --test-dir juce/build --output-on-failure`: 9 tests green.

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq juce/test/sq4_host_test.cpp juce/CMakeLists.txt
git commit -m "feat(seq-juce): FableSeq processor hosting all four engines"
```

---

### Task 9: Editor shell (Rack, scaling, background, snapshot)

**Files:**
- Create: `juce/source/seq/SeqEditor.h`, `juce/source/seq/SeqEditor.cpp` (replace Task 8 stub)
- Test: `juce/test/sq4_host_test.cpp` (extend: PNG snapshot)

**Interfaces:**
- Consumes: `source/ui/Theme.h` (col::bg, drawPanel, fonts), `source/ui/LookAndFeel.h` (`DarkLNF`), the `setParamInfoResolver` hook (`Controls.h:17-18`).
- Produces: `class SeqEditor : public juce::AudioProcessorEditor`, inner `struct Rack : juce::Component` with `static constexpr int LW = 1460, LH = 920;` and child slots for Tasks 10–13. Rack rows (from `seq.css`: page max-width 1460, grid `218px repeat(4, 1fr)`, gap 9, padding 14/18/22): header y=14 h=66; track heads y=89 h=54; six scene rows y=152 h=96 step 105; footer y=782 h=68; hint line y=858. Column xs: scene column x=18 w=218; track columns x=18+218+9+i*(292+9) w=292 (i=0..3).

- [ ] **Step 1: Write the failing test** — append to `sq4_host_test.cpp` `main` (before the summary print), following `bass_host_test.cpp`'s editor block:

```cpp
    // 8. Editor: correct logical size, paints without crashing, snapshot PNG.
    auto* ed = p.createEditor();
    CHECK(ed != nullptr);
    ed->setSize(1460, 920);
    juce::Image img(juce::Image::ARGB, 1460, 920, true);
    { juce::Graphics g(img); ed->paintEntireComponent(g, true); }
    if (argc > 1) {
        juce::File out(argv[1]);
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    // background pixel is the theme bg, not uninitialized black-with-alpha-0
    CHECK(img.getPixelAt(4, 900).getAlpha() == 255);
    delete ed;
```

- [ ] **Step 2: Run to verify failure** — the stub editor returns `GenericAudioProcessorEditor`; the alpha/size assertions fail or the snapshot shows the generic list.

- [ ] **Step 3: Implement** `SeqEditor` cloned from `BassEditor.{h,cpp}`'s shell (`BassEditor.cpp:23-66`): `Rack` child positioned by the geometry table above; editor ctor sets `setResizable(true, true)`, fixed aspect `LW/LH`, default size `LW×LH` scaled to fit the screen, `setLookAndFeel(&lnf_)` (cleared in dtor), installs the SQ-4 `paramInfo` resolver for `fui` controls (`fui::setParamInfoResolver(seqParamInfo)` pattern — copy how `BassEditor` registers `bassParamInfo`). `resized()` = uniform `AffineTransform::scale` + letterbox translate. `paint()` = `col::bg` fill + the radial top glow (copy `BassEditor.cpp:53-59`). Rack children are placeholder `juce::Component`s until Tasks 10–13 fill them.

- [ ] **Step 4: Run tests** — `sq4_host_test /tmp/sq4_editor.png` passes; open the PNG and confirm the dark shell renders at 1460×920.

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/SeqEditor.h juce/source/seq/SeqEditor.cpp juce/test/sq4_host_test.cpp
git commit -m "feat(seq-juce): SQ-4 editor shell with scaled rack"
```

---

### Task 10: SeqHeader — transport, quant, clock, scope, master knobs

**Files:**
- Create: `juce/source/seq/ui/SeqHeader.h`, `juce/source/seq/ui/SeqHeader.cpp`
- Modify: `juce/source/seq/SeqEditor.cpp` (mount at header slot)
- Test: `juce/test/sq4_host_test.cpp` (extend)

**Interfaces:**
- Consumes: `SeqAudioProcessor&` (conductor + atomics + `readScope`), `fui::Knob`, Theme helpers. Web reference: `src/seq/components/Header.tsx` + `Scope.tsx`; geometry from `seq.css` (`.sq-transport` buttons 32px high, beat dots 8px, scope 190×46).
- Produces: `class SeqHeader : public juce::Component, private juce::Timer` (30 Hz). Public test handles: `void playClick(); void stopAllClick(); void quantStep(int d);`.

- [ ] **Step 1: Write the failing test**

```cpp
    // 9. Header interactions drive the conductor.
    // (reach the header through a public accessor on SeqEditor: seqEditor->header())
    hdr.quantStep(1);
    CHECK(p.conductor().quant() == fable::Quant::Quarter);
    hdr.quantStep(-1);
    hdr.playClick();                       // pause
    CHECK(p.paused());
    hdr.playClick();
    hdr.stopAllClick();                    // schedules stops for owned tracks
```

- [ ] **Step 2: Run to verify failure** — no `SeqHeader`.

- [ ] **Step 3: Implement.** Layout left→right (paint-based, no heavy child components): logo text `FABLESEQ SQ-4` (dispFont, `drawSpaced`), play/pause + stop-all text buttons (32 px tall, rounded, Theme panel colors), `QUANT ◂ 1 BAR ▸` stepper (calls `conductor().cycleQuant(d)`), 4 beat dots (8 px circles; the dot at `songPos().beat` lit in `accentA`) + `BAR nn · 122 BPM` line, scope box right (`drawDisplayBox`, 190×46, polyline of `processor.readScope`), SWING + VOL small knobs on the far right — plain drag knobs (SeqKnob behavior: vertical drag, shift = fine, double-click = default) calling `conductor().setSwing(v)` / APVTS `master`. `timerCallback()` repaints dots/clock/scope from atomics; `playClick()` toggles `processor.setPaused`.

- [ ] **Step 4: Run tests** — pass; regenerate the snapshot PNG and eyeball the header against the web (`npm run dev`, `/seq/`).

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/ui/SeqHeader.h juce/source/seq/ui/SeqHeader.cpp juce/source/seq/SeqEditor.cpp juce/test/sq4_host_test.cpp
git commit -m "feat(seq-juce): header with transport, quant, clock and scope"
```

---

### Task 11: TrackHeadsView — scenes card + track heads

**Files:**
- Create: `juce/source/seq/ui/TrackHeadsView.h`, `juce/source/seq/ui/TrackHeadsView.cpp`
- Modify: `juce/source/seq/SeqEditor.cpp` (mount)
- Test: `juce/test/sq4_host_test.cpp` (extend)

**Interfaces:**
- Consumes: conductor views (owner/mute/solo/trackVol/session), factory patch name tables — `patchName(machine, patch)` port of `devices.ts:138-145` using `fable::DrumKits`/`BassPatches`/`Presets` name accessors (find the exact list symbols in `DrumKits.h`/`BassPatches.h`/`Presets.h`).
- Produces: `class TrackHeadsView : public juce::Component, private juce::Timer`. Public test handles: `void muteClick(int t); void soloClick(int t); void headClick(int t); void patchStep(int t, int d);` and `std::function<void(int)> onFocusTrack` (wired by Task 13).
- Web reference: `TrackHeads.tsx`; columns line up with the rack column table from Task 9.

- [ ] **Step 1: Write the failing test**

```cpp
    // 10. Track heads: mute/solo reach the conductor; patch stepper swaps sounds.
    heads.muteClick(0);
    CHECK(p.conductor().trackMuted(0));
    heads.muteClick(0);
    heads.soloClick(1);
    CHECK(p.conductor().soloed(1));
    heads.soloClick(1);
    heads.patchStep(2, 1);   // LEAD: preset 3 -> 4; session doc updated
    CHECK(p.conductor().session().tracks[2].patch.index == 4);
```

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement.** Column 0: SCENES card (title + `6 SCENES · 4 TRACKS` sub-line; becomes the `◂ SESSION` back button when Task 13's focus mode is active). Columns 1–4 per track: live LED (9 px dot, track color when `ownerOf(t) != -2`, dim otherwise), track name (click → `onFocusTrack(t)`), machine chip (`DR-1`/`BL-1`/`WT-1` outline chip), patch chip as a `◂ name ▸` stepper cycling factory patches (wraps at each machine's factory count; calls `conductor().setTrackPatch` AND pushes the new params to the engine through a new processor method `applyTrackPatch(int t)` that enqueues a param apply on the command FIFO), `M`/`S` toggle buttons (lit amber/cyan), xs VOL knob → `conductor().setTrackVol`. 30 Hz timer repaints LEDs/patch names.

- [ ] **Step 4: Run tests + snapshot.**

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/ui/TrackHeadsView.h juce/source/seq/ui/TrackHeadsView.cpp juce/source/seq/SeqEditor.cpp juce/test/sq4_host_test.cpp
git commit -m "feat(seq-juce): track heads with mute/solo/vol and patch stepper"
```

---

### Task 12: SceneGridView + SeqFooterView — the launcher grid

**Files:**
- Create: `juce/source/seq/ui/SceneGridView.h`, `juce/source/seq/ui/SceneGridView.cpp`
- Create: `juce/source/seq/ui/SeqFooterView.h`, `juce/source/seq/ui/SeqFooterView.cpp`
- Modify: `juce/source/seq/SeqEditor.cpp` (mount)
- Test: `juce/test/sq4_host_test.cpp` (extend)

**Interfaces:**
- Consumes: conductor views + `trackStep/trackBar/trackRms` atomics; `previewSteps` — port `src/seq/model.ts:61-80` into `SeqModel.h` as `std::array<StepBar,16> sqPreviewSteps(Machine, const uint8_t* bytes)` (StepBar `{int h; bool on;}`, same formulas) in this task.
- Produces: `class SceneGridView : public juce::Component, private juce::Timer` with public handles `void cellClick(int s, int t); void cellRightClick(int s, int t); void cellEditClick(int s, int t); void sceneLaunch(int s); void sceneMute(int s); void sceneStop(int s);` and `std::function<void(int,int)> onEditClip`. `class SeqFooterView` with `void stopAllClick(); void trackStopClick(int t);`.
- Web reference: `SceneRow.tsx` (cells + status text), `FooterRow.tsx` (NOW cells + VU). Cell status strings: `READY`, `QUEUED`, `LIVE`, `LIVE n/m` (multi-bar), `MUTED`, `LIVE · MUTED`.

- [ ] **Step 1: Write the failing test**

```cpp
    // 11. Grid semantics through real click handlers.
    grid.cellClick(2, 0);                          // launch DROP A drums
    CHECK(p.conductor().queueOf(0) == 2);
    renderRms(p, buf, 800); p.drainAcks();
    CHECK(p.conductor().ownerOf(0) == 2);
    grid.cellClick(2, 0);                          // click a LIVE cell -> stop
    CHECK(p.conductor().queueOf(0) == fable::SQ_STOP);
    renderRms(p, buf, 800); p.drainAcks();
    grid.cellRightClick(0, 1);                     // INTRO empty BASS cell -> pass-through
    bool pass = false;
    for (int x : p.conductor().session().scenes[0].pass) if (x == 1) pass = true;
    CHECK(pass);
    grid.sceneLaunch(3);                           // DROP B
    CHECK(p.conductor().queueOf(0) == 3 && p.conductor().queueOf(3) == 3);
    renderRms(p, buf, 800); p.drainAcks();
    footer.trackStopClick(3);
    CHECK(p.conductor().queueOf(3) == fable::SQ_STOP);
    footer.stopAllClick();
```

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement.**

`SceneGridView` paints six rows (row pitch from Task 9's table). Scene card (218 px col): round launch button `▶` (32 px, accent when any owner==s), `M` scene-mute toggle, `■` scene-stop, scene number + name, 4 per-track dots (track color when that cell is live), clip count, status line. Clip cell (per track): filled with `panelLo`, 2 px left border in track color when live; contents = eq icon (║║ animated at `trackStep` parity) or idle `▷`, clip name, `{bars}B` chip, 16-bar mini-graph from `sqPreviewSteps` (3 px bars, lit = on), bottom progress strip — width = `((trackBar*16+trackStep) % (bars*16)) / (bars*16)` when live (poll atomics; the web's CSS animation becomes a timer repaint), a `✎` glyph in the corner (hover-lit) → `onEditClip(s, t)`. Empty cell: dashed outline, centered `■` (stop) or `≈` (pass-through). `mouseDown` maps position → (s, t) → `cellClick`/`cellRightClick` (right button) / `cellEditClick` when on the ✎ glyph; scene-card hits → `sceneLaunch`/`sceneMute`/`sceneStop`. All handlers call the conductor; `QUEUED` cells pulse via the 30 Hz timer.

`SeqFooterView`: `STOP ALL` button, then per track a NOW cell `nn Scene · Clip · bar/bars` (from owner + `trackBar` atomics; blank when unowned), a small `■` per track, and a VU bar from `trackRms[t]` (fast attack, slow fall: `v = max(rms, v*0.92)` per tick).

- [ ] **Step 4: Run tests + snapshot** — compare against the web launcher side by side (headless recipe in `.claude/skills/verify`).

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/ui/SceneGridView.h juce/source/seq/ui/SceneGridView.cpp juce/source/seq/ui/SeqFooterView.h juce/source/seq/ui/SeqFooterView.cpp juce/source/seq/SeqEditor.cpp juce/test/sq4_host_test.cpp
git commit -m "feat(seq-juce): launcher scene grid and footer"
```

---

### Task 13: Focus mode — clip editing with live hot-swap

**Files:**
- Create: `juce/source/seq/ui/ClipEditView.h`, `juce/source/seq/ui/ClipEditView.cpp`
- Modify: `juce/source/seq/SeqEditor.cpp` (focus state + layout switch), `juce/source/seq/ui/TrackHeadsView.cpp` (tab-strip role), `juce/source/seq/ui/SceneGridView.cpp` (single-row mini-strip mode)
- Test: `juce/test/sq4_host_test.cpp` (extend)

**Interfaces:**
- Consumes: conductor (`updateClipBytes`, `createClip`), `SQ_HOSTED_MAX_BARS = 4`; per-machine grid interaction patterns from `StepSeqView.h` (DR-1) and `PitchSeqView.h` (BL-1) — read both before implementing; focus rules from `store.ts:362-377` and the spec `docs/superpowers/specs/2026-07-12-sq4-device-focus-design.md` §2–§3.
- Produces: on `SeqEditor`: `void enterFocus(int t, int s = -1); void exitFocus(); void focusScene(int s); std::pair<int,int> focus() const;` (returns {-1,-1} in session mode). `class ClipEditView : public juce::Component, private juce::Timer` with public handles `void toggleDrumCell(int pad, int step); void cycleDrumCell(int pad, int step); void toggleNoteCell(int lane, int step); void toggleAcc(int step); void toggleTie(int step); void setOct(int step, int oct); void barsStep(int d); void barClick(int b); void createClipClick();`.

- [ ] **Step 1: Write the failing test**

```cpp
    // 12. Focus mode: enter, edit a live clip, doc + engine hot-swap.
    ed2->enterFocus(0, 2);                              // DRUMS, DROP A
    CHECK(ed2->focus() == std::make_pair(0, 2));
    grid2.cellClick(2, 0); renderRms(p, buf, 800); p.drainAcks();
    auto before = p.conductor().session().scenes[2].clips[0].bytes;
    clipEd.toggleDrumCell(/*pad*/ 5, /*step*/ 3);
    auto after = p.conductor().session().scenes[2].clips[0].bytes;
    CHECK(before != after);
    CHECK(after[(size_t)fable::sqDr1Idx(0, 5, 3)] != before[(size_t)fable::sqDr1Idx(0, 5, 3)]);
    // audio keeps running (phase preserved by ClipHost::updateClip — Task 2 case 6)
    CHECK(renderRms(p, buf, 200) > 1e-5);
    // focus rules: head switch keeps scene, explicit scene wins, exit remembers
    ed2->enterFocus(1);
    CHECK(ed2->focus() == std::make_pair(1, 2));
    ed2->focusScene(4);
    CHECK(ed2->focus() == std::make_pair(1, 4));
    ed2->exitFocus();
    CHECK(ed2->focus() == std::make_pair(-1, -1));
    // create clip on an empty cell
    ed2->enterFocus(2, 0);                              // LEAD, INTRO (empty)
    clipEd.createClipClick();
    CHECK(p.conductor().session().scenes[0].hasClip[2]);
```

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement.**

Focus state lives on `SeqEditor` (the web keeps it in the store; here it is pure UI state): `enterFocus(t, s)` with the web's scene pick — explicit `s` wins, else `ownerOf(t)`, else last focused scene, else 0, clamped. Layout switch in `Rack::resized()`: focus mode = header + heads (unchanged, heads now the tab strip with the focused head glowing in its track color; SCENES card becomes `◂ SESSION`) + one `SceneGridView` row for the focused scene (mini strip, `setSingleRow(s)` mode with a slim rail of 6 numbered scene chips → `focusScene`) + `ClipEditView` filling the rest. Keys: Esc exits, 1–4 switch device, ↑/↓ move the scene rail (`keyPressed` on the editor). No FLIP animation in v1 — an instant relayout is the port; note it in the header comment.

`ClipEditView` renders the target `(scene, track)` clip:
- DR-1: 16 pads × 16 steps grid (rows labeled with the pad names from `DrumKits`), cell click cycles off→on→accent→off (`cycleDrumCell`), 4-step group shading, playhead column from `trackStep` when this clip is live.
- BL-1/WT-1: 12 lanes × 16 steps pitch grid + oct (−1/0/+1) / ACC / TIE(SLIDE) rows below, exactly PitchSeqView's interaction (click sets lane, click again clears).
- Bar chips `1..bars` select the visible bar (edit offset = `bar * sqBytesPerBar`); `◂ bars ▸` stepper clamps 1..4 (`SQ_HOSTED_MAX_BARS`); clips >4 bars render a lock banner `8-BAR CLIP — VIEW ONLY` and ignore edits (web `HostedClipBar.tsx` behavior).
- Empty target: centered `＋ CREATE CLIP` button → `conductor().createClip(s, t)`.
- Every edit: mutate a local byte copy, then `conductor().updateClipBytes(s, t, bytes, bars)` — the conductor already routes live/queued/idle (Task 7 case 14) and the doc is the source of truth. Re-read the clip from the session on `timerCallback` so external changes reflect.

- [ ] **Step 4: Run tests + snapshot** in focus mode (add a second PNG dump: `/tmp/sq4_focus.png`).

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/ui/ClipEditView.h juce/source/seq/ui/ClipEditView.cpp juce/source/seq/SeqEditor.cpp juce/source/seq/ui/TrackHeadsView.cpp juce/source/seq/ui/SceneGridView.cpp juce/test/sq4_host_test.cpp
git commit -m "feat(seq-juce): focus mode with per-machine clip editors"
```

---

### Task 14: Web-compatible session import/export + docs

**Files:**
- Create: `juce/source/seq/SessionCodec.h`, `juce/source/seq/SessionCodec.cpp` (JUCE layer — uses `juce::JSON`, `juce::Base64`)
- Modify: `juce/source/seq/SeqProcessor.cpp` (state uses the codec), `juce/source/seq/ui/SeqHeader.cpp` (LOAD/SAVE buttons), `juce/CMakeLists.txt` (add SessionCodec.cpp to `FABLE_SEQ_SOURCES`), `juce/README.md` (SQ-4 section)
- Test: `juce/test/sq4_host_test.cpp` (extend)

**Interfaces:**
- Produces:
```cpp
juce::String sessionToJson(const fable::SessionData&);            // web SessionDoc v:1 schema
bool sessionFromJson(const juce::String&, fable::SessionData&);   // validates; false on reject
```
Field mapping is the web schema exactly (`src/seq/protocol.ts:42-78`): `v:1`, `name`, `bpm`, `swing`, `quant` as the string `1 BAR|1/4|OFF`, `tracks[].{machine,name,color,gain,patch}` (`machine` as `DR1|BL1|WT1`, `color` as `#rrggbb`, patch `{kind:'factory',index}` or `{kind:'inline',data:{params:{...}}}`), `scenes[].{name,clips[],pass?}` with `clips[]` entries `null` or `{name,bars,pattern}` (`pattern` = base64 of the bytes).

- [ ] **Step 1: Write the failing test**

```cpp
    // 13. Session JSON round-trip, web-schema compatible.
    fable::SessionData s = fable::factorySession();
    juce::String json = sessionToJson(s);
    fable::SessionData r;
    CHECK(sessionFromJson(json, r));
    CHECK(r.name == s.name && r.scenes.size() == 6);
    CHECK(r.scenes[2].clips[0].bytes == s.scenes[2].clips[0].bytes);
    CHECK(r.tracks[0].color == 0xff4de8ffu);
    // schema spot-checks against the literal web format
    auto v = juce::JSON::parse(json);
    CHECK((int)v.getProperty("v", 0) == 1);
    CHECK(v.getProperty("quant", "").toString() == "1 BAR");
    CHECK(v.getProperty("scenes", juce::var())[0].getProperty("clips", juce::var())[1].isVoid()); // INTRO bass = null
    // rejects bad docs
    fable::SessionData junk;
    CHECK(!sessionFromJson("{\"v\":2}", junk));
    CHECK(!sessionFromJson("not json", junk));
```

Also assert cross-compatibility with a real web export: check in a fixture `juce/test/fixtures/web-session.json` (produce it once by running the web app — `npm run dev`, power on `/seq/`, copy `localStorage['fable.session.v1']`) and `CHECK(sessionFromJson(fixtureText, r2))`.

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement** the codec with `juce::JSON`/`juce::DynamicObject`/`juce::Base64` (validate via `fable::validateSession` after decode; reject `v != 1`). Wire `getStateInformation`/`setStateInformation` to store the JSON string in the `"SESSION"` child. Add `LOAD`/`SAVE` text buttons to the header using `juce::FileChooser` (async, `.json` filter) — LOAD replaces the conductor's session (stop all first), SAVE writes `sessionToJson`. Update `juce/README.md` with an SQ-4 section following the DR-1/BL-1 sections' structure: what it is, target/test names, scope decisions (internal clock only — no host-transport sync v1; patch editing = factory stepper, inline patches load-only; no FLIP animation; no watchdog).

- [ ] **Step 4: Run the FULL suite + build all formats**

```bash
cmake --build juce/build && ctest --test-dir juce/build --output-on-failure
```
Expected: 10/10 tests pass. Install per the memory'd flow (copy `FableSeq_artefacts` VST3/AU to `~/Library/Audio/Plug-Ins/...`) and smoke-test the Standalone app: launch DROP A, switch scenes on the bar, edit a live drum clip in focus mode, save/load a session, load a web-exported session.

- [ ] **Step 5: Commit**

```bash
git add juce/source/seq/SessionCodec.h juce/source/seq/SessionCodec.cpp juce/source/seq/SeqProcessor.cpp juce/source/seq/ui/SeqHeader.cpp juce/test juce/CMakeLists.txt juce/README.md
git commit -m "feat(seq-juce): web-compatible session files and docs"
```

---

## Self-Review Notes

- **Spec coverage** against `docs/sq4-clips.md` §2–§11 and the web behavior: session doc (T6/T14), shared timebase + boundary math (T1), five hosted messages + two-slot rules + phase-locked entry (T2–T5), per-machine stop semantics (T3 gate-off-seq-only / T4 release / T5 ring-out), adapter surface (engines' hosted API, T3–T5), conductor owner/queue + ack-flip + empty-cell stop buttons + pass-through (T7), audio graph gains/limiter/analyser equivalents (T8), launcher UI (T10–T12), Phase-4 focus editing + hot-swap routing (T13), persistence + validation + compatibility (T14). Deliberate v1 deviations are listed in Global Constraints and documented in README (T14).
- **Type consistency**: `Machine`/`Quant`/`SQ_STOP` defined once in `SeqProtocol.h` (T1); `HostEvent` once in `ClipHost.h` (T2); engine hosted methods share names across T3–T5; `ConductorIO` names (`io*`) match between T7's fake and T8's FIFO implementation; `SessionData` field names match between T6, T7, T13, T14.
- **Known judgment calls an implementer may revisit**: (a) exact worklet tick ordering — T2 step 3 mandates a line-by-line diff against `worklet.js:478-517`; (b) each engine's factory-apply helper names in tests (`applyFactoryPreset` etc.) must be replaced with the real symbols found in `Presets.h`/`DrumKits.h`/`BassPatches.h` + the standalone processors' `setCurrentProgram`; (c) the WT-1/drum/bass FX-chain call order in T8 step 3 must replicate the respective `processBlock`s.
