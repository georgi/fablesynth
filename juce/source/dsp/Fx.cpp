#include "Fx.h"
#include <algorithm>
#include <cmath>

namespace fable {

static constexpr double PI = 3.14159265358979323846;

// Safety-limiter static curve: threshold -8 dB (~0.398), ratio 14. Shared by
// the gain computer in process() and the WebAudio-matching makeup gain.
static constexpr double kLimThr = 0.398, kLimRatio = 14.0;

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
void Biquad::lowShelf(double freq, double gainDb, double sr, double q) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * PI * std::min(freq, sr * 0.49) / sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2 * q);
    double tsa = 2 * std::sqrt(A) * alpha;
    double a0 = (A + 1) + (A - 1) * cw + tsa;
    b0 = A * ((A + 1) - (A - 1) * cw + tsa) / a0;
    b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
    b2 = A * ((A + 1) - (A - 1) * cw - tsa) / a0;
    a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
    a2 = ((A + 1) + (A - 1) * cw - tsa) / a0;
}
void Biquad::highShelf(double freq, double gainDb, double sr, double q) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * PI * std::min(freq, sr * 0.49) / sr;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2 * q);
    double tsa = 2 * std::sqrt(A) * alpha;
    double a0 = (A + 1) - (A - 1) * cw + tsa;
    b0 = A * ((A + 1) + (A - 1) * cw + tsa) / a0;
    b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
    b2 = A * ((A + 1) + (A - 1) * cw - tsa) / a0;
    a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
    a2 = ((A + 1) - (A - 1) * cw - tsa) / a0;
}

// ---------------- shared web dynamics --------------------------------------
void AutoGain::prepare(double sampleRate) {
    energyCoef_ = 1.0 - std::exp(-1.0 / (0.3 * sampleRate));
    gainCoef_ = 1.0 - std::exp(-1.0 / (0.15 * sampleRate));
    reset();
}

void AutoGain::reset() {
    input_ = wet_ = 0.0;
    gain = target_ = 1.0;
    tick_ = 0;
}

double AutoGain::next(double inL, double inR, double wetL, double wetR) {
    const double input = 0.5 * (inL * inL + inR * inR);
    const double wet = 0.5 * (wetL * wetL + wetR * wetR);
    input_ += (input - input_) * energyCoef_;
    wet_ += (wet - wet_) * energyCoef_;
    if (tick_ == 0 && input > 1.0e-12 && input_ > 1.0e-12 && wet_ > 1.0e-16)
        target_ = std::max(0.003981071706, std::min(15.848931925, std::sqrt(input_ / wet_)));
    tick_ = (tick_ + 1) & 15;
    gain += (target_ - gain) * gainCoef_;
    return gain;
}

void PeakGuard::prepare(double sampleRate) {
    release_ = 1.0 - std::exp(-1.0 / (0.08 * sampleRate));
    reset();
}

double PeakGuard::gainFor(double l, double r) {
    const double peak = std::max(std::abs(l), std::abs(r));
    const double required = peak > kCeiling ? kCeiling / peak : 1.0;
    gain = std::min(required, gain + (1.0 - gain) * release_);
    if (required == 1.0 && 1.0 - gain < 1.0e-8) gain = 1.0;
    return gain;
}

void PeakGuard::process(float* L, float* R, int n) {
    for (int i = 0; i < n; ++i) {
        double l = L[i], r = R[i];
        if (!std::isfinite(l) || !std::isfinite(r)) { L[i] = R[i] = 0.0f; continue; }
        const double g = gainFor(l, r);
        L[i] = (float)(l * g); R[i] = (float)(r * g);
    }
}

static inline double webCompGainDb(double xDb, double thresholdDb, double ratio) {
    const double over = xDb - thresholdDb;
    if (over <= 0.0) return 0.0;
    const double slope = 1.0 / ratio - 1.0;
    if (over < 9.0) return slope * over * over / 18.0;
    return slope * (over - 4.5);
}

void WebCompressor::prepare(double sampleRate) {
    sr_ = sampleRate;
    smooth_ = 1.0 - std::exp(-1.0 / (0.02 * sr_));
    attack_ = 1.0 - std::exp(-1.0 / (0.003 * sr_));
    release_ = 1.0 - std::exp(-1.0 / (0.25 * sr_));
    autoGain.prepare(sr_);
    wetTarget = false;
    threshold_ = thresholdTarget_ = -16.0;
    ratio_ = ratioTarget_ = 4;
    reset();
}

