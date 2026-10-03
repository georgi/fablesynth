// LAB — five experimental stages that run after DRIVE and before CHORUS, so
// the echo and reverb tail whatever the lab produces:
//
//   CRUSH  -> RESO -> SHIFT -> SPRAY -> GLITCH
//
// CRUSH   sample-and-hold decimator + bit quantizer. Hold edges fall at their
//         exact sub-sample time (input interpolated at the edge, the output
//         sample box-filtered across it), so non-integer rates give clean
//         images instead of sample-grid jitter. CHAOS jitters the clock and
//         nudges held values by one quantization step.
// RESO    four damped feedback combs tuned to a chord. Each loop delay is
//         solved for the damping filter's and the Hermite read's phase at the
//         fundamental, so the combs ring in tune up to the top of the range.
// SHIFT   single-sideband frequency shifter (allpass Hilbert pair). The input
//         is band-limited (8th-order) so the shifted spectrum stays inside
//         0..Nyquist (no fold-over above, no wrap through 0 Hz below). Feedback runs through
//         a 1/16-note delay: each echo is shifted again and spirals.
// SPRAY   granular cloud. The grain buffer is low-passed (8th-order) for the
//         fastest grain, so pitched-up grains do not alias; Hann windows,
//         Hermite reads.
// GLITCH  tempo-synced beat repeat. A half-bar window may record DIV from the
//         downbeat (the first pass is the live signal) and repeat it. Repeats
//         splice with equal-power crossfades and read through a band-limited
//         (Kaiser-windowed sinc) interpolator, so DRIFT re-pitching is clean.
//
// This file is the reference implementation. src/engine/worklet.js carries a
// line-for-line port (class LabFx); keep the two in step. Buffers are float,
// arithmetic is double on both sides, and the RNG is the same xorshift32, so
// web and native output agree to rounding (see the lab parity fixture).
// All buffers are sized in prepare(); process() allocates nothing. A stage
// whose wet gain has faded to zero is skipped, then cleared once.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fable {

struct LabSettings {
    bool  crushOn = false;  float crushBits = 6, crushRate = 6000, crushChaos = 0.2f, crushMix = 1;
    bool  resoOn = false;   float resoNote = 48; int resoChord = 2; float resoDecay = 0.7f, resoMix = 0.5f;
    bool  shiftOn = false;  float shiftHz = 60, shiftFb = 0.5f, shiftSpread = 0.5f, shiftMix = 0.5f;
    bool  sprayOn = false;  float sprayPitch = 12, sprayDensity = 12, sprayScatter = 0.4f, sprayMix = 0.45f;
    bool  glitchOn = false; int glitchDiv = 2; float glitchChance = 0.35f, glitchDrift = 0, glitchMix = 1;
};

// RESO chord intervals (semitones above NOTE), in fx.reso.chord order.
inline const std::array<std::array<int, 4>, 6>& labResoChords() {
    static const std::array<std::array<int, 4>, 6> chords {{
        {{0, 12, 24, 36}}, // OCTAVES
        {{0, 7, 12, 19}},  // FIFTHS
        {{0, 3, 7, 10}},   // MINOR 7
        {{0, 4, 7, 14}},   // MAJOR 9
        {{0, 5, 7, 12}},   // SUS 4
        {{0, 6, 12, 18}},  // TRITONE
    }};
    return chords;
}
// GLITCH slice lengths in beats, in fx.glitch.div order (1/4 .. 1/64).
inline double labGlitchBeats(int div) {
    static constexpr double beats[] = {1.0, 0.5, 0.25, 0.125, 0.0625};
    return beats[std::clamp(div, 0, 4)];
}

namespace lab {
constexpr double kPi = 3.141592653589793;

struct Rng {
    uint32_t s = 0x9e3779b9u;
    double uni() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double)(s >> 8) * (1.0 / 16777216.0); }
    double bi() { return uni() * 2.0 - 1.0; }
};

// One-pole parameter smoother (20 ms), stepped per sample.
struct Glide {
    double cur = 0, target = 0, coef = 0.001;
    double next() { cur += (target - cur) * coef; return cur; }
    void snap() { cur = target; }
};

