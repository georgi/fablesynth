// PLATE reverb — C++ port of PlateReverb in src/engine/ott-worklet.js.
//
// A dense allpass-loop tank after Dattorro ("Effect Design, Part 1", JAES
// 1997): six input diffusers feed a figure-eight of two branches, each a
// modulated allpass, a delay, a damping low-pass, a second allpass and a
// delay. The output sums fourteen taps of the tank. Every element is an
// allpass or a pure delay, so the tail has no comb coloration, and the slow
// delay modulation breaks up the remaining modes. Lengths are the paper's, in
// samples at its 29761 Hz rate.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace fable {

namespace plate {
constexpr double kFs = 29761.0, kPi = 3.141592653589793;
// Input diffusers: the paper's four, then two longer ones. The extra pair
// raises the echo density of the first 150 ms above the ROOM network's.
constexpr int kInAp[6] = {142, 107, 379, 277, 613, 491};
constexpr double kInG[6] = {0.75, 0.75, 0.625, 0.625, 0.6, 0.6};
constexpr double kExcursion = 16;           // modulation depth of the tank allpasses
constexpr double kModHz[2] = {0.5, 0.31};   // one slow rate per branch
constexpr double kDiffusion1 = 0.7;
// Tank lines: 0/1 delays and 2 allpass of the left branch, 3/4 delays and 5
// allpass of the right branch.
constexpr int kLines[6] = {4453, 3720, 1800, 4217, 3163, 2656};
struct Tap { int line, pos, sign; };
constexpr Tap kTapsL[7] = {{3, 266, 1}, {3, 2974, 1}, {5, 1913, -1}, {4, 1996, 1}, {0, 1990, -1}, {2, 187, -1}, {1, 1066, -1}};
constexpr Tap kTapsR[7] = {{0, 353, 1}, {0, 3627, 1}, {2, 1228, -1}, {1, 2673, 1}, {3, 2111, -1}, {5, 335, -1}, {4, 121, -1}};
// One half of the tank holds the signal for this long (seconds, mean of the
// two branches), and passes it through DECAY twice.
constexpr double kBranchSec = (672 + 4453 + 1800 + 3720 + 908 + 4217 + 2656 + 3163) / 2.0 / kFs;

// SIZE -> RT60, the same law as the ROOM comb network: its eight combs have a
// mean delay of 31.2 ms and a feedback of 0.7 + 0.28 * size.
inline double decay(double size) {
    const double rt60 = (-3 * 0.0312) / std::log10(0.7 + 0.28 * std::min(1.0, std::max(0.0, size)));
    return std::pow(10.0, (-3 * kBranchSec) / (2 * rt60));
}
// DAMP -> the tank low-pass corner: 16 kHz at 0, 4 kHz at 0.5, 1 kHz at 1.
inline double dampHz(double damp) { return 16000 * std::pow(2.0, -4 * std::min(1.0, std::max(0.0, damp))); }

struct Line {
    std::vector<float> buf; int len = 2, n = 6, w = 0;
    void prepare(int length) { len = std::max(2, length); n = len + 4; buf.assign((size_t)n, 0.0f); w = 0; }
    void reset() { std::fill(buf.begin(), buf.end(), 0.0f); }
    // Sample written `d` writes ago (1 = the newest).
    inline float tap(int d) const { int i = w - d; if (i < 0) i += n; return buf[(size_t)i]; }
    inline double tapFrac(double d) const {
        const int i = (int)d; const double f = d - i;
        const double a = tap(i);
        return a + (tap(i + 1) - a) * f;
    }
    inline void write(double x) { buf[(size_t)w] = (float)x; if (++w >= n) w = 0; }
};
} // namespace plate

