// FX chain — C++ port of the Web Audio graph in src/engine/synth.ts:
// drive -> chorus -> ping-pong delay -> reverb -> master gain -> DC block ->
// safety limiter. The web app builds this from native WebAudio nodes; here each
// stage is reimplemented as pure DSP so it runs inside the plugin and the
// headless test harness alike.
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
    void  prepare(int maxSamples) { buf_.assign(maxSamples, 0.0f); w_ = 0; }
    void  reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); w_ = 0; }
    inline void write(float x) { buf_[w_] = x; if (++w_ >= (int)buf_.size()) w_ = 0; }
    inline float read(double delaySamples) const {
        int sz = (int)buf_.size();
        double rd = w_ - delaySamples;
        while (rd < 0) rd += sz;
        int i0 = (int)rd; double frac = rd - i0;
        int i1 = i0 + 1 < sz ? i0 + 1 : 0;
        return buf_[i0] + (float)frac * (buf_[i1] - buf_[i0]);
    }
private:
    std::vector<float> buf_;
    int w_ = 0;
};

// Freeverb building blocks.
struct FvComb {
    std::vector<float> buf; int idx = 0; float filt = 0, damp1 = 0.2f, damp2 = 0.8f, feedback = 0.84f;
    void prepare(int n) { buf.assign(n, 0.0f); idx = 0; filt = 0; }
    inline float process(float in) {
        float out = buf[idx];
        filt = out * damp2 + filt * damp1;
        buf[idx] = in + filt * feedback;
        if (++idx >= (int)buf.size()) idx = 0;
        return out;
    }
};
struct FvAllpass {
    std::vector<float> buf; int idx = 0; float feedback = 0.5f;
    void prepare(int n) { buf.assign(n, 0.0f); idx = 0; }
    inline float process(float in) {
        float bufout = buf[idx];
        float out = -in + bufout;
        buf[idx] = in + bufout * feedback;
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

private:
    double sr_ = 48000;

    // drive
    float driveK_ = 1, drivePre_ = 1;
    Smooth driveWet_, driveDry_;
    Biquad upL_, upR_, downL_, downR_; // 2x oversampling filters
    inline float shape(float x) const;

    // chorus
    double chPhase_ = 0;
    float  chRate_ = 0.6f, chDepth_ = 0.5f;
    Smooth chWet_, chDry_;
    DelayLine chDl1_, chDl2_;

    // delay
    Smooth dlTime_, dlFb_, dlWet_, dlDry_;
    DelayLine dlL_, dlR_;
    Biquad dlDamp_;

    // reverb
    std::array<FvComb, 8> combL_, combR_;
    std::array<FvAllpass, 4> apL_, apR_;
    Smooth verbWet_, verbDry_;
    float roomSize_ = 0.84f;

    // master + limiter
    Smooth masterGain_;
    Biquad dcL_, dcR_;
    double limEnv_ = 0;
    double limAtk_ = 0, limRel_ = 0;
    double limMakeup_ = 1; // WebAudio-spec makeup gain, computed in prepare()
};

} // namespace fable