// RBJ biquad, transposed direct form II.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(bool high, double hz, double q, double sr) {
        const double w = 2 * kPi * hz / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        const double k = high ? (1 + c) / 2 : (1 - c) / 2;
        b0 = k / a0; b1 = (high ? -2 * k : 2 * k) / a0; b2 = k / a0;
        a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    double process(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};
// Butterworth cascades: fourth order (two biquads) and eighth order (four).
struct Butter4 {
    Biquad s1, s2;
    void set(bool high, double hz, double sr) { s1.set(high, hz, 0.5411961001461970, sr); s2.set(high, hz, 1.3065629648763766, sr); }
    double process(double x) { return s2.process(s1.process(x)); }
    void reset() { s1.reset(); s2.reset(); }
};
struct Butter8 {
    std::array<Biquad, 4> s;
    void set(bool high, double hz, double sr) {
        static constexpr double q[4] = {0.5097955791041592, 0.6013448869350453, 0.8999762231364156, 2.5629154477415055};
        for (int i = 0; i < 4; ++i) s[(size_t)i].set(high, hz, q[i], sr);
    }
    double process(double x) { for (auto& b : s) x = b.process(x); return x; }
    void reset() { for (auto& b : s) b.reset(); }
};

// Power-of-two circular float buffer addressed by absolute sample position.
struct Ring {
    std::vector<float> b; long long mask = 0, w = 0;
    void prepare(int minSize) { int n = 1; while (n < minSize) n <<= 1; b.assign((size_t)n, 0.0f); mask = n - 1; w = 0; }
    void clear() { std::fill(b.begin(), b.end(), 0.0f); w = 0; }
    void write(double x) { b[(size_t)(w & mask)] = (float)x; ++w; }
    double at(long long i) const { return b[(size_t)(i & mask)]; }
    // 4-point Hermite at absolute position p (needs p-1 .. p+2 written).
    double hermite(double p) const {
        const double fl = std::floor(p); const long long i = (long long)fl; const double t = p - fl;
        const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
};

// Kaiser-windowed sinc (beta 7.5), 8 zero crossings each side, 512 table
// points per crossing, linearly interpolated.
struct SincTable {
    static constexpr int kHalf = 8, kRes = 512;
    std::vector<double> h;
    static double bessel0(double x) {
        double sum = 1, term = 1;
        for (int k = 1; k < 50; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; if (term < 1e-15 * sum) break; }
        return sum;
    }
    void build() {
        const double beta = 7.5, norm = bessel0(beta);
        h.assign((size_t)(kHalf * kRes + 2), 0.0);
        for (int i = 0; i <= kHalf * kRes; ++i) {
            const double x = (double)i / kRes, r = x / kHalf;
            const double sinc = i == 0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            h[(size_t)i] = sinc * bessel0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / norm;
        }
    }
    double tap(double x) const {
        x = std::abs(x) * kRes;
        const int i = (int)x; if (i >= kHalf * kRes) return 0;
        return h[(size_t)i] + (x - i) * (h[(size_t)i + 1] - h[(size_t)i]);
    }
    // Band-limited read at absolute position p. fc is the cutoff as a fraction
    // of Nyquist: 0.94 at normal speed, scaled by 1 / rate when reading faster.
    double read(const Ring& r, double p, double fc) const {
        const int half = (int)std::ceil(kHalf / fc);
        const double fl = std::floor(p); const long long i0 = (long long)fl; const double frac = p - fl;
        double sum = 0;
        for (int k = 1 - half; k <= half; ++k) sum += tap(((double)k - frac) * fc) * r.at(i0 + k);
        return sum * fc;
    }
};

// Loop delay for a damped comb that resonates exactly at hz: the integer
// delay minus the damping one-pole's phase delay, corrected for the Hermite
// read's own phase response (solved by fixed-point iteration).
inline double combDelay(double hz, double sr, double damp) {
    const double w = 2 * kPi * hz / sr, b = 1 - damp;
    const double tauLp = std::atan2(b * std::sin(w), 1 - b * std::cos(w)) / w;
    double d = sr / hz - tauLp;
    for (int it = 0; it < 4; ++it) {
        const double t = std::ceil(d) - d; // Hermite fraction at the read position
        const double c[4] = {-0.5 * t + t * t - 0.5 * t * t * t, 1 - 2.5 * t * t + 1.5 * t * t * t,
                             0.5 * t + 2 * t * t - 1.5 * t * t * t, -0.5 * t * t + 0.5 * t * t * t};
        double re = 0, im = 0;
        for (int k = -1; k <= 2; ++k) { const double ph = -w * (t - k); re += c[k + 1] * std::cos(ph); im += c[k + 1] * std::sin(ph); }
        d = sr / hz - tauLp + std::atan2(im, re) / w;
    }
    return d;
}

// Niemitalo's 90-degree allpass pair: two four-stage chains.
struct Hilbert {
    static constexpr double ka[4] = {0.6923878 * 0.6923878, 0.9360654322959 * 0.9360654322959,
                                     0.9882295226860 * 0.9882295226860, 0.9987488452737 * 0.9987488452737};
    static constexpr double kb[4] = {0.4021921162426 * 0.4021921162426, 0.8561710882420 * 0.8561710882420,
                                     0.9722909545651 * 0.9722909545651, 0.9952884791278 * 0.9952884791278};
    double xa[4][2] {}, ya[4][2] {}, xb[4][2] {}, yb[4][2] {}, delayed = 0;
    void reset() { *this = Hilbert{}; }
    static double chain(const double* k, double (*xs)[2], double (*ys)[2], double x) {
        for (int i = 0; i < 4; ++i) {
            const double y = k[i] * (x + ys[i][1]) - xs[i][1];
            xs[i][1] = xs[i][0]; xs[i][0] = x;
            ys[i][1] = ys[i][0]; ys[i][0] = y;
            x = y;
        }
        return x;
    }
    void process(double x, double& re, double& im) {
        re = delayed; delayed = chain(ka, xa, ya, x);
        im = chain(kb, xb, yb, x);
    }
};
} // namespace lab

