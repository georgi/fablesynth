// BL-1 acid bass voice engine — C++ port of src/bass/engine/worklet-bass.js.
// One mono last-note-priority voice: wavetable oscillator (band-limited mip
// playback with crossfade, unison + stereo spread), sine/polyblep-square sub,
// SVF filter with ADAA drive, filter AD env with accent boost, amp ADSR,
// slide (one-pole glide), bar-locked LFO -> cutoff, and the sample-accurate
// 16-step pitch sequencer (patterns/chain/swing/accent/slide ties).
//
// JUCE-independent on purpose (same discipline as Engine.h/DrumEngine.h):
// plain C++ so it is driven by the plugin AND exercised by the headless test
// harness. Host transport lock mirrors DrumEngine::setHostTransport.
#pragma once

#include "BassParams.h"
#include "../../dsp/ClipHost.h"
#include "../../dsp/Engine.h"      // fable::Rng + TablePtr (via Wavetables.h)

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace fable {

constexpr int    BL_MAXUNI      = 7;
constexpr float  BL_ACCENT_VEL  = 1.0f, BL_PLAIN_VEL = 0.72f;
// Finding B8: incoming MIDI at or above this velocity takes the accent path
// (the louder gain + brighter, shorter filter env the sequencer already has) —
// the mapping every 303 clone uses. Below it, velocity is plain level.
constexpr float  BL_MIDI_ACCENT_VEL = 0.8f;
constexpr double BL_GATE_FRAC   = 0.55;  // non-tied gates close at this step fraction
constexpr double BL_SWING_MAX   = 0.667;
constexpr double BL_DC_R        = 0.9998;
// Filter env sweep span (octaves at flt.env = +/-100%) and LFO span (octaves
// at depth = 100%). Accent multiplies the env peak and shortens its decay.
constexpr double BL_FENV_OCT    = 5;
constexpr double BL_LFO_OCT     = 2;
constexpr double BL_ACC_GAIN    = 0.7;
constexpr double BL_ACC_DEC_SHORTEN = 0.35;
constexpr int    BL_KEYTRACK_REF = 60;

// Finding J1: host automation arrives once per host block. Interpolating it
// ACROSS the block is what a block-rate value actually means, so each
// continuous parameter ramps from its value at render() entry to the target,
// evaluated once per <=128-sample chunk. The engine's existing intra-chunk
// ramps then interpolate the rest, and the parameter an automated knob feeds
// the DSP is continuous instead of a staircase at the block rate. Same scheme
// (and the same cap) as Engine::beginParamRamp, so a very long host block does
// not turn an automation move into a slow glide.
constexpr double BL_PARAM_RAMP_MAX_SEC = 0.050;
// Finding J2: discrete switches (filter type, table, sub shape/octave) are
// equal-power crossfaded over this window instead of taking effect instantly.
constexpr double BL_SWITCH_XFADE = 0.003; // 3 ms — the DR-1 filter-switch value

// Finding B3: the resonance taper (identical formula to WT-1's Engine.cpp, so
// the two JUCE filters and their two web twins stay one filter). "LP24" used
// to cascade two SVF stages with the SAME k = 2 - 1.93*res, so the peak at fc
// was (1/k)^2 — two coincident resonances — and with k bottoming out at 0.071
// the pole Q reached only ~14, so the filter never rang. The resonance now
// lives in stage 1 alone and stage 2 stays critically damped:
//   resT = res + 0.0035*res^4
//   k1   = max(BL_LP24_KMIN, 0.5*(2 - 1.93*resT)^2),  k2 = 2
// 1/(k1*k2) reproduces the legacy (1/k)^2 magnitude at fc to within 0.03 dB up
// to res = 0.9, so every factory patch keeps its timbre, while the resonant
// stage's Q climbs from 14 to ~470 at the top of the knob. One-pole-pair types
// (LP12, BP, HP, notch) keep k = 2 - 1.93*res unchanged. res is clamped to
// 0.999, matching WT-1 and both web twins: at exactly 1.0, k1 sits on the
// max() floor, so the last sliver of travel would do nothing and the plugin
// would ring 0.53 dB hotter than the browser.
constexpr double BL_LP24_KMIN = 0.002;   // pole Q 500, the top of the travel
constexpr double BL_LP24_K2   = 2.0;     // critically damped second stage

// Engine table view — identical shape to DrumEngine's DrumTable: shares the
// source table's sample data (src keeps it alive).
struct BassTable {
    int frames = 0, mips = 0, size = 0, mask = 0;
    const float* data = nullptr;
    TablePtr src;
};

// One unpacked sequencer step (seq.ts getStep).
struct BassStep {
    bool on = false, acc = false, slide = false;
    int  semi = 0;                 // note 0..11 + 12*oct, offset from BL_ROOT_MIDI
    int  duration = 1;             // 16th-note steps
};

class BassEngine {
public:
    void prepare(double sampleRate);

    // Finding J3: message-thread publication. Builds the new set, publishes a
    // raw pointer to it, and parks the outgoing set on a retire list that only
    // the message thread ever frees (collectRetiredTables). Never allocates
    // for, blocks, or frees anything on the audio thread.
    void setTables(std::vector<TablePtr> tables);
    // Message thread: free every retired set no render can still be reading.
    // Called from setTables and from the editor timer; safe to call anywhere
    // except the audio thread.
    void collectRetiredTables();
    size_t retiredTableSetCount() const { return retired_.size(); }

    // ---- parameters (Finding J1) ----
    // These write the BLOCK TARGETS. The DSP reads a smoothed copy that the
    // render loop advances once per <=128-sample chunk; snapParams() jumps the
    // smoothed copy to the targets (prepare, program load, state restore).
    void setParam(int id, float v) { target_[(size_t)id] = v; }
    void setParams(const BassParamArray& p) { target_ = p; }
    BassParamArray& params() { return target_; }
    void snapParams() { p_ = target_; }
    const BassParamArray& smoothedParams() const { return p_; }
    // Test hooks: turn the J1 smoother / the J2 switch crossfade off so a test
    // can measure the same render with and without the fix. Always on in the
    // plugin.
    void setParamSmoothing(bool on) { smoothParams_ = on; if (!on) snapParams(); }
    void setSwitchCrossfade(bool on) { switchXfade_ = on; }

    // Finding B3: res -> the SVF damping of both filter stages. Public so the
    // tests assert the shipping taper rather than a copy of it.
    static void resToK(double res, bool twoPole, double& k1, double& k2);

    // ---- voice control (worklet onMsg 'noteon'/'noteoff'/'panic') ----
    void keyOn(int semi, float vel, bool acc = false); // audition when stopped; legato = slide
    void keyOff(int semi);
    void panic();

    // ---- sequencer (worklet onMsg 'play'/'stop'/'pats'/'chain' + fireStep) ----
    void play();
    void stop();
    // worklet: `this.playing || this.clip` — a loaded hosted clip owns the
    // voice (audition gating, bar-locked LFO) exactly like the internal/
    // host-transport-locked sequencer does.
    bool isPlaying() const {
        return playing_ || hostPlaying_ || (hostClipMode_ && clipHost_.isPlaying());
    }
    void setPatterns(const uint8_t* data, int n);  // n must be BL_PATTERN_BYTES; copies
    void setChain(const int* list, int n);         // ignores empty; clamps entries + chainPos
    void setBpmOverride(double bpm);               // host tempo; <= 0 clears the override
    int  currentStep() const { return step_; }     // -1 when stopped
    int  currentPattern() const { return chain_[(size_t)chainPos_]; }
    int  chainLength() const { return chainLen_; }

    // ---- host transport lock (same contract as DrumEngine::setHostTransport):
    // while the host is rolling with a song position, absolute 16th k fires at
    //   p(k) = k*0.25 + (k odd ? swing*BL_SWING_MAX*0.25 : 0) ppq,
    // the pattern is chain[(k/16) % chain.size()], and k < 0 never fires.
    void setHostTransport(double ppq, double bpm, bool playing);

    // ---- SQ-4 hosted-clip mode (docs/sq4-clips.md §6, same contract as
    // Engine::setHostClipMode/hostTempo/...). While on, play()/stop() and the
    // host-transport-locked pattern firing are suppressed (hostClipMode_
    // guards render's internal-clock and host-transport branches) so the
    // standalone sequencer and the hosted clip can never double-fire the
    // voice; the standalone render path is otherwise byte-identical when
    // this is off.
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

    // Render n samples into stereo L/R (zero-filled first, voice accumulates).
    void render(float* L, float* R, int n);

    // viz (read by the processor after render, published as atomics) —
    // mirrors the worklet's 'viz' message fields.
    float vizPos = -1, vizEnv = 0, vizFenv = 0, vizCut = -1;
    bool  vizGate = false;
    int   vizSemi = -100;          // sounding note (offset from root), -100 = idle

    // Unpack step s of pattern pat from a packed pattern buffer (seq.ts getStep).
    static BassStep readStep(const uint8_t* pats, int pat, int s);

private:
    // ---- voice control (worklet noteOn/glideTo/release/kill) ----
    void noteOn(int semi, bool acc, float vel);
    void glideTo(int semi, bool acc);
    void release();
    void kill();

    // ---- sequencer ----
    void fireStep();
    double effectiveBpm() const;

    // host-lock helpers (DrumEngine scheme)
    double hostStepPpq(long k) const;
    void   hostResync();
    void   fireHostStep(long k);
    void   fireStepAt(int s, int pat, int patNext, double dur);  // shared step-fire body
    void   clipFireAt(int abs);   // hosted twin of fireStepAt; byte source is clipHost_'s clip

    // ---- render internals (worklet setupOsc/renderOsc/renderSub/
    //      lfoValue/setupFilter/runFilter/renderVoice) ----
    bool setupOsc(double noteAbs, int n);
    void renderOsc(float* tmpL, float* tmpR, int off, int n);
    void renderSub(float* tmpL, float* tmpR, int off, int n, double noteRootAbs);
    double lfoValue(double beats);
    void setupFilter(double noteAbs, double beats, int n);
    void runFilter(const float* inL, const float* inR,
                   float* outL, float* outR, double drive, int n);
    void renderVoice(float* L, float* R, int off, int n, double beats);

    // Finding J1/J2: advance the smoothed parameter copy by one chunk and arm
    // a crossfade for any discrete parameter that changed with it.
    void beginParamRamp(int n);
    void advanceParams(int n);
    void applyDiscreteParams();
    // Finding J2 helpers.
    struct OscSnap {
        double phases[BL_MAXUNI], pIncs[BL_MAXUNI];
        float  pGl[BL_MAXUNI], pGr[BL_MAXUNI];
        double pFt, posSm, subPhase, subIncPrev, semi;
        int    pOff0, pUni;
        bool   havePrev;
    };
    void saveOsc(OscSnap& s) const;
    void restoreOsc(const OscSnap& s);
    void oscPass(float* dstL, float* dstR, int n);   // the sub-block osc+sub loop
    // One SVF pass (drive output -> filter output) over `n` samples with its
    // own state block, so the old filter type can run beside the new one.
    void svfChain(float* bufL, float* bufR, int n, double c0c, double c1c,
                  int ftype, bool twoPole, double k1, double k2,
                  double* F, bool mono) const;

    BassParamArray target_ = defaultBassParams();   // host/UI block targets
    BassParamArray p_ = defaultBassParams();        // smoothed values the DSP reads
    BassParamArray ps_ = defaultBassParams();       // where this call's ramp starts
    int rampPos_ = 0, rampLen_ = 0;
    bool smoothParams_ = true, switchXfade_ = true;
    double sr_ = 48000;
    Rng    rng_;                   // LFO S&H (deterministic tests)

    // sequencer state
    std::vector<uint8_t> pats_ = std::vector<uint8_t>(BL_PATTERN_BYTES, 0);
    // Finding B6: the chain is a fixed array + count, not a vector — setChain
    // runs on the audio thread (BassProcessor::processBlock) and must never
    // allocate. A chain is at most BL_NPATTERNS bars by construction.
    std::array<int, BL_NPATTERNS> chain_ {{ 0 }};
    int    chainLen_ = 1;
    int    chainPos_ = 0;
    bool   playing_ = false;
    int    step_ = -1;
    double samplesToNext_ = 0;
    double samplesToGateOff_ = -1;
    double songPos_ = 0;           // samples since play, for the bar-locked LFO
    double bpmOverride_ = 0;       // > 0: host tempo wins over seq.bpm

    // ---- hosted-clip mode state (SQ-4) ----
    bool     hostClipMode_ = false;
    double   hostFrame_ = 0;
    double   anchorFrame_ = 0;     // shared timebase's beat zero (hostTempo)
    ClipHost clipHost_;

    // host transport lock state
    bool   hostPlaying_ = false;
    bool   hostSynced_  = false;
    double hostPpq_ = 0;
    double hostBpm_ = 120;
    double hostEndPpq_ = 0;
    long   hostNextK_ = 0;

    // ---- voice ----
    bool   gate_ = false;
    bool   acc_ = false;
    double vel_ = BL_PLAIN_VEL;
    double semi_ = 0;              // slid/current semitone offset from BL_ROOT_MIDI
    double semiTarget_ = 0;
    double fenvT_ = 1e9;           // samples since (non-slid) trigger
    int    ampStage_ = 0;          // 0 idle · 1 att · 2 dec/sus · 3 rel
    double ampLevel_ = 0;
    // Finding B6: reserved in prepare() so the MIDI path never allocates.
    std::vector<int> held_;        // keyboard stack, last = current

    // osc state
    double phases_[BL_MAXUNI] = {0};
    double incs_[BL_MAXUNI]   = {0};
    float  gl_[BL_MAXUNI]     = {0};
    float  gr_[BL_MAXUNI]     = {0};
    int    uni_ = 1;
    int    off0_ = 0, off1_ = 0, off0b_ = 0, off1b_ = 0;
    double mipBlend_ = 0, ft_ = 0, oscGain_ = 0;
    int    mask_ = 0, size_ = 0;
    const float* data_ = nullptr;
    double posSm_ = -1;
    double subPhase_ = 0, subIncPrev_ = -1;
    // Previous sub-block targets for the intra-block ramps (Finding 7).
    double pIncs_[BL_MAXUNI] = {0};
    float  pGl_[BL_MAXUNI] = {0}, pGr_[BL_MAXUNI] = {0};
    double pFt_ = 0;
    int    pOff0_ = -1, pUni_ = -1;
    bool   havePrev_ = false;

    // filter state
    double svf_[8] = {0};
    double cutSm_ = 0, curCut_ = 0;
    double cutTarget_ = 0, cutPrev_ = -1;   // chunk cutoff ramp (Finding 7)
    double satXL_ = 0, satXR_ = 0;
    int    ftype_ = 1; bool twoPole_ = true;
    double k1_ = 0, k2_ = 0;
    double fenvVal_ = 0;
    bool   mono_ = false, monoPrev_ = false;   // Finding B8: L == R fast path
    double gainPrev_ = -1;                     // Finding B2: accent gain ramp
    double shVal_ = 0; long shPhase_ = -1;
    double dcxL_ = 0, dcxR_ = 0, dcyL_ = 0, dcyR_ = 0;
    double dcR_ = BL_DC_R;                  // sr-derived DC pole (Finding 9)

    // ---- Finding J2: discrete-switch crossfades ----
    // Osc side (table / sub shape / sub octave): the chunk is rendered twice —
    // once with the new configuration from the live state, once with the old
    // configuration from oldOsc_ — and equal-power mixed. Filter side: the old
    // type keeps running on a copy of the SVF state (svfOld_) beside the new
    // one until the fade completes.
    int    xfLen_ = 144;                      // sr-derived in prepare()
    int    oscXfPos_ = 1 << 30;               // >= xfLen_ means "not fading"
    OscSnap oscOld_{};
    float  oldTbl_ = 0, oldSubShape_ = 0, oldSubOct_ = 0;
    int    fltXfPos_ = 1 << 30;
    double svfOld_[8] = {0};
    int    ftypeOld_ = 1; bool twoPoleOld_ = true;

    // Lock-free table publication (Finding J3) — identical scheme to
    // Engine::tablesPub_ (see Engine.h for the full rationale). The message
    // thread builds a complete immutable set and publishes a RAW pointer; the
    // audio thread loads it once per render() and uses it for the whole block.
    // The old free-function std::atomic_load on a shared_ptr was a hashed
    // spinlock in both libstdc++ and libc++, so the audio thread could block
    // behind a UI table swap, and the render's snapshot could drop the last
    // reference — a free inside the audio callback. Now the message thread
    // owns every set: a replaced one moves to retired_ tagged with the render
    // epoch, and collectRetiredTables() frees it once a LATER render has
    // started. Renders are strictly sequential, so a higher epoch proves the
    // render that could still hold the pointer has returned.
    using TableSet = std::vector<BassTable>;
    std::unique_ptr<const TableSet> live_ = std::make_unique<const TableSet>();
    std::atomic<const TableSet*> tablesPub_{live_.get()};
    std::atomic<uint64_t> renderEpoch_{0};
    struct RetiredSet { std::unique_ptr<const TableSet> set; uint64_t epoch = 0; };
    std::vector<RetiredSet> retired_;      // message thread only
    const TableSet* curTables_ = nullptr;  // render-call snapshot (audio thread only)

    // per-block scratch (worklet process quantum)
    float tmpL_[128] = {0}, tmpR_[128] = {0};
    float fL_[128] = {0}, fR_[128] = {0};
    float xL_[128] = {0}, xR_[128] = {0};     // J2: old osc configuration
    float fxL_[128] = {0}, fxR_[128] = {0};   // J2: old filter type
};

} // namespace fable
