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

// ---------------- Freeverb tuning (classic constants, scaled to sr) ----------------
static const int COMB_TUNE[8]   = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int AP_TUNE[4]     = {556, 441, 341, 225};
static const int STEREO_SPREAD  = 23;

void Fx::prepare(double sampleRate) {
    sr_ = sampleRate;
    double scale = sr_ / 44100.0;

    for (int i = 0; i < 8; i++) {
        combL_[i].prepare((int)(COMB_TUNE[i] * scale));
        combR_[i].prepare((int)((COMB_TUNE[i] + STEREO_SPREAD) * scale));
    }
    for (int i = 0; i < 4; i++) {
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
    masterGain_.setTime(0.02, sr_);
    driveDry_.snap(1); chDry_.snap(1); dlDry_.snap(1); verbDry_.snap(1);

    dcL_.highpass(8, 0.707, sr_);
    dcR_.highpass(8, 0.707, sr_);
    // 2x oversampling anti-image / anti-alias filters, cutoff just below base Nyquist.
    upL_.lowpass(sr_ * 0.45, 0.707, sr_ * 2);
    upR_.lowpass(sr_ * 0.45, 0.707, sr_ * 2);
    downL_.lowpass(sr_ * 0.45, 0.707, sr_ * 2);
    downR_.lowpass(sr_ * 0.45, 0.707, sr_ * 2);
    dlDamp_.lowpass(4500, 0.707, sr_);

    // Limiter envelope coefficients (attack 2ms, release 220ms).
    limAtk_ = 1 - std::exp(-1.0 / (0.002 * sr_));
    limRel_ = 1 - std::exp(-1.0 / (0.22 * sr_));

    // WebAudio's DynamicsCompressor applies spec-defined makeup gain
    // ((1/c(1))^0.6, c = static curve at 0 dBFS). The web app's limiter IS that
    // node, so match it here or the plugin sits ~4.5 dB under the web app.
    double c1 = std::pow(1.0 / kLimThr, 1.0 / kLimRatio - 1.0);
    limMakeup_ = std::pow(1.0 / c1, 0.6);
}

void Fx::reset() {
    chDl1_.reset(); chDl2_.reset(); dlL_.reset(); dlR_.reset();
    for (auto& c : combL_) std::fill(c.buf.begin(), c.buf.end(), 0.0f);
    for (auto& c : combR_) std::fill(c.buf.begin(), c.buf.end(), 0.0f);
    for (auto& a : apL_) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    for (auto& a : apR_) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    dcL_.reset(); dcR_.reset(); dlDamp_.reset();
    upL_.reset(); upR_.reset(); downL_.reset(); downR_.reset();
    limEnv_ = 0;
}

static inline float mixGate(bool on, float amount, bool wet) {
    if (wet) return on ? (float)std::sin(amount * PI / 2) : 0.0f;
    return on ? (float)std::cos(amount * PI / 2) : 1.0f;
}

void Fx::setParams(const ParamArray& p) {
    // drive
    float amt = p[FXDRIVE_AMT];
    driveK_ = 1 + amt * 24;
    drivePre_ = 1 + amt * 2;
    bool dOn = p[FXDRIVE_ON] > 0.5f;
    driveWet_.target = mixGate(dOn, p[FXDRIVE_MIX], true);
    driveDry_.target = mixGate(dOn, p[FXDRIVE_MIX], false);

    // chorus
    chRate_ = p[FXCHORUS_RATE];
    chDepth_ = p[FXCHORUS_DEPTH];
    bool cOn = p[FXCHORUS_ON] > 0.5f;
    chWet_.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8f, true);
    chDry_.target = mixGate(cOn, p[FXCHORUS_MIX] * 0.8f, false);

    // delay
    dlTime_.target = p[FXDELAY_TIME];
    dlFb_.target = p[FXDELAY_FB];
    bool delOn = p[FXDELAY_ON] > 0.5f;
    dlWet_.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85f, true);
    dlDry_.target = mixGate(delOn, p[FXDELAY_MIX] * 0.85f, false);

    // reverb — SIZE maps to roomsize/decay (longer & brighter tail with size)
    float size = p[FXREVERB_SIZE];
    roomSize_ = 0.7f + size * 0.28f;
    float damp = 0.4f - size * 0.2f;
    for (int i = 0; i < 8; i++) {
        combL_[i].feedback = combR_[i].feedback = roomSize_;
        combL_[i].damp1 = combR_[i].damp1 = damp;
        combL_[i].damp2 = combR_[i].damp2 = 1 - damp;
    }
    bool rOn = p[FXREVERB_ON] > 0.5f;
    verbWet_.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9f, true);
    verbDry_.target = mixGate(rOn, p[FXREVERB_MIX] * 0.9f, false);

    float vol = p[MASTER_VOLUME];
    masterGain_.target = vol * vol * 1.6f;
}

