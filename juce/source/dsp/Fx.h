// FX chain — C++ port of the Web Audio graph in src/engine/synth.ts:
// EQ -> OTT -> leveling compressor -> drive -> chorus -> tape echo -> reverb ->
// master gain -> DC block -> safety limiter. The web app builds this from native
// WebAudio nodes; here each stage is reimplemented as pure DSP so it runs inside
// the plugin and the headless test harness alike.
//
// The leveling compressor is a shared dynamics stage (WebAudio
// DynamicsCompressor semantics, ratio 4 / knee 9 dB / attack 10 ms /
// release 200 ms) with THRESH/ON params. The serialized MAKEUP field remains
// for compatibility while the web chain uses automatic gain matching.
//
// The convolution reverb (generated exponential-noise impulse) is approximated
// by a Freeverb-style network tuned by SIZE — a standard, real-time-safe stand-in
// that gives an equivalent diffuse tail without partitioned FFT convolution.
#pragma once

#include "Params.h"
#include "FxTelemetry.h"
#include <array>
#include <cmath>
#include <vector>

namespace fable {

// One-pole smoother toward a target (setTargetAtTime equivalent).
struct Smooth {
    float cur = 0, target = 0, coef = 0.01f;
    void  setTime(double tau, double sr) { coef = (float)(1.0 - std::exp(-1.0 / (tau * sr))); }
    inline float next() { cur += (target - cur) * coef; return cur; }
    inline float nextN(int n) {
        if (n > 0) cur += (target - cur) * (1.0f - std::pow(1.0f - coef, (float)n));
        return cur;
    }
    void  snap(float v) { cur = target = v; }
};

// Chunk-rate parameter ramp. setParams() only sets targets; Fx::process()
// advances one step per Fx::kCoefChunk samples and rebuilds the coefficients
// that depend on it there. An automated EQ/drive/reverb parameter therefore
// glides over ~15 ms in small steps instead of jumping once per host block
// (audio-engine review, finding J1). Steps are linear; smooth a frequency by
// ramping its log2 and exponentiating.
struct ChunkRamp {
    void setSteps(int s) { steps = s > 1 ? s : 1; }
    void setTarget(float t) {
        if (t == target) return;
        target = t; step = (t - cur) / (float)steps; left = steps;
    }
    // advances one chunk; returns true while the value is still moving, so the
    // caller only pays for a coefficient rebuild during the ramp
    inline bool next() {
        if (left <= 0) return false;
        cur = (--left == 0) ? target : cur + step;
        return true;
    }
    void snap(float v) { cur = target = v; left = 0; step = 0; }
    void snapToTarget() { cur = target; left = 0; step = 0; }
    float cur = 0, target = 0, step = 0;
    int   left = 0, steps = 8;
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    void   lowpass(double freq, double q, double sr);
    void   highpass(double freq, double q, double sr);
    void   lowShelf(double freq, double gainDb, double sr, double q = 0.7071067811865476);
    void   highShelf(double freq, double gainDb, double sr, double q = 0.7071067811865476);
    void   peaking(double freq, double q, double gainDb, double sr);
    inline double process(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

// Shared web FX primitives. These are deliberately small, allocation-free
// state machines so WT-1, BL-1 and DR-1 can use the same dynamics behavior.
class AutoGain {
public:
    explicit AutoGain(double sampleRate = 48000.0) { prepare(sampleRate); }
    void prepare(double sampleRate);
    void reset();
    double next(double inL, double inR, double wetL, double wetR);
    double gain = 1.0;
private:
    double energyCoef_ = 0, gainCoef_ = 0;
    double input_ = 0, wet_ = 0, target_ = 1;
    int tick_ = 0;
};

class PeakGuard {
public:
    explicit PeakGuard(double sampleRate = 48000.0) { prepare(sampleRate); }
    void prepare(double sampleRate);
    void reset() { gain = 1.0; }
    double gainFor(double l, double r);
    void process(float* L, float* R, int n);
    double gain = 1.0;
    static constexpr double kCeiling = 0.8912509381337456; // -1 dBFS
private:
    double release_ = 0;
};

// Shared 4:1 / 9 dB knee dynamics stage. The web version uses automatic gain
// matching and retains the serialized makeup field for compatibility only.
class WebCompressor {
public:
    explicit WebCompressor(double sampleRate = 48000.0) { prepare(sampleRate); }
    void prepare(double sampleRate);
    void setParams(bool on, float thresholdDb);
    void reset();
    void process(float* L, float* R, int n);
    void processSample(double inL, double inR, double& outL, double& outR);
    bool wetTarget = false;
    double env = 0, gain = 1;
    AutoGain autoGain;
private:
    double sr_ = 48000, smooth_ = 0, attack_ = 0, release_ = 0;
    double wet_ = 0, threshold_ = -16, thresholdTarget_ = -16;
    double gainStep_ = 0;
    int tick_ = 0;
};

// Three-band OTT-style dynamics stage shared by all instrument worklets.
class OttCompressor {
public:
    explicit OttCompressor(double sampleRate = 48000.0) { prepare(sampleRate); }
    void prepare(double sampleRate);
    void setParams(bool on, float depth, float time, float up, float down);
    void reset();
    void process(float* L, float* R, int n);
    void processSample(double inL, double inR, double& outL, double& outR);
    double depthTarget = 0, depth = 0;
    std::array<double, 3> env{{0, 0, 0}};
    std::array<double, 3> gain{{1, 1, 1}};
    AutoGain autoGain;
private:
    double sr_ = 48000, lowCoef_ = 0, highCoef_ = 0, smooth_ = 0;
    std::array<double, 4> split_{{0, 0, 0, 0}};
    std::array<double, 3> target_{{1, 1, 1}};
    std::array<double, 3> bandsL_{{0, 0, 0}}, bandsR_{{0, 0, 0}};
    std::array<double, 3> attack_{{0, 0, 0}}, release_{{0, 0, 0}};
    double up_ = 1, down_ = 1, time_ = 0;
    int tick_ = 0;
};

// Fractional-read delay line.
class DelayLine {
public:
    void  prepare(int maxSamples) { buf_.assign((size_t)maxSamples, 0.0f); w_ = 0; }
    void  reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); w_ = 0; }
    inline void write(float x) { buf_[(size_t)w_] = x; if (++w_ >= (int)buf_.size()) w_ = 0; }
    inline float read(double delaySamples) const {
        int sz = (int)buf_.size();
        double rd = w_ - delaySamples;
        while (rd < 0) rd += sz;
        int i0 = (int)rd; double frac = rd - i0;
        int i1 = i0 + 1 < sz ? i0 + 1 : 0;
        return buf_[(size_t)i0] + (float)frac * (buf_[(size_t)i1] - buf_[(size_t)i0]);
    }
    // 4-point Catmull-Rom read for modulated/fractional delays (chorus, echo);
    // integer-delay users (reverb combs, dry alignment) keep read().
    inline float readHermite(double delaySamples) const {
        int sz = (int)buf_.size();
        double rd = w_ - delaySamples;
        while (rd < 0) rd += sz;
        int i1 = (int)rd; float t = (float)(rd - i1);
        int i0 = i1 > 0 ? i1 - 1 : sz - 1;
        int i2 = i1 + 1 < sz ? i1 + 1 : 0;
        int i3 = i2 + 1 < sz ? i2 + 1 : 0;
        float y0 = buf_[(size_t)i0], y1 = buf_[(size_t)i1];
        float y2 = buf_[(size_t)i2], y3 = buf_[(size_t)i3];
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
private:
    std::vector<float> buf_;
    int w_ = 0;
};

// Odd-length Kaiser-windowed half-band FIR (cutoff = rate/4). Two per direction
// give the drive shaper a 4x oversampled path with >60 dB alias rejection in
// the audible region; taps are rate-relative so the design is sample-rate
// independent. Designed once in prepare(), no audio-thread allocation.
//
// Half-band structure: with an odd tap count every tap whose offset from the
// centre is even is exactly zero (design() forces those to 0), and the centre
// tap is 0.5. process() therefore walks only the non-zero taps, and the
// polyphase entry points below additionally skip the multiplies that the
// zero-stuffed interpolator input and the discarded decimation phase waste.
// Measured on the 47/17-tap pair used here: 86 MACs per base sample through
// the 4x drive path instead of 324 (audio-engine review, finding J4).
//
// Verified against the direct form this replaces, driving the full 4x drive
// path (47+17 tap cascade, 48 kHz, float output): max abs error 0.0 on a
// 20 Hz -> 20 kHz swept sine at hard drive, 6.9e-44 on white noise and 1.8e-44
// on an impulse — i.e. equal to the float denormal floor, which is 38 decades
// tighter than the 1e-6 the finding asks for. The impulse peak stays on sample
// 27 and its energy centroid is exactly 27.0, so the group delay and hence
// kDriveLatency are unchanged.
//
// A given instance must be driven in one mode only: process(), or the
// interpolate()/decimate() pair. They keep separate histories.
struct HalfBandFir {
    void design(int taps, double beta);
    void reset() {
        std::fill(z.begin(), z.end(), 0.0); pos = 0;
        std::fill(hx_.begin(), hx_.end(), 0.0);
        std::fill(he_.begin(), he_.end(), 0.0);
        std::fill(ho_.begin(), ho_.end(), 0.0);
        px_ = pd_ = 0;
    }
    // Generic direct form at the filter's own rate; zero taps skipped.
    inline double process(double x) {
        z[(size_t)pos] = x;
        double acc = 0; const int n = (int)h.size();
        const size_t nz = nzTap_.size();
        for (size_t k = 0; k < nz; k++) {
            int idx = pos - nzOff_[k]; if (idx < 0) idx += n;
            acc += nzTap_[k] * z[(size_t)idx];
        }
        if (++pos >= n) pos = 0;
        return acc;
    }
    // 2x interpolate: one base-rate sample in, both upsampled samples out.
    // Equivalent to process(2*x) then process(0) on the direct form.
    inline void interpolate(double x, double& y0, double& y1) {
        if (--px_ < 0) px_ = np_ - 1;
        hx_[(size_t)px_] = x; hx_[(size_t)(px_ + np_)] = x;
        const double* b = hx_.data() + px_;
        double a0 = 0, a1 = 0;
        for (int j = peA_; j <= peB_; j++) a0 += pe_[(size_t)j] * b[j];
        for (int j = poA_; j <= poB_; j++) a1 += po_[(size_t)j] * b[j];
        y0 = 2.0 * a0; y1 = 2.0 * a1;
    }
    // 2x decimate: the two high-rate samples of one output period in, the
    // output at the kept (even) phase out. Equivalent to process(x0) followed
    // by process(x1) keeping the first result.
    inline double decimate(double x0, double x1) {
        if (--pd_ < 0) pd_ = np_ - 1;
        he_[(size_t)pd_] = x0; he_[(size_t)(pd_ + np_)] = x0;
        ho_[(size_t)pd_] = x1; ho_[(size_t)(pd_ + np_)] = x1;
        const double* be = he_.data() + pd_;
        const double* bo = ho_.data() + pd_;
        double acc = 0;
        for (int j = peA_; j <= peB_; j++) acc += pe_[(size_t)j] * be[j];
        for (int j = poA_; j <= poB_; j++) acc += po_[(size_t)j] * bo[j + 1];
        return acc;
    }
    std::vector<double> h, z; int pos = 0;

private:
    // compact non-zero taps for process(): value plus its delay offset
    std::vector<double> nzTap_; std::vector<int> nzOff_;
    // polyphase decomposition: pe_[j] = h[2j], po_[j] = h[2j+1], with [A,B]
    // the inclusive non-zero index range of each phase
    std::vector<double> pe_, po_;
    int peA_ = 0, peB_ = -1, poA_ = 0, poB_ = -1;
    // per-phase histories, mirror-written so the taps read a contiguous window
    std::vector<double> hx_, he_, ho_;
    int np_ = 1, px_ = 0, pd_ = 0;
};

// 4x drive oversampler stages: 47-tap first half-band (2x), 17-tap second (4x).
// Total up+shape+down group delay is an exact integer in base samples.
constexpr int kHB1Taps = 47, kHB2Taps = 17;
constexpr int kDriveLatency = (kHB1Taps - 1) / 2 + (kHB2Taps - 1) / 4; // 27

// Lookahead brickwall limiter: fixed makeup gain feeding a delayed signal path,
// linked-stereo sliding-window-minimum gain that fully develops inside the
// ~1.5 ms lookahead, ~200 ms release, hard -1 dBFS sample-peak ceiling.
class LookaheadLimiter {
public:
    void prepare(double sampleRate, double makeup);
    void reset();
    int  latencySamples() const { return la_; }
    inline void process(float& l, float& r) {
        double xl = l * makeup_, xr = r * makeup_;
        double pk = std::max(std::abs(xl), std::abs(xr));
        double g = pk > kCeiling ? kCeiling / pk : 1.0;
        // monotonic ring queue: minimum required gain over the last la_+1 samples
        while (qh_ != qt_ && qv_[(size_t)prevQ(qt_)] >= g) qt_ = prevQ(qt_);
        qv_[(size_t)qt_] = g; qi_[(size_t)qt_] = t_; qt_ = nextQ(qt_);
        if (qi_[(size_t)qh_] < t_ - (long long)la_) qh_ = nextQ(qh_);
        double wmin = qv_[(size_t)qh_];
        env_ += (wmin - env_) * (wmin < env_ ? atk_ : rel_);
        float dl = dlL_[(size_t)w_], dr = dlR_[(size_t)w_];
        dlL_[(size_t)w_] = (float)xl; dlR_[(size_t)w_] = (float)xr;
        if (++w_ >= la_) w_ = 0;
        ++t_;
        double gg = env_;
        double pd = std::max(std::abs((double)dl), std::abs((double)dr));
        if (gg * pd > kCeiling) gg = kCeiling / pd; // catch smoothing residue
        l = (float)(dl * gg); r = (float)(dr * gg);
    }
    static constexpr double kCeiling = 0.8912509381337456; // -1 dBFS
private:
    inline int nextQ(int i) const { return i + 1 < qcap_ ? i + 1 : 0; }
    inline int prevQ(int i) const { return i > 0 ? i - 1 : qcap_ - 1; }
    std::vector<float> dlL_, dlR_;
    std::vector<double> qv_; std::vector<long long> qi_;
    int la_ = 72, w_ = 0, qcap_ = 74, qh_ = 0, qt_ = 0;
    long long t_ = 0;
    double env_ = 1, atk_ = 0, rel_ = 0, makeup_ = 1;
};

// Freeverb building blocks.
struct FvComb {
    std::vector<float> buf; int idx = 0; float filt = 0, damp1 = 0.2f, damp2 = 0.8f, feedback = 0.84f;
    void prepare(int n) { buf.assign((size_t)n, 0.0f); idx = 0; filt = 0; }
    void reset() { std::fill(buf.begin(), buf.end(), 0.0f); filt = 0; }
    inline float process(float in) {
        float out = buf[(size_t)idx];
        filt = out * damp2 + filt * damp1;
        buf[(size_t)idx] = in + filt * feedback;
        if (++idx >= (int)buf.size()) idx = 0;
        return out;
    }
};
struct FvAllpass {
    std::vector<float> buf; int idx = 0; float feedback = 0.5f;
    void prepare(int n) { buf.assign((size_t)n, 0.0f); idx = 0; }
    void reset() { std::fill(buf.begin(), buf.end(), 0.0f); }
    inline float process(float in) {
        float bufout = buf[(size_t)idx];
        float out = -in + bufout;
        buf[(size_t)idx] = in + bufout * feedback;
        if (++idx >= (int)buf.size()) idx = 0;
        return out;
    }
};

class Fx {
public:
    FxTelemetry telemetry() const { return meter_.read(); }
    void prepare(double sampleRate);
    void setParams(const ParamArray& p, double tempoBpm = 0.0); // reads fx.* and master.volume
    void process(float* L, float* R, int n);
    void reset();
    int  latencySamples() const { return kDriveLatency + lim_.latencySamples(); }

