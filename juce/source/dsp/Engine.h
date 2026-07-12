// FableSynth DSP core — C++ port of src/engine/worklet.js.
// 8-voice polyphonic wavetable engine: 2 morphing oscillators (unison), sub +
// noise, dual per-voice filter (SVF / comb / vowel) with ADAA drive, 2 ADSR
// envelopes, 2 LFOs, a 16-slot mod matrix, glide and voice stealing.
//
// JUCE-independent on purpose: the engine is a plain C++ object so it can be
// driven by the plugin AND exercised by a headless test harness.
#pragma once

#include "ClipHost.h"
#include "NoteSeq.h"
#include "Params.h"
#include "Wavetables.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace fable {

constexpr int NVOICES  = 8;
constexpr int MAXUNI   = 16;
constexpr int COMB_MAX = 4096;

// Fast deterministic RNG (xorshift32) — replaces Math.random() for noise,
// start-phase randomisation and S&H. Deterministic => reproducible tests.
struct Rng {
    uint32_t s = 0x9e3779b9u;
    inline float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) * (1.0f / 16777216.0f); }
};

class Env {
public:
    int    state = 0;
    double level = 0, s = 0.8;
    double ca = 0.01, cd = 0.001, cr = 0.001;
    void   set(double a, double d, double sus, double r, double sr);
    void   trigger() { state = 1; }
    void   release() { if (state != 0) state = 4; }
    void   kill()    { state = 0; level = 0; }
    double process();
    // Block advance for block-rate consumers. Idle (0) and sustain (3) are
    // fixed points of process(), so they skip the loop — exact, not approximate.
    double processBlock(int n) {
        if (state == 0) return level;
        if (state == 3) { level = s; return level; }
        for (int i = 0; i < n; i++) process();
        return level;
    }
private:
    double a_ = -1, d_ = -1, r_ = -1, sr_ = -1; // last-set values (coef cache key)
};

class Lfo {
public:
    double phase = 0, hold = 0;
    long   elapsed = 0;                 // samples since reset (for rise/fade-in)
    Rng*   rng = nullptr;
    void   reset() { phase = 0; hold = rng->next() * 2 - 1; elapsed = 0; }
    double valueOff(int shape, double off) const;       // reads frac(phase + off)
    double riseGain(double riseSec, double sr) const {
        return riseSec <= 0 ? 1.0 : std::min(1.0, (double)elapsed / (riseSec * sr));
    }
    void   advance(double rate, int n, double sr);
};

struct FilterState {
    double svf[8]  = {0};   // 2 stages x 2 ch x (ic1, ic2)
    double fmt[12] = {0};   // formant: 2 ch x 3 bands x (s1, s2)
    std::array<float, COMB_MAX> combL{};
    std::array<float, COMB_MAX> combR{};
    int    combW = 0;
    double cutSm = 0;
    double satXL = 0, satXR = 0;       // ADAA drive: previous input per channel
    int    ftype = 0; bool twoPole = false;
    double a1 = 0, a2 = 0, a3 = 0, k1 = 0;
    double combLen = 1, combFb = 0;
    double fc[9]  = {0};               // formant biquad coefs: 3 bands x (b0,a1,a2)
    double famp[3] = {0};
    void   reset();
};

struct OscState {
    double phases[MAXUNI] = {0};
    double incs[MAXUNI]   = {0};
    double ratios[MAXUNI] = {0};
    float  gl[MAXUNI]     = {0};
    float  gr[MAXUNI]     = {0};
    int    uni = 1;
    int    off0 = 0, off1 = 0, off0b = 0, off1b = 0;
    double mipBlend = 0, ft = 0, gain = 0;
    double sumW2 = 0;
    int    cacheUni = -1;
    double cacheDet = 0, cacheSpr = 0, cacheBlend = 0, cachePan = 0;
    int    mask = 0, size = 0;
    const float* data = nullptr;
    double posSm = -1;
};

