// DR-1 WebAssembly bridge (proof of concept, docs/wasm-dr1.md).
// A plain C API around one DrumEngine (which owns its DrumFx chains), driven
// by src/drum/engine/worklet-drum-wasm.js. JUCE-free. It adds no DSP: every
// call forwards to the entry points DrumAudioProcessor uses.
//
// Memory contract: the module is linked with a fixed heap (no growth), so the
// worklet's typed-array views never detach. Every buffer the worklet writes
// into or reads from is a static array exported by pointer. Only the
// message-time calls (create, tables, automation) allocate; dr_render never does.
#include "../dsp/DrumEngine.h"
#include "../dsp/DrumParams.h"
#include "../dsp/DrumRhythm.h"
#include "../../dsp/wasm/HostedWasm.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#define DR_EXPORT extern "C" __attribute__((used, visibility("default")))

using namespace fable;

namespace {

constexpr int kBlock = 128;                  // AudioWorklet render quantum
constexpr int kMaxTables = 64;
constexpr int kPatternBytes = DR_NPATTERNS * DR_NPADS * DR_STEPS;
constexpr int kLaneInts = 10 + 256;          // see dr_set_rhythm
constexpr int kCapture = 4096;               // padAudio block (worklet-drum.js CAPTURE_LEN)
constexpr int kTelemetryValues = FxTelemetry::count;

DrumEngine* engine = nullptr;

float outs[DR_NBUSES][2][kBlock];
float kickKey[kBlock];
float keyIn[KEY_CHANNELS][kBlock];
uint8_t patterns[kPatternBytes];
int chain[DR_NPATTERNS * 4];
int32_t rhythmBuf[DR_RHYTHM_LANES * kLaneInts];
DrumRhythm rhythm, clipRhythm;
wasm::Hosted hosted;                         // clip bytes, events, the automation staging bank
bool hostOn = false;
float captureBuf[kCapture];
uint64_t captureCursor[DR_NPADS], captureStart[DR_NPADS];
float telemetry[kTelemetryValues];
uint64_t telemetrySerial = 0, telemetryReverbSerial = 0;
int telemetryPad = -2, telemetryBus = -2;

std::vector<std::shared_ptr<GeneratedTable>> staging;

// Unpacks rhythmBuf (layout: see dr_set_rhythm) into `out`.
void readRhythm(DrumRhythm& out) {
    out = DrumRhythm{};
    for (int i = 0; i < DR_RHYTHM_LANES; ++i) {
        const int32_t* l = rhythmBuf + i * kLaneInts;
        if (!l[0]) continue;
        auto& lane = out.lanes[(size_t)i];
        lane.enabled = l[1] != 0; lane.micro = l[2] != 0; lane.delayMs = l[3];
        lane.sourceBar = l[4]; lane.steps = l[5]; lane.rotation = l[6];
        lane.mode = l[7] ? DrumRhythmMode::fit : DrumRhythmMode::grid;
        lane.cycleBeats = l[8];
        const int n = std::clamp((int)l[9], 0, 256);
        for (int k = 0; k < n; ++k) lane.stepDelayMs[(size_t)k] = l[10 + k];
    }
}

} // namespace

// ---- lifecycle ----
DR_EXPORT void dr_create() {
    if (!engine) { engine = new DrumEngine(); hosted.create(); }
}
DR_EXPORT void dr_prepare(double sampleRate) {
    engine->prepare(sampleRate);
    engine->enablePadFx(true);
    engine->setKickKeyOutput(kickKey);
}
DR_EXPORT int dr_latency() { return engine->latencySamples(); }

// ---- parameters (ids from DrumParams, the single source) ----
DR_EXPORT int dr_param_count() { return DR_NUM_PARAMS; }
DR_EXPORT const char* dr_param_name(int id) {
    const auto& info = drumParamInfo();
    return id >= 0 && id < (int)info.size() ? info[(size_t)id].pid.c_str() : "";
}
DR_EXPORT void dr_set_param(int id, float v) {
    if (id >= 0 && id < DR_NUM_PARAMS) engine->setParam(id, v);
}