    // Coefficient-update granularity. process() runs the sample loop in chunks
    // of this length and advances the ChunkRamps between them, so the EQ,
    // drive and reverb coefficients follow an automated parameter at 1.5 kHz
    // (48 kHz / 32) rather than at the host block rate.
    static constexpr int kCoefChunk = 32;

private:
    FxMeter meter_;
    double sr_ = 48000;
    double tempoBpm_ = 120.0;

    // Chunk-rate targets for everything whose coefficients are expensive to
    // rebuild. setParams() feeds these; advanceCoefs() consumes them.
    ChunkRamp eqLoDb_, eqMidDb_, eqHiDb_, eqMidF2_; // eqMidF2_ is log2(Hz)
    ChunkRamp driveAmt_, chRateT_, chDepthT_, verbSize_;
    float eqMidHz_ = 900; // exact target, used once the log ramp comes to rest
    int  rampSteps_ = 8;  // nominal ~15 ms, widened to span a long host block
    void setRampSteps(int steps);
    bool primed_ = false;    // first setParams after prepare() lands instantly
    bool forceCoefs_ = true; // rebuild everything on the next chunk boundary
    void advanceCoefs(bool force);

    // Four-band parametric EQ (first FX). The original gain ids keep their
    // established ramps; the appended frequency/Q/type/bypass ids are
    // applied at the same coefficient boundary.
    Biquad eqLoL_, eqLoR_, eqMidL_, eqMidR_, eqMid2L_, eqMid2R_, eqHiL_, eqHiR_;
    float eqLoFreq_ = 120, eqMid2Freq_ = 2500, eqHiFreq_ = 6000;
    float eqMid2Db_ = 0, eqLoQ_ = 0.70710678f, eqMidQ_ = 0.9f;
    float eqMid2Q_ = 0.9f, eqHiQ_ = 0.70710678f;
    int eqLoType_ = 0, eqMidType_ = 1, eqMid2Type_ = 1, eqHiType_ = 2;
    bool eqLoOn_ = true, eqMidOn_ = true, eqMid2On_ = true, eqHiOn_ = true;
    bool eqExtDirty_ = true;