class Voice {
public:
    int    note = 60; double vel = 1; bool gate = false; long age = 0;
    double pitch = 60, velGain = 0;
    // Note queued behind a steal fade (Env state 5); fired by renderBlock once
    // the fading voice reaches silence. Mirrors the worklet's voice.pending.
    bool   hasPending = false;
    int    pendNote = 60; double pendVel = 1, pendStart = 60;
    Env    ampEnv, modEnv;
    Lfo    lfo1, lfo2;
    OscState oA, oB;
    double subPhase = 0;
    double pb[7] = {0};                // pink noise filter state
    FilterState f1, f2;
    double dcxL = 0, dcxR = 0, dcyL = 0, dcyR = 0;

    bool   active() const { return ampEnv.state != 0; }
    void   noteOn(int n, double v, double startPitch, long a, Rng& rng);
    void   noteOff() { gate = false; ampEnv.release(); modEnv.release(); }
    void   kill()    { gate = false; hasPending = false; ampEnv.kill(); modEnv.kill(); }
};

// Engine table view — mirrors the worklet's {frames,mips,size,mask,data}.
// Shares the source table's sample data (src keeps it alive): swapping the
// pool moves pointers, never the multi-MB float pyramids.
struct EngineTable {
    int frames = 0, mips = 0, size = 0, mask = 0;
    const float* data = nullptr; // = src->data.data(); nullptr marks an empty slot
    TablePtr src;
};

class Engine {
public:
    void prepare(double sampleRate);
    void setTables(std::vector<TablePtr> tables);
    void setParam(int id, float v) { p_[id] = v; }
    void setParams(const ParamArray& p) { p_ = p; }
    ParamArray& params() { return p_; }

    void noteOn(int note, double vel);
    void noteOff(int note);
    void pitchBend(double semis) { bend_ = semis; }
    // Clamp to a sane musical range; non-finite or <=1 falls back to 120. The
    // upper bound keeps a degenerate host tempo from making the per-block LFO
    // phase step exceed a cycle.
    void setBpm(double b) { bpm_ = (std::isfinite(b) && b > 1.0) ? std::min(b, 1000.0) : 120.0; }
    // Host transport for synced-LFO phase locking: ppq = quarter notes since the
    // song origin, playing = host transport running. When playing, a synced
    // free-run LFO derives its phase from ppq so it lines up with the downbeat.
    // ppq is sanitised so a non-finite host position can't latch a NaN phase.
    void setTransport(double ppq, bool playing) { ppq_ = std::isfinite(ppq) ? ppq : 0.0; playing_ = playing; }
    void panic() { for (auto& v : voices_) v.kill(); seqNote_ = -1; seqToGateOff_ = -1; }

    // ---- note sequencer (port of worklet.js seqRead/seqGateOff/seqTie/seqFire).
    // 16 steps x 4 chained patterns firing noteOn/noteOff into the polyphonic
    // voice allocator; a tie retunes the sounding voice legato (no envelope
    // retrigger — MASTER_GLIDE decides snap vs slide); accents fire velocity
    // SEQ_ACCENT_VEL vs SEQ_PLAIN_VEL so VELO mod routes respond; the gate
    // closes at SEQ_GATE of the step unless the next step ties in.
    void seqPlay();                                 // worklet 'play' (yields to a rolling host)
    void seqStop();                                 // worklet 'stop'
    bool seqIsPlaying() const { return seqPlaying_ || seqHostPlaying_; }
    void setSeqPatterns(const uint8_t* data, int n); // n must be SEQ_PATTERN_BYTES; copies
    void setSeqChain(const int* list, int n);        // ignores empty; clamps entries + chainPos
    void setBpmOverride(double bpm);                 // host tempo wins over SEQ_BPM; <= 0 clears
    int  seqCurrentStep() const { return seqStep_; } // -1 when stopped
    int  seqCurrentPattern() const { return seqChain_[(size_t)seqChainPos_]; }
    // The MIDI note the sequencer (standalone or hosted-clip) voice is
    // currently sounding, -1 when none — shared seqNote_/seqGateOff() state,
    // so this observes hosted-clip mode too (test/UI observability).
    int  seqCurrentNote() const { return seqNote_; }

    // Host transport lock (same contract as BassEngine::setHostTransport):
    // while the host is rolling with a song position, absolute 16th k fires at
    //   p(k) = k*0.25 + (k odd ? swing*SEQ_SWING_MAX*0.25 : 0) ppq,
    // the pattern is chain[(k/16) % chain.size()], and k < 0 never fires.
    void setSeqHostTransport(double ppq, double bpm, bool playing);