class LabFx {
public:
    static constexpr int kCoefChunk = 32; // SHIFT band limits follow its glide at this rate

    void prepare(double sampleRate) {
        sr_ = sampleRate;
        const double glide = 1.0 - std::exp(-1.0 / (0.02 * sr_));
        for (auto* w : wets()) w->coef = glide;
        for (auto* g : glides()) g->coef = glide;
        sinc_.build();
        for (auto& c : combs_) c.ring.prepare((int)(sr_ / 30.0) + 8);
        for (auto& s : shift_) s.ring.prepare((int)(sr_ * 0.4) + 8);
        spray_.prepare((int)(sr_ * 2.5));
        for (auto& g : glitch_) g.prepare((int)(sr_ * 4.0));
        dcCoef_ = std::exp(-2.0 * lab::kPi * 20.0 / sr_);
        fade_ = std::max(8.0, std::round(0.002 * sr_));
        gateStep_ = 1.0 / (0.006 * sr_);
        sprayCut_ = -1;
        setParams(settings_, patchBpm_);
        reset();
    }

    void reset() {
        for (auto* w : wets()) { w->cur = 0; w->cleared = true; }
        clearCrush(); clearReso(); clearShift(); clearSpray(); clearGlitch();
        beat_ = 0;
        for (auto* g : glides()) g->snap();
        for (auto& c : combs_) c.d = c.target;
    }

