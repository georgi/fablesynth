// FX chain — C++ port of the Web Audio graph in src/engine/synth.ts:
// drive -> chorus -> ping-pong delay -> reverb -> leveling compressor ->
// master gain -> DC block -> safety limiter. The web app builds this from native
// WebAudio nodes; here each stage is reimplemented as pure DSP so it runs inside
// the plugin and the headless test harness alike.
//
// The leveling compressor is the last FX (WebAudio DynamicsCompressor semantics,
// ratio 4 / knee 9 dB / attack 10 ms / release 200 ms) with THRESH/MAKEUP/ON
// params — defaults ON to bring patches to roughly the same loudness. Same
// static-curve math as DR-1's bus compressor (source/drum/dsp/DrumFx).
//
// The convolution reverb (generated exponential-noise impulse) is approximated
// by a Freeverb-style network tuned by SIZE — a standard, real-time-safe stand-in
// that gives an equivalent diffuse tail without partitioned FFT convolution.
#pragma once

#include "Params.h"
#include <array>
#include <cmath>
#include <vector>

namespace fable {

// One-pole smoother toward a target (setTargetAtTime equivalent).
struct Smooth {
    float cur = 0, target = 0, coef = 0.01f;
    void  setTime(double tau, double sr) { coef = (float)(1.0 - std::exp(-1.0 / (tau * sr))); }
    inline float next() { cur += (target - cur) * coef; return cur; }
    void  snap(float v) { cur = target = v; }
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    void   lowpass(double freq, double q, double sr);
    void   highpass(double freq, double q, double sr);
    void   lowShelf(double freq, double gainDb, double sr);
    void   highShelf(double freq, double gainDb, double sr);
    void   peaking(double freq, double q, double gainDb, double sr);
    inline double process(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
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
struct HalfBandFir {
    void design(int taps, double beta);
    void reset() { std::fill(z.begin(), z.end(), 0.0); pos = 0; }
    inline double process(double x) {
        z[(size_t)pos] = x;
        double acc = 0; int idx = pos; const int n = (int)h.size();
        for (int i = 0; i < n; i++) { acc += h[(size_t)i] * z[(size_t)idx]; if (--idx < 0) idx = n - 1; }
        if (++pos >= n) pos = 0;
        return acc;
    }
    std::vector<double> h, z; int pos = 0;
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
    void prepare(double sampleRate);
    void setParams(const ParamArray& p); // reads fx.* and master.volume
    void process(float* L, float* R, int n);
    void reset();
    int  latencySamples() const { return kDriveLatency + lim_.latencySamples(); }

private:
    double sr_ = 48000;

    // 3-band tone EQ (first FX): fixed-corner low/high shelves + sweepable mid
    // bell. Coefficients recomputed in setParams; 0 dB gain is exact bypass.
    Biquad eqLoL_, eqLoR_, eqMidL_, eqMidR_, eqHiL_, eqHiR_;

    // drive
    float driveK_ = 1, drivePre_ = 1, driveNorm_ = 1.0f;
    Smooth driveWet_, driveDry_;
    HalfBandFir up1L_, up2L_, dn2L_, dn1L_, up1R_, up2R_, dn2R_, dn1R_; // 4x oversampler
    DelayLine dryL_, dryR_; // constant-latency dry path aligned with the shaper FIRs
    bool driveOff_ = false, driveGated_ = false;
    inline float shape(float x) const;
    float driveChannel(HalfBandFir& u1, HalfBandFir& u2, HalfBandFir& d2, HalfBandFir& d1, double x);

    // chorus
    double chPhase_ = 0;
    float  chRate_ = 0.6f, chDepth_ = 0.5f;
    Smooth chWet_, chDry_;
    DelayLine chDl1_, chDl2_;
    bool chorusOff_ = false, chorusGated_ = false;

    // delay
    Smooth dlTime_, dlFb_, dlWet_, dlDry_;
    DelayLine dlL_, dlR_;
    Biquad dlDamp_;
    bool delayOff_ = false, delayGated_ = false;

    // reverb
    std::array<FvComb, 8> combL_, combR_;
    std::array<FvAllpass, 4> apL_, apR_;
    Smooth verbWet_, verbDry_;
    float roomSize_ = 0.84f;
    bool verbOff_ = false, verbGated_ = false;

    // leveling compressor (WebAudio DynamicsCompressor semantics), last FX
    Smooth compThrDb_, compMakeup_, compWet_, compDry_;
    double compEnv_ = 0, compAtk_ = 0, compRel_ = 0;
    bool compOff_ = false, compGated_ = false;

    // master + limiter
    Smooth masterGain_;
    Biquad dcL_, dcR_;
    LookaheadLimiter lim_; // WebAudio-spec makeup applied inside, computed in prepare()
};

} // namespace fable
