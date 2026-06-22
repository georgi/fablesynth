// FableSynth DSP core — C++ port of src/engine/worklet.js.
// 8-voice polyphonic wavetable engine: 2 morphing oscillators (unison), sub +
// noise, dual per-voice filter (SVF / comb / vowel) with ADAA drive, 2 ADSR
// envelopes, 2 LFOs, a 16-slot mod matrix, glide and voice stealing.
//
// JUCE-independent on purpose: the engine is a plain C++ object so it can be
// driven by the plugin AND exercised by a headless test harness.
#pragma once

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
constexpr int MAXUNI   = 7;
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
    double processBlock(int n) { for (int i = 0; i < n; i++) process(); return level; }
private:
    double a_ = -1, d_ = -1, r_ = -1; // last-set values (coef cache key)
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
    float  gl[MAXUNI]     = {0};
    float  gr[MAXUNI]     = {0};
    int    uni = 1;
    int    off0 = 0, off1 = 0, off0b = 0, off1b = 0;
    double mipBlend = 0, ft = 0, gain = 0;
    int    mask = 0, size = 0;
    const float* data = nullptr;
    double posSm = -1;
};

class Voice {
public:
    int    note = 60; double vel = 1; bool gate = false; long age = 0;
    double pitch = 60, velGain = 0;
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
    void   kill()    { gate = false; ampEnv.kill(); modEnv.kill(); }
};

// Engine table view — mirrors the worklet's {frames,mips,size,mask,data}.
struct EngineTable {
    int frames = 0, mips = 0, size = 0, mask = 0;
    std::vector<float> data;
};

class Engine {
public:
    void prepare(double sampleRate);
    void setTables(const std::vector<GeneratedTable>& tables);
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
    void panic() { for (auto& v : voices_) v.kill(); }

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

    ParamArray p_ = defaultParams();
    std::vector<EngineTable> tables_;
    // Guards tables_ so the message thread can swap in new tables (user import /
    // delete / preset load) while the audio thread renders. setTables builds the
    // replacement off-lock and only holds the lock for the O(1) swap; render
    // try-locks and emits one block of silence on the rare collision.
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
