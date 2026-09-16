// DR-1 FX chain. Stage code follows source/dsp/Fx.cpp (the WT-1 port of the
// same web topology); the lockstep reference for constants and ordering is
// src/drum/engine/drum-synth.ts:144-303.
#include "DrumFx.h"

#include <algorithm>
#include <cmath>

namespace fable {

static constexpr double PI = 3.14159265358979323846;

// Safety-limiter static curve: threshold -8 dB (~0.398), ratio 14 — identical
// to the web limiter (drum-synth.ts) and WT-1's Fx.cpp.
static constexpr double kLimThr = 0.398, kLimRatio = 14.0;

// ---------------- Freeverb tuning (classic constants, scaled to sr) ----------------
static const int COMB_TUNE[8]   = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int AP_TUNE[4]     = {556, 441, 341, 225};
static const int STEREO_SPREAD  = 23;

void DrumFx::prepare(double sampleRate) {
    meter_.prepare(sampleRate);
    eq_.prepare(sampleRate);
    sr_ = sampleRate;
    double scale = sr_ / 44100.0;

    for (size_t i = 0; i < 8; i++) {
        combL_[i].prepare((int)(COMB_TUNE[i] * scale));
        combR_[i].prepare((int)((COMB_TUNE[i] + STEREO_SPREAD) * scale));
    }
    for (size_t i = 0; i < 4; i++) {
        apL_[i].prepare((int)(AP_TUNE[i] * scale));
        apR_[i].prepare((int)((AP_TUNE[i] + STEREO_SPREAD) * scale));
        apL_[i].feedback = apR_[i].feedback = 0.5f;
    }

    chDl1_.prepare((int)(0.05 * sr_));
    chDl2_.prepare((int)(0.05 * sr_));
    dlL_.prepare((int)(2.0 * sr_) + 4);
    dlR_.prepare((int)(2.0 * sr_) + 4);

    driveWet_.setTime(0.02, sr_); driveDry_.setTime(0.02, sr_);
    chWet_.setTime(0.02, sr_); chDry_.setTime(0.02, sr_);
    dlTime_.setTime(0.08, sr_); dlFb_.setTime(0.02, sr_);
    dlWet_.setTime(0.02, sr_); dlDry_.setTime(0.02, sr_);
    verbWet_.setTime(0.02, sr_); verbDry_.setTime(0.02, sr_);
    driveDry_.snap(1); chDry_.snap(1); dlDry_.snap(1); verbDry_.snap(1);

    // 4x drive oversampler: cascaded Kaiser half-band FIR pairs (>60 dB
    // rejection in the audible-alias region), designed once here.
    up1L_.design(kHB1Taps, 6.0); up2L_.design(kHB2Taps, 6.0);
    dn2L_.design(kHB2Taps, 6.0); dn1L_.design(kHB1Taps, 6.0);
    up1R_.design(kHB1Taps, 6.0); up2R_.design(kHB2Taps, 6.0);
    dn2R_.design(kHB2Taps, 6.0); dn1R_.design(kHB1Taps, 6.0);
    dryL_.prepare(kDriveLatency + 4);
    dryR_.prepare(kDriveLatency + 4);
    dlDamp_.lowpass(4500, 0.707, sr_);

    ott_.prepare(sr_); comp_.prepare(sr_);
    headroomInput_.prepare(sr_); headroomOtt_.prepare(sr_); headroomComp_.prepare(sr_);
    headroomDrive_.prepare(sr_); headroomChorus_.prepare(sr_); headroomDelay_.prepare(sr_);
    headroomReverb_.prepare(sr_); delayFeedbackGuard_.prepare(sr_);

    reset(); // full state clear: re-prepare must never keep stale recursive state
}

void DrumFx::reset() {
    meter_.reset();
    eq_.reset();
    chDl1_.reset(); chDl2_.reset(); dlL_.reset(); dlR_.reset();
    dryL_.reset(); dryR_.reset();
    for (auto& c : combL_) c.reset();
    for (auto& c : combR_) c.reset();
    for (auto& a : apL_) a.reset();
    for (auto& a : apR_) a.reset();
    dlDamp_.reset();
    up1L_.reset(); up2L_.reset(); dn2L_.reset(); dn1L_.reset();
    up1R_.reset(); up2R_.reset(); dn2R_.reset(); dn1R_.reset();
    ott_.reset(); comp_.reset();
    headroomInput_.reset(); headroomOtt_.reset(); headroomComp_.reset(); headroomDrive_.reset();
    headroomChorus_.reset(); headroomDelay_.reset(); headroomReverb_.reset(); delayFeedbackGuard_.reset();
    idle_ = false; idleSilent_ = 0;
    chPhase_ = 0;
    driveGated_ = chorusGated_ = delayGated_ = verbGated_ = false;
    // settle smoothers at their targets so no stale ramp survives a re-prepare
    driveWet_.snap(driveWet_.target); driveDry_.snap(driveDry_.target);
    chWet_.snap(chWet_.target); chDry_.snap(chDry_.target);
    dlTime_.snap(dlTime_.target); dlFb_.snap(dlFb_.target);
    dlWet_.snap(dlWet_.target); dlDry_.snap(dlDry_.target);
    verbWet_.snap(verbWet_.target); verbDry_.snap(verbDry_.target);
}

static inline float mixGate(bool on, float amount, bool wet) {
    if (wet) return on ? (float)std::sin(amount * PI / 2) : 0.0f;
    return on ? (float)std::cos(amount * PI / 2) : 1.0f;
}

void DrumFx::setParams(const DrumParamArray& p, int pad) {
    const int b = dpid(std::max(0, std::min(DR_NPADS - 1, pad)), 0);
    eq_.setParams(p.data() + b + DP_FXEQ_ON);
    // drive
    float amt = p[(size_t)(b + DP_FXDRIVE_AMT)];
    drivePre_ = 1 + amt * 2;
    driveK_ = 1 + amt * 12;
    driveNorm_ = 1.0f / (drivePre_ * std::tanh(driveK_));
    bool dOn = p[(size_t)(b + DP_FXDRIVE_ON)] > 0.5f;
    driveOff_ = !dOn;
    driveWet_.target = mixGate(dOn, p[(size_t)(b + DP_FXDRIVE_MIX)], true);
    driveDry_.target = mixGate(dOn, p[(size_t)(b + DP_FXDRIVE_MIX)], false);

    // Web dynamics. The legacy COMP/OTT gain fields are retained in the
    // parameter schema for portable state but automatic gain is used here.
    compOff_ = p[(size_t)(b + DP_FXCOMP_ON)] <= 0.5f;
    comp_.setParams(!compOff_, p[(size_t)(b + DP_FXCOMP_THR)]);
    ott_.setParams(p[(size_t)(b + DP_FXOTT_ON)] > 0.5f,
                   p[(size_t)(b + DP_FXOTT_DEPTH)], p[(size_t)(b + DP_FXOTT_TIME)],
                   p[(size_t)(b + DP_FXOTT_UP)], p[(size_t)(b + DP_FXOTT_DOWN)]);

    // chorus
    chRate_ = p[(size_t)(b + DP_FXCHORUS_RATE)];
    chDepth_ = p[(size_t)(b + DP_FXCHORUS_DEPTH)];
    bool cOn = p[(size_t)(b + DP_FXCHORUS_ON)] > 0.5f;
    chorusOff_ = !cOn;
    chWet_.target = mixGate(cOn, p[(size_t)(b + DP_FXCHORUS_MIX)] * 0.8f, true);
    chDry_.target = mixGate(cOn, p[(size_t)(b + DP_FXCHORUS_MIX)] * 0.8f, false);

    // delay
    dlTime_.target = p[(size_t)(b + DP_FXDELAY_TIME)];
    dlFb_.target = p[(size_t)(b + DP_FXDELAY_FB)];
    bool delOn = p[(size_t)(b + DP_FXDELAY_ON)] > 0.5f;
    delayOff_ = !delOn;
    dlWet_.target = mixGate(delOn, p[(size_t)(b + DP_FXDELAY_MIX)] * 0.85f, true);
    dlDry_.target = mixGate(delOn, p[(size_t)(b + DP_FXDELAY_MIX)] * 0.85f, false);

    // reverb — SIZE maps to roomsize/decay (longer & brighter tail with size)
    float size = p[(size_t)(b + DP_FXREVERB_SIZE)];
    verbSize_ = std::max(0.0f, std::min(1.0f, size));
    roomSize_ = 0.7f + size * 0.28f;
    float damp = 0.4f - size * 0.2f;
    for (size_t i = 0; i < 8; i++) {
        combL_[i].feedback = combR_[i].feedback = roomSize_;
        combL_[i].damp1 = combR_[i].damp1 = damp;
        combL_[i].damp2 = combR_[i].damp2 = 1 - damp;
    }
    bool rOn = p[(size_t)(b + DP_FXREVERB_ON)] > 0.5f;
    verbOff_ = !rOn;
    verbWet_.target = mixGate(rOn, p[(size_t)(b + DP_FXREVERB_MIX)] * 0.9f, true);
    verbDry_.target = mixGate(rOn, p[(size_t)(b + DP_FXREVERB_MIX)] * 0.9f, false);
}

float DrumFx::shape(float x) const {
    // tanh is bounded — no pre-clamp (a hard clamp is its own nonsmooth nonlinearity)
    return std::tanh(x * driveK_) * driveNorm_;
}

// One channel through the 4x oversampled shaper. Finding J4: the four
// half-bands run through the polyphase entry points instead of the direct
// form. interpolate() produces both upsampled samples from one base sample
// without the multiplies the zero-stuffed input wastes, and decimate() takes
// the two high-rate samples of an output period and returns the kept phase
// without computing the discarded one — 86 MACs per base sample instead of
// 324, for a bit-identical result (Fx.h documents the verification). Each
// filter here is driven in exactly one mode, which the API requires.
float DrumFx::driveChannel(HalfBandFir& u1, HalfBandFir& u2, HalfBandFir& d2, HalfBandFir& d1, double x) {
    double a0, a1;
    u1.interpolate(x, a0, a1);
    double b0, b1;
    u2.interpolate(a0, b0, b1);
    const double c0 = d2.decimate((double)shape((float)b0), (double)shape((float)b1));
    u2.interpolate(a1, b0, b1);
    const double c1 = d2.decimate((double)shape((float)b0), (double)shape((float)b1));
    return (float)d1.decimate(c0, c1);        // keeps the base-rate phase
}

void DrumFx::process(float* L, float* R, int n) {
    processImpl(L, R, nullptr, nullptr, n);
}

void DrumFx::processInsert(float* L, float* R, float* sendL, float* sendR, int n) {
    processImpl(L, R, sendL, sendR, n);
}

void DrumFx::processImpl(float* L, float* R, float* sendL, float* sendR, int n) {
    // Finding D8: chain-level activity gate. Peak the block's input first; while
    // the chain is idle a silent input means there is nothing for it to do, so
    // the entire chain (drive oversampler, compressor, chorus, delay, Freeverb)
    // is skipped and its state stays frozen. The output is already the input.
    float inPk = 0;
    for (int i = 0; i < n; i++)
        inPk = std::max(inPk, std::max(std::abs(L[i]), std::abs(R[i])));
    if (idle_) {
        if (inPk <= kIdleLevel) return;
        idle_ = false;
        idleSilent_ = 0;
    }

    // Gate only when OFF; mix==0 while ON must keep state accumulation alive.
    bool driveGate = driveOff_ && driveWet_.target == 0.0f && std::abs(driveWet_.cur) < 1.0e-6f;
    bool chorusGate = chorusOff_ && chWet_.target == 0.0f && std::abs(chWet_.cur) < 1.0e-6f;
    bool delayGate = delayOff_ && dlWet_.target == 0.0f && std::abs(dlWet_.cur) < 1.0e-6f;
    bool verbGate = verbOff_ && verbWet_.target == 0.0f && std::abs(verbWet_.cur) < 1.0e-6f;

    if (driveGate && !driveGated_) {
        driveWet_.snap(0); driveDry_.snap(1);
        up1L_.reset(); up2L_.reset(); dn2L_.reset(); dn1L_.reset();
        up1R_.reset(); up2R_.reset(); dn2R_.reset(); dn1R_.reset();
    }
    if (chorusGate && !chorusGated_) {
        chWet_.snap(0); chDry_.snap(1);
        chDl1_.reset(); chDl2_.reset();
    }
    if (delayGate && !delayGated_) {
        dlWet_.snap(0); dlDry_.snap(1);
        dlL_.reset(); dlR_.reset(); dlDamp_.reset();
    }
    if (verbGate && !verbGated_) {
        verbWet_.snap(0); verbDry_.snap(1);
        for (auto& c : combL_) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.filt = 0.0f; }
        for (auto& c : combR_) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.filt = 0.0f; }
        for (auto& a : apL_) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
        for (auto& a : apR_) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    }

    driveGated_ = driveGate;
    chorusGated_ = chorusGate;
    delayGated_ = delayGate;
    verbGated_ = verbGate;

    headroomInput_.process(L, R, n);
    eq_.process(L, R, n);

    float outPk = 0;
    for (int i = 0; i < n; i++) {
        float l = L[i], r = R[i];

        // ---- OTT -> leveling compressor (automatic level matching) ----
        double ottL, ottR;
        meter_.level(FxTelemetry::ottIn, l, r);
            ott_.processSample(l, r, ottL, ottR);
            meter_.level(FxTelemetry::ottOut, ottL, ottR); l = (float)ottL; r = (float)ottR;
        double gOtt = headroomOtt_.gainFor(l, r); l *= (float)gOtt; r *= (float)gOtt;
        double compL, compR;
        meter_.level(FxTelemetry::compIn, l, r);
            comp_.processSample(l, r, compL, compR);
            meter_.level(FxTelemetry::compOut, compL, compR); l = (float)compL; r = (float)compR;
        double gComp = headroomComp_.gainFor(l, r); l *= (float)gComp; r *= (float)gComp;

        // ---- drive (4x oversampled tanh waveshaper) ----
        // The dry/bypass path always runs through a kDriveLatency delay so the
        // dry/wet mix stays time-aligned with the shaper's FIR group delay and
        // chain latency is constant whether drive is active or gated.
        dryL_.write(l); dryR_.write(r);
        float dlyL = dryL_.read((double)(kDriveLatency + 1));
        float dlyR = dryR_.read((double)(kDriveLatency + 1));
        if (!driveGated_) {
            float wet = driveWet_.next(), dry = driveDry_.next();
            float dl = driveChannel(up1L_, up2L_, dn2L_, dn1L_, (double)drivePre_ * l);
            float dr = driveChannel(up1R_, up2R_, dn2R_, dn1R_, (double)drivePre_ * r);
            l = dry * dlyL + wet * dl;
            r = dry * dlyR + wet * dr;
        } else {
            l = dlyL; r = dlyR;
        }
        double gDrive = headroomDrive_.gainFor(l, r); l *= (float)gDrive; r *= (float)gDrive;

        // ---- chorus (two modulated taps, stereo) ----
        if (!chorusGated_) {
            chPhase_ += chRate_ / sr_;
            if (chPhase_ >= 1) chPhase_ -= 1;
            double lfo = std::sin(2 * PI * chPhase_);
            double depth = 0.0008 + chDepth_ * 0.0045;
            float mono = 0.5f * (l + r);
            chDl1_.write(mono);
            chDl2_.write(mono);
            double d1 = (0.012 + depth * lfo) * sr_;
            double d2 = (0.017 - depth * 0.8 * lfo) * sr_;
            float c1 = chDl1_.readHermite(d1);
            float c2 = chDl2_.readHermite(d2);
            float wet = chWet_.next(), dry = chDry_.next();
            l = dry * l + wet * c1;
            r = dry * r + wet * c2;
        }
        double gChorus = headroomChorus_.gainFor(l, r); l *= (float)gChorus; r *= (float)gChorus;

        // ---- ping-pong delay ----
        if (!delayGated_) {
            double dt = dlTime_.next() * sr_;
            float fb = dlFb_.next();
            float dL = dlL_.readHermite(dt);
            float dR = dlR_.readHermite(dt);
            float mono = 0.5f * (l + r);
            float feedbackL = mono + fb * dR;
            float feedbackR = (float)dlDamp_.process(fb * dL);
            double gFb = delayFeedbackGuard_.gainFor(feedbackL, feedbackR);
            dlL_.write(feedbackL * (float)gFb);
            dlR_.write(feedbackR * (float)gFb);
            float wet = dlWet_.next(), dry = dlDry_.next();
            meter_.stereo(FxTelemetry::echoL, wet * dL, wet * dR);
                l = dry * l + wet * dL;
            r = dry * r + wet * dR;
        }
        double gDelay = headroomDelay_.gainFor(l, r); l *= (float)gDelay; r *= (float)gDelay;

        // ---- reverb send (Freeverb is shared per output bus) ----
        if (!verbGated_ && sendL != nullptr && sendR != nullptr) {
            float wet = verbWet_.next(), dry = verbDry_.next();
            sendL[i] = l * wet; sendR[i] = r * wet;
            l *= dry; r *= dry;
        } else if (!verbGated_) {
            float input = (l + r) * 0.015f; // fixed input gain (Freeverb convention)
            float outL = 0, outR = 0;
            for (size_t c = 0; c < 8; c++) { outL += combL_[c].process(input); outR += combR_[c].process(input); }
            for (size_t a = 0; a < 4; a++) { outL = apL_[a].process(outL); outR = apR_[a].process(outR); }
            float wet = verbWet_.next(), dry = verbDry_.next();
            meter_.stereo(FxTelemetry::verbL, wet * outL, wet * outR);
                l = dry * l + wet * outL;
            r = dry * r + wet * outR;
        } else if (sendL != nullptr && sendR != nullptr) {
            sendL[i] = sendR[i] = 0.0f;
        }
        meter_.finish(ott_, comp_, dlTime_.cur);
        double gVerb = headroomReverb_.gainFor(l, r); l *= (float)gVerb; r *= (float)gVerb;

        L[i] = l; R[i] = r;
        outPk = std::max(outPk, std::max(std::abs(l), std::abs(r)));
    }

    // Finding D8: the chain may only be gated once its own OUTPUT has gone
    // quiet as well — that is what makes a seconds-long reverb tail safe. The
    // hold just stops the gate from chattering around the threshold.
    if (inPk <= kIdleLevel && outPk <= kIdleLevel) idleSilent_ += n;
    else idleSilent_ = 0;
    if (idleSilent_ >= kIdleHold * sr_) idle_ = true;
}

