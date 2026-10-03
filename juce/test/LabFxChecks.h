// LAB fidelity measurements (dsp/LabFx.h) and the web parity fixture.
// Each check measures a property the hi-fi design promises: in-tune RESO
// combs, SHIFT sideband and through-zero rejection, alias-free SPRAY and
// GLITCH re-pitching, click-free GLITCH splices, and clean CRUSH images.
#pragma once

#include "../source/dsp/LabFx.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace labcheck {

using Check = std::function<void(bool, const std::string&, const std::string&)>;
constexpr double kSr = 48000.0, kPi = 3.141592653589793;

struct Stereo { std::vector<float> l, r; };

inline Stereo run(const fable::LabSettings& s, int n, const std::function<double(int, int)>& input, double bpm = 120) {
    fable::LabFx lab; lab.prepare(kSr); lab.setParams(s, bpm);
    Stereo out; out.l.resize((size_t)n); out.r.resize((size_t)n);
    for (int i = 0; i < n; ++i) {
        float l = (float)input(i, 0), r = (float)input(i, 1);
        lab.processSample(l, r);
        out.l[(size_t)i] = l; out.r[(size_t)i] = r;
    }
    return out;
}

// Hann-windowed power spectrum of the last 2^bits samples.
// window = false takes the first 2^bits samples unwindowed (impulse responses).
inline std::vector<double> spectrum(const std::vector<float>& x, int bits, bool window = true) {
    const int n = 1 << bits; const size_t off = window ? x.size() - (size_t)n : 0;
    std::vector<std::complex<double>> a((size_t)n);
    for (int i = 0; i < n; ++i) a[(size_t)i] = x[off + (size_t)i] * (window ? 0.5 - 0.5 * std::cos(2 * kPi * i / n) : 1.0);
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
        if (i < j) std::swap(a[(size_t)i], a[(size_t)j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const std::complex<double> w = std::polar(1.0, -2 * kPi / len);
        for (int i = 0; i < n; i += len) {
            std::complex<double> wk = 1;
            for (int k = 0; k < len / 2; ++k, wk *= w) {
                const auto u = a[(size_t)(i + k)], v = a[(size_t)(i + k + len / 2)] * wk;
                a[(size_t)(i + k)] = u + v; a[(size_t)(i + k + len / 2)] = u - v;
            }
        }
    }
    std::vector<double> p((size_t)n / 2);
    for (int i = 0; i < n / 2; ++i) p[(size_t)i] = std::norm(a[(size_t)i]);
    return p;
}
inline double binHz(int bits) { return kSr / (1 << bits); }
// Power summed over +-w bins around hz.
inline double bandPower(const std::vector<double>& p, int bits, double hz, int w = 3) {
    const int c = (int)std::lround(hz / binHz(bits)); double s = 0;
    for (int i = std::max(0, c - w); i <= std::min((int)p.size() - 1, c + w); ++i) s += p[(size_t)i];
    return s;
}
inline double total(const std::vector<double>& p, double loHz, double hiHz, int bits) {
    double s = 0;
    for (size_t i = 0; i < p.size(); ++i) { const double f = (double)i * binHz(bits); if (f >= loHz && f < hiHz) s += p[i]; }
    return s;
}
inline double db(double ratio) { return 10 * std::log10(std::max(ratio, 1e-30)); }
inline std::string fmt(double v, const char* unit) { char b[64]; std::snprintf(b, sizeof b, "%.2f %s", v, unit); return b; }

inline void runFidelityChecks(const Check& check) {
    const auto sine = [](double hz, double amp) { return [=](int i, int) { return amp * std::sin(2 * kPi * hz * i / kSr); }; };

    // RESO: the comb rings at its target pitch. The ringing (pole) frequency
    // is measured from the impulse response: the phase advance of a Hann-
    // windowed projection at f0 between two windows 0.1 s apart. OCTAVES keeps
    // the lowest comb isolated (the other combs have troughs there).
    {
        double worst = 0; std::string each;
        for (int note : {36, 60, 84}) {
            fable::LabSettings s; s.resoOn = true; s.resoMix = 1; s.resoDecay = 1; s.resoChord = 0; s.resoNote = (float)note;
            const auto out = run(s, 48000, [](int i, int) { return i == 0 ? 0.5 : 0.0; });
            const double f0 = 440.0 * std::exp2((note - 69) / 12.0);
            const int n = 8192, t1 = 2048, dt = 4800;
            const auto project = [&](int t) {
                std::complex<double> acc = 0;
                for (int k = 0; k < n; ++k)
                    acc += (double)out.l[(size_t)(t + k)] * (0.5 - 0.5 * std::cos(2 * kPi * k / n)) * std::polar(1.0, -2 * kPi * f0 * k / kSr);
                return acc;
            };
            double dphi = std::arg(project(t1 + dt)) - std::arg(project(t1)) - 2 * kPi * f0 * dt / kSr;
            dphi -= 2 * kPi * std::round(dphi / (2 * kPi));
            const double f = f0 + dphi / (2 * kPi * dt / kSr);
            const double cents = 1200 * std::log2(f / f0);
            worst = std::max(worst, std::abs(cents)); each += fmt(cents, "") + " ";
        }
        check(worst < 1.0, "RESO comb rings in tune at C2, C4, C6 (max error)", fmt(worst, "cents") + " [" + each + "]");
    }

    // SHIFT: the unwanted sideband and through-zero wrap are suppressed.
    {
        double worst = 1e9;
        for (double f : {200.0, 1000.0, 8000.0}) {
            fable::LabSettings s; s.shiftOn = true; s.shiftMix = 1; s.shiftFb = 0; s.shiftSpread = 0; s.shiftHz = 100;
            const auto out = run(s, 1 << 16, sine(f, 0.25));
            const auto p = spectrum(out.l, 15);
            worst = std::min(worst, db(bandPower(p, 15, f + 100) / bandPower(p, 15, f - 100)));
        }
        check(worst > 35, "SHIFT +100 Hz suppresses the mirror sideband (200 Hz-8 kHz, worst)", fmt(worst, "dB"));
        fable::LabSettings s; s.shiftOn = true; s.shiftMix = 1; s.shiftFb = 0; s.shiftSpread = 0; s.shiftHz = -100;
        const auto out = run(s, 1 << 16, sine(60, 0.25));
        const auto p = spectrum(out.l, 15);
        const auto in = spectrum(run(fable::LabSettings{}, 1 << 16, sine(60, 0.25)).l, 15);
        const double wrap = db(bandPower(p, 15, 40) / bandPower(in, 15, 60));
        check(wrap < -20, "SHIFT -100 Hz does not wrap 60 Hz through zero (40 Hz image)", fmt(wrap, "dB"));
    }

    // SPRAY: two octaves up, a 9 kHz tone has nowhere legal to go.
    {
        fable::LabSettings s; s.sprayOn = true; s.sprayMix = 1; s.sprayPitch = 24; s.sprayScatter = 0; s.sprayDensity = 12;
        const auto hi = run(s, 1 << 17, sine(9000, 0.25));
        const auto lo = run(s, 1 << 17, sine(1000, 0.25));
        double eHi = 0, eLo = 0;
        for (size_t i = 1 << 16; i < hi.l.size(); ++i) { eHi += hi.l[i] * hi.l[i]; eLo += lo.l[i] * lo.l[i]; }
        check(db(eHi / eLo) < -40, "SPRAY +24 st: 9 kHz alias residue vs a legal 1 kHz grain", fmt(db(eHi / eLo), "dB"));
    }

    // GLITCH: rising repeats (to 4x) of an 11 kHz tone leave nothing below 8 kHz.
    {
        fable::LabSettings s; s.glitchOn = true; s.glitchMix = 1; s.glitchChance = 1; s.glitchDiv = 2; s.glitchDrift = 1;
        const auto out = run(s, (int)(kSr * 3), sine(11000, 0.25));
        // Analyse the second half-bar window (1-2 s): it reaches rate 4 after ~0.62 s.
        std::vector<float> win(out.l.begin() + (long)(kSr * 1.0), out.l.begin() + (long)(kSr * 1.0) + (1 << 15));
        const auto p = spectrum(win, 15);
        const double alias = db(total(p, 20, 8000, 15) / total(p, 20, 24000, 15));
        check(alias < -40, "GLITCH 4x re-pitch of 11 kHz: alias energy below 8 kHz", fmt(alias, "dB"));
        // Splices: no step larger than the sine's own slope allows.
        fable::LabSettings c; c.glitchOn = true; c.glitchMix = 1; c.glitchChance = 1; c.glitchDiv = 3; c.glitchDrift = 0;
        const auto spl = run(c, (int)(kSr * 3), sine(440, 0.5));
        double maxStep = 0;
        for (size_t i = 1; i < spl.l.size(); ++i) maxStep = std::max(maxStep, (double)std::abs(spl.l[i] - spl.l[i - 1]));
        const double slope = 0.5 * 2 * kPi * 440 / kSr;
        check(maxStep < 1.6 * slope, "GLITCH splices stay within 1.6x the source slope (no clicks)", fmt(maxStep / slope, "x"));
    }

    // CRUSH: a non-integer hold rate gives only the sampled-signal images.
    {
        fable::LabSettings s; s.crushOn = true; s.crushMix = 1; s.crushBits = 16; s.crushRate = 3100; s.crushChaos = 0;
        const int bits = 16;
        const auto out = run(s, 1 << 17, sine(1000, 0.5));
        const auto p = spectrum(out.l, bits);
        double lines = 0;
        std::vector<bool> used(p.size(), false);
        for (int k = 0; k <= 16; ++k) for (double f : {k * 3100.0 + 1000, k * 3100.0 - 1000}) {
            double g = std::fmod(std::abs(f), kSr); if (g > kSr / 2) g = kSr - g;
            const int c = (int)std::lround(g / binHz(bits));
            for (int i = std::max(0, c - 4); i <= std::min((int)p.size() - 1, c + 4); ++i)
                if (!used[(size_t)i]) { used[(size_t)i] = true; lines += p[(size_t)i]; }
        }
        double all = 0; for (double v : p) all += v;
        const double spur = db((all - lines) / all);
        check(spur < -30, "CRUSH 3.1 kHz hold: energy outside the image lines", fmt(spur, "dB"));
    }
}

// ---- web parity fixture -----------------------------------------------------
// The same deterministic input runs through every stage; the fixture keeps the
// per-1024-sample RMS of each channel. src/engine/lab.test.ts renders the same
// input through lab-worklet.js and compares.
inline double parityInput(int i, int ch, uint32_t& seed) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    const double noise = (double)(seed >> 8) / 8388608.0 - 1.0;
    const double gate = (i % 12000) < 6000 ? 1.0 : 0.2;
    const double f1 = ch == 0 ? 220.0 : 221.0;
    return gate * (0.25 * std::sin(2 * kPi * f1 * i / kSr) + 0.15 * std::sin(2 * kPi * 1375.0 * i / kSr)) + 0.05 * noise;
}
inline std::vector<std::pair<const char*, fable::LabSettings>> parityCases() {
    fable::LabSettings crush; crush.crushOn = true; crush.crushMix = 1; crush.crushBits = 5; crush.crushRate = 4100; crush.crushChaos = 0.3f;
    fable::LabSettings reso; reso.resoOn = true; reso.resoMix = 1; reso.resoNote = 45; reso.resoDecay = 0.85f;
    fable::LabSettings shift; shift.shiftOn = true; shift.shiftMix = 1; shift.shiftHz = -70; shift.shiftFb = 0.6f; shift.shiftSpread = 0.7f;
    fable::LabSettings spray; spray.sprayOn = true; spray.sprayMix = 1; spray.sprayPitch = 12; spray.sprayDensity = 14; spray.sprayScatter = 0.5f;
    fable::LabSettings glitch; glitch.glitchOn = true; glitch.glitchMix = 1; glitch.glitchChance = 1; glitch.glitchDiv = 2; glitch.glitchDrift = -0.5f;
    return {{"crush", crush}, {"reso", reso}, {"shift", shift}, {"spray", spray}, {"glitch", glitch}};
}
inline bool writeParityFixture(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    const int n = (int)(kSr * 3), block = 1024;
    std::fprintf(f, "{\n  \"sampleRate\": %d, \"bpm\": 120, \"samples\": %d, \"block\": %d,\n  \"cases\": {", (int)kSr, n, block);
    bool firstCase = true;
    for (const auto& [name, s] : parityCases()) {
        uint32_t seed = 12345;
        std::vector<double> in((size_t)n * 2);
        for (int i = 0; i < n; ++i) { in[(size_t)i * 2] = parityInput(i, 0, seed); in[(size_t)i * 2 + 1] = parityInput(i, 1, seed); }
        const auto out = run(s, n, [&](int i, int ch) { return in[(size_t)i * 2 + (size_t)ch]; });
        std::fprintf(f, "%s\n    \"%s\": {", firstCase ? "" : ",", name); firstCase = false;
        for (int ch = 0; ch < 2; ++ch) {
            const auto& x = ch == 0 ? out.l : out.r;
            std::fprintf(f, "%s\"%s\": [", ch ? ", " : "", ch ? "R" : "L");
            for (int b = 0; b * block < n; ++b) {
                double e = 0; const int end = std::min(n, (b + 1) * block);
                for (int i = b * block; i < end; ++i) e += (double)x[(size_t)i] * x[(size_t)i];
                std::fprintf(f, "%s%.6e", b ? ", " : "", std::sqrt(e / (end - b * block)));
            }
            std::fprintf(f, "]");
        }
        std::fprintf(f, "}");
    }
    std::fprintf(f, "\n  }\n}\n");
    std::fclose(f);
    return true;
}

} // namespace labcheck
