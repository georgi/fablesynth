#include "Fx.h"
#include <algorithm>
#include <cmath>

namespace fable {

static constexpr double PI = 3.14159265358979323846;

// Safety-limiter static curve: threshold -8 dB (~0.398), ratio 14. Shared by
// the gain computer in process() and the WebAudio-matching makeup gain.
static constexpr double kLimThr = 0.398, kLimRatio = 14.0;

// Leveling compressor: fixed WebAudio DynamicsCompressor settings (ratio 4,
// knee 9 dB, attack 10 ms, release 200 ms) with THRESH/MAKEUP params. Same
// static-curve math as DR-1's bus compressor.
static constexpr double kCompRatio = 4.0, kCompKnee = 9.0;

// WebAudio DynamicsCompressor static curve, in dB of gain reduction (<= 0):
// below thr 0 dB; within [thr, thr+knee] quadratic transition; above, slope
// 1/ratio - 1.
static inline double compGainDb(double xDb, double thrDb) {
    double over = xDb - thrDb;
    if (over <= 0) return 0.0;
    if (over < kCompKnee)
        return (1.0 / kCompRatio - 1.0) * over * over / (2.0 * kCompKnee);
    return (1.0 / kCompRatio - 1.0) * (over - kCompKnee * 0.5);
}

// ---------------- Biquad designs (RBJ cookbook) ----------------
void Biquad::lowpass(double freq, double q, double sr) {
    double w0 = 2 * PI * std::min(freq, sr * 0.49) / sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2 * q);
    double a0 = 1 + alpha;
    b0 = (1 - cw) / 2 / a0;
    b1 = (1 - cw) / a0;
    b2 = b0;
    a1 = (-2 * cw) / a0;
    a2 = (1 - alpha) / a0;
}
void Biquad::highpass(double freq, double q, double sr) {
    double w0 = 2 * PI * std::min(freq, sr * 0.49) / sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2 * q);
    double a0 = 1 + alpha;
    b0 = (1 + cw) / 2 / a0;
    b1 = -(1 + cw) / a0;
    b2 = b0;
    a1 = (-2 * cw) / a0;
    a2 = (1 - alpha) / a0;
}
// EQ designs — RBJ shelving/peaking. gainDb 0 yields exact unity (transparent
// bypass). Shelf slope S = 1; peaking uses the passed Q.
void Biquad::lowShelf(double freq, double gainDb, double sr) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * PI * std::min(freq, sr * 0.49) / sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double S = 1.0; // shelf slope
    double alpha = sw / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2);
    double tsa = 2 * std::sqrt(A) * alpha;
    double a0 = (A + 1) + (A - 1) * cw + tsa;
    b0 = A * ((A + 1) - (A - 1) * cw + tsa) / a0;
    b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
    b2 = A * ((A + 1) - (A - 1) * cw - tsa) / a0;
    a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
    a2 = ((A + 1) + (A - 1) * cw - tsa) / a0;
}
void Biquad::highShelf(double freq, double gainDb, double sr) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * PI * std::min(freq, sr * 0.49) / sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double S = 1.0; // shelf slope
    double alpha = sw / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2);
    double tsa = 2 * std::sqrt(A) * alpha;
    double a0 = (A + 1) - (A - 1) * cw + tsa;
    b0 = A * ((A + 1) + (A - 1) * cw + tsa) / a0;
    b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
    b2 = A * ((A + 1) + (A - 1) * cw - tsa) / a0;
    a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
    a2 = ((A + 1) - (A - 1) * cw - tsa) / a0;
}
void Biquad::peaking(double freq, double q, double gainDb, double sr) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * PI * std::min(freq, sr * 0.49) / sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2 * q);
    double a0 = 1 + alpha / A;
    b0 = (1 + alpha * A) / a0;
    b1 = (-2 * cw) / a0;
    b2 = (1 - alpha * A) / a0;
    a1 = (-2 * cw) / a0;
    a2 = (1 - alpha / A) / a0;
}

// ---------------- shared oversampling / limiter helpers ----------------
static double besselI0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 64; k++) {
        term *= x * x / (4.0 * k * k);
        sum += term;
        if (term < 1e-16 * sum) break;
    }
    return sum;
}