void WebCompressor::setParams(bool on, float thresholdDb, float attack, float release, float ratio) {
    attack_ = 1.0 - std::exp(-1.0 / (std::clamp((double)attack, 0.0001, 0.1) * sr_));
    release_ = 1.0 - std::exp(-1.0 / (std::clamp((double)release, 0.01, 2.0) * sr_));
    ratioTarget_ = std::clamp((double)ratio, 1.0, 20.0);
    wetTarget = on;
    thresholdTarget_ = std::max(-40.0, std::min(0.0, (double)thresholdDb));
}

void WebCompressor::reset() {
    env = 0.0; gain = 1.0; gainStep_ = 0.0; tick_ = 0;
    wet_ = 0.0; autoGain.reset();
}

void WebCompressor::processSample(double inL, double inR, double& outL, double& outR) {
    if (!wetTarget && wet_ < 1.0e-6) {
        if (wet_ != 0.0) { wet_ = 0.0; reset(); }
        outL = inL; outR = inR; return;
    }
    const double peak = std::max(std::abs(inL), std::abs(inR));
    env += (peak - env) * (peak > env ? attack_ : release_);
    threshold_ += (thresholdTarget_ - threshold_) * smooth_;
    ratio_ += (ratioTarget_ - ratio_) * smooth_;
    if (tick_ == 0) {
        const double db = webCompGainDb(20.0 * std::log10(std::max(1.0e-9, env)), threshold_, ratio_);
        const double targetGain = std::pow(10.0, db / 20.0);
        gainStep_ = (targetGain - gain) / 32.0;
    }
    tick_ = (tick_ + 1) & 31;
    gain += gainStep_;
    const double g = gain * autoGain.next(inL, inR, gain * inL, gain * inR);
    wet_ += ((wetTarget ? 1.0 : 0.0) - wet_) * smooth_;
    outL = inL + wet_ * (g * inL - inL);
    outR = inR + wet_ * (g * inR - inR);
}

void WebCompressor::process(float* L, float* R, int n) {
    for (int i = 0; i < n; ++i) {
        double l, r; processSample(L[i], R[i], l, r);
        L[i] = (float)l; R[i] = (float)r;
    }
}

void OttCompressor::prepare(double sampleRate) {
    sr_ = sampleRate;
    lowCoef_ = 1.0 - std::exp(-2.0 * PI * 120.0 / sr_);
    highCoef_ = 1.0 - std::exp(-2.0 * PI * 2500.0 / sr_);
    smooth_ = 1.0 - std::exp(-1.0 / (0.015 * sr_));
    autoGain.prepare(sr_);
    // Force the time-dependent envelope coefficients to be rebuilt for the
    // new sample rate, even when the serialized TIME value is unchanged.
    time_ = 0.0;
    setParams(false, 0.35f, 1.0f, 1.0f, 1.0f);
    reset();
}

void OttCompressor::setParams(bool on, float d, float time, float up, float down) {
    depthTarget = on ? std::max(0.0, std::min(1.0, (double)d)) : 0.0;
    up_ = std::max(0.0, std::min(2.0, (double)up));
    down_ = std::max(0.0, std::min(2.0, (double)down));
    time = (float)std::max(0.01, std::min(10.0, (double)time));
    if (time != time_) {
        time_ = time;
        const double a[3] = {0.008, 0.003, 0.001};
        const double r[3] = {0.18, 0.12, 0.08};
        for (int b = 0; b < 3; ++b) {
            attack_[(size_t)b] = 1.0 - std::exp(-1.0 / (a[b] * time_ * sr_));
            release_[(size_t)b] = 1.0 - std::exp(-1.0 / (r[b] * time_ * sr_));
        }
    }
}

void OttCompressor::reset() {
    split_.fill(0.0); env.fill(0.0); gain.fill(1.0); target_.fill(1.0);
    bandsL_.fill(0.0); bandsR_.fill(0.0); tick_ = 0; depth = 0.0;
    autoGain.reset();
}

