// DR-1 drum voice engine — C++ port of src/drum/engine/worklet-drum.js.
// 16 one-shot pad voices: one wavetable oscillator plus a raw sample player,
// tilted noise, pitch env, one-shot AHD amp env,
// SVF filter with ADAA drive, 4-slot mod matrix, choke groups, per-pad
// level/pan/velocity and 5-bus (MAIN + AUX 1-4) output routing.
//
// JUCE-independent on purpose (same discipline as Engine.h): plain C++ so it
// is driven by the plugin AND exercised by the headless test harness.
// Includes the sample-accurate step sequencer (play/stop/patterns/chain/
// swing/bpm-override) ported from the worklet's process()/fireStep().
#pragma once

#include "DrumParams.h"
#include "DrumFx.h"
#include "../../dsp/ClipHost.h"
#include "../../dsp/Engine.h"       // fable::Rng + TablePtr (via Wavetables.h)

#include <cstdint>
#include <memory>
#include <vector>

namespace fable {

constexpr int    DR_MAXUNI     = 7;
constexpr int    DR_NBUSES     = 5;      // 0 = MAIN, 1..4 = AUX
constexpr float  DR_ACCENT_VEL = 1.0f, DR_PLAIN_VEL = 0.72f;
constexpr double DR_SWING_MAX  = 0.667;
constexpr double DR_CHOKE_FADE = 0.12;
constexpr double DR_DC_R       = 0.9998;
constexpr int    DR_MOD_LOG_D  = 5;      // CUTOFF mod: +/-5 octaves
constexpr int    DR_BASE_NOTE  = 60;

// Engine table view — mirrors the worklet's {frames,mips,size,mask,data}.
// Identical shape to Engine.h's EngineTable: shares the source table's
// sample data (src keeps it alive), so pool swaps move pointers, never
// the multi-MB float pyramids.
struct DrumTable {
    int frames = 0, mips = 0, size = 0, mask = 0;
    const float* data = nullptr;   // nullptr marks an empty slot
    TablePtr src;
};

class DrumEngine {
public:
    void prepare(double sampleRate);
    void enablePadFx(bool on) { padFxEnabled_ = on; }
    int latencySamples() const { return padFxEnabled_ ? padFx_[0].latencySamples() : 0; }
    void setTables(std::vector<TablePtr> tables);   // same mutex swap scheme as Engine::setTables
    void setParam(int id, float v) { p_[(size_t)id] = v; }
    void setParams(const DrumParamArray& p) { p_ = p; }
    DrumParamArray& params() { return p_; }

    void trigger(int pad, float vel);               // worklet trigger() incl. choke + phase reset
    void panic();
    void selectPad(int i);                          // viz target

    // ---- sequencer (worklet onMsg 'play'/'stop'/'pats'/'chain' + fireStep) ----
    void play();                                    // step=-1, chainPos=0, samplesToNext=0
    void stop();
    bool isPlaying() const { return playing_ || hostPlaying_; }
    void setPatterns(const uint8_t* data, int n);   // n must be 4*16*16; copies
    void setChain(const int* list, int n);          // ignores empty; clamps entries + chainPos
    void setBpmOverride(double bpm);                // host tempo; <= 0 clears the override
    int  currentStep() const { return step_; }      // -1 when stopped
    int  currentPattern() const { return chain_[(size_t)chainPos_]; }

    // ---- host transport lock (the standard JUCE AudioPlayHead behavior) ----
    // Call once per block, before render(). While playing, the sequencer is
    // slaved to song position: absolute 16th k fires at
    //   p(k) = k*0.25 + (k odd ? swing*DR_SWING_MAX*0.25 : 0) ppq,
    // the pattern is chain[(k/16) % chain.size()], and k < 0 (host pre-roll)
    // never fires. Jumps/loops resync from ppq. Host playing suppresses the
    // internal play()/stop() transport; host stop leaves the sequencer stopped.
    void setHostTransport(double ppq, double bpm, bool playing);