    void setParams(const LabSettings& s, double bpm) {
        settings_ = s; patchBpm_ = bpm;
        if (tempoOverride_ > 0) bpm = tempoOverride_;
        bpm_ = std::clamp(std::isfinite(bpm) && bpm > 1 ? bpm : 120.0, 40.0, 300.0);
        crushWet_.target = s.crushOn ? std::clamp((double)s.crushMix, 0.0, 1.0) : 0.0;
        resoWet_.target = s.resoOn ? std::clamp((double)s.resoMix, 0.0, 1.0) : 0.0;
        shiftWet_.target = s.shiftOn ? std::clamp((double)s.shiftMix, 0.0, 1.0) : 0.0;
        sprayWet_.target = s.sprayOn ? std::clamp((double)s.sprayMix, 0.0, 1.0) : 0.0;
        glitchWet_.target = s.glitchOn ? std::clamp((double)s.glitchMix, 0.0, 1.0) : 0.0;

        crushBits_.target = std::clamp((double)s.crushBits, 1.0, 16.0);
        crushRate_.target = std::clamp((double)s.crushRate, 50.0, sr_);

        const auto& chord = labResoChords()[(size_t)std::clamp(s.resoChord, 0, 5)];
        const double maxDelay = (double)combs_[0].ring.b.size() - 8;
        for (size_t i = 0; i < combs_.size(); ++i) {
            const double hz = 440.0 * std::exp2((std::clamp((double)s.resoNote, 24.0, 84.0) + chord[i] - 69.0) / 12.0);
            combs_[i].target = std::clamp(lab::combDelay(hz, sr_, kDamp), 4.0, std::max(4.0, maxDelay));
        }
        resoFb_.target = 0.6 + 0.395 * std::clamp((double)s.resoDecay, 0.0, 1.0);

        shiftHz_.target = std::clamp((double)s.shiftHz, -1000.0, 1000.0);
        shiftFb_.target = std::clamp((double)s.shiftFb, 0.0, 0.9);
        shiftSpread_.target = std::clamp((double)s.shiftSpread, 0.0, 1.0);
        shiftDelay_ = std::min(15.0 / bpm_ * sr_, std::max(8.0, (double)shift_[0].ring.b.size() - 8)); // a 1/16 note

        sprayRatio_ = std::exp2(std::clamp((double)s.sprayPitch, -24.0, 24.0) / 12.0);
        const double density = std::clamp((double)s.sprayDensity, 1.0, 40.0);
        sprayInterval_ = sr_ / density;
        sprayLen_ = std::clamp(3.0 / density, 0.04, 0.3) * sr_;
        sprayNorm_ = 1.0 / std::sqrt(std::max(1.0, density * sprayLen_ / sr_));
        // The fastest grain (pitch plus scatter detune) sets the anti-alias cutoff.
        const double maxRatio = sprayRatio_ * std::exp2(std::clamp((double)s.sprayScatter, 0.0, 1.0) * 0.3 / 12.0);
        const double cut = std::min(0.45, 0.45 / maxRatio) * sr_;
        if (std::abs(cut - sprayCut_) > 1e-6) { sprayCut_ = cut; sprayLp_.set(false, cut, sr_); }
    }

    // Host beat position at the start of the next process() call. Without
    // it GLITCH runs a free clock at the set tempo.
    void setTransport(double ppq, bool playing) { if (playing && std::isfinite(ppq)) beat_ = ppq; }
    // A session tempo (SQ-4) that wins over the patch tempo; <= 0 clears.
    void setTempoOverride(double bpm) {
        const double next = std::isfinite(bpm) && bpm > 1 ? bpm : 0.0;
        if (std::abs(next - tempoOverride_) > 1e-9) { tempoOverride_ = next; setParams(settings_, patchBpm_); }
    }

    // True while any stage is audible or still has state to clear.
    bool active() const {
        for (const auto* w : wetsConst()) if (w->live() || !w->cleared) return true;
        return false;
    }

    void processSample(float& l, float& r) {
        double x = l, y = r;
        stage(crushWet_, x, y, &LabFx::crush, &LabFx::clearCrush);
        stage(resoWet_, x, y, &LabFx::reso, &LabFx::clearReso);
        stage(shiftWet_, x, y, &LabFx::shift, &LabFx::clearShift);
        stage(sprayWet_, x, y, &LabFx::spray, &LabFx::clearSpray);
        stage(glitchWet_, x, y, &LabFx::glitch, &LabFx::clearGlitch);
        beat_ += bpm_ / 60.0 / sr_;
        l = (float)x; r = (float)y;
    }

private:
    static constexpr double kDamp = 0.65; // RESO loop damping (one-pole coefficient)