void OttCompressor::processSample(double inL, double inR, double& outL, double& outR) {
    if (depthTarget == 0.0 && depth < 1.0e-6) {
        if (depth != 0.0) { depth = 0.0; reset(); }
        outL = inL; outR = inR; return;
    }
    auto& s = split_;
    s[0] += lowCoef_ * (inL - s[0]);
    s[1] += lowCoef_ * (inR - s[1]);
    s[2] += highCoef_ * (inL - s[0] - s[2]);
    s[3] += highCoef_ * (inR - s[1] - s[3]);
    bandsL_[0] = s[0]; bandsL_[1] = s[2]; bandsL_[2] = inL - s[0] - s[2];
    bandsR_[0] = s[1]; bandsR_[1] = s[3]; bandsR_[2] = inR - s[1] - s[3];
    double wetL = 0, wetR = 0;
    for (int b = 0; b < 3; ++b) {
        const double peak = std::max(std::abs(bandsL_[(size_t)b]), std::abs(bandsR_[(size_t)b]));
        env[(size_t)b] += (peak - env[(size_t)b]) *
            (peak > env[(size_t)b] ? attack_[(size_t)b] : release_[(size_t)b]);
        if (tick_ == 0) {
            const double db = 20.0 * std::log10(std::max(1.0e-9, env[(size_t)b]));
            const double floor = std::max(0.0, std::min(1.0, (db + 90.0) / 18.0));
            const double boost = std::min(24.0, std::max(0.0, -36.0 - db) * 0.75) * up_ * floor;
            const double cut = std::max(0.0, db + 18.0) * 0.9 * down_;
            target_[(size_t)b] = std::pow(10.0, (boost - cut) / 20.0);
        }
        gain[(size_t)b] += (target_[(size_t)b] - gain[(size_t)b]) * smooth_;
        wetL += bandsL_[(size_t)b] * gain[(size_t)b];
        wetR += bandsR_[(size_t)b] * gain[(size_t)b];
    }
    tick_ = (tick_ + 1) & 15;
    depth += (depthTarget - depth) * smooth_;
    const double compensation = autoGain.next(inL, inR, wetL, wetR);
    outL = inL + depth * (wetL * compensation - inL);
    outR = inR + depth * (wetR * compensation - inR);
}

void OttCompressor::process(float* L, float* R, int n) {
    for (int i = 0; i < n; ++i) {
        double l, r; processSample(L[i], R[i], l, r);
        L[i] = (float)l; R[i] = (float)r;
    }
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
    // Half-band: force the structurally-zero taps to exact zero. sin(PI*m/2)
    // for an even integer m evaluates to ~1e-16, not 0, so the direct form was
    // paying for taps ~320 dB down. Zeroing them makes the polyphase split
    // below exact and the skipped multiplies free of any error.
    if (taps % 2 == 1) {
        int c = (taps - 1) / 2;
        for (int i = 0; i < taps; i++)
            if (i != c && ((i - c) % 2) == 0) h[(size_t)i] = 0.0;
    }

    // compact non-zero tap list for the generic direct form
    nzTap_.clear(); nzOff_.clear();
    for (int i = 0; i < taps; i++)
        if (h[(size_t)i] != 0.0) { nzTap_.push_back(h[(size_t)i]); nzOff_.push_back(i); }

    // polyphase split; np_ covers the longest history either entry point reads
    pe_.assign((size_t)((taps + 1) / 2), 0.0);
    po_.assign((size_t)(taps / 2), 0.0);
    for (size_t j = 0; j < pe_.size(); j++) pe_[j] = h[2 * j];
    for (size_t j = 0; j < po_.size(); j++) po_[j] = h[2 * j + 1];
    auto range = [](const std::vector<double>& v, int& a, int& b) {
        a = 0; b = -1;
        for (int j = 0; j < (int)v.size(); j++)
            if (v[(size_t)j] != 0.0) { if (b < 0) a = j; b = j; }
    };
    range(pe_, peA_, peB_);
    range(po_, poA_, poB_);
    np_ = (int)std::max(pe_.size(), po_.size() + 1);
    hx_.assign((size_t)(2 * np_), 0.0);
    he_.assign((size_t)(2 * np_), 0.0);
    ho_.assign((size_t)(2 * np_), 0.0);
    px_ = pd_ = 0;

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
    meter_.prepare(sampleRate);
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

    driveWet_.setTime(0.02, sr_); driveDry_.setTime(0.02, sr_);
    chWet_.setTime(0.02, sr_); chDry_.setTime(0.02, sr_);
    dlTime_.setTime(0.08, sr_); dlFb_.setTime(0.02, sr_);
    dlWet_.setTime(0.02, sr_); dlDry_.setTime(0.02, sr_);
    dlSat_.setTime(0.03, sr_); dlWow_.setTime(0.03, sr_);
    dlFlutter_.setTime(0.03, sr_); dlWidth_.setTime(0.03, sr_); dlMode_.setTime(0.03, sr_);
    verbWet_.setTime(0.02, sr_); verbDry_.setTime(0.02, sr_);
    masterGain_.setTime(0.02, sr_);
    driveDry_.snap(1); chDry_.snap(1); dlDry_.snap(1); verbDry_.snap(1); dlWidth_.snap(1);

    // ~15 ms of chunk-rate glide for the coefficient-bearing parameters
    rampSteps_ = (int)std::lround(0.015 * sr_ / (double)kCoefChunk);
    setRampSteps(rampSteps_);
    primed_ = false; // the first setParams after a prepare() lands instantly

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
    dlDamp_.lowpass(4500, 0.707, sr_); dlDampR_.lowpass(4500, 0.707, sr_);
    dlHpL_.highpass(45, 0.707, sr_); dlHpR_.highpass(45, 0.707, sr_);
    ott_.prepare(sr_); comp_.prepare(sr_);
    headroomInput_.prepare(sr_); headroomEq_.prepare(sr_); headroomOtt_.prepare(sr_);
    headroomComp_.prepare(sr_); headroomDrive_.prepare(sr_); headroomChorus_.prepare(sr_);
    headroomDelay_.prepare(sr_); headroomReverb_.prepare(sr_); delayFeedbackGuard_.prepare(sr_);

    // WebAudio's DynamicsCompressor applies spec-defined makeup gain
    // ((1/c(1))^0.6, c = static curve at 0 dBFS). The web app's limiter IS that
    // node, so keep its ~4.5 dB makeup ahead of the lookahead limiter or the
    // plugin sits under the web app's loudness.
    double c1 = std::pow(1.0 / kLimThr, 1.0 / kLimRatio - 1.0);
    lim_.prepare(sr_, std::pow(1.0 / c1, 0.6));

    reset(); // full state clear: re-prepare must never keep stale recursive state
}

