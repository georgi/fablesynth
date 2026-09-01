// One DR-1 pad FX chain — C++ port of the Web Audio graph in
// src/drum/engine/drum-synth.ts buildFx()/applyAllFx():
// drive -> comp -> chorus -> ping-pong delay -> reverb. Same topology as WT-1's
// Fx (source/dsp/Fx.h, the template for every shared stage) plus the bus
// compressor, which follows
// WebAudio DynamicsCompressorNode semantics (ratio 4, knee 9 dB, attack 3 ms,
// release 250 ms, spec-defined implicit makeup) with THRESH/MAKEUP params.
//
// The convolution reverb (generated exponential-noise impulse) is approximated
// by the same Freeverb network as WT-1, tuned by SIZE. JUCE-free.
//
// Finding D1: master gain, DC block and the safety limiter are NOT part of this
// chain. They live in DrumBusOut (below) and run once on each summed output bus
// — the web graph is `16 pads -> sum -> master gain -> DC -> limiter`
// (drum-synth.ts:204-218, 296), so limiting sixteen pads independently left the
// bus itself without a ceiling.
#pragma once

#include "DrumParams.h"
#include "../../dsp/Fx.h"   // Smooth, Biquad, DelayLine, FvComb, FvAllpass

#include <array>

namespace fable {

class DrumFx {
public:
    void prepare(double sampleRate);
    void setParams(const DrumParamArray& p, int pad); // reads pad<i>.fx.*
    void process(float* L, float* R, int n); // in-place, before pad output routing
    void reset();
    int  latencySamples() const { return kDriveLatency; }
    // Finding D8: true while the whole chain is bypassed because its input AND
    // its own output have been below kIdleLevel for kIdleHold. Read by the
    // tests (and DrumEngine::activeFxChains) to prove the gate engages.
    bool isIdle() const { return idle_; }

private:
    double sr_ = 48000;

    // Finding D8: sixteen chains used to run every sample whether or not there
    // was anything to process — 16 x (Freeverb's 24 delays + a compressor doing
    // pow+log10 per sample + two Hermite delays + the 4x oversampled drive).
    // The chain is gated only once BOTH its input and its own output have sat
    // below kIdleLevel (-100 dBFS) for kIdleHold, so a multi-second reverb tail
    // is never truncated: the criterion is the measured output level, not
    // whether the pad was triggered. Gating freezes the recursive state instead
    // of clearing it, so re-engaging cannot step by more than kIdleLevel.
    static constexpr float  kIdleLevel = 1.0e-5f;
    static constexpr double kIdleHold  = 0.25;
    bool   idle_ = false;
    double idleSilent_ = 0;      // samples since input+output were last audible

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
    // Finding D8: the static curve (pow + log10) is evaluated once per
    // kCompUpdate samples and the resulting gain is ramped linearly across that
    // window. The envelope follower still runs per sample, so peak detection is
    // unchanged; only the curve lookup is decimated, which the 3 ms attack
    // already smooths over.
    static constexpr int kCompUpdate = 32;
    double compG_ = 1, compGStep_ = 0;
    int    compGCount_ = 0;

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
};

// Per-bus output stage (Finding D1): master gain -> DC block -> lookahead
// safety limiter, run once on the summed bus instead of once per pad. One
// instance per DR_NBUSES output, so MAIN has a real -1 dBFS ceiling.
class DrumBusOut {
public:
    void prepare(double sampleRate);
    void setParams(const DrumParamArray& p);   // shared master.volume only
    void process(float* L, float* R, int n);   // in-place
    void reset();
    int  latencySamples() const { return lim_.latencySamples(); }

private:
    double sr_ = 48000;
    Smooth masterGain_;
    Biquad dcL_, dcR_;
    LookaheadLimiter lim_; // WebAudio-spec makeup applied inside, computed in prepare()
};

} // namespace fable