    struct Wet {
        double cur = 0, target = 0, coef = 0.001;
        bool cleared = true;
        bool live() const { return target > 0.0 || cur > 1.0e-5; }
    };
    using StageFn = void (LabFx::*)(double&, double&);
    using ClearFn = void (LabFx::*)();
    void stage(Wet& w, double& l, double& r, StageFn run, ClearFn clear) {
        if (w.live()) {
            w.cleared = false;
            w.cur += (w.target - w.cur) * w.coef;
            double wl = l, wr = r;
            (this->*run)(wl, wr);
            l += w.cur * (wl - l); r += w.cur * (wr - r);
        } else if (!w.cleared) {
            w.cur = 0; w.cleared = true; (this->*clear)();
        }
    }
    std::array<Wet*, 5> wets() { return {&crushWet_, &resoWet_, &shiftWet_, &sprayWet_, &glitchWet_}; }
    std::array<const Wet*, 5> wetsConst() const { return {&crushWet_, &resoWet_, &shiftWet_, &sprayWet_, &glitchWet_}; }
    std::array<lab::Glide*, 6> glides() { return {&crushBits_, &crushRate_, &resoFb_, &shiftHz_, &shiftFb_, &shiftSpread_}; }

    // ---- CRUSH ----
    double quantize(double x, double steps) {
        double y = std::round(x * steps) / steps;
        const double chaos = settings_.crushChaos;
        if (chaos > 0 && rng_.uni() < chaos * 0.15) y += (rng_.uni() < 0.5 ? -1.0 : 1.0) / steps;
        return std::clamp(y, -1.0, 1.0);
    }
    void crush(double& l, double& r) {
        const double bits = crushBits_.next(), inc = crushRate_.next() / sr_;
        double outL = crushL_, outR = crushR_;
        crushPhase_ += inc;
        if (crushPhase_ >= 1.0) {
            crushPhase_ -= 1.0;
            const double ago = std::min(1.0, crushPhase_ / inc); // the edge, in samples before now
            const double steps = std::exp2(bits - 1.0);
            const double newL = quantize(l + ago * (crushPrevL_ - l), steps);
            const double newR = quantize(r + ago * (crushPrevR_ - r), steps);
            outL = crushL_ + ago * (newL - crushL_); // box-filter the step across this sample
            outR = crushR_ + ago * (newR - crushR_);
            crushL_ = newL; crushR_ = newR;
            const double chaos = settings_.crushChaos;
            if (chaos > 0) crushPhase_ -= chaos * 0.9 * rng_.uni();
        }
        crushPrevL_ = l; crushPrevR_ = r;
        l = outL; r = outR;
    }
    void clearCrush() { crushPhase_ = 1.0; crushL_ = crushR_ = crushPrevL_ = crushPrevR_ = 0; }

    // ---- RESO ----
    struct Comb { lab::Ring ring; double lp = 0, d = 100, target = 100; };
    void reso(double& l, double& r) {
        const double x = 0.5 * (l + r), g = resoFb_.next();
        std::array<double, 4> out {};
        for (size_t i = 0; i < combs_.size(); ++i) {
            auto& c = combs_[i];
            c.d += (c.target - c.d) * 0.0015;
            const double y = c.ring.hermite((double)c.ring.w - c.d);
            c.lp += kDamp * (y - c.lp);
            c.ring.write(std::tanh(x * (1.0 - g) + g * c.lp));
            out[i] = y;
        }
        l = 0.5 * (out[0] + out[2] + 0.5 * (out[1] + out[3]));
        r = 0.5 * (out[1] + out[3] + 0.5 * (out[0] + out[2]));
    }
    void clearReso() { for (auto& c : combs_) { c.ring.clear(); c.lp = 0; c.d = c.target; } }

