// BL-1 FX chain. Stage code follows source/drum/dsp/DrumFx.cpp (the DR-1 port
// of the same web topology, minus the bus compressor); the lockstep reference
// for constants and ordering is src/bass/engine/bass-synth.ts:129-272.
#include "BassFx.h"

#include <algorithm>
#include <cmath>

namespace fable {

static constexpr double PI = 3.14159265358979323846;

// Safety-limiter static curve: threshold -6 dB (~0.501), ratio 14 — the web
// bass limiter's DynamicsCompressorNode settings (bass-synth.ts:198-203).
static constexpr double kLimThr = 0.501, kLimRatio = 14.0;

// ---------------- Freeverb tuning (classic constants, scaled to sr) ----------------
static const int COMB_TUNE[8]  = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int AP_TUNE[4]    = {556, 441, 341, 225};
static const int STEREO_SPREAD = 23;

void BassFx::prepare(double sampleRate) {
    meter_.prepare(sampleRate);
    eq_.prepare(sampleRate);
    sr_ = sampleRate;
    driveColorL_.prepare(sr_); driveColorR_.prepare(sr_);
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

    // ~15 ms of coefficient ramp, in kCoefChunk-sample steps (Finding J1).
    const int steps = std::max(1, (int)std::lround(0.015 * sr_ / kCoefChunk));
    driveAmtR_.setSteps(steps); chRateR_.setSteps(steps);
    chDepthR_.setSteps(steps); verbSizeR_.setSteps(steps);

    driveWet_.setTime(0.02, sr_); driveDry_.setTime(0.02, sr_);
    chWet_.setTime(0.02, sr_); chDry_.setTime(0.02, sr_);
    dlTime_.setTime(0.08, sr_); dlFb_.setTime(0.02, sr_);
    dlWet_.setTime(0.02, sr_); dlDry_.setTime(0.02, sr_);
    verbWet_.setTime(0.02, sr_); verbDry_.setTime(0.02, sr_);
    masterGain_.setTime(0.02, sr_);
    driveDry_.snap(1); chDry_.snap(1); dlDry_.snap(1); verbDry_.snap(1);

    dcL_.highpass(8, 0.707, sr_);
    dcR_.highpass(8, 0.707, sr_);
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

    // WebAudio's DynamicsCompressor applies spec-defined makeup gain
    // ((1/c(1))^0.6, c = static curve at 0 dBFS). The web app's limiter IS that
    // node, so keep its makeup ahead of the lookahead limiter or the plugin
    // sits several dB under the web app's loudness.
    double c1 = std::pow(1.0 / kLimThr, 1.0 / kLimRatio - 1.0);
    lim_.prepare(sr_, std::pow(1.0 / c1, 0.6));

    reset(); // full state clear: re-prepare must never keep stale recursive state
}

void BassFx::reset() {
    meter_.reset();
    eq_.reset();
    chDl1_.reset(); chDl2_.reset(); dlL_.reset(); dlR_.reset();
    dryL_.reset(); dryR_.reset();
    for (auto& c : combL_) c.reset();
    for (auto& c : combR_) c.reset();
    for (auto& a : apL_) a.reset();
    for (auto& a : apR_) a.reset();
    dcL_.reset(); dcR_.reset(); dlDamp_.reset();
    ott_.reset(); driveColorL_.reset(); driveColorR_.reset(); comp_.reset();
    headroomInput_.reset(); headroomOtt_.reset(); headroomComp_.reset(); headroomDrive_.reset();
    headroomChorus_.reset(); headroomDelay_.reset(); headroomReverb_.reset(); delayFeedbackGuard_.reset();
    up1L_.reset(); up2L_.reset(); dn2L_.reset(); dn1L_.reset();
    up1R_.reset(); up2R_.reset(); dn2R_.reset(); dn1R_.reset();
    lim_.reset();
    chPhase_ = 0;
    chunkPos_ = 0;
    driveGated_ = chorusGated_ = delayGated_ = verbGated_ = false;
    compGated_ = false;
    // A re-prepare must not leave a ramp mid-flight.
    driveAmtR_.snapToTarget(); chRateR_.snapToTarget();
    chDepthR_.snapToTarget(); verbSizeR_.snapToTarget();
    updateCoefs(true);
    // settle smoothers at their targets so no stale ramp survives a re-prepare
    driveWet_.snap(driveWet_.target); driveDry_.snap(driveDry_.target);
    chWet_.snap(chWet_.target); chDry_.snap(chDry_.target);
    dlTime_.snap(dlTime_.target); dlFb_.snap(dlFb_.target);
    dlWet_.snap(dlWet_.target); dlDry_.snap(dlDry_.target);
    verbWet_.snap(verbWet_.target); verbDry_.snap(verbDry_.target);
    masterGain_.snap(masterGain_.target);
}

static inline float mixGate(bool on, float amount, bool wet) {
    if (wet) return on ? (float)std::sin(amount * PI / 2) : 0.0f;
    return on ? (float)std::cos(amount * PI / 2) : 1.0f;
}

void BassFx::setParams(const BassParamArray& p) {
    eq_.setParams(p.data() + BL_FXEQ_ON);
    compOff_ = p[BL_FXCOMP_ON] <= 0.5f;
    driveColorL_.setParams(p[BL_FXDRIVE_TYPE], p[BL_FXDRIVE_TONE]);
    driveColorR_.setParams(p[BL_FXDRIVE_TYPE], p[BL_FXDRIVE_TONE]);
    comp_.setParams(!compOff_, p[BL_FXCOMP_THR], p[BL_FXCOMP_ATT], p[BL_FXCOMP_REL], p[BL_FXCOMP_RATIO]);
    ott_.setParams(p[BL_FXOTT_ON] > 0.5f, p[BL_FXOTT_DEPTH], p[BL_FXOTT_TIME], p[BL_FXOTT_UP], p[BL_FXOTT_DOWN]);

    // drive — AMT ramps; the shaper gains are rebuilt in updateCoefs.
    driveAmtR_.setTarget(p[BL_FXDRIVE_AMT]);
    bool dOn = p[BL_FXDRIVE_ON] > 0.5f;
    driveOff_ = !dOn;
    driveWet_.target = mixGate(dOn, p[BL_FXDRIVE_MIX], true);
    driveDry_.target = mixGate(dOn, p[BL_FXDRIVE_MIX], false);

    // chorus
    chRateR_.setTarget(p[BL_FXCHORUS_RATE]);
    chDepthR_.setTarget(p[BL_FXCHORUS_DEPTH]);
    bool cOn = p[BL_FXCHORUS_ON] > 0.5f;
    chorusOff_ = !cOn;
    chWet_.target = mixGate(cOn, p[BL_FXCHORUS_MIX] * 0.8f, true);
    chDry_.target = mixGate(cOn, p[BL_FXCHORUS_MIX] * 0.8f, false);

    // delay
    dlTime_.target = p[BL_FXDELAY_TIME];
    dlFb_.target = p[BL_FXDELAY_FB];
    bool delOn = p[BL_FXDELAY_ON] > 0.5f;
    delayOff_ = !delOn;
    dlWet_.target = mixGate(delOn, p[BL_FXDELAY_MIX] * 0.85f, true);
    dlDry_.target = mixGate(delOn, p[BL_FXDELAY_MIX] * 0.85f, false);

    // reverb — SIZE maps to roomsize/decay (longer & brighter tail with size);
    // the comb coefficients follow it in updateCoefs.
    verbSizeR_.setTarget(p[BL_FXREVERB_SIZE]);
    bool rOn = p[BL_FXREVERB_ON] > 0.5f;
    verbOff_ = !rOn;
    verbWet_.target = mixGate(rOn, p[BL_FXREVERB_MIX] * 0.9f, true);
    verbDry_.target = mixGate(rOn, p[BL_FXREVERB_MIX] * 0.9f, false);

    float vol = p[BL_MASTER_VOLUME];
    masterGain_.target = vol * vol * 1.6f;
}

// Finding J1: rebuild every coefficient that derives from a ramped parameter.
// Called once per kCoefChunk samples, and only while something is moving.
void BassFx::updateCoefs(bool force) {
    bool moved = force;
    moved |= driveAmtR_.next();
    moved |= chRateR_.next();
    moved |= chDepthR_.next();
    const bool verbMoved = verbSizeR_.next();
    if (moved) {
        const float amt = driveAmtR_.cur;
        drivePre_ = 1 + amt * 2;
        driveK_ = 1 + amt * 12;
        driveNorm_ = 1.0f / (drivePre_ * std::tanh(driveK_));
        chRate_ = chRateR_.cur;
        chDepth_ = chDepthR_.cur;
    }
    if (verbMoved || force) {
        const float size = verbSizeR_.cur;
        roomSize_ = 0.7f + size * 0.28f;
        const float damp = 0.4f - size * 0.2f;
        for (size_t i = 0; i < 8; i++) {
            combL_[i].feedback = combR_[i].feedback = roomSize_;
            combL_[i].damp1 = combR_[i].damp1 = damp;
            combL_[i].damp2 = combR_[i].damp2 = 1 - damp;
        }
    }
}

void BassFx::snapRamps() {
    driveAmtR_.snapToTarget(); chRateR_.snapToTarget();
    chDepthR_.snapToTarget(); verbSizeR_.snapToTarget();
    updateCoefs(true);
}

// One channel through the 4x oversampled shaper. Finding J4: polyphase, the
// same transcription as Fx::driveChannel — interpolate() replaces the
// process(2x)/process(0) pair and decimate() replaces the process/process
// pair whose second result was thrown away, so neither the zero-stuffed
// multiplies nor the discarded decimation phase are computed at all. Same
// filters, same group delay, same kDriveLatency. Each HalfBandFir here is
// driven ONLY through interpolate/decimate (the two modes keep separate
// histories and must never be mixed on one instance).
float BassFx::driveChannel(HalfBandFir& u1, HalfBandFir& u2, HalfBandFir& d2, HalfBandFir& d1, double x, DriveColor& color) {
    auto shape = [&](float v) { return (float)color.shape(v, driveK_, driveNorm_); };
    double a0, a1;
    u1.interpolate(x, a0, a1);                       // 2x
    double b00, b01, b10, b11;
    u2.interpolate(a0, b00, b01);                    // 4x
    u2.interpolate(a1, b10, b11);
    const double s0a = shape((float)b00), s0b = shape((float)b01);
    const double c0 = d2.decimate(s0a, s0b);
    const double s1a = shape((float)b10), s1b = shape((float)b11);
    const double c1 = d2.decimate(s1a, s1b);
    return (float)d1.decimate(c0, c1);
}

void BassFx::process(float* L, float* R, int n) {
    // Gate only when OFF; mix==0 while ON must keep state accumulation alive.
    bool driveGate = driveOff_ && driveWet_.target == 0.0f && std::abs(driveWet_.cur) < 1.0e-6f;
    bool chorusGate = chorusOff_ && chWet_.target == 0.0f && std::abs(chWet_.cur) < 1.0e-6f;
    bool delayGate = delayOff_ && dlWet_.target == 0.0f && std::abs(dlWet_.cur) < 1.0e-6f;
    bool verbGate = verbOff_ && verbWet_.target == 0.0f && std::abs(verbWet_.cur) < 1.0e-6f;

    if (driveGate && !driveGated_) {
        driveWet_.snap(0); driveDry_.snap(1);
        driveColorL_.reset(); driveColorR_.reset();
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
    // WebCompressor owns its wet fade and state reset, so it continues to run
    // while switched off just long enough to settle its bypass.
    compGated_ = false;

    headroomInput_.process(L, R, n);
    eq_.process(L, R, n);

    for (int i = 0; i < n; i++) {
        if (chunkPos_ == 0) updateCoefs(false);
        if (++chunkPos_ >= kCoefChunk) chunkPos_ = 0;
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

        // ---- drive (4x oversampled tanh waveshaper, post-accent) ----
        // The dry/bypass path always runs through a kDriveLatency delay so the
        // dry/wet mix stays time-aligned with the shaper's FIR group delay and
        // chain latency is constant whether drive is active or gated.
        dryL_.write(l); dryR_.write(r);
        float dlyL = dryL_.read((double)(kDriveLatency + 1));
        float dlyR = dryR_.read((double)(kDriveLatency + 1));
        if (!driveGated_) {
            float wet = driveWet_.next(), dry = driveDry_.next();
            float dl = driveChannel(up1L_, up2L_, dn2L_, dn1L_, (double)drivePre_ * l, driveColorL_);
            float dr = driveChannel(up1R_, up2R_, dn2R_, dn1R_, (double)drivePre_ * r, driveColorR_);
            l = dry * dlyL + wet * driveColorL_.processTone(dl);
            r = dry * dlyR + wet * driveColorR_.processTone(dr);
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

        // ---- reverb (Freeverb) ----
        if (!verbGated_) {
            float input = (l + r) * 0.015f; // fixed input gain (Freeverb convention)
            float outL = 0, outR = 0;
            for (size_t c = 0; c < 8; c++) { outL += combL_[c].process(input); outR += combR_[c].process(input); }
            for (size_t a = 0; a < 4; a++) { outL = apL_[a].process(outL); outR = apR_[a].process(outR); }
            float wet = verbWet_.next(), dry = verbDry_.next();
            meter_.stereo(FxTelemetry::verbL, wet * outL, wet * outR);
                l = dry * l + wet * outL;
            r = dry * r + wet * outR;
        }
        meter_.finish(ott_, comp_, dlTime_.cur);
        double gVerb = headroomReverb_.gainFor(l, r); l *= (float)gVerb; r *= (float)gVerb;

        // ---- master gain ----
        float g = masterGain_.next();
        l *= g; r *= g;

        // ---- DC block ----
        l = (float)dcL_.process(l);
        r = (float)dcR_.process(r);

        // ---- lookahead safety limiter (makeup inside, -1 dBFS ceiling) ----
        lim_.process(l, r);

        L[i] = l; R[i] = r;
    }
}

} // namespace fable