void Fx::reset() {
    meter_.reset();
    chDl1_.reset(); chDl2_.reset(); dlL_.reset(); dlR_.reset();
    dryL_.reset(); dryR_.reset();
    for (auto& c : combL_) c.reset();
    for (auto& c : combR_) c.reset();
    for (auto& a : apL_) a.reset();
    for (auto& a : apR_) a.reset();
    dcL_.reset(); dcR_.reset(); dlDamp_.reset(); dlDampR_.reset(); dlHpL_.reset(); dlHpR_.reset();
    eqLoL_.reset(); eqLoR_.reset(); eqMidL_.reset(); eqMidR_.reset();
    eqMid2L_.reset(); eqMid2R_.reset(); eqHiL_.reset(); eqHiR_.reset();
    up1L_.reset(); up2L_.reset(); dn2L_.reset(); dn1L_.reset();
    up1R_.reset(); up2R_.reset(); dn2R_.reset(); dn1R_.reset();
    ott_.reset(); driveColorL_.reset(); driveColorR_.reset(); comp_.reset();
    headroomInput_.reset(); headroomEq_.reset(); headroomOtt_.reset(); headroomComp_.reset();
    headroomDrive_.reset(); headroomChorus_.reset(); headroomDelay_.reset(); headroomReverb_.reset();
    delayFeedbackGuard_.reset();
    lim_.reset();
    chPhase_ = 0;
    driveSilent_ = chorusGated_ = delayGated_ = verbGated_ = compGated_ = false;
    // settle smoothers at their targets so no stale ramp survives a re-prepare
    driveWet_.snap(driveWet_.target); driveDry_.snap(driveDry_.target);
    chWet_.snap(chWet_.target); chDry_.snap(chDry_.target);
    dlTime_.snap(dlTime_.target); dlFb_.snap(dlFb_.target);
    dlWet_.snap(dlWet_.target); dlDry_.snap(dlDry_.target);
    verbWet_.snap(verbWet_.target); verbDry_.snap(verbDry_.target);
    dlSat_.snap(dlSat_.target); dlWow_.snap(dlWow_.target); dlFlutter_.snap(dlFlutter_.target);
    dlWidth_.snap(dlWidth_.target); dlMode_.snap(dlMode_.target);
    dlTone_ = dlToneTarget_; tapeClock_ = 0; dlDriftL_ = dlDriftR_ = 0; delayInitialized_ = false;
    masterGain_.snap(masterGain_.target);
    // same for the chunk ramps: land on target and rebuild once on the next chunk
    eqLoDb_.snapToTarget(); eqMidDb_.snapToTarget(); eqHiDb_.snapToTarget();
    eqMidF2_.snapToTarget(); driveAmt_.snapToTarget();
    chRateT_.snapToTarget(); chDepthT_.snapToTarget(); verbSize_.snapToTarget();
    eqExtDirty_ = true; forceCoefs_ = true;
}

