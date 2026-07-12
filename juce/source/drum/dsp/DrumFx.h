// DR-1 master FX chain — C++ port of the Web Audio graph in
// src/drum/engine/drum-synth.ts buildFx()/applyAllFx():
// drive -> comp -> chorus -> ping-pong delay -> reverb -> master gain ->
// DC block -> safety limiter. Same topology as WT-1's Fx (source/dsp/Fx.h,
// the template for every shared stage) plus the bus compressor, which follows
// WebAudio DynamicsCompressorNode semantics (ratio 4, knee 9 dB, attack 3 ms,
// release 250 ms, spec-defined implicit makeup) with THRESH/MAKEUP params.
//
// The convolution reverb (generated exponential-noise impulse) is approximated
// by the same Freeverb network as WT-1, tuned by SIZE. JUCE-free.
#pragma once

#include "DrumParams.h"
#include "../../dsp/Fx.h"   // Smooth, Biquad, DelayLine, FvComb, FvAllpass

#include <array>

namespace fable {

class DrumFx {
public:
    void prepare(double sampleRate);
    void setParams(const DrumParamArray& p); // reads DG_FX* and DG_MASTER_VOLUME
    void process(float* L, float* R, int n); // in-place, MAIN bus only
    void reset();
    int  latencySamples() const { return kDriveLatency + lim_.latencySamples(); }

private:
    double sr_ = 48000;

    // drive
    float driveK_ = 1, drivePre_ = 1, driveNorm_ = 1.0f;
    Smooth driveWet_, driveDry_;
    HalfBandFir up1L_, up2L_, dn2L_, dn1L_, up1R_, up2R_, dn2R_, dn1R_; // 4x oversampler
    DelayLine dryL_, dryR_; // constant-latency dry path aligned with the shaper FIRs
    bool driveOff_ = false, driveGated_ = false;
    inline float shape(float x) const;
    float driveChannel(HalfBandFir& u1, HalfBandFir& u2, HalfBandFir& d2, HalfBandFir& d1, double x);

    // compressor (WebAudio DynamicsCompressorNode semantics)
    Smooth compThrDb_, compMakeup_, compWet_, compDry_;
    double compEnv_ = 0;
    double compAtk_ = 0, compRel_ = 0;
    bool compOff_ = false, compGated_ = false;

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

    // master + limiter
    Smooth masterGain_;
    Biquad dcL_, dcR_;
    LookaheadLimiter lim_; // WebAudio-spec makeup applied inside, computed in prepare()
};

} // namespace fable