    // ---- SQ-4 hosted-clip mode (docs/sq4-clips.md §6, same contract as
    // Engine::setHostClipMode/hostTempo/...). While on, play()/stop() and the
    // host-transport-locked pattern firing are suppressed (hostClipMode_
    // guards render's internal-clock and host-transport branches) so the
    // standalone sequencer and the hosted clip can never double-fire pads;
    // the standalone render path is otherwise byte-identical when this is
    // off. Unlike WT-1/BL-1, DR-1 pads are one-shot fire-and-forget voices —
    // the worklet's hostTick/clipFire never gate or choke anything on stop
    // or swap (src/drum/engine/worklet-drum.js:173-235), so sounding pads
    // just ring out through both.
    void setHostClipMode(bool on, int maxBlock = 0) {
        hostClipMode_ = on;
        // Reserve the clip host's buffers so no launch/update/tick allocates on
        // the audio thread (4096 = SQ_MAX_BARS * DR1 bytes-per-bar covers every
        // machine; the event headroom is sized to maxBlock — see hostMaxEvents).
        if (on) clipHost_.prepare(SQ_MAX_BARS * 256, hostMaxEvents(maxBlock));
        else clipHost_.clear();
    }
    void hostTempo(double bpm, double swing, double anchorFrame) {
        setBpmOverride(bpm);
        anchorFrame_ = anchorFrame;
        clipHost_.setTempo(effectiveBpm(), swing, sr_, anchorFrame);
    }
    void hostClip(const uint8_t* data, int bytes, int bars, double atFrame, int tag = 0) {
        clipHost_.scheduleClip(data, (size_t)bytes, bars, atFrame, tag);
    }
    void hostClipStop(double atFrame) { clipHost_.scheduleStop(atFrame); }
    void hostClipUpdate(const uint8_t* data, int bytes, int bars) {
        clipHost_.updateClip(data, (size_t)bytes, bars);
    }
    void hostSetFrame(double blockStartFrame) { hostFrame_ = blockStartFrame; } // SQ-4 processor calls before render() each block
    // Lossless drain: copy up to `max`, erase only the copied prefix, keep the
    // rest for the next call (Finding 3 — the SeqProcessor loops until 0).
    int  takeHostEvents(HostEvent* out, int max) {
        if (max <= 0 || out == nullptr) return 0;
        int n = std::min((int)clipHost_.events.size(), max);
        std::copy(clipHost_.events.begin(), clipHost_.events.begin() + n, out);
        clipHost_.events.erase(clipHost_.events.begin(), clipHost_.events.begin() + n);
        return n;
    }
    size_t hostEventsCapacity() const { return clipHost_.eventsCapacity(); }
    // Worst-case host-event count for one prepared block (see Engine.h's
    // hostMaxEvents for the full rationale): ceil(maxBlock / minStepDur) + 8,
    // minStepDur at max bpm 200; maxBlock<=0 keeps the old fixed 64.
    int hostMaxEvents(int maxBlock) const {
        if (maxBlock <= 0) return 64;
        const double minStepDur = sr_ * 60.0 / 200.0 / 4.0;
        const int n = (int)std::ceil((double)maxBlock / minStepDur) + 8;
        return std::max(64, n);
    }

    // Render n samples into 5 stereo buses. outs[b][0]=L, outs[b][1]=R;
    // render() zero-fills all 10 buffers first, then pads accumulate into
    // the bus selected by their OUT param.
    void render(float* outs[DR_NBUSES][2], int n);

    // viz (read by the processor after render, published as atomics)
    float vizA = -1, vizB = -1, vizEnv = 0;
    // pads triggered since last consume (bit i = pad i) — UI LED flashes
    uint32_t consumeHits() { uint32_t h = hits_; hits_ = 0; return h; }

private:
    // ---- internals ported from worklet-drum.js (makeOscState / ----
    // ---- makeFilterState / PadVoice, js:28-73)                  ----
    struct OscState {
        double phases[DR_MAXUNI] = {0};
        double incs[DR_MAXUNI]   = {0};
        float  gl[DR_MAXUNI]     = {0};
        float  gr[DR_MAXUNI]     = {0};
        int    uni = 1;
        int    off0 = 0, off1 = 0, off0b = 0, off1b = 0;
        double mipBlend = 0, ft = 0, gain = 0;
        int    mask = 0, size = 0;
        const float* data = nullptr;
        double posSm = -1;
        // Previous sub-block targets for the intra-block ramps (Finding 7).
        double pIncs[DR_MAXUNI] = {0};
        float  pGl[DR_MAXUNI] = {0}, pGr[DR_MAXUNI] = {0};
        double pFt = 0;
        int    pOff0 = -1, pUni = -1;
        bool   havePrev = false;
    };
    struct FilterState {
        double svf[8] = {0};       // 2 stages x 2 ch x (ic1, ic2)
        double cutSm = 0;
        double cutTarget = 0, cutPrev = -1;  // chunk cutoff ramp (Finding 7)
        double satXL = 0, satXR = 0;   // ADAA drive: previous input per channel
        int    ftype = 0; bool twoPole = false;
        double k1 = 0;
    };
    struct SampleState {
        double pos = -1;
        int index = -1;
        bool done = false;
    };
    struct PadVoice {
        bool   active = false;
        double vel = 1, rand = 0;
        long   t = 0;              // samples since trigger
        double ampLevel = 0; bool choking = false;
        OscState oA;
        SampleState sample;
        FilterState f;
        double noiseY = 0;
        double ringPhase = 0.25;
        double dcxL = 0, dcxR = 0, dcyL = 0, dcyR = 0;
        double lgPrev = -1;        // level*vel gain ramp start (Finding 7)