static inline float mixGate(bool on, float amount, bool wet) {
    if (wet) return on ? (float)std::sin(amount * PI / 2) : 0.0f;
    return on ? (float)std::cos(amount * PI / 2) : 1.0f;
}

void Fx::setParams(const ParamArray& p, double tempoBpm) {
    // Host automation arrives once per block. Everything whose coefficients are
    // expensive to rebuild only gets a *target* here; advanceCoefs() steps it
    // and rebuilds per kCoefChunk samples inside process(). Call sites are
    // unchanged — setParams() is still safe to call once per block.

    // Four-band tone EQ. The original gain ids remain the ramped controls;
    // appended controls add the web band's frequency, Q, type and bypass.
    bool eqOn = p[FXEQ_ON] > 0.5f;
    eqLoDb_.setTarget(eqOn && p[FXEQLON] > 0.5f ? p[FXEQ_LOW] : 0.0f);
    eqMidDb_.setTarget(eqOn && p[FXEQMON] > 0.5f ? p[FXEQ_MID] : 0.0f);
    eqHiDb_.setTarget(eqOn && p[FXEQHON] > 0.5f ? p[FXEQ_HIGH] : 0.0f);
    eqMidHz_ = std::max(20.0f, std::min(20000.0f, p[FXEQ_MFREQ]));
    eqMidF2_.setTarget(std::log2(eqMidHz_)); // log-domain sweep
    eqLoFreq_ = std::max(20.0f, std::min(20000.0f, p[FXEQLFREQ]));
    eqMid2Freq_ = std::max(20.0f, std::min(20000.0f, p[FXEQM2FREQ]));
    eqHiFreq_ = std::max(20.0f, std::min(20000.0f, p[FXEQHFREQ]));
    eqMid2Db_ = eqOn && p[FXEQM2ON] > 0.5f ? std::max(-15.0f, std::min(15.0f, p[FXEQMID2])) : 0.0f;
    eqLoQ_ = std::max(0.2f, std::min(12.0f, p[FXEQLQ]));
    eqMidQ_ = std::max(0.2f, std::min(12.0f, p[FXEQMQ]));
    eqMid2Q_ = std::max(0.2f, std::min(12.0f, p[FXEQM2Q]));
    eqHiQ_ = std::max(0.2f, std::min(12.0f, p[FXEQHQ]));
    eqLoType_ = std::max(0, std::min(2, (int)p[FXEQLTYPE]));
    eqMidType_ = std::max(0, std::min(2, (int)p[FXEQMTYPE]));
    eqMid2Type_ = std::max(0, std::min(2, (int)p[FXEQM2TYPE]));
    eqHiType_ = std::max(0, std::min(2, (int)p[FXEQHTYPE]));
    eqLoOn_ = p[FXEQLON] > 0.5f; eqMidOn_ = p[FXEQMON] > 0.5f;
    eqMid2On_ = p[FXEQM2ON] > 0.5f; eqHiOn_ = p[FXEQHON] > 0.5f;
    eqExtDirty_ = true;

    // drive
    driveAmt_.setTarget(p[FXDRIVE_AMT]);
    bool dOn = p[FXDRIVE_ON] > 0.5f;
    driveWet_.target = mixGate(dOn, p[FXDRIVE_MIX], true);
    driveDry_.target = mixGate(dOn, p[FXDRIVE_MIX], false);

    // chorus
    chRateT_.setTarget(p[FXCHORUS_RATE]);
    chDepthT_.setTarget(p[FXCHORUS_DEPTH]);
    bool cOn = p[FXCHORUS_ON] > 0.5f;
    chorusOff_ = !cOn;
    chWet_.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8f, true);
    chDry_.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8f, false);

    // Tape delay. Sync divisions are quarter-note cycles, matching the web.
    static constexpr double divs[] = {1.0, 1.5, 0.5, 0.75, 1.0 / 3.0, 0.25};
    int div = std::max(0, std::min(5, (int)p[FXDELAY_DIV]));
    tempoBpm_ = std::isfinite(tempoBpm) && tempoBpm > 0.0
        ? tempoBpm : std::max(1.0, (double)p[SEQ_BPM]);
    double bpm = tempoBpm_;
    dlTime_.target = (float)std::max(0.02, std::min(1.5,
        p[FXDELAY_SYNC] > 0.5f ? 60.0 / bpm * divs[div] : (double)p[FXDELAY_TIME]));
    if (!delayInitialized_) { dlTime_.snap(dlTime_.target); delayInitialized_ = true; }
    dlFb_.target = std::max(0.0f, std::min(0.92f, p[FXDELAY_FB]));
    dlToneTarget_ = std::max(400.0f, std::min(12000.0f, p[FXDELAY_TONE] > 0 ? p[FXDELAY_TONE] : 4500.0f));
    dlSat_.target = std::max(0.0f, std::min(1.0f, p[FXDELAY_SAT]));
    dlWow_.target = std::max(0.0f, std::min(1.0f, p[FXDELAY_WOW]));
    dlFlutter_.target = std::max(0.0f, std::min(1.0f, p[FXDELAY_FLUTTER]));
    dlWidth_.target = std::max(0.0f, std::min(1.0f, p[FXDELAY_WIDTH]));
    dlMode_.target = p[FXDELAY_MODE] > 0.5f ? 1.0f : 0.0f;
    bool delOn = p[FXDELAY_ON] > 0.5f;
    delayOff_ = !delOn;
    dlWet_.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85f, true);
    dlDry_.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85f, false);

    // reverb — SIZE maps to roomsize/decay (longer & brighter tail with size)
    verbSize_.setTarget(p[FXREVERB_SIZE]);
    bool rOn = p[FXREVERB_ON] > 0.5f;
    verbOff_ = !rOn;
    verbWet_.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9f, true);
    verbDry_.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9f, false);

    compOff_ = p[FXCOMP_ON] <= 0.5f;
    driveColorL_.setParams(p[FXDRIVE_TYPE], p[FXDRIVE_TONE]);
    driveColorR_.setParams(p[FXDRIVE_TYPE], p[FXDRIVE_TONE]);
    comp_.setParams(!compOff_, p[FXCOMP_THR], p[FXCOMP_ATT], p[FXCOMP_REL], p[FXCOMP_RATIO]);
    ott_.setParams(p[FXOTT_ON] > 0.5f, p[FXOTT_DEPTH], p[FXOTT_TIME], p[FXOTT_UP], p[FXOTT_DOWN]);

    float vol = p[MASTER_VOLUME];
    masterGain_.target = vol * vol * 1.6f;

    // A patch load (the first setParams after prepare()) must land instantly —
    // only later changes are automation and get the 15 ms glide.
    if (!primed_) {
        primed_ = true;
        eqLoDb_.snapToTarget(); eqMidDb_.snapToTarget(); eqHiDb_.snapToTarget();
        eqMidF2_.snapToTarget(); driveAmt_.snapToTarget();
        chRateT_.snapToTarget(); chDepthT_.snapToTarget(); verbSize_.snapToTarget();
        forceCoefs_ = true;
    }
}