float Fx::shape(float x) const {
    float c = std::max(-1.0f, std::min(1.0f, x));
    return (float)(std::tanh(c * driveK_) / std::tanh(driveK_));
}

void Fx::process(float* L, float* R, int n) {
    for (int i = 0; i < n; i++) {
        float l = L[i], r = R[i];

        // ---- drive (2x oversampled tanh waveshaper) ----
        {
            float wet = driveWet_.next(), dry = driveDry_.next();
            // upsample (zero-stuff x2, gain 2), shape, downsample
            float u0 = (float)upL_.process(2.0 * drivePre_ * l);
            float u1 = (float)upL_.process(0.0);
            float s0 = shape(u0), s1 = shape(u1);
            downL_.process(s0);
            float dl = (float)downL_.process(s1);
            float ru0 = (float)upR_.process(2.0 * drivePre_ * r);
            float ru1 = (float)upR_.process(0.0);
            float rs0 = shape(ru0), rs1 = shape(ru1);
            downR_.process(rs0);
            float dr = (float)downR_.process(rs1);
            l = dry * l + wet * dl;
            r = dry * r + wet * dr;
        }

        // ---- chorus (two modulated taps, stereo) ----
        {
            chPhase_ += chRate_ / sr_;
            if (chPhase_ >= 1) chPhase_ -= 1;
            double lfo = std::sin(2 * PI * chPhase_);
            double depth = 0.0008 + chDepth_ * 0.0045;
            float mono = 0.5f * (l + r);
            chDl1_.write(mono);
            chDl2_.write(mono);
            double d1 = (0.012 + depth * lfo) * sr_;
            double d2 = (0.017 - depth * 0.8 * lfo) * sr_;
            float c1 = chDl1_.read(d1);
            float c2 = chDl2_.read(d2);
            float wet = chWet_.next(), dry = chDry_.next();
            l = dry * l + wet * c1;
            r = dry * r + wet * c2;
        }

        // ---- ping-pong delay ----
        {
            double dt = dlTime_.next() * sr_;
            float fb = dlFb_.next();
            float dL = dlL_.read(dt);
            float dR = dlR_.read(dt);
            float mono = 0.5f * (l + r);
            dlL_.write(mono + fb * dR);
            dlR_.write((float)dlDamp_.process(fb * dL));
            float wet = dlWet_.next(), dry = dlDry_.next();
            l = dry * l + wet * dL;
            r = dry * r + wet * dR;
        }

        // ---- reverb (Freeverb) ----
        {
            float input = (l + r) * 0.015f; // fixed input gain (Freeverb convention)
            float outL = 0, outR = 0;
            for (int c = 0; c < 8; c++) { outL += combL_[c].process(input); outR += combR_[c].process(input); }
            for (int a = 0; a < 4; a++) { outL = apL_[a].process(outL); outR = apR_[a].process(outR); }
            float wet = verbWet_.next(), dry = verbDry_.next();
            l = dry * l + wet * outL;
            r = dry * r + wet * outR;
        }

        // ---- master gain ----
        float g = masterGain_.next();
        l *= g; r *= g;

        // ---- DC block ----
        l = (float)dcL_.process(l);
        r = (float)dcR_.process(r);

        // ---- safety limiter (feed-forward peak compressor) ----
        {
            double peak = std::max(std::abs((double)l), std::abs((double)r));
            double coef = peak > limEnv_ ? limAtk_ : limRel_;
            limEnv_ += (peak - limEnv_) * coef;
            // static curve: threshold -8 dB, ratio 14 (hard knee; the web's
            // knee=4 only differs inside a 4 dB window below threshold)
            double gain = 1.0;
            if (limEnv_ > kLimThr) {
                double over = limEnv_ / kLimThr;             // linear overshoot
                gain = std::pow(over, 1.0 / kLimRatio - 1.0);
            }
            l *= (float)(gain * limMakeup_);
            r *= (float)(gain * limMakeup_);
        }

        L[i] = l; R[i] = r;
    }
}

} // namespace fable
