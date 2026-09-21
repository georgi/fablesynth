// One DR-1 FX chain — used independently by each pad and once after each
// routed drum-bus sum for the global group strip. C++ port of the Web Audio
// graph in src/drum/engine/drum-synth.ts buildFx()/applyAllFx():
// OTT -> leveling compressor -> drive -> chorus -> ping-pong delay -> reverb
// send when used as a pad insert. Group instances use their direct reverb,
// then the bus applies master gain, DC block and the lookahead limiter.
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
#include "../../dsp/ParametricEq.h"

#include "DrumParams.h"
#include "../../dsp/Fx.h"   // Smooth, Biquad, DelayLine, FvComb, FvAllpass

#include <array>
#include <algorithm>

namespace fable {

class DrumFx {
public:
    FxTelemetry telemetry() const { return meter_.read(); }
    void prepare(double sampleRate);
    void setParams(const DrumParamArray& p, int pad); // reads pad<i>.fx.*
    void setGroupParams(const DrumParamArray& p);     // reads post-mix fx.*
    void process(float* L, float* R, int n); // in-place, before pad output routing
    // Insert-only form used by DrumEngine: returns the equal-power reverb send
    // while leaving reverb processing to the shared per-bus network.
    void processInsert(float* L, float* R, float* sendL, float* sendR, int n);
    void reset();
    int  latencySamples() const { return kDriveLatency; }
    // Finding D8: true while the whole chain is bypassed because its input AND
    // its own output have been below kIdleLevel for kIdleHold. Read by the
    // tests (and DrumEngine::activeFxChains) to prove the gate engages.
    bool isIdle() const { return idle_; }
    float reverbSize() const { return verbSize_; }
    float reverbSendWeight() const { return verbWet_.target; }

private:
    ParametricEq eq_;
    FxMeter meter_;
    void setParamsAt(const DrumParamArray& p, int base);
    void processImpl(float* L, float* R, float* sendL, float* sendR, int n);
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
    DriveColor driveColorL_, driveColorR_;
    float driveChannel(HalfBandFir& u1, HalfBandFir& u2, HalfBandFir& d2, HalfBandFir& d1, double x, DriveColor& color);

    // OTT plus leveling compressor (shared web dynamics primitives)
    OttCompressor ott_;
    WebCompressor comp_;
    PeakGuard headroomInput_, headroomOtt_, headroomComp_, headroomDrive_;
    PeakGuard headroomChorus_, headroomDelay_, headroomReverb_, delayFeedbackGuard_;
    bool compOff_ = true;
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
    float verbSize_ = 0.4f;
    float roomSize_ = 0.84f;
    bool verbOff_ = false, verbGated_ = false;
};

// One shared Freeverb network per drum output bus, matching the web worklet's
// post-insert reverb send topology.
class DrumReverb {
public:
    FxTelemetry telemetry() const { return meter_.read(); }
    void prepare(double sampleRate);
    void setSize(float size) { size_.target = std::max(0.0f, std::min(1.0f, size)); }
    void process(float* inL, float* inR, float* outL, float* outR, int n);
    void reset();
    bool isActive() const { return !gated_; }

private:
    FxMeter meter_;
    double sr_ = 48000;
    PeakGuard inputGuard_;
    Smooth size_;
    std::array<FvComb, 8> combL_, combR_;
    std::array<FvAllpass, 4> apL_, apR_;
    float feedback_ = 0.812f, damp1_ = 0.32f, damp2_ = 0.68f;
    bool gated_ = true;
    double silent_ = 0;
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
    PeakGuard inputGuard_;
    Smooth masterGain_;
    Biquad dcL_, dcR_;
    LookaheadLimiter lim_; // WebAudio-spec makeup applied inside, computed in prepare()
};

} // namespace fable
