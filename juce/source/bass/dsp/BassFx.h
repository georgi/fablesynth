// BL-1 master FX chain — C++ port of the Web Audio graph in
// src/bass/engine/bass-synth.ts buildFx()/applyAllFx():
// OTT -> leveling compressor -> drive -> chorus -> ping-pong delay -> reverb
// -> master gain -> DC block -> safety limiter. The limiter threshold is -6 dB,
// matching the web bass limiter. The convolution reverb (generated exponential-noise
// impulse) is approximated by the same Freeverb network, tuned by SIZE.
// JUCE-free.
#pragma once
#include "../../dsp/ParametricEq.h"

#include "BassParams.h"
#include "../../dsp/Fx.h"   // Smooth, Biquad, DelayLine, FvComb, FvAllpass

#include <array>

namespace fable {

class BassFx {
public:
    FxTelemetry telemetry() const { return meter_.read(); }
    void prepare(double sampleRate);
    void setParams(const BassParamArray& p); // reads BL_FX* and BL_MASTER_VOLUME
    void process(float* L, float* R, int n); // in-place, stereo
    void reset();
    // Jump every ramped coefficient to its target (prepare / patch load), so a
    // fresh start is not a 15 ms glide up from silence.
    void snapRamps();
    int  latencySamples() const { return kDriveLatency + lim_.latencySamples(); }

private:
    ParametricEq eq_;
    FxMeter meter_;
    double sr_ = 48000;

    // Finding J1: AMT / chorus RATE+DEPTH / reverb SIZE reach the FX as block
    // values, so their derived coefficients used to jump once per host block.
    // They now ramp over ~15 ms in kCoefChunk-sample steps, the same scheme
    // Fx uses for its EQ/drive/reverb coefficients. (Mix, feedback, delay time
    // and master gain were already per-sample Smooths.)
    static constexpr int kCoefChunk = 32;
    int chunkPos_ = 0;
    ChunkRamp driveAmtR_, chRateR_, chDepthR_, verbSizeR_;
    void updateCoefs(bool force);

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
    float  chRate_ = 0.6f, chDepth_ = 0.3f;
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

    // Web chain extension: OTT -> leveling compressor precedes the existing
    // drive/chorus/delay/reverb insert. Legacy BL compressor makeup is ignored
    // by the web implementation and remains serialized only.
    OttCompressor ott_;
    WebCompressor comp_;
    PeakGuard headroomInput_, headroomOtt_, headroomComp_, headroomDrive_;
    PeakGuard headroomChorus_, headroomDelay_, headroomReverb_, delayFeedbackGuard_;
    bool compOff_ = true, compGated_ = false;

    // master + limiter
    Smooth masterGain_;
    Biquad dcL_, dcR_;
    LookaheadLimiter lim_; // WebAudio-spec makeup applied inside, computed in prepare()
};

} // namespace fable