// ---------------- shared drum reverb ---------------------------------------
void DrumReverb::prepare(double sampleRate) {
    meter_.prepare(sampleRate);
    sr_ = sampleRate;
    inputGuard_.prepare(sr_);
    const double scale = sr_ / 44100.0;
    for (size_t i = 0; i < 8; ++i) {
        combL_[i].prepare((int)(COMB_TUNE[i] * scale));
        combR_[i].prepare((int)((COMB_TUNE[i] + STEREO_SPREAD) * scale));
    }
    for (size_t i = 0; i < 4; ++i) {
        apL_[i].prepare((int)(AP_TUNE[i] * scale));
        apR_[i].prepare((int)((AP_TUNE[i] + STEREO_SPREAD) * scale));
        apL_[i].feedback = apR_[i].feedback = 0.5f;
    }
    size_.setTime(0.02, sr_);
    size_.snap(0.4f);
    reset();
}

void DrumReverb::reset() {
    meter_.reset();
    for (auto& c : combL_) c.reset();
    for (auto& c : combR_) c.reset();
    for (auto& a : apL_) a.reset();
    for (auto& a : apR_) a.reset();
    inputGuard_.reset(); size_.snap(size_.target);
    gated_ = true; silent_ = 0;
}

void DrumReverb::process(float* inL, float* inR, float* outL, float* outR, int n) {
    float inPk = 0;
    for (int i = 0; i < n; ++i)
        inPk = std::max(inPk, std::max(std::abs(inL[i]), std::abs(inR[i])));
    constexpr float kGateEps = 1.0e-5f;
    constexpr double kGateHold = 0.25;
    if (gated_) {
        if (inPk <= kGateEps) return;
        gated_ = false; silent_ = 0;
    }
    inputGuard_.process(inL, inR, n);
    const float s = size_.nextN(n);
    feedback_ = 0.7f + s * 0.28f;
    damp1_ = 0.4f - s * 0.2f;
    damp2_ = 1.0f - damp1_;
    for (size_t i = 0; i < 8; ++i) {
        combL_[i].feedback = combR_[i].feedback = feedback_;
        combL_[i].damp1 = combR_[i].damp1 = damp1_;
        combL_[i].damp2 = combR_[i].damp2 = damp2_;
    }
    float outPk = 0;
    for (int i = 0; i < n; ++i) {
        const float input = (inL[i] + inR[i]) * 0.015f;
        float l = 0, r = 0;
        for (size_t c = 0; c < 8; ++c) { l += combL_[c].process(input); r += combR_[c].process(input); }
        for (size_t a = 0; a < 4; ++a) { l = apL_[a].process(l); r = apR_[a].process(r); }
        meter_.stereo(FxTelemetry::verbL, l, r); meter_.finishReverb();
        outL[i] += l; outR[i] += r;
        outPk = std::max(outPk, std::max(std::abs(l), std::abs(r)));
    }
    if (inPk <= kGateEps && outPk <= kGateEps) silent_ += n;
    else silent_ = 0;
    if (silent_ >= kGateHold * sr_) gated_ = true;
}