    // ---- SHIFT ----
    struct ShiftChannel {
        lab::Ring ring; lab::Hilbert hilbert; lab::Butter8 lp, hp;
        double phase = 0, dcX = 0, dcY = 0, hz = 1e9;
    };
    void tuneShift(ShiftChannel& c, double hz) {
        // Band-limit so every shifted component stays inside (0, Nyquist).
        const double lpHz = std::clamp(0.47 * sr_ - std::max(0.0, hz), 500.0, 0.45 * sr_);
        const double hpHz = std::clamp(hz < 0 ? -hz * 1.05 : 20.0, 20.0, 0.4 * sr_);
        c.lp.set(false, lpHz, sr_); c.hp.set(true, hpHz, sr_);
        c.hz = hz;
    }
    double shiftChannel(ShiftChannel& c, double x, double hz, double fb) {
        const double fed = x + fb * c.ring.hermite((double)c.ring.w - shiftDelay_);
        double re, im;
        c.hilbert.process(c.hp.process(c.lp.process(fed)), re, im);
        c.phase += hz / sr_; c.phase -= std::floor(c.phase);
        const double a = 2.0 * lab::kPi * c.phase;
        const double y = re * std::cos(a) + im * std::sin(a); // upper sideband: +hz shifts up
        c.dcY = y - c.dcX + dcCoef_ * c.dcY; c.dcX = y; // DC-blocked, soft-clipped feedback
        c.ring.write(std::tanh(c.dcY));
        return y;
    }
    void shift(double& l, double& r) {
        const double hz = shiftHz_.next(), fb = shiftFb_.next(), spread = shiftSpread_.next();
        const double hzR = hz * (1.0 - 2.0 * spread);
        if (--shiftTick_ <= 0) {
            shiftTick_ = kCoefChunk;
            if (std::abs(hz - shift_[0].hz) > 0.01) tuneShift(shift_[0], hz);
            if (std::abs(hzR - shift_[1].hz) > 0.01) tuneShift(shift_[1], hzR);
        }
        l = shiftChannel(shift_[0], l, hz, fb);
        r = shiftChannel(shift_[1], r, hzR, fb);
    }
    void clearShift() {
        for (auto& c : shift_) {
            c.ring.clear(); c.hilbert.reset(); c.lp.reset(); c.hp.reset();
            c.phase = c.dcX = c.dcY = 0; c.hz = 1e9;
        }
        shiftTick_ = 0;
    }

    // ---- SPRAY ----
    struct Grain { bool on = false; double pos = 0, inc = 1, gl = 1, gr = 1; int len = 1, age = 0; };
    void spawnGrain() {
        for (auto& g : grains_) {
            if (g.on) continue;
            const double scatter = settings_.sprayScatter;
            const double ratio = sprayRatio_ * std::exp2(scatter * 0.3 * rng_.bi() / 12.0);
            const bool reverse = rng_.uni() < scatter * 0.35;
            g.len = std::max(16, (int)sprayLen_);
            g.inc = reverse ? -ratio : ratio;
            const double behind = 64.0 + (reverse ? 0.0 : std::max(0.0, g.len * (ratio - 1.0)))
                                + scatter * rng_.uni() * 0.8 * sr_;
            g.pos = (double)spray_.w - behind;
            const double pan = rng_.bi() * std::min(1.0, 0.3 + scatter);
            const double a = (pan + 1.0) * 0.25 * lab::kPi;
            g.gl = std::cos(a) * std::sqrt(2.0); g.gr = std::sin(a) * std::sqrt(2.0);
            g.age = 0; g.on = true;
            return;
        }
    }
    void spray(double& l, double& r) {
        spray_.write(sprayLp_.process(0.5 * (l + r)));
        if (--sprayCountdown_ <= 0) {
            spawnGrain();
            sprayCountdown_ += sprayInterval_ * (0.5 + rng_.uni());
        }
        double ol = 0, orr = 0;
        for (auto& g : grains_) {
            if (!g.on) continue;
            const double s = std::sin(lab::kPi * (double)g.age / (double)g.len);
            const double v = spray_.hermite(g.pos) * s * s;
            ol += v * g.gl; orr += v * g.gr;
            g.pos += g.inc;
            if (++g.age >= g.len) g.on = false;
        }
        l = ol * sprayNorm_; r = orr * sprayNorm_;
    }
    void clearSpray() {
        spray_.clear(); sprayLp_.reset();
        for (auto& g : grains_) g.on = false;
        sprayCountdown_ = 0;
    }