    // ---- SQ-4 hosted-clip mode (docs/sq4-clips.md §6) ----
    // While on, seqPlay()/seqStop() and host-transport seq firing are
    // suppressed (guarded by hostClipMode_) so the standalone sequencer and
    // the hosted clip can never double-fire the same voice slot; the
    // standalone render path is otherwise byte-identical when this is off.
    void setHostClipMode(bool on, int maxBlock = 0) {
        hostClipMode_ = on;
        // Reserve the clip host's buffers so no launch/update/tick allocates on
        // the audio thread (4096 = SQ_MAX_BARS * DR1 bytes-per-bar covers every
        // machine; the event headroom is sized to maxBlock — see hostMaxEvents).
        if (on) clipHost_.prepare(SQ_MAX_BARS * 256, hostMaxEvents(maxBlock));
        else clipHost_.clear();
    }
    void hostTempo(double bpm, double swing, double anchorFrame) {
        setBpm(bpm);
        clipHost_.setTempo(bpm_, swing, sr_, anchorFrame);
    }
    void hostClip(const uint8_t* data, int bytes, int bars, double atFrame, int tag = 0) {
        clipHost_.scheduleClip(data, (size_t)bytes, bars, atFrame, tag);
    }
    void hostClipStop(double atFrame) { clipHost_.scheduleStop(atFrame); }
    void hostClipUpdate(const uint8_t* data, int bytes, int bars) {
        clipHost_.updateClip(data, (size_t)bytes, bars);
    }
    void hostSetFrame(double blockStartFrame) { hostFrame_ = blockStartFrame; } // SQ-4 processor calls before render() each block
    // Lossless drain: copy up to `max` events, then erase ONLY the copied
    // prefix, keeping the remainder for the next call (Finding 3). The old
    // clear()-everything dropped Pos events past `max` when one prepared block
    // spanned more grid steps than the caller's buffer; the SeqProcessor now
    // loops takeHostEvents until it returns 0 so a burst can't leak. erase()
    // shrinks in place (no realloc), so this stays audio-thread-safe.
    int  takeHostEvents(HostEvent* out, int max) {
        if (max <= 0 || out == nullptr) return 0;
        int n = std::min((int)clipHost_.events.size(), max);
        std::copy(clipHost_.events.begin(), clipHost_.events.begin() + n, out);
        clipHost_.events.erase(clipHost_.events.begin(), clipHost_.events.begin() + n);
        return n;
    }
    // Test hook: the clip host's reserved event capacity, to assert the
    // worst-case sizing holds with no audio-thread realloc (Finding 3).
    size_t hostEventsCapacity() const { return clipHost_.eventsCapacity(); }

    // Worst-case host-event count for one prepared render block (Finding 3).
    // A single prepared chunk can span many grid steps in an offline render;
    // each step emits one Pos event, plus Start/Stop/entry headroom. The
    // shortest possible step is at the max bpm (200), so
    // ceil(maxBlock / minStepDur) + 8 bounds it. Diverges from the web's fixed
    // 64 deliberately: JUCE hands offline renders arbitrarily large blocks, and
    // a reserve() overflow here would realloc on the audio thread. maxBlock<=0
    // (the standalone/unit callers) keeps the old fixed 64.
    int hostMaxEvents(int maxBlock) const {
        if (maxBlock <= 0) return 64;
        const double minStepDur = sr_ * 60.0 / 200.0 / 4.0; // sqSamplesPerStep(200, sr_)
        const int n = (int)std::ceil((double)maxBlock / minStepDur) + 8;
        return std::max(64, n);
    }

    // One unpacked engine step: on/acc/tie + semitone offset from SEQ_ROOT
    // (worklet seqRead). Static so the harness asserts the unpack directly.
    struct SeqReadStep { bool on = false, acc = false, tie = false; int semi = 0; };
    static SeqReadStep readSeqStep(const uint8_t* pats, int pat, int s);