// ---------------- DrumBusOut (Finding D1) ----------------
// The tail of the web graph, applied to a summed bus: master gain -> DC block
// -> lookahead safety limiter. Identical stage code and constants to the
// per-pad version it replaced, so a single sounding pad keeps its old loudness.
void DrumBusOut::prepare(double sampleRate) {
    sr_ = sampleRate;
    inputGuard_.prepare(sr_);
    masterGain_.setTime(0.02, sr_);
    dcL_.highpass(8, 0.707, sr_);
    dcR_.highpass(8, 0.707, sr_);

    // WebAudio's DynamicsCompressor applies spec-defined makeup gain
    // ((1/c(1))^0.6, c = static curve at 0 dBFS). The web app's limiter IS that
    // node, so keep its ~4.5 dB makeup ahead of the lookahead limiter or the
    // plugin sits under the web app's loudness.
    double c1 = std::pow(1.0 / kLimThr, 1.0 / kLimRatio - 1.0);
    lim_.prepare(sr_, std::pow(1.0 / c1, 0.6));

    reset();
}

void DrumBusOut::reset() {
    inputGuard_.reset(); dcL_.reset(); dcR_.reset();
    lim_.reset();
    masterGain_.snap(masterGain_.target);
}

void DrumBusOut::setParams(const DrumParamArray& p) {
    const float vol = p[DG_MASTER_VOLUME];
    masterGain_.target = vol * vol * 1.6f;
}

void DrumBusOut::process(float* L, float* R, int n) {
    inputGuard_.process(L, R, n);
    for (int i = 0; i < n; i++) {
        const float g = masterGain_.next();
        float l = L[i] * g, r = R[i] * g;
        l = (float)dcL_.process(l);
        r = (float)dcR_.process(r);
        lim_.process(l, r);
        L[i] = l; R[i] = r;
    }
}

} // namespace fable