void HalfBandFir::design(int taps, double beta) {
    h.assign((size_t)taps, 0.0);
    double M = taps - 1, ib = besselI0(beta);
    for (int i = 0; i < taps; i++) {
        double m = i - M * 0.5;
        double sinc = std::fpclassify(m) == FP_ZERO ? 0.5 : std::sin(PI * 0.5 * m) / (PI * m);
        double t = 2.0 * m / M;
        h[(size_t)i] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - t * t))) / ib;
    }
    z.assign((size_t)taps, 0.0);
    pos = 0;
}

void LookaheadLimiter::prepare(double sampleRate, double makeup) {
    la_ = std::max(8, (int)std::lround(0.0015 * sampleRate)); // ~1.5 ms lookahead
    qcap_ = la_ + 2;
    dlL_.assign((size_t)la_, 0.0f);
    dlR_.assign((size_t)la_, 0.0f);
    qv_.assign((size_t)qcap_, 1.0);
    qi_.assign((size_t)qcap_, 0);
    atk_ = 1.0 - std::exp(-4.0 / la_);              // develops fully inside the window
    rel_ = 1.0 - std::exp(-1.0 / (0.2 * sampleRate)); // ~200 ms release
    makeup_ = makeup;
    reset();
}

void LookaheadLimiter::reset() {
    std::fill(dlL_.begin(), dlL_.end(), 0.0f);
    std::fill(dlR_.begin(), dlR_.end(), 0.0f);
    qh_ = qt_ = 0; w_ = 0; t_ = 0; env_ = 1.0;
}

// ---------------- Freeverb tuning (classic constants, scaled to sr) ----------------
static const int COMB_TUNE[8]   = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int AP_TUNE[4]     = {556, 441, 341, 225};
static const int STEREO_SPREAD  = 23;

void Fx::prepare(double sampleRate) {
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
    compThrDb_.setTime(0.02, sr_); compMakeup_.setTime(0.02, sr_);
    compWet_.setTime(0.02, sr_); compDry_.setTime(0.02, sr_);
    masterGain_.setTime(0.02, sr_);
    driveDry_.snap(1); chDry_.snap(1); dlDry_.snap(1); verbDry_.snap(1); compDry_.snap(1);

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

    // Leveling compressor envelope coefficients (attack 10 ms, release 200 ms).
    compAtk_ = 1.0 - std::exp(-1.0 / (0.010 * sr_));
    compRel_ = 1.0 - std::exp(-1.0 / (0.200 * sr_));

    // WebAudio's DynamicsCompressor applies spec-defined makeup gain
    // ((1/c(1))^0.6, c = static curve at 0 dBFS). The web app's limiter IS that
    // node, so keep its ~4.5 dB makeup ahead of the lookahead limiter or the
    // plugin sits under the web app's loudness.
    double c1 = std::pow(1.0 / kLimThr, 1.0 / kLimRatio - 1.0);
    lim_.prepare(sr_, std::pow(1.0 / c1, 0.6));

    reset(); // full state clear: re-prepare must never keep stale recursive state
}

void Fx::reset() {
    chDl1_.reset(); chDl2_.reset(); dlL_.reset(); dlR_.reset();
    dryL_.reset(); dryR_.reset();
    for (auto& c : combL_) c.reset();
    for (auto& c : combR_) c.reset();
    for (auto& a : apL_) a.reset();
    for (auto& a : apR_) a.reset();
    dcL_.reset(); dcR_.reset(); dlDamp_.reset();
    eqLoL_.reset(); eqLoR_.reset(); eqMidL_.reset(); eqMidR_.reset(); eqHiL_.reset(); eqHiR_.reset();
    up1L_.reset(); up2L_.reset(); dn2L_.reset(); dn1L_.reset();
    up1R_.reset(); up2R_.reset(); dn2R_.reset(); dn1R_.reset();
    compEnv_ = 0;
    lim_.reset();
    chPhase_ = 0;
    driveGated_ = chorusGated_ = delayGated_ = verbGated_ = compGated_ = false;
    // settle smoothers at their targets so no stale ramp survives a re-prepare
    driveWet_.snap(driveWet_.target); driveDry_.snap(driveDry_.target);
    chWet_.snap(chWet_.target); chDry_.snap(chDry_.target);
    dlTime_.snap(dlTime_.target); dlFb_.snap(dlFb_.target);
    dlWet_.snap(dlWet_.target); dlDry_.snap(dlDry_.target);
    verbWet_.snap(verbWet_.target); verbDry_.snap(verbDry_.target);
    compThrDb_.snap(compThrDb_.target); compMakeup_.snap(compMakeup_.target);
    compWet_.snap(compWet_.target); compDry_.snap(compDry_.target);
    masterGain_.snap(masterGain_.target);
}

