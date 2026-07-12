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

#include <cstdint>
#include <mutex>
#include <vector>

namespace fable {

constexpr int    BL_MAXUNI      = 7;
constexpr float  BL_ACCENT_VEL  = 1.0f, BL_PLAIN_VEL = 0.72f;
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
};

class BassEngine {
public:
    void prepare(double sampleRate);
    void setTables(std::vector<TablePtr> tables);
    void setParam(int id, float v) { p_[(size_t)id] = v; }
    void setParams(const BassParamArray& p) { p_ = p; }
    BassParamArray& params() { return p_; }

    // ---- voice control (worklet onMsg 'noteon'/'noteoff'/'panic') ----
    void keyOn(int semi, float vel);   // audition when stopped; legato = slide
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
    void setHostClipMode(bool on) {
        hostClipMode_ = on;
        // Reserve the clip host's buffers so no launch/update/tick allocates on
        // the audio thread (4096 = SQ_MAX_BARS * DR1 bytes-per-bar covers every
        // machine; 64 events is far above the per-block worst case).
        if (on) clipHost_.prepare(SQ_MAX_BARS * 256, 64);
        else clipHost_.clear();
    }
    void hostTempo(double bpm, double swing, double anchorFrame) {
        setBpmOverride(bpm);
        anchorFrame_ = anchorFrame;
        clipHost_.setTempo(effectiveBpm(), swing, sr_, anchorFrame);
    }
    void hostClip(const uint8_t* data, int bytes, int bars, double atFrame) {
        clipHost_.scheduleClip(data, (size_t)bytes, bars, atFrame);
    }
    void hostClipStop(double atFrame) { clipHost_.scheduleStop(atFrame); }
    void hostClipUpdate(const uint8_t* data, int bytes, int bars) {
        clipHost_.updateClip(data, (size_t)bytes, bars);
    }
    void hostSetFrame(double blockStartFrame) { hostFrame_ = blockStartFrame; } // SQ-4 processor calls before render() each block
    int  takeHostEvents(HostEvent* out, int max) {
        if (max <= 0 || out == nullptr) return 0;
        int n = std::min((int)clipHost_.events.size(), max);
        std::copy(clipHost_.events.begin(), clipHost_.events.begin() + n, out);
        clipHost_.events.clear();
        return n;
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
    bool setupOsc(double noteAbs);
    void renderOsc(float* tmpL, float* tmpR, int off, int n);
    void renderSub(float* tmpL, float* tmpR, int off, int n, double noteRootAbs);
    double lfoValue(double beats);
    void setupFilter(double noteAbs, double beats);
    void runFilter(const float* inL, const float* inR,
                   float* outL, float* outR, double drive, int n);
    void renderVoice(float* L, float* R, int off, int n, double beats);

    BassParamArray p_ = defaultBassParams();
    double sr_ = 48000;
    Rng    rng_;                   // LFO S&H (deterministic tests)

    // sequencer state
    std::vector<uint8_t> pats_ = std::vector<uint8_t>(BL_PATTERN_BYTES, 0);
    std::vector<int> chain_ { 0 };
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
    double subPhase_ = 0;

    // filter state
    double svf_[8] = {0};
    double cutSm_ = 0, curCut_ = 0;
    double satXL_ = 0, satXR_ = 0;
    int    ftype_ = 1; bool twoPole_ = true;
    double a1_ = 0, a2_ = 0, a3_ = 0, k1_ = 0;
    double fenvVal_ = 0;
    double shVal_ = 0; long shPhase_ = -1;
    double dcxL_ = 0, dcxR_ = 0, dcyL_ = 0, dcyR_ = 0;

    std::vector<BassTable> tables_;
    // Guards tables_ exactly like DrumEngine: setTables builds the replacement
    // off-lock and swaps under it; render try-locks and emits one block of
    // silence on the rare collision.
    std::mutex tablesMutex_;

    // per-block scratch (worklet process quantum)
    float tmpL_[128] = {0}, tmpR_[128] = {0};
    float fL_[128] = {0}, fR_[128] = {0};
};

} // namespace fable
