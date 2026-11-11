// WT-1 WebAssembly bridge (docs/wasm-wt1.md).
// A plain C API around one fable::Engine + its fable::Fx, driven by
// src/engine/worklet-wasm.js. JUCE-free. It adds no DSP: every call forwards
// to the public entry points FableAudioProcessor uses.
//
// Memory contract: the module is linked with a fixed heap (no growth), so the
// worklet's typed-array views never detach. Every buffer the worklet writes
// into is a static array exported by pointer. Only the message-time calls
// (create, table slots) allocate; wt_render never does.
#include "../Engine.h"
#include "../Fx.h"
#include "../Params.h"
#include "HostedWasm.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#define WT_EXPORT extern "C" __attribute__((used, visibility("default")))

using namespace fable;

namespace {

constexpr int kBlock = 128;          // AudioWorklet render quantum
constexpr int kMaxTables = 256;      // worklet.js slot limit
constexpr int kMaxChain = 64;
constexpr int kTelemetryValues = FxTelemetry::count;

Engine* engine = nullptr;
Fx* fx = nullptr;
double fxBpm = 120.0;                // worklet.js this.bpm (LFO sync, echo sync)
double transportBeats = 0.0;         // worklet.js this.transportBeats (LAB grid)
double sampleRateHz = 48000.0;

float outL[kBlock], outR[kBlock];
uint8_t patterns[SEQ_PATTERN_BYTES];
int chain[kMaxChain];
int arpNotes[kMaxArpCycle];
uint8_t arpFlags[32];                 // hits[16], accents[16]
uint8_t arpStack[kMaxArpCycle * kMaxCompNotes];
uint8_t arpStackCount[kMaxArpCycle];
uint8_t arpLens[kMaxArpCycle];
int8_t scaleTable[64 * 12];
float telemetry[kTelemetryValues];
float modViz[NUM_MOD_DESTS];
uint32_t telemetrySerial = 0;

// Table slots, the worklet's this.tables. An empty slot publishes as an empty
// EngineTable, as a hole in the JS array reads as undefined.
std::vector<std::shared_ptr<GeneratedTable>> slots;
wasm::Hosted hosted;
bool hostOn = false;
double hostAnchor = 0.0;             // shared-timebase beat zero ('tempo')

// The arp held in the arp buffers (written by the worklet before the call).
ArpPattern readArp(int enabled, double rate, double gate, int length, int comp, int strum) {
    ArpPattern a;
    a.enabled = enabled != 0;
    if (a.enabled) {
        a.rate = rate; a.gate = gate;
        a.length = std::clamp(length, 16, kMaxArpCycle);
        std::copy(arpNotes, arpNotes + a.length, a.notes.begin());
        for (int i = 0; i < 16; ++i) {
            a.hits[(size_t)i] = arpFlags[i] != 0;
            a.accents[(size_t)i] = arpFlags[16 + i] != 0;
        }
        a.comp = comp != 0;
        if (a.comp) {
            a.strum = strum;
            for (int s = 0; s < a.length; ++s) {
                a.stackCount[(size_t)s] = (uint8_t)std::min<int>(arpStackCount[s], kMaxCompNotes);
                a.lens[(size_t)s] = arpLens[s];
                for (int k = 0; k < kMaxCompNotes; ++k)
                    a.stack[(size_t)s][(size_t)k] = arpStack[s * kMaxCompNotes + k];
            }
        }
    }
    return a;
}

void publishTables() {
    std::vector<TablePtr> set(slots.begin(), slots.end());
    engine->setTables(std::move(set));
    // Worklet messages and process() share one thread, so nothing is rendering.
    engine->collectRetiredTables();
}

} // namespace

// ---- lifecycle ----
WT_EXPORT void wt_create() {
    if (!engine) { engine = new Engine(); fx = new Fx(); hosted.create(); }
}
WT_EXPORT void wt_prepare(double sampleRate) {
    sampleRateHz = std::max(1.0, sampleRate);
    engine->prepare(sampleRate);
    fx->prepare(sampleRate);
    engine->setParams(defaultParams());
    // The web build has no host transport: synced LFOs always follow this.bpm.
    engine->setBpm(fxBpm);
    engine->setTransport(0.0, true);
    transportBeats = 0.0;
}
WT_EXPORT int wt_latency() { return fx->latencySamples(); }