static inline float mixGate(bool on, float amount, bool wet) {
    if (wet) return on ? (float)std::sin(amount * PI / 2) : 0.0f;
    return on ? (float)std::cos(amount * PI / 2) : 1.0f;
}

void Fx::setParams(const ParamArray& p) {
    // 3-band tone EQ (first FX). Gains apply only when on; off forces 0 dB, an
    // exact unity bypass. Shelves at fixed corners (120 Hz / 6 kHz), mid bell
    // sweepable with fixed Q 0.9 — mirrors the WebAudio graph in synth.ts.
    bool eqOn = p[FXEQ_ON] > 0.5f;
    float loDb = eqOn ? p[FXEQ_LOW] : 0.0f;
    float midDb = eqOn ? p[FXEQ_MID] : 0.0f;
    float hiDb = eqOn ? p[FXEQ_HIGH] : 0.0f;
    float mFreq = p[FXEQ_MFREQ];
    eqLoL_.lowShelf(120.0, loDb, sr_);   eqLoR_.lowShelf(120.0, loDb, sr_);
    eqMidL_.peaking(mFreq, 0.9, midDb, sr_); eqMidR_.peaking(mFreq, 0.9, midDb, sr_);
    eqHiL_.highShelf(6000.0, hiDb, sr_); eqHiR_.highShelf(6000.0, hiDb, sr_);

    // drive
    float amt = p[FXDRIVE_AMT];
    drivePre_ = 1 + amt * 2;
    driveK_ = 1 + amt * 12;
    driveNorm_ = 1.0f / (drivePre_ * std::tanh(driveK_));
    bool dOn = p[FXDRIVE_ON] > 0.5f;
    driveOff_ = !dOn;
    driveWet_.target = mixGate(dOn, p[FXDRIVE_MIX], true);
    driveDry_.target = mixGate(dOn, p[FXDRIVE_MIX], false);

    // chorus
    chRate_ = p[FXCHORUS_RATE];
    chDepth_ = p[FXCHORUS_DEPTH];
    bool cOn = p[FXCHORUS_ON] > 0.5f;
    chorusOff_ = !cOn;
    chWet_.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8f, true);
    chDry_.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8f, false);

    // delay
    dlTime_.target = p[FXDELAY_TIME];
    dlFb_.target = p[FXDELAY_FB];
    bool delOn = p[FXDELAY_ON] > 0.5f;
    delayOff_ = !delOn;
    dlWet_.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85f, true);
    dlDry_.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85f, false);

    // reverb — SIZE maps to roomsize/decay (longer & brighter tail with size)
    float size = p[FXREVERB_SIZE];
    roomSize_ = 0.7f + size * 0.28f;
    float damp = 0.4f - size * 0.2f;
    for (size_t i = 0; i < 8; i++) {
        combL_[i].feedback = combR_[i].feedback = roomSize_;
        combL_[i].damp1 = combR_[i].damp1 = damp;
        combL_[i].damp2 = combR_[i].damp2 = 1 - damp;
    }
    bool rOn = p[FXREVERB_ON] > 0.5f;
    verbOff_ = !rOn;
    verbWet_.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9f, true);
    verbDry_.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9f, false);

    // leveling compressor — implicit spec makeup (static curve at 0 dBFS) times
    // the user MAKEUP, so quiet patches lift while the 4:1 curve tames loud ones.
    float thrDb = p[FXCOMP_THR];
    compThrDb_.target = thrDb;
    double implicit = std::pow(10.0, -0.6 * compGainDb(0.0, thrDb) / 20.0);
    compMakeup_.target = (float)(implicit * std::pow(10.0, p[FXCOMP_GAIN] / 20.0));
    bool kOn = p[FXCOMP_ON] > 0.5f;
    compOff_ = !kOn;
    compWet_.target = mixGate(kOn, 1.0f, true);
    compDry_.target = mixGate(kOn, 1.0f, false);

    float vol = p[MASTER_VOLUME];
    masterGain_.target = vol * vol * 1.6f;
}

float Fx::shape(float x) const {
    // tanh is bounded — no pre-clamp (a hard clamp is its own nonsmooth nonlinearity)
    return std::tanh(x * driveK_) * driveNorm_;
}

