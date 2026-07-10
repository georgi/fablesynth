# DR-1 JUCE Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship "FableSynth DR-1" as a second JUCE plugin (AU · VST3 · Standalone) — a faithful C++ port of the web drum machine with real 5-bus multi-out, host tempo sync, and drop-WAV pad import.

**Architecture:** JUCE-free pure-C++ DSP core (`DrumEngine`, `DrumFx`, `DrumParams`, `DrumTables`, `DrumKits`) ported 1:1 from `src/drum/` (the lockstep reference), wrapped by a thin `DrumAudioProcessor` (APVTS, buses, MIDI, state) and a pixel-faithful `DrumEditor` built from the shared `fui::` components. Mirrors the existing WT-1 port in `juce/` exactly.

**Tech Stack:** C++17, JUCE 8.0.4 (FetchContent), CMake ≥3.22, ctest. Headless DSP tests build with bare `g++ -std=c++17` (no JUCE).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-10-dr1-juce-port-design.md`. Web reference sources: `src/drum/engine/worklet-drum.js`, `src/drum/engine/drum-synth.ts`, `src/drum/params.ts`, `src/drum/kits.ts`, `src/drum/engine/drumtables.ts`, `src/drum/seq.ts`.
- All new DSP files live in `juce/source/drum/dsp/`, are **JUCE-independent** (no juce includes), namespace `fable`, and compile with `g++ -std=c++17` alone.
- All new UI files live in `juce/source/drum/ui/` (namespace `fui`) or `juce/source/drum/`.
- Reuse, never copy: `fable::ParamInfo/Curve/Kind/normToValue/valueToNorm` (Params.h), `fable::GeneratedTable/TablePtr/buildUserTable/generateTables/fft` (Wavetables.h), `fable::Rng` (Engine.h), `fable::Smooth/Biquad/DelayLine/FvComb/FvAllpass` (Fx.h), `fable::UserTable/userTableFromWave/makeUserTable/mixToMono/detectCycleLength/sliceToFrames/singleCycleFrame` (UserTables.h), `fui::Knob/Stepper/PowerButton` (ui/Controls.h), `ui/Theme.h`, `ui/Displays.h`, `WavetableView`.
- **WT-1 sources must not change behavior.** Adding an include or making a helper non-static is allowed; changing DSP or UI logic is not.
- Exact web constants everywhere: PLAIN_VEL 0.72, ACCENT_VEL 1.0, SWING_MAX 0.667, CHOKE_FADE 0.12, BASE_NOTE 60, DC_R 0.9998, MOD_LOG_D 5, 16-sample mod subblocks, table order THUD CRACK TINE GRIT PRIME BLOOM PULSE VOX CHIME GLITCH.
- Plugin identity: product name "FableSynth DR-1", target `FableDrum`, `PLUGIN_CODE Fdr1`, `PLUGIN_MANUFACTURER_CODE Fabl`, formats AU VST3 Standalone, IS_SYNTH, NEEDS_MIDI_INPUT.
- Every task ends with: run its tests (expected output stated), then `git commit`.
- Build commands: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` then `cmake --build build` (run from `juce/`). Headless: `g++ -std=c++17 -O2 -o /tmp/drum_engine_test test/drum_engine_test.cpp source/drum/dsp/*.cpp source/dsp/Fx.cpp source/dsp/Wavetables.cpp source/dsp/UserTables.cpp source/dsp/Params.cpp && /tmp/drum_engine_test` (Fx.cpp supplies the shared Biquad/Freeverb primitives DrumFx reuses).
- If a build or test fails, fix it before moving on. Never commit red.

---

### Task 1: DrumParams + headless test harness + CMake test target

**Files:**
- Create: `juce/source/drum/dsp/DrumParams.h`
- Create: `juce/source/drum/dsp/DrumParams.cpp`
- Create: `juce/test/drum_engine_test.cpp`
- Modify: `juce/CMakeLists.txt` (append `drum_engine_test` target at the bottom, next to `engine_test`)

**Interfaces:**
- Consumes: `fable::ParamInfo`, `Curve`, `Kind`, `normToValue`, `valueToNorm` from `source/dsp/Params.h`.
- Produces (used by every later task):

```cpp
// DrumParams.h — namespace fable
constexpr int DR_NPADS = 16;
constexpr int DR_MIDI_BASE = 36;              // pads 0..15 = notes 36..51
constexpr int DR_NPATTERNS = 4;
constexpr int DR_STEPS = 16;

extern const std::vector<std::string> DRUM_TABLE_NAMES;   // THUD CRACK TINE GRIT PRIME BLOOM PULSE VOX CHIME GLITCH
extern const std::vector<std::string> DRUM_FILTER_TYPES;  // LP 12, LP 24, BP 12, HP 12, NOTCH
extern const std::vector<std::string> DMOD_SOURCES;       // —, MOD ENV, VELO, RAND
extern const std::vector<std::string> DMOD_DESTS;         // —, A POS, B POS, LEVEL, CUTOFF, PITCH, A FINE, B FINE, NOISE LVL, RES
extern const std::vector<std::string> CHOKE_NAMES;        // —, CHK 1..CHK 4
extern const std::vector<std::string> OUT_NAMES;          // MAIN, AUX 1..AUX 4

// Per-pad field offsets — order MUST match PAD_DEFS in src/drum/params.ts.
enum DPadField : int {
    DP_OSCA_TABLE = 0, DP_OSCA_POS, DP_OSCA_TUNE, DP_OSCA_FINE, DP_OSCA_PHASE,
    DP_OSCA_UNISON, DP_OSCA_DETUNE, DP_OSCA_LEVEL,
    DP_OSCB_TABLE, DP_OSCB_POS, DP_OSCB_TUNE, DP_OSCB_FINE, DP_OSCB_PHASE,
    DP_OSCB_UNISON, DP_OSCB_DETUNE, DP_OSCB_LEVEL,
    DP_NOISE_COLOR, DP_NOISE_LEVEL,
    DP_PENV_AMT, DP_PENV_DEC,
    DP_AENV_ATT, DP_AENV_HOLD, DP_AENV_DEC, DP_AENV_CURVE,
    DP_FLT_ON, DP_FLT_TYPE, DP_FLT_CUT, DP_FLT_RES, DP_FLT_DRIVE,
    DP_MOD1_SRC, DP_MOD1_DST, DP_MOD1_AMT,
    DP_MOD2_SRC, DP_MOD2_DST, DP_MOD2_AMT,
    DP_MOD3_SRC, DP_MOD3_DST, DP_MOD3_AMT,
    DP_MOD4_SRC, DP_MOD4_DST, DP_MOD4_AMT,
    DP_MODENV_DEC,
    DP_LVL, DP_PAN, DP_V2L, DP_V2M, DP_CHOKE, DP_OUT,
    DPAD_NFIELDS                                   // == 48
};

enum DGlobalPid : int {
    DG_SEQ_BPM = DR_NPADS * DPAD_NFIELDS,          // 768
    DG_MASTER_SWING, DG_MASTER_VOLUME,
    DG_FXDRIVE_ON, DG_FXDRIVE_AMT, DG_FXDRIVE_MIX,
    DG_FXCOMP_ON, DG_FXCOMP_THR, DG_FXCOMP_GAIN,
    DG_FXCHORUS_ON, DG_FXCHORUS_RATE, DG_FXCHORUS_DEPTH, DG_FXCHORUS_MIX,
    DG_FXDELAY_ON, DG_FXDELAY_TIME, DG_FXDELAY_FB, DG_FXDELAY_MIX,
    DG_FXREVERB_ON, DG_FXREVERB_SIZE, DG_FXREVERB_MIX,
    DR_NUM_PARAMS                                  // == 788
};

inline constexpr int dpid(int padI, int field) { return padI * DPAD_NFIELDS + field; }
inline constexpr int doscBase(int osc) { return osc == 0 ? DP_OSCA_TABLE : DP_OSCB_TABLE; }

using DrumParamArray = std::array<float, DR_NUM_PARAMS>;

const std::vector<ParamInfo>& drumParamInfo();   // ordered by flat id; pid strings "pad0.oscA.table" … "fx.reverb.mix"
DrumParamArray defaultDrumParams();
int drumIdFromString(const std::string& pid);    // -1 if unknown
```