    // ---- GLITCH ----
    // A read head is the live input or a band-limited read of the record.
    struct Head { bool live = true; double pos = 0, rate = 1; };
    double readHead(const Head& h, int ch, double dry) const {
        if (h.live) return dry;
        return sinc_.read(glitch_[(size_t)ch], h.pos, 0.94 * std::min(1.0, 1.0 / h.rate));
    }
    void spliceTo(const Head& next) { tail_ = head_; head_ = next; xfade_ = (int)fade_; }
    void glitch(double& l, double& r) {
        const double now = (double)glitch_[0].w;
        glitch_[0].write(l); glitch_[1].write(r);
        const long period = (long)std::floor(beat_ / 2.0); // decide every half bar
        if (period != glitchPeriod_) {
            const bool first = glitchPeriod_ == -1;
            glitchPeriod_ = period;
            glitchActive_ = !first && rng_.uni() < settings_.glitchChance;
            if (glitchActive_) {
                // The window opened (beat - 2 * period) beats ago; the slice starts there.
                const double late = (beat_ - 2.0 * (double)period) * 60.0 / bpm_ * sr_;
                glitchSlice_ = std::max(64.0, labGlitchBeats(settings_.glitchDiv) * 60.0 / bpm_ * sr_);
                glitchStart_ = now - std::min(late, 0.5 * glitchSlice_);
                if (glitchGate_ > 0.0) spliceTo({true, now, 1.0}); else { head_ = {true, now, 1.0}; xfade_ = 0; }
                glitchPos_ = now - glitchStart_; glitchRepeat_ = 0;
                glitchGate_ = 1.0;
            }
        }
        if (!glitchActive_) glitchGate_ = std::max(0.0, glitchGate_ - gateStep_);
        if (glitchGate_ <= 0.0) return;

        double hl = readHead(head_, 0, l), hr = readHead(head_, 1, r);
        if (xfade_ > 0) {
            const double t = 1.0 - (double)xfade_ / fade_;
            const double a = std::sin(t * 0.5 * lab::kPi), b = std::cos(t * 0.5 * lab::kPi);
            hl = a * hl + b * readHead(tail_, 0, l);
            hr = a * hr + b * readHead(tail_, 1, r);
            tail_.pos += tail_.rate; --xfade_;
        }
        head_.pos += head_.rate;
        if (glitchActive_) {
            glitchPos_ += head_.rate;
            if (glitchPos_ >= glitchSlice_) {
                glitchPos_ -= glitchSlice_;
                ++glitchRepeat_;
                const double rate = std::clamp(std::exp2(settings_.glitchDrift * 0.25 * glitchRepeat_), 0.25, 4.0);
                spliceTo({false, glitchStart_ + glitchPos_, rate});
            }
        }
        // Equal-power return to the dry signal when the window closes.
        const double a = std::sin(glitchGate_ * 0.5 * lab::kPi), b = std::cos(glitchGate_ * 0.5 * lab::kPi);
        l = a * hl + b * l; r = a * hr + b * r;
    }
    void clearGlitch() {
        for (auto& g : glitch_) g.clear();
        head_ = tail_ = Head{}; xfade_ = 0;
        glitchActive_ = false; glitchGate_ = 0; glitchPeriod_ = -1; glitchPos_ = 0; glitchRepeat_ = 0;
    }

    double sr_ = 48000, bpm_ = 120, beat_ = 0, patchBpm_ = 120, tempoOverride_ = 0;
    double dcCoef_ = 0.997, fade_ = 96, gateStep_ = 0.003;
    LabSettings settings_;
    lab::Rng rng_;
    lab::SincTable sinc_;
    Wet crushWet_, resoWet_, shiftWet_, sprayWet_, glitchWet_;

    lab::Glide crushBits_, crushRate_;
    double crushPhase_ = 1, crushL_ = 0, crushR_ = 0, crushPrevL_ = 0, crushPrevR_ = 0;

    std::array<Comb, 4> combs_; lab::Glide resoFb_;

    std::array<ShiftChannel, 2> shift_; lab::Glide shiftHz_, shiftFb_, shiftSpread_;
    double shiftDelay_ = 6000; int shiftTick_ = 0;

    lab::Ring spray_; lab::Butter8 sprayLp_; std::array<Grain, 24> grains_;
    double sprayRatio_ = 2, sprayInterval_ = 4000, sprayLen_ = 4000, sprayCountdown_ = 0, sprayNorm_ = 1, sprayCut_ = -1;

    std::array<lab::Ring, 2> glitch_;
    Head head_, tail_; int xfade_ = 0;
    double glitchSlice_ = 1000, glitchStart_ = 0, glitchPos_ = 0, glitchGate_ = 0;
    long glitchPeriod_ = -1; int glitchRepeat_ = 0; bool glitchActive_ = false;
};

} // namespace fable