// ---- wavetables: alloc writes into wasm memory, commit publishes the set ----
DR_EXPORT float* dr_table_alloc(int index, int frames, int mips, int size) {
    if (index < 0 || index >= kMaxTables || frames <= 0 || mips <= 0 || size <= 0) return nullptr;
    if ((int)staging.size() <= index) staging.resize((size_t)index + 1);
    auto t = std::make_shared<GeneratedTable>();
    t->frames = frames; t->mips = mips; t->size = size;
    t->data.assign((size_t)frames * mips * size, 0.0f);
    staging[(size_t)index] = t;
    return t->data.data();
}
DR_EXPORT void dr_tables_commit(int count) {
    std::vector<TablePtr> set;
    for (int i = 0; i < count && i < (int)staging.size(); ++i) set.push_back(staging[(size_t)i]);
    staging.clear();
    engine->setTables(std::move(set));   // keeps index order; an empty slot stays empty
}

// ---- pads ----
DR_EXPORT void dr_trigger(int pad, float vel) { engine->trigger(pad, vel); }
DR_EXPORT void dr_panic() { engine->panic(); }
DR_EXPORT void dr_select(int pad) { engine->selectPad(pad); }

// ---- sequencer ----
DR_EXPORT uint8_t* dr_patterns_ptr() { return patterns; }
DR_EXPORT void dr_set_patterns() { engine->setPatterns(patterns, kPatternBytes); }
DR_EXPORT int* dr_chain_ptr() { return chain; }
DR_EXPORT int dr_chain_max() { return (int)(sizeof chain / sizeof chain[0]); }
DR_EXPORT void dr_set_chain(int n) { engine->setChain(chain, std::clamp(n, 0, dr_chain_max())); }
// Per lane, kLaneInts ints: present, enabled, micro, delayMs, sourceBar, steps,
// rotation, mode (0 grid, 1 fit), cycleBeats, stepDelay count, 256 step delays.
// present == 0 everywhere, or on == 0, clears the rhythm.
DR_EXPORT int32_t* dr_rhythm_ptr() { return rhythmBuf; }
DR_EXPORT void dr_set_rhythm(int on) {
    if (!on) { engine->setDrumRhythm(nullptr); return; }
    readRhythm(rhythm);
    engine->setDrumRhythm(&rhythm);
}
DR_EXPORT void dr_play() { engine->play(); }
DR_EXPORT void dr_stop() { engine->stop(); }
DR_EXPORT int dr_current_step() { return engine->currentStep(); }
DR_EXPORT int dr_current_pattern() { return engine->currentPattern(); }
DR_EXPORT int dr_is_playing() { return engine->isPlaying() ? 1 : 0; }
DR_EXPORT uint32_t dr_consume_hits() { return engine->consumeHits(); }
DR_EXPORT int dr_lane_position(int i) { return engine->lanePosition(i); }

// ---- standalone sequence automation (precompiled tables, 'seqauto') ----
DR_EXPORT int dr_auto_max_lanes() { return AUTO_MAX_LANES; }
DR_EXPORT int dr_auto_table_size() { return AUTO_TABLE_SIZE; }
DR_EXPORT float* dr_auto_lane(int index, int paramId, int len, int fit, int rot, int isFx) {
    return hosted.lane(index, paramId, DR_NUM_PARAMS, len, fit, rot, isFx);
}
DR_EXPORT void dr_auto_commit(int count) {
    engine->setSequenceAutomation(hosted.automation(std::max(0, count)));
}

// ---- SQ-4 hosted clips (docs/wasm-sq4.md): the DrumEngine calls SeqProcessor uses ----
DR_EXPORT void dr_host(int on) {
    hostOn = on != 0;
    engine->setHostClipMode(hostOn, kBlock);
}
// Full panic: drop the playing and pending clip (and its automation).
DR_EXPORT void dr_host_clear() {
    engine->setHostClipMode(false);
    if (hostOn) engine->setHostClipMode(true, kBlock);
}
DR_EXPORT void dr_host_tempo(double bpm, double swing, double anchor) { engine->hostTempo(bpm, swing, anchor); }
DR_EXPORT uint8_t* dr_clip_ptr() { return hosted.clip; }
DR_EXPORT int dr_clip_max() { return wasm::kClipBytes; }
// Clip bytes in dr_clip_ptr, the lane rhythm in dr_rhythm_ptr (rhythmOn), the
// automation in the staged lanes (autoCount < 0: none). update != 0
// hot-swaps the pending or playing clip (atFrame and tag unused).
DR_EXPORT void dr_host_clip(int update, int bytes, int bars, double atFrame, int tag, int rhythmOn, int autoCount) {
    bytes = std::clamp(bytes, 0, wasm::kClipBytes);
    if (rhythmOn) readRhythm(clipRhythm);
    const DrumRhythm* r = rhythmOn ? &clipRhythm : nullptr;
    const AutoBank* automation = hosted.automation(autoCount);
    if (update) engine->hostClipUpdate(hosted.clip, bytes, bars, r, automation);
    else engine->hostClip(hosted.clip, bytes, bars, atFrame, tag, r, automation);
}
DR_EXPORT void dr_host_stop(double atFrame) { engine->hostClipStop(atFrame); }
DR_EXPORT double* dr_events_ptr() { return hosted.events; }
DR_EXPORT int dr_events_take() { return hosted.take(*engine); }