void Fx::setRampSteps(int steps) {
    for (ChunkRamp* r : { &eqLoDb_, &eqMidDb_, &eqHiDb_, &eqMidF2_,
                          &driveAmt_, &chRateT_, &chDepthT_, &verbSize_ })
        r->setSteps(steps);
}

// Advance the chunk ramps and rebuild only the coefficient sets whose ramp
// actually moved. In the steady state (targets reached) every branch is false,
// so this costs eight comparisons per chunk and nothing else.
void Fx::advanceCoefs(bool force) {
    bool lo = eqLoDb_.next(), mid = eqMidDb_.next();
    bool hi = eqHiDb_.next(), mf = eqMidF2_.next();
    if (force || lo || mid || hi || mf || eqExtDirty_) {
        double loDb = eqLoDb_.cur, midDb = eqMidDb_.cur, hiDb = eqHiDb_.cur;
        // exp2(log2(f)) is off by an ulp or two, so land on the exact request
        // once the ramp is at rest; only the glide itself runs in log space.
        double mFreq = eqMidF2_.left > 0 ? std::exp2((double)eqMidF2_.cur) : (double)eqMidHz_;
        auto design = [&](Biquad& b, double f, double q, double gain, int type) {
            if (type == 0) b.lowShelf(f, gain, sr_, q);
            else if (type == 2) b.highShelf(f, gain, sr_, q);
            else b.peaking(f, q, gain, sr_);
        };
        design(eqLoL_, eqLoFreq_, eqLoQ_, loDb, eqLoType_); design(eqLoR_, eqLoFreq_, eqLoQ_, loDb, eqLoType_);
        design(eqMidL_, mFreq, eqMidQ_, midDb, eqMidType_); design(eqMidR_, mFreq, eqMidQ_, midDb, eqMidType_);
        design(eqMid2L_, eqMid2Freq_, eqMid2Q_, eqMid2Db_, eqMid2Type_); design(eqMid2R_, eqMid2Freq_, eqMid2Q_, eqMid2Db_, eqMid2Type_);
        design(eqHiL_, eqHiFreq_, eqHiQ_, hiDb, eqHiType_); design(eqHiR_, eqHiFreq_, eqHiQ_, hiDb, eqHiType_);
        eqExtDirty_ = false;
    }

    if (driveAmt_.next() || force) {
        float amt = driveAmt_.cur;
        drivePre_ = 1 + amt * 2;
        driveK_ = 1 + amt * 12;
        driveNorm_ = 1.0f / (drivePre_ * std::tanh(driveK_));
    }

    bool cr = chRateT_.next(), cd = chDepthT_.next();
    if (force || cr || cd) { chRate_ = chRateT_.cur; chDepth_ = chDepthT_.cur; }

    if (verbSize_.next() || force) {
        float size = verbSize_.cur;
        roomSize_ = 0.7f + size * 0.28f;
        float damp = 0.4f - size * 0.2f;
        for (size_t i = 0; i < 8; i++) {
            combL_[i].feedback = combR_[i].feedback = roomSize_;
            combL_[i].damp1 = combR_[i].damp1 = damp;
            combL_[i].damp2 = combR_[i].damp2 = 1 - damp;
        }
    }
}