// ---- parameters (ids from Params.cpp, the single source) ----
WT_EXPORT int wt_param_count() { return NUM_PARAMS; }
WT_EXPORT const char* wt_param_name(int id) {
    const auto& info = paramInfo();
    return id >= 0 && id < NUM_PARAMS ? info[(size_t)id].pid.c_str() : "";
}
// worklet.js writes `p` directly for init and for each 'p' message.
WT_EXPORT void wt_set_param(int id, float v) {
    if (id < 0 || id >= NUM_PARAMS) return;
    engine->setParam(id, v);
}
WT_EXPORT void wt_set_bpm(double bpm) { fxBpm = bpm; engine->setBpm(bpm); }
WT_EXPORT void wt_bend(double semis) { engine->pitchBend(semis); }

// ---- wavetables: alloc writes into wasm memory, then the set is republished ----
WT_EXPORT float* wt_table_alloc(int index, int frames, int mips, int size) {
    if (index < 0 || index >= kMaxTables || frames <= 0 || mips <= 0 || size <= 0) return nullptr;
    if ((int)slots.size() <= index) slots.resize((size_t)index + 1);
    auto t = std::make_shared<GeneratedTable>();
    t->frames = frames; t->mips = mips; t->size = size;
    t->data.assign((size_t)frames * mips * size, 0.0f);
    slots[(size_t)index] = t;
    return t->data.data();
}
WT_EXPORT void wt_table_count(int n) {
    slots.resize((size_t)std::clamp(n, 0, kMaxTables));
}
WT_EXPORT void wt_tables_commit() { publishTables(); }

// ---- sequencer ----
WT_EXPORT uint8_t* wt_patterns_ptr() { return patterns; }
WT_EXPORT int wt_patterns_bytes() { return SEQ_PATTERN_BYTES; }
WT_EXPORT void wt_set_patterns() { engine->setSeqPatterns(patterns, SEQ_PATTERN_BYTES); }
WT_EXPORT int* wt_chain_ptr() { return chain; }
WT_EXPORT void wt_set_chain(int n) { engine->setSeqChain(chain, std::clamp(n, 0, kMaxChain)); }
WT_EXPORT void wt_play() { engine->seqPlay(); }
WT_EXPORT void wt_stop() { engine->seqStop(); }
WT_EXPORT int wt_current_step() { return engine->seqCurrentStep(); }
WT_EXPORT int wt_current_pattern() { return engine->seqCurrentPattern(); }

// ---- voice ----
WT_EXPORT void wt_note_on(int note, double vel) { engine->noteOn(note, vel); }
WT_EXPORT void wt_note_off(int note) { engine->noteOff(note); }
WT_EXPORT void wt_panic() { engine->panic(); fx->reset(); }

// ---- arp / comp / scale / rhythm ----
WT_EXPORT int* wt_arp_notes_ptr() { return arpNotes; }
WT_EXPORT uint8_t* wt_arp_flags_ptr() { return arpFlags; }
WT_EXPORT uint8_t* wt_arp_stack_ptr() { return arpStack; }
WT_EXPORT uint8_t* wt_arp_stack_count_ptr() { return arpStackCount; }
WT_EXPORT uint8_t* wt_arp_lens_ptr() { return arpLens; }
WT_EXPORT int wt_arp_max() { return kMaxArpCycle; }
WT_EXPORT int wt_comp_max_notes() { return kMaxCompNotes; }
WT_EXPORT void wt_set_arp(int enabled, double rate, double gate, int length, int comp, int strum) {
    engine->setArp(readArp(enabled, rate, gate, length, comp, strum));
}
WT_EXPORT int8_t* wt_scale_ptr() { return scaleTable; }
WT_EXPORT void wt_set_scale(int length) { engine->setSeqScale(wasm::scaleMap(scaleTable, length)); }
WT_EXPORT void wt_set_rhythm(int enabled, int fit, int steps, int rotation, int cycleBeats) {
    engine->setSequenceRhythm(wasm::sequenceRhythm(enabled, fit, steps, rotation, cycleBeats));
}

// ---- automation tables: 'seqauto' and hosted clip lanes share one staging bank ----
WT_EXPORT int wt_auto_max_lanes() { return AUTO_MAX_LANES; }
WT_EXPORT float* wt_auto_lane(int index, int paramId, int len, int fit, int rot, int isFx) {
    return hosted.lane(index, paramId, NUM_PARAMS, len, fit, rot, isFx);
}
// Standalone sequence automation (worklet.js 'seqauto').
WT_EXPORT void wt_auto_commit(int count) { engine->setSequenceAutomation(hosted.automation(std::max(0, count))); }