// ---- side-chain key input (KEY_CHANNELS mono channels) ----
DR_EXPORT int dr_key_channels() { return KEY_CHANNELS; }
DR_EXPORT float* dr_key_in_ptr(int c) { return keyIn[std::clamp(c, 0, KEY_CHANNELS - 1)]; }
DR_EXPORT int dr_wants_key() { return engine->wantsSidechainInput() ? 1 : 0; }

// ---- render: one quantum into the static bus buffers ----
DR_EXPORT float* dr_out_ptr(int bus, int ch) { return outs[std::clamp(bus, 0, DR_NBUSES - 1)][ch ? 1 : 0]; }
DR_EXPORT float* dr_kick_ptr() { return kickKey; }
// frame: the block's first context frame (the SQ-4 shared timebase).
DR_EXPORT void dr_render(int n, int withKey, double frame) {
    n = std::clamp(n, 0, kBlock);
    engine->hostSetFrame(frame);
    static const float* keyPtrs[KEY_CHANNELS];
    for (int c = 0; c < KEY_CHANNELS; ++c) keyPtrs[c] = keyIn[c];
    engine->setSidechainInput(withKey ? keyPtrs : nullptr);
    float* o[DR_NBUSES][2];
    for (int b = 0; b < DR_NBUSES; ++b) { o[b][0] = outs[b][0]; o[b][1] = outs[b][1]; }
    engine->render(o, n);
}

// ---- viz (read after dr_render): [a, b, env] ----
DR_EXPORT float dr_viz(int field) {
    return field == 0 ? engine->vizA : field == 1 ? engine->vizB : engine->vizEnv;
}

// ---- telemetry of one pad chain + its bus reverb ----
DR_EXPORT float* dr_telemetry_ptr() { return telemetry; }
DR_EXPORT int dr_telemetry_count() { return kTelemetryValues; }
// Copies a fresh snapshot and returns 1 when a meter published since the last call.
DR_EXPORT int dr_telemetry_take(int pad, int bus) {
    const FxTelemetry t = engine->fxTelemetry(pad, bus);
    const bool same = pad == telemetryPad && bus == telemetryBus;
    if (same && t.serial == telemetrySerial && t.reverbSerial == telemetryReverbSerial) return 0;
    telemetryPad = pad; telemetryBus = bus;
    telemetrySerial = t.serial; telemetryReverbSerial = t.reverbSerial;
    std::copy(t.values.begin(), t.values.end(), telemetry);
    return 1;
}

// ---- KIT MIX pad capture: post-FX mono per pad ----
DR_EXPORT float* dr_capture_ptr() { return captureBuf; }
DR_EXPORT void dr_capture(int on) {
    engine->setPadCapture(on != 0);
    for (int i = 0; i < DR_NPADS; ++i) captureCursor[i] = captureStart[i] = engine->padCapture(i).frames();
}
// Copies the next kCapture frames of a pad into captureBuf when that many are
// ready. Returns the frame count since capture began, or 0 when not ready.
DR_EXPORT double dr_capture_pull(int pad) {
    if (pad < 0 || pad >= DR_NPADS) return 0;
    const auto& tap = engine->padCapture(pad);
    if (tap.frames() - captureCursor[pad] < (uint64_t)kCapture) return 0;
    if (tap.pull(captureCursor[pad], captureBuf, nullptr, kCapture) != kCapture) return 0;
    return (double)(captureCursor[pad] - captureStart[pad]);
}