// One channel through the 4x oversampled shaper: 2x half-band interpolate,
// 2x again, tanh at 4x, then the mirrored decimators. Zero-stuff gain (x2 per
// stage) is applied at each stage input; decimation keeps the phase aligned
// with the integer kDriveLatency group delay.
float Fx::driveChannel(HalfBandFir& u1, HalfBandFir& u2, HalfBandFir& d2, HalfBandFir& d1, double x) {
    double a[2] = { u1.process(2.0 * x), u1.process(0.0) };
    double y = 0;
    for (int k = 0; k < 2; k++) {
        double b0 = u2.process(2.0 * a[k]);
        double b1 = u2.process(0.0);
        double c = d2.process((double)shape((float)b0));
        d2.process((double)shape((float)b1)); // discarded decimation phase
        double d = d1.process(c);
        if (k == 0) y = d;                    // keep the base-rate phase
    }
    return (float)y;
}

void Fx::process(float* L, float* R, int n) {
    // Gate only when OFF; mix==0 while ON must keep state accumulation alive.
    bool driveGate = driveOff_ && driveWet_.target == 0.0f && std::abs(driveWet_.cur) < 1.0e-6f;
    bool chorusGate = chorusOff_ && chWet_.target == 0.0f && std::abs(chWet_.cur) < 1.0e-6f;
    bool delayGate = delayOff_ && dlWet_.target == 0.0f && std::abs(dlWet_.cur) < 1.0e-6f;
    bool verbGate = verbOff_ && verbWet_.target == 0.0f && std::abs(verbWet_.cur) < 1.0e-6f;
    bool compGate = compOff_ && compWet_.target == 0.0f && std::abs(compWet_.cur) < 1.0e-6f;

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
    if (compGate && !compGated_) {
        compWet_.snap(0); compDry_.snap(1);
        compEnv_ = 0;
    }

    driveGated_ = driveGate;
    chorusGated_ = chorusGate;
    delayGated_ = delayGate;
    verbGated_ = verbGate;
    compGated_ = compGate;

    for (int i = 0; i < n; i++) {
        float l = L[i], r = R[i];

        // ---- 3-band tone EQ (first FX; 0 dB coeffs = transparent) ----
        l = (float)eqHiL_.process(eqMidL_.process(eqLoL_.process(l)));
        r = (float)eqHiR_.process(eqMidR_.process(eqLoR_.process(r)));

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

        // ---- ping-pong delay ----
        if (!delayGated_) {
            double dt = dlTime_.next() * sr_;
            float fb = dlFb_.next();
            float dL = dlL_.readHermite(dt);
            float dR = dlR_.readHermite(dt);
            float mono = 0.5f * (l + r);
            dlL_.write(mono + fb * dR);
            dlR_.write((float)dlDamp_.process(fb * dL));
            float wet = dlWet_.next(), dry = dlDry_.next();
            l = dry * l + wet * dL;
            r = dry * r + wet * dR;
        }

        // ---- reverb (Freeverb) ----
        if (!verbGated_) {
            float input = (l + r) * 0.015f; // fixed input gain (Freeverb convention)
            float outL = 0, outR = 0;
            for (size_t c = 0; c < 8; c++) { outL += combL_[c].process(input); outR += combR_[c].process(input); }
            for (size_t a = 0; a < 4; a++) { outL = apL_[a].process(outL); outR = apR_[a].process(outR); }
            float wet = verbWet_.next(), dry = verbDry_.next();
            l = dry * l + wet * outL;
            r = dry * r + wet * outR;
        }

        // ---- leveling compressor (WebAudio DynamicsCompressor semantics) ----
        // Last FX: peak-linked detector, spec static curve + makeup, brings
        // patches to roughly the same loudness before the master gain.
        if (!compGated_) {
            double pk = std::max(std::abs((double)l), std::abs((double)r));
            double coef = pk > compEnv_ ? compAtk_ : compRel_;
            compEnv_ += (pk - compEnv_) * coef;
            float thrDb = compThrDb_.next();
            double cg = 1.0;
            if (compEnv_ > 1.0e-6)
                cg = std::pow(10.0, compGainDb(20.0 * std::log10(compEnv_), thrDb) / 20.0);
            cg *= compMakeup_.next();
            float wet = compWet_.next(), dry = compDry_.next();
            l = dry * l + wet * (float)(l * cg);
            r = dry * r + wet * (float)(r * cg);
        }

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