// ---- SQ-4 hosted clips (docs/wasm-sq4.md): the Engine calls SeqProcessor uses ----
WT_EXPORT void wt_host(int on) {
    hostOn = on != 0;
    engine->setHostClipMode(hostOn, kBlock);
}
// Full panic: drop the playing and pending clip (and its automation).
WT_EXPORT void wt_host_clear() {
    engine->setHostClipMode(false);
    if (hostOn) engine->setHostClipMode(true, kBlock);
}
WT_EXPORT void wt_host_tempo(double bpm, double swing, double anchor) {
    hostAnchor = anchor;
    engine->hostTempo(bpm, swing, anchor);
}
WT_EXPORT uint8_t* wt_clip_ptr() { return hosted.clip; }
WT_EXPORT int wt_clip_max() { return wasm::kClipBytes; }
// Clip bytes in wt_clip_ptr, the arp in the arp buffers, the scale in
// wt_scale_ptr, the automation in the staged lanes (autoCount < 0: none).
// update != 0 hot-swaps the pending or playing clip (atFrame, tag unused).
WT_EXPORT void wt_host_clip(int update, int bytes, int bars, double atFrame, int tag,
                            int arpOn, double rate, double gate, int length, int comp, int strum,
                            int rOn, int rFit, int rSteps, int rRot, int rCycle,
                            int scaleLen, int autoCount) {
    bytes = std::clamp(bytes, 0, wasm::kClipBytes);
    const ArpPattern arp = readArp(arpOn, rate, gate, length, comp, strum);
    const SequenceRhythm rhythm = wasm::sequenceRhythm(rOn, rFit, rSteps, rRot, rCycle);
    const ScaleMap scale = wasm::scaleMap(scaleTable, scaleLen);
    const AutoBank* automation = hosted.automation(autoCount);
    if (update) engine->hostClipUpdate(hosted.clip, bytes, bars, arp, automation, rhythm, scale);
    else engine->hostClip(hosted.clip, bytes, bars, atFrame, tag, arp, automation, rhythm, scale);
}
WT_EXPORT void wt_host_stop(double atFrame) { engine->hostClipStop(atFrame); }
WT_EXPORT double* wt_events_ptr() { return hosted.events; }
WT_EXPORT int wt_events_take() { return hosted.take(*engine); }

// ---- render: one quantum into the static L/R buffers ----
WT_EXPORT float* wt_out_l() { return outL; }
WT_EXPORT float* wt_out_r() { return outR; }
// frame: the block's first context frame (the SQ-4 shared timebase).
WT_EXPORT void wt_render(int n, double frame) {
    n = std::clamp(n, 0, kBlock);
    fx->setParams(engine->params(), fxBpm);
    // The LAB GLITCH grid; hosted, it follows the shared timebase (worklet.js).
    fx->setTransport(hostOn ? std::max(0.0, frame - hostAnchor) * (fxBpm / 60.0) / sampleRateHz : transportBeats, true);
    engine->hostSetFrame(frame);
    engine->render(outL, outR, n);
    if (engine->takeAutomationFxDirty()) fx->setParams(engine->params(), fxBpm); // SeqProcessor::renderWt
    fx->process(outL, outR, n);
    transportBeats += n / sampleRateHz * (fxBpm / 60.0);
}

// ---- viz + telemetry (read after wt_render) ----
// [a, b, active voices]
WT_EXPORT float wt_viz(int field) {
    switch (field) {
        case 0: return (float)engine->vizA;
        case 1: return (float)engine->vizB;
        default: return (float)engine->vizActive;
    }
}
WT_EXPORT float* wt_mod_ptr() { return modViz; }
WT_EXPORT int wt_mod_count() { return NUM_MOD_DESTS; }
// Copies the live route sums; returns 1 while a routed voice sounds.
WT_EXPORT int wt_mod_take() {
    std::copy(engine->vizMod, engine->vizMod + NUM_MOD_DESTS, modViz);
    return engine->vizModAny ? 1 : 0;
}
WT_EXPORT float* wt_telemetry_ptr() { return telemetry; }
WT_EXPORT int wt_telemetry_count() { return kTelemetryValues; }
// Copies a fresh snapshot and returns 1 when the meter published since the last call.
WT_EXPORT int wt_telemetry_take() {
    const FxTelemetry t = fx->telemetry();
    if ((uint32_t)t.serial == telemetrySerial) return 0;
    telemetrySerial = (uint32_t)t.serial;
    std::copy(t.values.begin(), t.values.end(), telemetry);
    return 1;
}