- [ ] **Step 1: Write the failing test.** Create `juce/test/drum_engine_test.cpp` with the same `check()` harness style as `test/engine_test.cpp:22-27` (copy the `check`/`finite`/`rms`/`peak` helpers verbatim) and a params section:

```cpp
#include "../source/drum/dsp/DrumParams.h"
// … helpers copied from engine_test.cpp …
int main() {
    using namespace fable;
    printf("\n== 1. DrumParams ==\n");
    check(DPAD_NFIELDS == 48, "48 per-pad fields");
    check(DR_NUM_PARAMS == 788, "788 total params");
    const auto& info = drumParamInfo();
    check((int)info.size() == DR_NUM_PARAMS, "info covers all params");
    check(info[dpid(0, DP_OSCA_TABLE)].pid == "pad0.oscA.table", "pad0 table pid");
    check(info[dpid(3, DP_FLT_CUT)].pid == "pad3.flt.cut", "pad3 cut pid");
    check(info[DG_SEQ_BPM].pid == "seq.bpm", "bpm pid");
    check(info[DG_FXREVERB_MIX].pid == "fx.reverb.mix", "last pid");
    auto d = defaultDrumParams();
    check(d[DG_SEQ_BPM] == 126.0f, "bpm default 126");
    check(d[dpid(5, DP_OSCA_LEVEL)] == 0.75f && d[dpid(5, DP_OSCB_LEVEL)] == 0.0f, "osc level defaults");
    check(d[dpid(0, DP_AENV_DEC)] == 0.24f && d[dpid(0, DP_LVL)] == 0.8f, "env/lvl defaults");
    check(d[DG_FXCOMP_ON] == 1.0f && d[DG_FXREVERB_ON] == 1.0f, "comp+reverb default on");
    check(drumIdFromString("pad15.out") == dpid(15, DP_OUT), "idFromString");
    check(drumIdFromString("nope") == -1, "unknown id -> -1");
    // log-curve mapping identical to web: flt.cut min 20 max 20000, norm 0.5 -> sqrt(20*20000)
    const auto& cut = info[dpid(0, DP_FLT_CUT)];
    check(std::abs(normToValue(cut, 0.5f) - std::sqrt(20.0f * 20000.0f)) < 1.0f, "log curve midpoint");
    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Run to verify it fails.** `g++ -std=c++17 -O2 -o /tmp/det juce/test/drum_engine_test.cpp` → expect compile error (`DrumParams.h` not found).

- [ ] **Step 3: Implement `DrumParams.{h,cpp}`.** Header exactly as in Interfaces above. In the .cpp, build the info table once from a static per-pad def list that transcribes `src/drum/params.ts:29-71` **row for row** (same order, min, max, def, curve, kind, options) — osc fields ×2 with the A/B default differences (table def 0/1, level def 0.75/0), then noise/penv/aenv/flt/mod1-4/modenv/lvl/pan/v2l/v2m/choke/out — then the 20 globals from `params.ts:75-96`. Per-pad pids are `"pad" + i + "." + suffix`. Bool defs are floats 0/1. Enum params use `Curve::Int` and their option vector. `drumIdFromString` builds a static `std::unordered_map<std::string,int>` on first call.

- [ ] **Step 4: Run test, verify pass.** `g++ -std=c++17 -O2 -o /tmp/det juce/test/drum_engine_test.cpp juce/source/drum/dsp/DrumParams.cpp juce/source/dsp/Params.cpp && /tmp/det` → `ALL PASS`, exit 0. (Params.cpp is needed for `normToValue`.)

- [ ] **Step 5: Add CMake target.** In `juce/CMakeLists.txt`, after the `engine_test` block:

```cmake
add_executable(drum_engine_test
    test/drum_engine_test.cpp
    source/drum/dsp/DrumParams.cpp
    source/dsp/Params.cpp
    source/dsp/Wavetables.cpp
    source/dsp/UserTables.cpp)