    // drive
    float driveK_ = 1, drivePre_ = 1, driveNorm_ = 1.0f;
    Smooth driveWet_, driveDry_;
    HalfBandFir up1L_, up2L_, dn2L_, dn1L_, up1R_, up2R_, dn2R_, dn1R_; // 4x oversampler
    DelayLine dryL_, dryR_; // constant-latency dry path aligned with the shaper FIRs
    // true while the wet gain is zero (stage OFF or MIX 0): the 4x shaper is
    // skipped entirely and the dry delay carries the signal
    bool driveSilent_ = false;
    inline float shape(float x) const;
    float driveChannel(HalfBandFir& u1, HalfBandFir& u2, HalfBandFir& d2, HalfBandFir& d1, double x);

    // chorus
    double chPhase_ = 0;
    float  chRate_ = 0.6f, chDepth_ = 0.5f;
    Smooth chWet_, chDry_;
    DelayLine chDl1_, chDl2_;
    bool chorusOff_ = false, chorusGated_ = false;

    // Tape echo. The original delay ids remain the base time/feedback/mix;
    // appended controls add tone loss, saturation, drift, width, mode and BPM
    // sync while retaining the same delay buffer and latency contract.
    Smooth dlTime_, dlFb_, dlWet_, dlDry_;
    DelayLine dlL_, dlR_;
    Biquad dlDamp_, dlDampR_, dlHpL_, dlHpR_;
    Smooth dlSat_, dlWow_, dlFlutter_, dlWidth_, dlMode_;
    float dlTone_ = 4500, dlToneTarget_ = 4500;
    double tapeClock_ = 0, dlDriftL_ = 0, dlDriftR_ = 0;
    bool delayInitialized_ = false;
    bool delayOff_ = false, delayGated_ = false;

    // reverb
    std::array<FvComb, 8> combL_, combR_;
    std::array<FvAllpass, 4> apL_, apR_;
    Smooth verbWet_, verbDry_;
    float roomSize_ = 0.84f;
    bool verbOff_ = false, verbGated_ = false;

    // shared web dynamics: OTT -> leveling compressor
    OttCompressor ott_;
    WebCompressor comp_;
    PeakGuard headroomInput_, headroomEq_, headroomOtt_, headroomComp_;
    PeakGuard headroomDrive_, headroomChorus_, headroomDelay_, headroomReverb_;
    PeakGuard delayFeedbackGuard_;
    bool compOff_ = false, compGated_ = false;

    // master + limiter
    Smooth masterGain_;
    Biquad dcL_, dcR_;
    LookaheadLimiter lim_; // WebAudio-spec makeup applied inside, computed in prepare()
};

} // namespace fable