        void trigger(double v, double rnd);
        void choke() { if (active) choking = true; }
        void kill()  { active = false; choking = false; ampLevel = 0; }
    };
    struct Mod {                   // padMod() destination accumulators
        double posA = 0, posB = 0, level = 0, cut = 0, pitch = 0;
        double fineA = 0, fineB = 0, noise = 0, res = 0;
    };

    Mod  padMod(int padI, const PadVoice& v) const;
    bool setupOsc(OscState& o, int base, double pitchEnv,
                  double mPos, double mFine, double mPitch, int n);
    static void renderOsc(OscState& o, float* tmpL, float* tmpR, int off, int n);
    void renderSample(SampleState& state, int padI, double pitchEnv,
                      double modStart, double modFine, double modPitch,
                      float* tmpL, float* tmpR, int off, int n) const;
    void setupFilter(FilterState& fs, int padI, double mCut, double mRes, int n);
    void runFilter(FilterState& fs, const float* inL, const float* inR,
                   float* outL, float* outR, double drive, int n) const;
    double ampEnv(const PadVoice& v, int padI, int i) const;
    void renderPad(PadVoice& v, int padI, float* L, float* R, int off, int n);
    void fireStep();               // js:493-518
    double effectiveBpm() const;   // DG_SEQ_BPM clamp, or bpmOverride_ when host-tempo'd

    // host-lock helpers
    double hostStepPpq(long k) const;   // p(k) with the current swing
    void   hostResync();                // smallest k >= 0 with p(k) >= hostPpq_
    void   fireHostStep(long k);        // trigger step k%16 of chain[(k/16) % len]

    // hosted-clip fire (docs/sq4-clips.md §6): byte source is clipHost_'s
    // live clip rather than pats_/chain_; no tie/lookahead state to carry
    // (each pad's val is independent, unlike BL-1/WT-1's slide chains).
    void clipFireAt(int abs);

    float param(int id) const { return p_[(size_t)id]; }

    // sequencer state (js:82-89)
    std::vector<uint8_t> pats_ =
        std::vector<uint8_t>(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
    std::vector<int> chain_ { 0 };
    int    chainPos_ = 0;
    bool   playing_ = false;
    int    step_ = -1;
    double samplesToNext_ = 0;
    double bpmOverride_ = 0;       // > 0: host tempo wins over DG_SEQ_BPM

    // host transport lock state
    bool   hostPlaying_ = false;
    bool   hostSynced_  = false;   // hostNextK_ valid for hostPpq_
    double hostPpq_ = 0;           // ppq at the start of the next render block
    double hostBpm_ = 120;
    double hostEndPpq_ = 0;        // expected block-end ppq (continuity check)
    long   hostNextK_ = 0;         // next absolute 16th to fire

    // ---- hosted-clip mode state (SQ-4) ----
    bool     hostClipMode_ = false;
    double   hostFrame_ = 0;
    double   anchorFrame_ = 0;     // shared timebase's beat zero (hostTempo)
    ClipHost clipHost_;

    DrumParamArray p_ = defaultDrumParams();
    uint32_t hits_ = 0;
    // Lock-free table publication (Finding 2) — same scheme as Engine::tables_:
    // setTables atomically publishes an immutable set; render() atomic_loads
    // one snapshot per call and keeps it alive for the whole block.
    using TableSet = std::vector<DrumTable>;
    std::shared_ptr<const TableSet> tables_ = std::make_shared<TableSet>();
    std::shared_ptr<const TableSet> retired_;
    const TableSet* curTables_ = nullptr;
    PadVoice voices_[DR_NPADS];
    double sr_ = 48000;
    double dcR_ = DR_DC_R;         // sr-derived DC pole (Finding 9)
    Rng    rng_;                   // trigger rand + noise (deterministic tests)
    int    sel_ = 0;

    // per-block scratch (worklet process quantum)
    float tmpL_[128] = {0}, tmpR_[128] = {0};
    float fL_[128] = {0}, fR_[128] = {0};
    float padL_[128] = {0}, padR_[128] = {0};
    std::array<DrumFx, DR_NPADS> padFx_;
    bool padFxEnabled_ = false; // pure engine tests/embedders opt in explicitly
};

} // namespace fable