target_compile_features(drum_engine_test PRIVATE cxx_std_17)
add_test(NAME drum_engine_test COMMAND drum_engine_test)
```

(Later tasks append `source/drum/dsp/*.cpp` files here as they are created.) Verify: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target drum_engine_test && ./build/drum_engine_test` from `juce/` → `ALL PASS`.

- [ ] **Step 6: Commit.** `git add juce/source/drum juce/test/drum_engine_test.cpp juce/CMakeLists.txt && git commit -m "feat(dr1-juce): DrumParams — 788-param definition table + headless harness"`

---

### Task 2: DrumTables

**Files:**
- Create: `juce/source/drum/dsp/DrumTables.h`, `juce/source/drum/dsp/DrumTables.cpp`
- Modify: `juce/test/drum_engine_test.cpp` (new section), `juce/CMakeLists.txt` (add DrumTables.cpp to drum_engine_test)

**Interfaces:**
- Consumes: `fable::buildUserTable`, `GeneratedTable`, `FRAMES`, `SIZE` (Wavetables.h); `fable::generateTables()`.
- Produces: `std::vector<GeneratedTable> generateDrumTables();` — exactly THUD, CRACK, TINE, GRIT in that order. The full DR-1 table list is `generateDrumTables()` followed by `generateTables()` (drum tables first), matching `DRUM_TABLE_NAMES`.

- [ ] **Step 1: Write failing tests** (append section to drum_engine_test.cpp):

```cpp
printf("\n== 2. Drum tables ==\n");
auto dt = generateDrumTables();
check(dt.size() == 4, "4 drum tables");
check(dt[0].name == "THUD" && dt[1].name == "CRACK" && dt[2].name == "TINE" && dt[3].name == "GRIT", "names/order");
for (auto& t : dt) {
    check(t.frames == FRAMES && t.size == SIZE && t.mips == MIPS, t.name + " geometry");
    check(finite(t.data) && (int)t.data.size() == t.frames * t.mips * t.size, t.name + " data valid");
    float pk = peak(t.data);
    check(pk > 0.1f && pk <= 1.001f, t.name + " normalized peak", std::to_string(pk));
}
```

- [ ] **Step 2: Run, verify fails** (link error: `generateDrumTables` undefined).

- [ ] **Step 3: Implement.** Port `src/drum/engine/drumtables.ts` function-for-function: each FrameFn fills a `std::vector<float>(SIZE)` for `t = f/(FRAMES-1)`, f = 0..FRAMES-1; then `buildUserTable(name, frames)`. Formulas are pure math — transcribe exactly, including GRIT's hash (`r = sin((i+1)*127.1)*43758.5453; held = (r - floor(r))*2 - 1`) and its `hold = 2 + round(30*t)` sample-and-hold. Note `buildUserTable` normalizes peaks — that is expected and matches the web.

- [ ] **Step 4: Run tests, verify pass** (headless g++ line from Global Constraints, now including DrumTables.cpp). Expected: `ALL PASS`.

- [ ] **Step 5: Update CMake + run `ctest --test-dir build -R drum_engine_test`.** Expected: 1 test passed.

- [ ] **Step 6: Commit.** `git commit -m "feat(dr1-juce): THUD/CRACK/TINE/GRIT drum tables via shared band-limit pipeline"`

---

### Task 3: DrumEngine — pad voice path (no sequencer yet)

**Files:**
- Create: `juce/source/drum/dsp/DrumEngine.h`, `juce/source/drum/dsp/DrumEngine.cpp`
- Modify: `juce/test/drum_engine_test.cpp`, `juce/CMakeLists.txt`

**Interfaces:**
- Consumes: `DrumParams.h`, `fable::TablePtr/EngineTable` idea (define its own `DrumTable` view struct identical to `EngineTable` in Engine.h:125-129), `fable::Rng` (Engine.h — include it; Engine.h is header-heavy but JUCE-free).
- Produces (Tasks 4, 7, 11 rely on these exact signatures):

```cpp
// DrumEngine.h — namespace fable
constexpr int   DR_MAXUNI    = 7;
constexpr int   DR_NBUSES    = 5;      // 0 = MAIN, 1..4 = AUX
constexpr float DR_ACCENT_VEL = 1.0f, DR_PLAIN_VEL = 0.72f;
constexpr double DR_SWING_MAX = 0.667;

class DrumEngine {
public:
    void prepare(double sampleRate);
    void setTables(std::vector<TablePtr> tables);       // same mutex swap scheme as Engine::setTables
    void setParam(int id, float v) { p_[id] = v; }
    void setParams(const DrumParamArray& p) { p_ = p; }
    DrumParamArray& params() { return p_; }

    void trigger(int pad, float vel);                    // port of worklet trigger() incl. choke + phase reset
    void panic();
    void selectPad(int i);                               // viz target

    // Task 4 adds: play/stop/setPatterns/setChain/setBpmOverride/step queries.

    // Render n samples into 5 stereo buses. outs[b][0]=L, outs[b][1]=R; buffers are
    // NOT cleared by the engine caller-side — render() zero-fills all 10 first.
    void render(float* outs[DR_NBUSES][2], int n);

    // viz (read by the processor after render, published as atomics)
    float vizA = -1, vizB = -1, vizEnv = 0;
    // pads triggered since last consume (bit i = pad i) — UI LED flashes
    uint32_t consumeHits() { uint32_t h = hits_; hits_ = 0; return h; }
private:
    // internals: PadVoice/OscState/FilterState structs ported from worklet-drum.js
    DrumParamArray p_ = defaultDrumParams();
    uint32_t hits_ = 0;
    // …
};
```

- [ ] **Step 1: Write failing tests** (new section; build the table list once: `auto tabs = generateDrumTables(); auto wt = generateTables(); … engine.setTables(all)` as `TablePtr`s via `std::make_shared<const GeneratedTable>(std::move(t))`):

```cpp
printf("\n== 3. DrumEngine voice path ==\n");
DrumEngine eng; eng.prepare(48000);
eng.setTables(allTables());                       // helper: drum + wt-1 tables as TablePtrs
auto renderMain = [&](DrumEngine& e, int n) {     // helper: render, return MAIN L
    std::vector<float> bufs[DR_NBUSES][2]; float* outs[DR_NBUSES][2];
    for (int b = 0; b < DR_NBUSES; b++) for (int c = 0; c < 2; c++) {
        bufs[b][c].assign(n, 0.0f); outs[b][c] = bufs[b][c].data(); }
    e.render(outs, n); return bufs[0][0];
};
// trigger produces audio; silence without trigger
check(rms(renderMain(eng, 24000)) < 1e-6, "silent before trigger");
eng.trigger(0, 1.0f);
auto out = renderMain(eng, 24000);
check(finite(out) && rms(out) > 1e-4, "pad 0 trigger produces audio");
// accent louder than plain
eng.panic(); eng.trigger(1, DR_PLAIN_VEL);  double rp = rms(renderMain(eng, 12000));
eng.panic(); eng.trigger(1, DR_ACCENT_VEL); double ra = rms(renderMain(eng, 12000));
check(ra > rp * 1.05, "accent > plain", std::to_string(ra) + " vs " + std::to_string(rp));
// amp env decays to silence (default dec 0.24s → well dead by 2s)
eng.panic(); eng.trigger(0, 1.0f); renderMain(eng, 48000);
check(rms(renderMain(eng, 48000)) < 1e-5, "voice ends after AHD");
// pitch env: penv.amt +24 → higher zero-crossing rate early vs late
// choke: pad 4 and 5 in group 1; triggering 5 silences 4 quickly
eng.panic();
eng.setParam(dpid(4, DP_CHOKE), 1); eng.setParam(dpid(5, DP_CHOKE), 1);
eng.setParam(dpid(4, DP_AENV_DEC), 2.0f);
eng.trigger(4, 1.0f); renderMain(eng, 4800);
eng.trigger(5, 1.0f);
eng.setParam(dpid(5, DP_OSCA_LEVEL), 0.0f);       // mute 5 so we only hear 4's tail
auto tail = renderMain(eng, 4800);
check(rms(tail, 2400) < 1e-4, "choke silences group peer");
// filter on changes spectrum: LP with low cutoff kills highs (compare rms of diff)
// mod matrix: MOD ENV -> CUTOFF moves filter (smoke: output differs from amt 0)
// determinism: two identical engines produce identical output (seeded Rng)
DrumEngine e1, e2; e1.prepare(48000); e2.prepare(48000);
e1.setTables(allTables()); e2.setTables(allTables());
e1.trigger(0, 1); e2.trigger(0, 1);
auto o1 = renderMain(e1, 12000), o2 = renderMain(e2, 12000);
bool same = true; for (int i = 0; i < 12000; i++) if (o1[i] != o2[i]) { same = false; break; }
check(same, "deterministic render (seeded RNG)");
```

Also add: NaN/denormal scan over a 2 s render with all 16 pads triggered at once (`finite()` + peak < 4), and an anti-aliasing check: pad with `oscA.table` = GRIT (worst case), tune +24, long aenv.dec, no filter/noise → render 1 s, `aliasFloorDb` (copy the helper from engine_test.cpp:52-74, f0 from the pad's pitch) < −55 dB — proving the mip pipeline is wired correctly.

- [ ] **Step 2: Run, verify fails** (DrumEngine.h missing).

- [ ] **Step 3: Implement the port.** `DrumEngine.cpp` is a line-faithful translation of `worklet-drum.js`:
  - `PadVoice`/`makeOscState`/`makeFilterState` (js:28-73) → structs with the same members (`Float64Array` → `double[DR_MAXUNI]`, svf `double[8]`). `trigger(v)` resets exactly the same state (js:61-69). `rand` comes from a `Rng` member of the engine (`rng_.next()*2-1`), not `rand()`.
  - `trigger(padI, vel)` (js:126-143): choke group scan over `p_[dpid(j, DP_CHOKE)]`, velocity clamp, phase preset `(clamp01(phase)*2048) mod 2048` for all unison voices. Sets `hits_ |= 1u << padI`.
  - `padMod` (js:145-169): sources `{0, env, vel*v2m, rand}` with `env = exp(-v.t / (max(0.002, modenvDec/4.5) * sr))`; destination offsets ×24 st (PITCH), ×200 ct (FINE), others raw.
  - `setupOsc` (js:171-230): base pitch `60 + tune + (fine + mFine)/100 + pitchEnv + mPitch`; freq guard `0 < f <= 0.45*sr`; level² with 1.2 clamp and 1e-5 gate; posSm one-pole 0.35; mip select `mipF = log2(cps*maxRatio*1024/0.475)`, blend window W = 0.07 into the next-finer mip; unison detune ±50 ct spread, stereo spread 0.6, gain `level*0.32/sqrt(uni)`.
  - `renderOsc` (js:232-280): both the no-blend and blend loops, linear interp within frame and across the two frames (`ft`), per-unison equal-power pan.
  - `setupFilter`/`runFilter` (js:282-367): SVF coefficients `g = tan(pi*cut/sr)`, `k = 2 - 1.93*res`, cutoff smoothing 0.5, LP24 = second pass (`twoPole` when type 1); ADAA drive with `lcosh` (js:23-26), `dg = 1+drive*7`, `dcomp = dg^-0.55`, fallback `dcomp*tanh(dg*0.5*(a+xp))` when |dx| ≤ 1e-5.
  - `ampEnv` (js:369-383) and `renderPad` (js:385-454): 16-sample subblocks re-running `setupOsc` with the pitch env `pe = amt*exp(-4.5*(t+at)/(dec*sr))`; noise white→one-pole tilt `a = 0.02 + (color+1)*0.49`, gain `level²*0.35` (noise RNG = engine `Rng`); optional filter (`DP_FLT_ON`); velocity gain `1 - v2l*(1-vel)`; pad level², equal-power pan; per-voice DC block (DC_R 0.9998); choke fade ×(1−0.12) per sample, kill < 1e-4; end-of-env kill.
  - `render(outs, n)` (Task 3 version): zero all 10 buffers, then for each active voice `renderPad` into `outs[out][0/1]` where `out = clamp(int(p_[dpid(i, DP_OUT)]), 0, 4)`. Chunk internally to ≤128-sample blocks (like `Engine::render`) so `padMod`'s block-rate env matches the worklet's 128-sample process cadence. After render, publish `vizA/vizB/vizEnv` from the selected pad's voice (posSm/-1 idle, ampLevel).
  - `setTables`: same mutex + pointer-swap scheme as `Engine::setTables` (Engine.h:174-179); render try-locks and outputs silence on collision.

- [ ] **Step 4: Run tests, verify pass.** Headless g++ line (now + DrumEngine.cpp) → `ALL PASS`.

- [ ] **Step 5: CMake + ctest green. Commit:** `git commit -m "feat(dr1-juce): DrumEngine pad voice path — 1:1 worklet-drum.js port"`

---

### Task 4: DrumEngine — sequencer, chain, swing, multi-out verification

**Files:**
- Modify: `juce/source/drum/dsp/DrumEngine.h`, `DrumEngine.cpp`, `juce/test/drum_engine_test.cpp`

**Interfaces (added to DrumEngine, used by Tasks 7, 12):**

```cpp
void play();                                  // step=-1, chainPos=0, samplesToNext=0
void stop();
bool isPlaying() const;
void setPatterns(const uint8_t* data, int n); // n must be 4*16*16; copies
void setChain(const int* list, int n);        // ignores empty; clamps chainPos
void setBpmOverride(double bpm);              // host tempo; <= 0 clears override
int  currentStep() const;                     // -1 when stopped
int  currentPattern() const;                  // chain[chainPos]
```

- [ ] **Step 1: Write failing tests:**

```cpp
printf("\n== 4. Sequencer ==\n");
// timing: bpm 120 → step = 6000 samples @48k. Pattern A: pad 0 on steps 0 and 4.
DrumEngine se; se.prepare(48000); se.setTables(allTables());
std::vector<uint8_t> pats(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
pats[0 * DR_NPADS * DR_STEPS + 0 * DR_STEPS + 0] = 2;   // pad0 step0 accent
pats[0 * DR_NPADS * DR_STEPS + 0 * DR_STEPS + 4] = 1;
se.setPatterns(pats.data(), (int)pats.size());
se.setParam(DG_SEQ_BPM, 120); se.setParam(DG_MASTER_SWING, 0);
se.setParam(dpid(0, DP_AENV_DEC), 0.02f);               // short blip → onsets measurable
se.play();
auto out = renderMain(se, 48000);
// find onsets: first sample where |x| exceeds 1e-4 after >1000 quiet samples
// expect onsets at ~0 and ~24000 (step 4 of 6000-sample steps), tolerance ±64 (block quantized firing is sample-accurate; tolerance covers attack ramp)
// swing: swing=1 delays odd steps by 0.667*6000 samples: pad on step 1 fires at 6000+4002
// chain: chain = [0,1]; pattern B (index 1) has pad1 on step 0; after 16 steps the next bar plays pattern B → pad1 onset at 96000+…
// stop(): no further steps fire; currentStep() == -1
// bpm override: setBpmOverride(240) halves the step length regardless of DG_SEQ_BPM
```

Write these as concrete checks with an `onsets(buf)` helper returning sample indices of attack starts. Chain test: use `renderMain(se, 16*6000 + 12000)` and assert pad-1 blip lands in bar 2.

- [ ] **Step 2: Run, verify fails** (play/setPatterns undefined).

- [ ] **Step 3: Implement.** Port `fireStep` (js:493-518) exactly: `dur = (60/bpm/4)*sr` with bpm = override > 0 ? override : clamp(p_[DG_SEQ_BPM], 60, 200); bar wrap advances `chainPos = (chainPos+1) % chain.size()` and resets step to −1 before computing `s = (step+1) % 16`; accent 2 → 1.0, on 1 → 0.72 via the normal `trigger()` (so choke + hit flags work); swing timing `samplesToNext = dur - offNow + offNext` where `off = (s % 2 == 1) ? swing*0.667*dur : 0`. The render loop splits each block at `samplesToNext` boundaries exactly like js:462-477 (`run = min(run, ceil(samplesToNext))`, fire when ≤ 0, decrement by run). `currentStep/currentPattern` are plain members read after render (the processor republishes them as atomics).

- [ ] **Step 4: Multi-out test** (same section): route pad 0 → AUX 2 (`se.setParam(dpid(0, DP_OUT), 2)`), trigger, render; assert `rms(bufs[2][0]) > 1e-4` and `rms(bufs[0][0]) < 1e-6`.

- [ ] **Step 5: Run all tests → ALL PASS; ctest green. Commit:** `git commit -m "feat(dr1-juce): sample-accurate step sequencer — swing, accents, chain, multi-out"`

---

### Task 5: DrumFx

**Files:**
- Create: `juce/source/drum/dsp/DrumFx.h`, `juce/source/drum/dsp/DrumFx.cpp`
- Modify: `juce/test/drum_engine_test.cpp`, `juce/CMakeLists.txt`

**Interfaces:**
- Consumes: `Smooth`, `Biquad`, `DelayLine`, `FvComb`, `FvAllpass` from `source/dsp/Fx.h`; `DrumParams.h`.
- Produces (Task 7 relies on):

```cpp
// DrumFx.h — namespace fable
class DrumFx {
public:
    void prepare(double sampleRate);
    void setParams(const DrumParamArray& p);   // reads DG_FX*, DG_MASTER_VOLUME
    void process(float* L, float* R, int n);   // in-place, MAIN bus only
    void reset();
};
```

- [ ] **Step 1: Write failing tests:**

```cpp
printf("\n== 5. DrumFx ==\n");
// passthrough-ish: all stages off, volume 0.78 → output ≈ input * 0.78² * 1.6
// comp reduces crest factor: feed a sparse impulse train, comp on thr -30:
//   peak/rms ratio with comp < without comp
// drive on amt 1 mix 1: sine in → THD rises (harmonic energy at 3f0 > -40 dB rel f0)
// delay on: impulse in → energy present at t = fx.delay.time (0.36 s)
// reverb on: impulse in → tail energy at 1 s > silence, decays by 6 s
// all finite, no NaN across a 2 s noise render with every stage on
```

Write concretely with the `rms`/`peak`/`aliasFloorDb`-style helpers already in the file.

- [ ] **Step 2: Run, verify fails.**

- [ ] **Step 3: Implement.** Follow `source/dsp/Fx.cpp` stage code as the template (it already ports drive/chorus/ping-pong delay/Freeverb/master/DC/limiter from the same web topology). Differences vs WT-1, all from `src/drum/engine/drum-synth.ts:144-303`:
  - Stage order: drive → **comp** → chorus → delay → reverb → master → DC block → limiter.
  - Every stage: equal-power wet/dry (`wet = sin(mix*π/2)`, `dry = cos(mix*π/2)`, 0.02 s Smooth), OFF → wet 0 dry 1.
  - **COMP (new):** WebAudio `DynamicsCompressorNode` semantics with ratio 4, knee 9 dB, attack 0.003 s, release 0.25 s, threshold = `DG_FXCOMP_THR` dB. Implement per the WebAudio spec the same way `Fx.cpp`'s limiter does (copy its envelope/attack-release/implicit-makeup structure — search "lim" in Fx.cpp), generalized with the soft-knee curve: below `thr` gain 0 dB; within knee `[thr, thr+knee]` quadratic transition; above, slope `1/ratio − 1`. Implicit makeup per spec (`makeup = pow(1/g(0dBFS), 0.6)` form used in Fx.cpp), then explicit MAKEUP `10^(DG_FXCOMP_GAIN/20)`. Detector on `max(|L|,|R|)`. Mix per web: fully wet when ON.
  - Chorus mix ×0.8, delay mix ×0.85, reverb mix ×0.9 (web scaling); delay time smoothing 0.08 s; feedback damping lowpass 4.5 kHz; master volume `vol²*1.6`.
  - Reverb: Freeverb with SIZE → room size + damping exactly as `Fx.cpp` maps `fx.reverb.size`.

- [ ] **Step 4: Run tests → ALL PASS; ctest green. Commit:** `git commit -m "feat(dr1-juce): DrumFx — drive/comp/chorus/delay/reverb chain with WebAudio-spec compressor"`

---

### Task 6: DrumKits

**Files:**
- Create: `juce/source/drum/dsp/DrumKits.h`, `juce/source/drum/dsp/DrumKits.cpp`
- Modify: `juce/test/drum_engine_test.cpp`, `juce/CMakeLists.txt`

**Interfaces:**
- Produces (Task 8 relies on):

```cpp
// DrumKits.h — namespace fable
struct DrumKit {
    std::string name;
    std::vector<std::pair<std::string, float>> params;   // overrides on defaults, string pids
    std::array<std::string, DR_NPADS> padNames;
    std::vector<uint8_t> patterns;                        // 4*16*16
    std::vector<int> chain;                               // e.g. {0}
};
const std::vector<DrumKit>& factoryKits();                // TR-VOID, ROOM ONE, BITCRUSH
DrumParamArray applyKit(const DrumKit& kit);              // defaults + overrides
```

- [ ] **Step 1: Failing tests:** 3 kits, names/order; every override pid resolves (`drumIdFromString != -1`) and is within `[min,max]`; TR-VOID: bpm 126, pad0 `oscA.tune` −14, pads 5/6 choke 1 + `flt.on`; patterns: pad 0 has hits on steps 0/4/8/12 with accent on 0 (value 2); ROOM ONE bpm 116; BITCRUSH `oscA.table` == 3 on all pads; end-to-end: `applyKit(TR-VOID)` into a `DrumEngine`, play 1 bar, MAIN is finite with rms > 1e-4.

- [ ] **Step 2: Run, fails. Step 3: Implement** by transcribing `src/drum/kits.ts:18-113` (PAD_NAMES, `trVoidPatterns` sets, the 16-row `sounds` table, the roomOne/bitcrush derivations — implement the derivations in C++ the same way, deriving from `trVoidParams()`). `applyKit` = `defaultDrumParams()` + overrides via `drumIdFromString`.

- [ ] **Step 4: Tests pass; ctest green. Step 5: Commit:** `git commit -m "feat(dr1-juce): TR-VOID / ROOM ONE / BITCRUSH factory kits"`

---

### Task 7: DrumProcessor + FableDrum plugin target + host-test scaffold

**Files:**
- Create: `juce/source/drum/DrumProcessor.h`, `juce/source/drum/DrumProcessor.cpp`
- Create: `juce/test/drum_host_test.cpp`
- Modify: `juce/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 1–6; WT-1 patterns from `PluginProcessor.{h,cpp}` (APVTS layout creation, atomics, scope ring buffer, state scheme).
- Produces (editor Tasks 9–13 rely on):

```cpp
class DrumAudioProcessor : public juce::AudioProcessor {
public:
    DrumAudioProcessor();
    // standard overrides as in FableAudioProcessor; getName() = "FableSynth DR-1"
    juce::AudioProcessorValueTreeState apvts;

    // pads / transport (message thread → engine via processBlock-applied flags)
    void triggerPad(int pad, float vel);                  // UI audition; thread-safe queue
    void setSeqPlaying(bool on);
    bool isSeqPlaying() const;
    int  getCurrentStep() const;                          // atomic, -1 stopped
    int  getCurrentPattern() const;
    uint32_t consumeHitFlags();                           // atomic exchange(0)
    bool isHostSynced() const;                            // host reported a tempo this block
    double getHostBpm() const;

    // pattern / chain / names / selection (message thread; engine synced in processBlock)
    uint8_t getStep(int pattern, int pad, int step) const;
    void    setStep(int pattern, int pad, int step, uint8_t v);   // 0/1/2
    const std::vector<int>& getChain() const;
    void setChain(std::vector<int> c);
    int  getEditPattern() const;  void setEditPattern(int p);
    juce::String getPadName(int i) const;  void setPadName(int i, juce::String n);
    int  getSelectedPad() const;  void setSelectedPad(int i);     // notifies listeners
    juce::ChangeBroadcaster selectionBroadcaster;

    // kits as programs (WT-1 pattern)
    int getNumPrograms() override;                        // 3
    void setCurrentProgram(int) override;                 // applyKit onto APVTS + patterns/chain/names

    // tables (drum list: 4 drum + 6 wt + user slots) — same API shape as WT-1
    int numTables() const; const fable::GeneratedTable* tableAt(int idx) const;
    juce::String tableName(int idx) const;
    int addUserTableForPad(int pad, fable::UserTable t);  // assigns pad's OSC A table param, returns index or -1

    // HUD
    void  readScope(float* dst, int n) const;             // MAIN post-FX ring buffer
    float getVizPos(int osc) const; float getVizEnv() const;
    bool  getMidiActive() const;
    double getCurrentSr() const;
};
```

- [ ] **Step 1: CMake target.** Add to `juce/CMakeLists.txt` (inside the `FABLE_BUILD_PLUGIN` block) a second `juce_add_plugin(FableDrum PRODUCT_NAME "FableSynth DR-1" COMPANY_NAME "FableSynth" PLUGIN_MANUFACTURER_CODE Fabl PLUGIN_CODE Fdr1 FORMATS AU VST3 Standalone IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE EDITOR_WANTS_KEYBOARD_FOCUS TRUE COPY_PLUGIN_AFTER_BUILD FALSE)` with sources: all `source/drum/*.cpp`, `source/drum/ui/*.cpp` (as created), `source/drum/dsp/*.cpp`, shared `source/ui/Controls.cpp source/ui/Displays.cpp source/WavetableView.cpp source/dsp/{Wavetables,Params,UserTables,Fx}.cpp`; same compile definitions, `juce_generate_juce_header(FableDrum)`, same link libraries, add FableDrum to the APPLE re-sign loop (`foreach(tgt FableSynth FableDrum)` around the existing `foreach(fmt …)`). Also add `drum_host_test` as a `juce_add_console_app` mirroring `plugin_host_test` with the FableDrum source list + `add_test`.
  Note: a temporary editor stub is fine at this point — `createEditor()` returning `new juce::GenericAudioProcessorEditor(*this)` until Task 9 replaces it.

- [ ] **Step 2: Write failing host tests** (`juce/test/drum_host_test.cpp`, modeled on `plugin_host_test.cpp`):

```cpp
// 1. construct → 788 parameters in apvts (count via processor.getParameters(), minus bypass if JUCE adds one)
// 2. bus layout: 5 stereo output buses; getBusCount(false) == 5
// 3. prepareToPlay(48000, 512); MIDI note 36 vel 1.0 → MAIN produces audio within 2 blocks
// 4. pad routed to AUX2 (setParameter pad0.out = 2/4 normalized) + note 36 → bus 2 audible, MAIN silent
// 5. sequencer: setSeqPlaying(true) with TR-VOID (program 0) → audio over one bar; getCurrentStep() advances
// 6. host tempo: set a mock AudioPlayHead reporting bpm 90 → isHostSynced() true and step spacing matches 90 bpm
// 7. all outputs finite
```

Use `proc.setPlayHead(&mockPlayHead)` with a `juce::AudioPlayHead` subclass returning `PositionInfo` with `setBpm(90)`.

- [ ] **Step 3: Implement DrumProcessor.**
  - Buses: constructor `BusesProperties().withOutput("MAIN", juce::AudioChannelSet::stereo(), true).withOutput("AUX 1", …, false) … withOutput("AUX 4", …, false)`; `isBusesLayoutSupported`: MAIN must be stereo; each AUX stereo or disabled.
  - APVTS layout from `drumParamInfo()` exactly like `FableAudioProcessor::createLayout()` builds from `paramInfo()` (same Float/Bool/Choice mapping, same `NormalisableRange` built from `normToValue`/`valueToNorm` — read PluginProcessor.cpp and mirror; use `juce::AudioProcessorParameterGroup` per pad, groups "PAD 01"… "PAD 16", "SEQ", "FX"). Cache `rawParams[DR_NUM_PARAMS]` atomics.
  - `processBlock`: read playhead → bpm override + hostSynced atomic; drain the UI trigger/transport/pattern-dirty queues (a lock-free `juce::AbstractFifo` of small command structs {type, a, b, c} — trigger, play, stop, patterns-changed, chain-changed, select); copy changed rawParams into the engine (WT-1 loop); MIDI: `noteOn` 36–51 → `engine.trigger(note - 36, velocity)`, midiGlow. Render: get bus buffers via `getBusBuffer(buffer, false, b)` for b = 0..4 (buses may be disabled → null/0-channel; pass scratch buffers for disabled AUX so the engine API always gets 10 pointers); `engine.render(outs, n)`; `fx.process(mainL, mainR, n)`; scope ring-buffer push; publish step/pattern/hits/viz atomics.
  - Patterns/chain/names/selection: owned by the processor on the message thread (`std::array<uint8_t, 4*16*16>`), mirrored into the engine through the command queue (pattern edits copy the whole array via a double buffer + atomic flag — simplest correct scheme: keep `patternsShared_` under a `std::mutex`, engine copy applied in processBlock via try-lock).
  - Kits as programs: `setCurrentProgram` → `applyKit` onto APVTS (via `param->setValueNotifyingHost(valueToNorm(...))` like WT-1's `setCurrentProgram`), plus patterns/chain/names.
  - State: WT-1 scheme (PluginProcessor.cpp:249-309) extended — root `"DR1STATE"` containing the APVTS tree, `USERTABLES` pool (identical code), and a `"DRUM"` child with properties: `patterns` (Base64 of the 1024 bytes), `chain` (comma-joined string), `padNames` (16 child props or newline-joined), `selectedPad`, `editPattern`. Restore order: user tables → rebuildEngineTables → patterns/chain/names → APVTS replaceState.
  - Tables: `tables_` = 4 drum + 6 WT-1 procedural (built in constructor), plus `userTables` pool with `MAX_USER_TABLES` slots; combined indexing identical to WT-1 (`tableAt`, `tableName` show "USER n"). `addUserTableForPad` adds to pool then sets `pad<i>.oscA.table` to the new combined index.

- [ ] **Step 4: Build + run.** `cmake --build build --target drum_host_test FableDrum_Standalone && ./build/drum_host_test_artefacts/Release/drum_host_test` (check actual artefact path) → all checks pass, exit 0. Also `ctest --test-dir build` → engine, drum_engine, plugin_host, drum_host all green.

- [ ] **Step 5: Commit:** `git commit -m "feat(dr1-juce): DrumAudioProcessor — 5-bus multi-out, MIDI pads, host tempo sync, kit programs, state"`

---

### Task 8: State round-trip + kit/program coverage in drum_host_test

**Files:**
- Modify: `juce/test/drum_host_test.cpp`

**Interfaces:** consumes Task 7's processor API only.

- [ ] **Step 1: Add failing checks:** (a) tweak `pad3.flt.cut`, set a step `setStep(0, 3, 7, 2)`, chain `{0,1}`, pad name "ZAP", selected pad 3, import a tiny synthetic user table via `addUserTableForPad(2, makeUserTable("IMP", {frame of sin}))`; `getStateInformation` → fresh processor → `setStateInformation` → all of it round-trips (param value, step, chain, name, selection, user table name + pad2 oscA.table index). (b) programs: `setCurrentProgram(1)` → name "ROOM ONE", `seq.bpm` param reads 116; program 2 "BITCRUSH". (c) legacy-tolerance: bare APVTS xml loads without crashing.
- [ ] **Step 2: Run → new checks fail (or pass if Task 7 was complete — then verify by mutating one expectation, confirming the harness catches it, and reverting).**
- [ ] **Step 3: Fix any gaps found. All green.**
- [ ] **Step 4: Commit:** `git commit -m "test(dr1-juce): state round-trip + kit program coverage"`

---

### Task 9: DrumEditor shell + Header

**Files:**
- Create: `juce/source/drum/DrumEditor.h`, `juce/source/drum/DrumEditor.cpp`
- Create: `juce/source/drum/ui/DrumHeader.h`, `juce/source/drum/ui/DrumHeader.cpp`
- Modify: `juce/source/drum/DrumProcessor.cpp` (`createEditor` returns the real editor), `juce/CMakeLists.txt` (add ui cpps to both FableDrum and drum_host_test)

**Interfaces:**
- Consumes: `fui::Knob/Stepper` (Controls.h), `ui/Theme.h` panel painting, `fui::DarkLNF` (ui/LookAndFeel.h), scope display pattern from `ui/Displays.h`, WT-1's `Rack` fixed-logical-size scaling (PluginEditor.h: `LW/LH` + editor scales child).
- Produces: `class DrumRack : public juce::Component` with `static constexpr int LW, LH` (measure from the web: open `src/drum/drum.css` + `DrumApp.tsx`, take the app grid's logical pixel size — expect ~1440×1000; use the CSS values, don't guess); `DrumEditor : juce::AudioProcessorEditor, juce::DragAndDropContainer` scaling DrumRack exactly as `FableAudioProcessorEditor` does. `DrumHeader` component: wordmark ("FABLESYNTH **DR-1**" per web Header.tsx), kit Stepper driven by programs (prev/next call `setCurrentProgram`, label = program name), scope canvas (Timer 30 Hz reading `readScope`, drawn like the WT-1 scope in Displays.cpp), MIDI LED (`getMidiActive`), BPM readout (custom drag-to-edit number bound to `seq.bpm`; when `isHostSynced()` show the SYNC tag and ignore drags, displaying `getHostBpm()`), SWING + VOL `fui::Knob`s (`master.swing`, `master.volume`).

- [ ] **Step 1:** Read `src/drum/DrumApp.tsx`, `src/drum/components/Header.tsx`, `src/drum/drum.css` and `juce/source/PluginEditor.cpp` + `ui/Panels.cpp` (TopBar) for the concrete layout/paint code to mirror. Note the grid areas and px sizes in a comment atop DrumEditor.cpp.
- [ ] **Step 2:** Implement shell + header; editor opens with empty placeholder panels below the header (plain `Theme`-painted rectangles labeled for Tasks 10–13).
- [ ] **Step 3:** Extend `drum_host_test.cpp`: `snapshotEditor`-style headless PNG render (copy plugin_host_test.cpp:26-30, size = DrumRack::LW×LH, output `drum_editor.png`) and assert the image is non-blank (sample a few pixels ≠ background). Run → passes.
- [ ] **Step 4:** Build Standalone, launch briefly (macOS: `open build/FableDrum_artefacts/Release/Standalone/"FableSynth DR-1.app"`), visually confirm header renders. Kill it.
- [ ] **Step 5: Commit:** `git commit -m "feat(dr1-juce): DR-1 editor shell + header (kit stepper, scope, BPM/SYNC, swing/vol)"`

---

### Task 10: PadGrid + PadStrip + QWERTY + drop-WAV

**Files:**
- Create: `juce/source/drum/ui/PadGrid.h`, `PadGrid.cpp`, `PadStrip.h`, `PadStrip.cpp`
- Modify: `juce/source/drum/DrumEditor.{h,cpp}` (mount them), CMakeLists source lists

**Interfaces:**
- Consumes: web `src/drum/components/PadGrid.tsx`, `PadStrip.tsx`, `useDrumKeys.ts` (key map `1234/qwer/asdf/zxcv` → pads 12-15/8-11/4-7/0-3 — verify rows in useDrumKeys.ts; pad 01 is bottom-left), processor API (`triggerPad`, `getSelectedPad/setSelectedPad`, `consumeHitFlags`, `getPadName`, `addUserTableForPad`), `fable::mixToMono/detectCycleLength/sliceToFrames/singleCycleFrame` + `makeUserTable` for WAV import, `juce::AudioFormatManager` for decoding.
- Produces: `PadGrid : juce::Component, juce::FileDragAndDropTarget, juce::KeyListener, juce::Timer` — 4×4 tiles (number, LED flashing from hit flags, name, choke/out tag, cyan ring on selection; click = `setSelectedPad` + `triggerPad(i, 1.0f)`); `PadStrip` — `fui::Stepper` ×2 (`pad<sel>.choke`, `pad<sel>.out`) + `fui::Knob` ×4 (`lvl/pan/v2l/v2m`), rebuilt on `selectionBroadcaster`.

- [ ] **Step 1:** Implement PadGrid (paint per web CSS: tile colors/LED/tag from drum.css classes) with a 30 Hz timer consuming hit flags for LED decay; keyboard via `getTopLevelComponent()->addKeyListener` when editor gains focus.
- [ ] **Step 2:** WAV drop: `isInterestedInFileDrag` → `.wav/.aif/.aiff/.flac/.mp3`; `filesDropped(files, x, y)` → hit-test tile → decode with `AudioFormatManager::createReaderFor`, read up to ~10 s, `mixToMono` → `detectCycleLength` → `sliceToFrames` (fallback `singleCycleFrame`) → `makeUserTable(basename, frames)` → `addUserTableForPad(pad, …)`. (This mirrors the web `usertables.ts` import path — check UserTables.h comments.)
- [ ] **Step 3:** PadStrip with attachment-rebuild on selection change (destroy + recreate the 6 controls with new param ids; they bind by id string).
- [ ] **Step 4:** Extend drum_host_test: after snapshot, simulate `filesDropped` with a generated temp WAV (write a 440 Hz sine WAV via `juce::WavAudioFormat`) on pad 0 → assert `numTables()` grew and `pad0.oscA.table` param points at it. Run → pass. Manual: launch Standalone, click pads (sound + selection ring), press keys, drop a WAV.
- [ ] **Step 5: Commit:** `git commit -m "feat(dr1-juce): pad grid + strip — audition, QWERTY, LED flashes, drop-WAV import"`

---

### Task 11: Pad editor panels (OSC A/B, NOISE, PITCH/AMP ENV, FILTER, MOD)

**Files:**
- Create: `juce/source/drum/ui/DrumPanels.h`, `juce/source/drum/ui/DrumPanels.cpp`
- Modify: `juce/source/drum/DrumEditor.{h,cpp}`, CMakeLists

**Interfaces:**
- Consumes: web `OscSection.tsx`, `NoiseSection.tsx`, `PitchEnvPanel.tsx`, `AmpEnvPanel.tsx`, `FilterSection.tsx`, `ModPanel.tsx`, `PosSlider.tsx`, `DrumEnvView.tsx`, `NoiseView.tsx`; JUCE `WavetableView` (needs a table+pos provider — WT-1's is bound to FableAudioProcessor; if it can't be reused directly, add a small `DrumTerrainView` in DrumPanels.cpp that renders the same perspective terrain from `tableAt(idx)->viz` frames with the live `getVizPos` highlight, following WavetableView.cpp's draw code); `fui::Knob/Stepper/PowerButton`; `ui/Displays.h` filter/env view patterns.
- Produces: `OscPanel(proc, osc /*0=A cyan,1=B amber*/)`, `NoisePanel`, `PitchEnvPanel`, `AmpEnvPanel`, `FilterPanel`, `ModPanel` — each binds to `pad<selected>.<field>` ids and rebuilds its controls on `selectionBroadcaster`. Mod rows: `fui::Stepper` src ▸ `fui::Stepper` dst + `fui::Knob` amt ×4 + MOD ENV DEC knob.

- [ ] **Step 1:** Implement a shared `PadBoundPanel` base handling the rebuild-on-selection plumbing (`changeListenerCallback` → `rebuild()`; subclasses create children with `pad(sel, field)` param ids).
- [ ] **Step 2:** OSC panels: table Stepper (with `nameProvider` for USER slots like WT-1), terrain view with live pos (`getVizPos(osc)`, fall back to POS param when idle — WT-1 behavior), POS slider (VSlider or the web's horizontal PosSlider — match web: horizontal; write a small `HSlider` in DrumPanels.cpp mirroring VSlider), 6 knobs TUNE/FINE/PHASE/UNI/DET/LVL. Noise: readout "WHITE", noise curve view (port NoiseView.tsx canvas: tilt response curve), COLOR + LVL knobs. Envs: knobs + env shape view animating with `getVizEnv()` (port DrumEnvView.tsx). Filter: PowerButton (`flt.on`), type Stepper, response curve (reuse Displays' filter view drawing with SVF math), CUT/RES/DRIVE knobs. Mod: 4 rows + DEC knob.
- [ ] **Step 3:** Snapshot test: drum_host_test re-renders the editor PNG; assert non-blank regions where panels sit. Select pad 3 via `setSelectedPad(3)` before one snapshot to prove rebinding doesn't crash headless.
- [ ] **Step 4:** Manual: Standalone — tweak a knob on pad 2, hear the change on audition; switch pads, knobs re-bind (values jump to the new pad's).
- [ ] **Step 5: Commit:** `git commit -m "feat(dr1-juce): per-pad editor panels with selection rebinding + live views"`

---

### Task 12: StepSeqView

**Files:**
- Create: `juce/source/drum/ui/StepSeqView.h`, `StepSeqView.cpp`
- Modify: `juce/source/drum/DrumEditor.{h,cpp}`, CMakeLists

**Interfaces:**
- Consumes: web `StepSeq.tsx` + `SelBar.tsx`; processor `setSeqPlaying/isSeqPlaying/getCurrentStep/getCurrentPattern/getStep/setStep/getChain/setChain/getEditPattern/setEditPattern/getSelectedPad/getPadName`.
- Produces: `StepSeqView : juce::Component, juce::Timer` — play/stop button; A–D pattern buttons (click = setEditPattern; while CHAIN toggle is on, clicks append to a pending chain committed on toggle-off — mirror StepSeq.tsx's exact interaction); chain readout "A→B→…"; 16 step buttons in 4 groups, click cycles `getStep`→ 0→1→2→0 (`setStep`); amber playhead ring on `getCurrentStep()` while playing (30 Hz timer); "EDITING: <pad name>" label.

- [ ] **Step 1:** Read StepSeq.tsx for the exact tap-cycle and chain-building semantics; implement.
- [ ] **Step 2:** drum_host_test: programmatic check — `setStep(0, 0, 3, 1)` then simulate a click cycle via the component (or directly assert view→processor writes by calling the same handler), assert 1→2→0 cycling; snapshot includes the seq row.
- [ ] **Step 3:** Manual: program a beat in Standalone, hear it, watch the playhead; build a chain A→B and hear the switch at the bar.
- [ ] **Step 4: Commit:** `git commit -m "feat(dr1-juce): step sequencer view — patterns, chain builder, playhead"`

---

### Task 13: DrumFxRack + OUT panel

**Files:**
- Create: `juce/source/drum/ui/DrumFxRack.h`, `DrumFxRack.cpp`
- Modify: `juce/source/drum/DrumEditor.{h,cpp}`, CMakeLists

**Interfaces:**
- Consumes: web `FxRack.tsx`, `OutPanel.tsx`; `fui::Knob/PowerButton`; processor pad.out params.
- Produces: five knob groups (DRIVE amt/mix, COMP thresh/makeup, CHORUS rate/depth/mix, DELAY time/fdbk/mix, REVERB size/mix — each with PowerButton) + OUT summary panel listing MAIN/AUX1–4 with the pads currently assigned to each (rebuilt on a 1 Hz timer or parameter listener; now reflects *real* routing).

- [ ] **Step 1:** Implement (straight port of the web layout; reuse WT-1 FxPanel structure from ui/Panels.cpp as the code template).
- [ ] **Step 2:** Snapshot check covers the rack row. Manual: toggle REVERB in Standalone, hear it.
- [ ] **Step 3: Commit:** `git commit -m "feat(dr1-juce): FX rack + OUT routing panel"`

---

### Task 14: Docs + final verification gate

**Files:**
- Modify: `juce/README.md` (DR-1 section: architecture table for `source/drum/`, build/test commands, `docs/drum_editor.png` snapshot), root `README.md` (link the DR-1 plugin), `juce/docs/` (commit the generated `drum_editor.png`)
- Modify: `juce/test/drum_host_test.cpp` if the snapshot path needs to write into `juce/docs/`

**Steps:**

- [ ] **Step 1: Full clean verification.** From `juce/`: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build --output-on-failure` → **all 4 tests pass** (engine_test, drum_engine_test, plugin_host_test, drum_host_test). Confirm artifacts exist: `build/FableDrum_artefacts/Release/VST3/`, `Standalone/`, `AU/`.
- [ ] **Step 2:** Regenerate editor snapshot via `drum_host_test <output-dir>` into `juce/docs/drum_editor.png`.
- [ ] **Step 3:** Write README sections (mirror the WT-1 README's tone/structure; include the multi-out, host-sync, and drop-WAV notes and the Freeverb/compressor mapping notes).
- [ ] **Step 4:** Headless g++ sanity (proves DSP stayed JUCE-free): the Global Constraints g++ line → `ALL PASS`.
- [ ] **Step 5:** Install locally per the usual flow (see memory: copy `FableSynth DR-1.vst3`/`.component` to `~/Library/Audio/Plug-Ins/{VST3,Components}/`) — optional if running unattended; skip rather than block.
- [ ] **Step 6: Commit:** `git commit -m "docs(dr1-juce): README + editor snapshot — DR-1 JUCE port complete"`
