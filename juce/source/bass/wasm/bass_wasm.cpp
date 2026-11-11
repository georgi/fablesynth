// BL-1 WebAssembly bridge (proof of concept, docs/wasm-bl1.md).
// A plain C API around one BassEngine + its BassFx, driven by
// src/bass/engine/worklet-bass-wasm.js. JUCE-free. It adds no DSP: every
// call forwards to the same entry points BassAudioProcessor uses.
//
// Memory contract: the module is linked with a fixed heap (no growth), so the
// worklet's typed-array views never detach. Every buffer the worklet writes
// into is a static array exported by pointer. Only the message-time calls
// (create, tables, keyOn's held list) allocate; bl_render never does.
#include "../dsp/BassEngine.h"
#include "../dsp/BassFx.h"
#include "../dsp/BassParams.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#define BL_EXPORT extern "C" __attribute__((used, visibility("default")))

using namespace fable;

namespace {

constexpr int kBlock = 128;          // AudioWorklet render quantum
constexpr int kMaxTables = 16;
constexpr int kMaxChain = 64;
constexpr int kTelemetryValues = FxTelemetry::count;

BassEngine* engine = nullptr;
BassFx* fx = nullptr;
bool fxDirty = true, fxSnap = true;

float outL[kBlock], outR[kBlock];
uint8_t patterns[BL_PATTERN_BYTES];
int chain[kMaxChain];
int arpNotes[kMaxArpCycle];
uint8_t arpFlags[48];                 // hits[16], accents[16], slides[16]
int8_t scaleTable[64 * 12];
float telemetry[kTelemetryValues];
uint32_t telemetrySerial = 0;

std::vector<std::shared_ptr<GeneratedTable>> staging;

} // namespace

// ---- lifecycle ----
BL_EXPORT void bl_create() {
    if (!engine) { engine = new BassEngine(); fx = new BassFx(); }
}
BL_EXPORT void bl_prepare(double sampleRate) {
    engine->prepare(sampleRate);
    fx->prepare(sampleRate);
    engine->params() = defaultBassParams();
    engine->snapParams();
    fx->setParams(engine->params());
    fx->snapRamps();
    fxDirty = false; fxSnap = false;
}
BL_EXPORT int bl_latency() { return fx->latencySamples(); }

// ---- parameters (ids from BassParams, the single source) ----
BL_EXPORT int bl_param_count() { return BL_NUM_PARAMS; }
BL_EXPORT const char* bl_param_name(int id) {
    const auto& info = bassParamInfo();
    return id >= 0 && id < (int)info.size() ? info[(size_t)id].pid.c_str() : "";
}
BL_EXPORT void bl_set_param(int id, float v) {
    if (id < 0 || id >= BL_NUM_PARAMS) return;
    engine->setParam(id, v);
    fxDirty = true;
}
// Patch load / 'init': jump the smoothed copies and the FX ramps to the targets.
BL_EXPORT void bl_snap_params() { engine->snapParams(); fxDirty = true; fxSnap = true; }

// ---- wavetables: alloc writes into wasm memory, commit publishes the set ----
BL_EXPORT float* bl_table_alloc(int index, int frames, int mips, int size) {
    if (index < 0 || index >= kMaxTables || frames <= 0 || mips <= 0 || size <= 0) return nullptr;
    if ((int)staging.size() <= index) staging.resize((size_t)index + 1);
    auto t = std::make_shared<GeneratedTable>();
    t->frames = frames; t->mips = mips; t->size = size;
    t->data.assign((size_t)frames * mips * size, 0.0f);
    staging[(size_t)index] = t;
    return t->data.data();
}
BL_EXPORT void bl_tables_commit(int count) {
    std::vector<TablePtr> set;
    for (int i = 0; i < count && i < (int)staging.size(); ++i)
        if (staging[(size_t)i]) set.push_back(staging[(size_t)i]);
    staging.clear();
    engine->setTables(std::move(set));
    // Worklet messages and process() share one thread, so nothing is rendering.
    engine->collectRetiredTables();
}