    // Render the summed (pre-FX) voice mix into L/R. Chunks internally to the
    // 128-sample block cadence so block-rate modulation matches the web engine
    // regardless of the host buffer size.
    void render(float* L, float* R, int n);

    // Live visualization feedback (modulated wt positions + active voice count).
    double vizA = -1, vizB = -1; int vizActive = 0;

private:
    bool setupOsc(OscState& o, int base, Voice& v, const double* pm, double mPitch, double mPan);
    void renderOsc(OscState& o, float* tmpL, float* tmpR, int n);
    void setupFilter(FilterState& fs, int base, Voice& v, double e2, double mCut, const double* pm);
    void runFilter(FilterState& fs, const float* inL, const float* inR,
                   float* outL, float* outR, double drive, int n);
    void renderVoice(Voice& v, float* L, float* R, int n);
    void renderBlock(float* L, float* R, int n, double ppqChunk); // n <= 128
    double lfoHz(int base) const;
    void updateGlobalLfo(Lfo& g, int base, double ppqChunk, int n);

    // ---- note sequencer internals ----
    void seqGateOff();                       // worklet seqGateOff
    void seqTie(int n, double vel);          // worklet seqTie (legato retune)
    void seqFire();                          // worklet seqFire (internal clock)
    void seqFireAt(int s, int pat, int patNext, double dur); // shared step-fire body
    void clipFireAt(int abs); // hosted twin of seqFireAt; byte source is clipHost_'s clip
    double seqEffectiveBpm() const;
    // host-lock helpers (BassEngine scheme)
    double seqHostStepPpq(long k) const;
    void   seqHostResync();
    void   seqFireHostStep(long k);

    ParamArray p_ = defaultParams();
    std::vector<EngineTable> tables_;
    // Guards tables_ so the message thread can swap in new tables (user import /
    // delete / preset load) while the audio thread renders. setTables builds the
    // replacement off-lock (pointer copies — sample data is shared, never
    // duplicated) and only holds the lock for the O(1) swap; render try-locks
    // and emits one block of silence on the rare collision.
    std::mutex tablesMutex_;
    std::array<Voice, NVOICES> voices_;
    double sr_ = 48000;
    double bend_ = 0;
    double lastPitch_ = 60;
    long   clock_ = 0;
    Rng    rng_;
    Lfo    gLfo1_, gLfo2_;             // free-running (retrig=0) global LFO phases
    double bpm_ = 120;
    double ppq_ = 0;                  // host transport position (quarter notes)
    bool   playing_ = false;          // host transport running

    // ---- note sequencer state (worklet fields) ----
    std::vector<uint8_t> seqPats_ = makeEmptySeqPatterns();
    std::vector<int> seqChain_ { 0 };
    int    seqChainPos_ = 0;
    bool   seqPlaying_ = false;       // internal clock running
    int    seqStep_ = -1;
    double seqToNext_ = 0;            // samples until the next step fires
    double seqToGateOff_ = -1;        // samples until the gate closes (-1 = none/tied)
    int    seqNote_ = -1;             // midi note the sequencer is sounding (-1 = none)
    double seqSongPos_ = 0;           // samples since play (virtual transport for synced LFOs)
    double bpmOverride_ = 0;          // > 0: host tempo wins over SEQ_BPM

    // ---- hosted-clip mode state (SQ-4) ----
    bool     hostClipMode_ = false;
    double   hostFrame_ = 0;
    ClipHost clipHost_;

    // host transport lock state (BassEngine scheme)
    bool   seqHostPlaying_ = false;
    bool   seqHostSynced_  = false;
    double seqHostPpq_ = 0;
    double seqHostBpm_ = 120;
    double seqHostEndPpq_ = 0;
    long   seqHostNextK_ = 0;

    // Per-voice modulated parameter snapshot: p_ with each route's offset folded in
    // (Lin/Log curve rules from the design contract). Reused per voice — no per-call
    // allocation. Reading pm_ for non-modulated fields is safe (pm_ == p_ there).
    double pm_[NUM_PARAMS];

    // per-block scratch (128)
    float tmpL_[128], tmpR_[128];
    float bL_[128], bR_[128];
    float f1L_[128], f1R_[128], f2L_[128], f2R_[128];
};

} // namespace fable