class PlateReverb {
public:
    void prepare(double sampleRate) {
        sr_ = sampleRate;
        const double k = sr_ / plate::kFs;
        auto len = [k](int n) { return std::max(2, (int)std::lround(n * k)); };
        for (size_t i = 0; i < 6; i++) inAp_[i].prepare(len(plate::kInAp[i]));
        exc_ = plate::kExcursion * k;
        // Modulated allpasses carry the excursion as extra length.
        modLen_[0] = len(672); modLen_[1] = len(908);
        for (size_t i = 0; i < 2; i++) modAp_[i].prepare(modLen_[i] + (int)std::ceil(exc_) + 2);
        for (size_t i = 0; i < 6; i++) lines_[i].prepare(len(plate::kLines[i]));
        auto pos = [&](const plate::Tap& t) {
            return std::min(lines_[(size_t)t.line].len, std::max(1, (int)std::lround(t.pos * k)));
        };
        for (size_t i = 0; i < 7; i++) { tapL_[i] = pos(plate::kTapsL[i]); tapR_[i] = pos(plate::kTapsR[i]); }
        for (size_t i = 0; i < 2; i++) modInc_[i] = 2 * plate::kPi * plate::kModHz[i] / sr_;
        hpK_ = 1 - std::exp(-2 * plate::kPi * 120 / sr_); // input low cut, 120 Hz
        setParams(0.5, 0.5);
        reset();
    }
    void reset() {
        for (auto& l : inAp_) l.reset();
        for (auto& l : modAp_) l.reset();
        for (auto& l : lines_) l.reset();
        hp_ = lpL_ = lpR_ = 0; ph0_ = 0; ph1_ = plate::kPi / 2;
        outL = outR = 0;
    }
    void setParams(double size, double damp) {
        decay_ = plate::decay(size);
        diffusion2_ = std::min(0.5, std::max(0.25, decay_ + 0.15));
        damp_ = std::exp(-2 * plate::kPi * plate::dampHz(damp) / sr_);
    }
    // One mono input sample; the stereo result is in outL / outR.
    inline void process(double x) {
        hp_ += hpK_ * (x - hp_);
        x -= hp_;
        for (size_t i = 0; i < 6; i++) {
            auto& line = inAp_[i]; const double g = plate::kInG[i];
            const double d = line.tap(line.len);
            const double v = x - g * d;
            line.write(v);
            x = d + g * v;
        }
        auto& ln = lines_;
        const double decay = decay_, g2 = diffusion2_, damp = damp_;
        const double endL = ln[1].tap(ln[1].len), endR = ln[4].tap(ln[4].len);
        ph0_ += modInc_[0]; if (ph0_ > 2 * plate::kPi) ph0_ -= 2 * plate::kPi;
        ph1_ += modInc_[1]; if (ph1_ > 2 * plate::kPi) ph1_ -= 2 * plate::kPi;

        // Left branch, fed by the input and the end of the right branch.
        double a = x + decay * endR;
        double d = modAp_[0].tapFrac(modLen_[0] + exc_ * std::sin(ph0_));
        double v = a + plate::kDiffusion1 * d;
        modAp_[0].write(v);
        a = d - plate::kDiffusion1 * v;
        d = ln[0].tap(ln[0].len); ln[0].write(a);
        lpL_ = d + damp * (lpL_ - d);
        a = lpL_ * decay;
        d = ln[2].tap(ln[2].len);
        v = a - g2 * d;
        ln[2].write(v);
        ln[1].write(d + g2 * v);

        // Right branch, fed by the input and the end of the left branch.
        a = x + decay * endL;
        d = modAp_[1].tapFrac(modLen_[1] + exc_ * std::sin(ph1_));
        v = a + plate::kDiffusion1 * d;
        modAp_[1].write(v);
        a = d - plate::kDiffusion1 * v;
        d = ln[3].tap(ln[3].len); ln[3].write(a);
        lpR_ = d + damp * (lpR_ - d);
        a = lpR_ * decay;
        d = ln[5].tap(ln[5].len);
        v = a - g2 * d;
        ln[5].write(v);
        ln[4].write(d + g2 * v);

        double l = 0, r = 0;
        for (size_t i = 0; i < 7; i++) {
            l += plate::kTapsL[i].sign * ln[(size_t)plate::kTapsL[i].line].tap(tapL_[i]);
            r += plate::kTapsR[i].sign * ln[(size_t)plate::kTapsR[i].line].tap(tapR_[i]);
        }
        outL = 0.6 * l; outR = 0.6 * r;
    }
    double outL = 0, outR = 0;

private:
    double sr_ = 48000;
    std::array<plate::Line, 6> inAp_, lines_;
    std::array<plate::Line, 2> modAp_;
    int modLen_[2] = {2, 2}, tapL_[7] = {0}, tapR_[7] = {0};
    double exc_ = 0, modInc_[2] = {0, 0}, hpK_ = 0;
    double decay_ = 0.5, diffusion2_ = 0.5, damp_ = 0;
    double hp_ = 0, lpL_ = 0, lpR_ = 0, ph0_ = 0, ph1_ = 0;
};

} // namespace fable