// One channel through the 4x oversampled shaper: 2x half-band interpolate,
// 2x again, tanh at 4x, then the mirrored decimators. The polyphase entry
// points carry the zero-stuff gain (x2 per stage) internally and drop the
// multiplies against the stuffed zeros and the discarded decimation phase;
// decimation keeps the phase aligned with the integer kDriveLatency group
// delay, so the chain latency is exactly what it was.
float Fx::driveChannel(HalfBandFir& u1, HalfBandFir& u2, HalfBandFir& d2, HalfBandFir& d1, double x, DriveColor& color) {
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

void Fx::process(float* L, float* R, int n) {
    // The ramp owns a sample-clocked duration established at prepare(). Do not
    // resize it from the host block length: a single 1024-sample callback and
    // eight 128-sample callbacks must consume the same automation trajectory.

    // Gate only when OFF; mix==0 while ON must keep state accumulation alive —
    // every stage below has a tail or a feedback loop that has to keep running.
    // The drive is the exception: it has neither, so a zero wet gain makes its
    // whole 4x path dead weight whether the stage is OFF *or* just mixed to
    // zero. driveSilent covers both (mixGate returns 0 wet in each case), so it
    // subsumes the old OFF-only gate. Matches src/engine/worklet.js.
    bool driveSilent = driveWet_.target == 0.0f && std::abs(driveWet_.cur) < 1.0e-6f;
    bool chorusGate = chorusOff_ && chWet_.target == 0.0f && std::abs(chWet_.cur) < 1.0e-6f;
    bool delayGate = delayOff_ && dlWet_.target == 0.0f && std::abs(dlWet_.cur) < 1.0e-6f;
    bool verbGate = verbOff_ && verbWet_.target == 0.0f && std::abs(verbWet_.cur) < 1.0e-6f;

    if (driveSilent && !driveSilent_) {
        // Entering silence: land the mix exactly (wet 0 / dry 1 is the target in
        // both cases) and clear the FIR histories so the shaper restarts from a
        // known state. On leaving, it refills in kDriveLatency samples (0.56 ms)
        // under a wet gain still ramping up over 20 ms.
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
        dlL_.reset(); dlR_.reset(); dlDamp_.reset(); dlDampR_.reset(); dlHpL_.reset(); dlHpR_.reset();
        delayFeedbackGuard_.reset(); tapeClock_ = 0; dlDriftL_ = dlDriftR_ = 0;
    }
    if (!delayGate) {
        dlTone_ += (dlToneTarget_ - dlTone_) * (1.0 - std::exp(-(double)n / (0.03 * sr_)));
        dlDamp_.lowpass(dlTone_, 0.707, sr_); dlDampR_.lowpass(dlTone_, 0.707, sr_);
    }
    if (verbGate && !verbGated_) {
        verbWet_.snap(0); verbDry_.snap(1);
        for (auto& c : combL_) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.filt = 0.0f; }
        for (auto& c : combR_) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.filt = 0.0f; }
        for (auto& a : apL_) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
        for (auto& a : apR_) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    }

    driveSilent_ = driveSilent;
    chorusGated_ = chorusGate;
    delayGated_ = delayGate;
    verbGated_ = verbGate;
    // WebCompressor owns its wet fade and state reset, so it must continue to
    // receive samples while being switched off.
    compGated_ = false;

    // Shared web headroom stage at the graph input. It also sanitizes a bad
    // host sample before recursive DSP sees it.
    headroomInput_.process(L, R, n);

    // The sample loop runs in kCoefChunk-sample chunks; the ramped EQ / drive /
    // reverb coefficients are rebuilt once per chunk, so automation of those
    // follows a 15 ms glide instead of stepping once per host block.
    for (int off = 0; off < n; off += kCoefChunk) {
        const int end = std::min(off + kCoefChunk, n);
        advanceCoefs(forceCoefs_);
        forceCoefs_ = false;

        for (int i = off; i < end; i++) {
            float l = L[i], r = R[i];

            // ---- four-band tone EQ (first FX; 0 dB coeffs = transparent) ----
            l = (float)eqHiL_.process(eqMid2L_.process(eqMidL_.process(eqLoL_.process(l))));
            r = (float)eqHiR_.process(eqMid2R_.process(eqMidR_.process(eqLoR_.process(r))));
            double gEq = headroomEq_.gainFor(l, r); l *= (float)gEq; r *= (float)gEq;

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
            if (!driveSilent_) {
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

            // ---- stereo tape echo ----
            if (!delayGated_) {
                double dt = dlTime_.next();
                float fb = dlFb_.next();
                float wow = dlWow_.next() * 0.0025f, flutter = dlFlutter_.next() * 0.00022f;
                double t = tapeClock_ / sr_;
                dlDriftL_ = wow * (0.72 * std::sin(2 * PI * 0.23 * t) + 0.28 * std::sin(2 * PI * 0.37 * t))
                         + flutter * std::sin(2 * PI * 6.7 * t);
                dlDriftR_ = wow * (0.72 * std::sin(2 * PI * 0.23 * t + 1.8) + 0.28 * std::sin(2 * PI * 0.41 * t + 0.7))
                         + flutter * std::sin(2 * PI * 8.3 * t + 1.1);
                tapeClock_ += 1.0;
                float dL = dlL_.readHermite(std::max(0.005, dt + dlDriftL_) * sr_);
                float dR = dlR_.readHermite(std::max(0.005, dt + dlDriftR_) * sr_);
                float mono = 0.5f * (l + r);
                float mode = dlMode_.next(), sat = dlSat_.next(), k = 1.0f + sat * 4.0f;
                float recL = mono + mode * (l - mono) + fb * (dR + mode * (dL - dR));
                float recR = mode * r + fb * (dL + mode * (dR - dL));
                recL += sat * ((float)std::tanh(k * recL) / k - recL);
                recR += sat * ((float)std::tanh(k * recR) / k - recR);
                float fL = (float)dlDamp_.process(dlHpL_.process(recL));
                float fR = (float)dlDampR_.process(dlHpR_.process(recR));
                double gFb = delayFeedbackGuard_.gainFor(fL, fR);
                dlL_.write(fL * (float)gFb); dlR_.write(fR * (float)gFb);
                float wet = dlWet_.next(), dry = dlDry_.next();
                float mid = 0.5f * (dL + dR), side = 0.5f * (dL - dR) * dlWidth_.next();
                meter_.stereo(FxTelemetry::echoL, wet * (mid + side), wet * (mid - side));
                l = dry * l + wet * (mid + side);
                r = dry * r + wet * (mid - side);
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
            meter_.finish(ott_, comp_, dlTime_.cur, dlDriftL_, dlDriftR_);
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
}

} // namespace fable