// ---- sequencer ----
BL_EXPORT uint8_t* bl_patterns_ptr() { return patterns; }
BL_EXPORT void bl_set_patterns() { engine->setPatterns(patterns, BL_PATTERN_BYTES); }
BL_EXPORT int* bl_chain_ptr() { return chain; }
BL_EXPORT void bl_set_chain(int n) { engine->setChain(chain, std::clamp(n, 0, kMaxChain)); }
BL_EXPORT void bl_play() { engine->play(); }
BL_EXPORT void bl_stop() { engine->stop(); }
BL_EXPORT int bl_current_step() { return engine->currentStep(); }
BL_EXPORT int bl_current_pattern() { return engine->currentPattern(); }
BL_EXPORT int bl_is_playing() { return engine->isPlaying() ? 1 : 0; }
// Packed step flags for the 'step' message: bit0 on, bit1 acc, bit2 slide,
// bits 8.. = semitone offset + 128. Reads the engine's own pattern copy rules.
BL_EXPORT int bl_step_info(int pat, int s) {
    const BassStep st = BassEngine::readStep(patterns, pat, s);
    return (st.on ? 1 : 0) | (st.acc ? 2 : 0) | (st.slide ? 4 : 0) | ((st.semi + 128) << 8);
}

// ---- voice ----
BL_EXPORT void bl_key_on(int semi, float vel) { engine->keyOn(semi, vel, false); }
BL_EXPORT void bl_key_off(int semi) { engine->keyOff(semi); }
BL_EXPORT void bl_panic() { engine->panic(); fx->reset(); }

// ---- arp / scale / rhythm ----
BL_EXPORT int* bl_arp_notes_ptr() { return arpNotes; }
BL_EXPORT uint8_t* bl_arp_flags_ptr() { return arpFlags; }
BL_EXPORT int bl_arp_max() { return kMaxArpCycle; }
BL_EXPORT void bl_set_arp(int enabled, double rate, double gate, int length) {
    ArpPattern a;
    a.enabled = enabled != 0;
    if (a.enabled) {
        a.rate = rate; a.gate = gate;
        a.length = std::clamp(length, 16, kMaxArpCycle);
        std::copy(arpNotes, arpNotes + a.length, a.notes.begin());
        for (int i = 0; i < 16; ++i) {
            a.hits[(size_t)i] = arpFlags[i] != 0;
            a.accents[(size_t)i] = arpFlags[16 + i] != 0;
            a.slides[(size_t)i] = arpFlags[32 + i] != 0;
        }
    }
    engine->setArp(a);
}
BL_EXPORT int8_t* bl_scale_ptr() { return scaleTable; }
BL_EXPORT void bl_set_scale(int length) {
    ScaleMap m;
    m.length = std::clamp(length, 0, 64);
    for (int t = 0; t < m.length; ++t)
        for (int k = 0; k < 12; ++k) m.table[(size_t)t][(size_t)k] = scaleTable[t * 12 + k];
    engine->setSeqScale(m);
}
BL_EXPORT void bl_set_rhythm(int enabled, int fit, int steps, int rotation, int cycleBeats) {
    SequenceRhythm r;
    r.enabled = enabled != 0; r.fit = fit != 0;
    r.steps = steps; r.rotation = rotation; r.cycleBeats = cycleBeats;
    engine->setSequenceRhythm(r);
}

// ---- render: one quantum into the static L/R buffers ----
BL_EXPORT float* bl_out_l() { return outL; }
BL_EXPORT float* bl_out_r() { return outR; }
BL_EXPORT void bl_render(int n) {
    n = std::clamp(n, 0, kBlock);
    if (fxDirty) {
        fx->setParams(engine->params());
        if (fxSnap) { fx->snapRamps(); fxSnap = false; }
        fxDirty = false;
    }
    engine->render(outL, outR, n);
    fx->process(outL, outR, n);
}

// ---- viz + telemetry (read after bl_render) ----
// [pos, env, fenv, cut, gate, semi]
BL_EXPORT float bl_viz(int field) {
    switch (field) {
        case 0: return engine->vizPos;
        case 1: return engine->vizEnv;
        case 2: return engine->vizFenv;
        case 3: return engine->vizCut;
        case 4: return engine->vizGate ? 1.0f : 0.0f;
        default: return (float)engine->vizSemi;
    }
}
BL_EXPORT float* bl_telemetry_ptr() { return telemetry; }
BL_EXPORT int bl_telemetry_count() { return kTelemetryValues; }
// Copies a fresh snapshot and returns 1 when the meter published since the last call.
BL_EXPORT int bl_telemetry_take() {
    const FxTelemetry t = fx->telemetry();
    if ((uint32_t)t.serial == telemetrySerial) return 0;
    telemetrySerial = (uint32_t)t.serial;
    std::copy(t.values.begin(), t.values.end(), telemetry);
    return 1;
}
