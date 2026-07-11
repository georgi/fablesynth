// BL-1 master FX chain — C++ port of the Web Audio graph in
// src/bass/engine/bass-synth.ts buildFx()/applyAllFx():
// drive -> chorus -> ping-pong delay -> reverb -> master gain -> DC block ->
// safety limiter. Same topology as DR-1's DrumFx minus the bus compressor
// ("no comp — accents live"); the limiter threshold is -6 dB, matching the
// web bass limiter. The convolution reverb (generated exponential-noise
// impulse) is approximated by the same Freeverb network, tuned by SIZE.
// JUCE-free.
#pragma once

#include "BassParams.h"
#include "../../dsp/Fx.h"   // Smooth, Biquad, DelayLine, FvComb, FvAllpass

#include <array>

namespace fable {

class BassFx {
public:
    void prepare(double sampleRate);
    void setParams(const BassParamArray& p); // reads BL_FX* and BL_MASTER_VOLUME
    void process(float* L, float* R, int n); // in-place, stereo
    void reset();

private:
    double sr_ = 48000;

    // drive
    float driveK_ = 1, drivePre_ = 1, driveNorm_ = 1.0f;
    Smooth driveWet_, driveDry_;
    Biquad upL_, upR_, downL_, downR_; // 2x oversampling filters
    bool driveOff_ = false, driveGated_ = false;
    inline float shape(float x) const;

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

    // master + limiter
    Smooth masterGain_;
    Biquad dcL_, dcR_;
    double limEnv_ = 0;
    double limAtk_ = 0, limRel_ = 0;
    double limMakeup_ = 1; // WebAudio-spec makeup gain, computed in prepare()
};

} // namespace fable
