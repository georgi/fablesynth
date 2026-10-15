// Shared metrics for the drive-oversampler regression checks (WT-1, BL-1,
// DR-1). Header-only; each engine test supplies its own render function.
#pragma once

#include "../source/dsp/DriveOs.h"
#include "../source/dsp/Wavetables.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

namespace driveos_check {

inline double window(int i, int N) {
    const double t = 2 * M_PI * i / (N - 1);
    return 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
}

inline std::vector<double> powerSpectrum(const float* x, int N) {
    std::vector<double> re((size_t)N), im((size_t)N, 0.0);
    for (int i = 0; i < N; i++) re[(size_t)i] = x[i] * window(i, N);
    fable::fft(re.data(), im.data(), N, false);
    std::vector<double> p((size_t)N / 2);
    for (int k = 0; k < N / 2; k++) p[(size_t)k] = re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k];
    return p;
}

// Inharmonic energy 20 Hz - 16 kHz relative to the strongest harmonic's lobe,
// in dB. Bins within 8 of a harmonic are masked (Blackman-Harris sidelobes
// are below -92 dB), so only distortion products that fold back count.
inline double inharmonicDb(const float* x, int N, double sr, double f0) {
    const auto p = powerSpectrum(x, N);
    const double bin = sr / N;
    std::vector<char> harm((size_t)N / 2, 0);
    for (int h = 1; h * f0 < sr * 0.5; h++) {
        const int c = (int)std::lround(h * f0 / bin);
        for (int k = std::max(0, c - 8); k <= c + 8 && k < N / 2; k++) harm[(size_t)k] = 1;
    }
    double lobe = 0, inh = 0, best = 0;
    int bestK = 1;
    for (int k = 1; k < N / 2; k++)
        if (harm[(size_t)k] && p[(size_t)k] > best) { best = p[(size_t)k]; bestK = k; }
    for (int k = std::max(0, bestK - 8); k <= bestK + 8 && k < N / 2; k++) lobe += p[(size_t)k];
    for (int k = (int)std::ceil(20 / bin); k < N / 2 && k * bin <= 16000; k++)
        if (!harm[(size_t)k]) inh += p[(size_t)k];
    return 10 * std::log10(std::max(inh, 1e-30) / lobe);
}

// Drive-on vs drive-off gain across the harmonics of f0, in dB, after removing
// a smooth quadratic trend in frequency. Both renders must hold the same note
// at a level small enough for the saturator to act as a pure gain, so the
// ratio is the gain of the drive stage alone. A smooth trend is allowed (the
// ADAA average rolls off gently); periodic notches from a dry path that is
// misaligned with the oversampled path are not. Returns the largest residual.
inline double gainRippleDb(const float* off, const float* on, int N, double sr, double f0) {
    const auto a = powerSpectrum(off, N);
    const auto b = powerSpectrum(on, N);
    const double bin = sr / N;
    double top = 0;
    for (double v : a) top = std::max(top, v);
    std::vector<double> fx, dy;
    for (int h = 1; h * f0 <= 16000; h++) {
        const int c = (int)std::lround(h * f0 / bin);
        int k = c;
        for (int q = std::max(1, c - 2); q <= c + 2 && q < N / 2; q++)
            if (a[(size_t)q] > a[(size_t)k]) k = q;
        if (a[(size_t)k] < top * 1e-7) continue;      // harmonic below -70 dB re the loudest
        fx.push_back(h * f0 / 16000.0);
        dy.push_back(10 * std::log10(b[(size_t)k] / a[(size_t)k]));
        if (!std::isfinite(dy.back())) return 1e9;
    }
    if (getenv("DRIVEOS_DEBUG")) for (size_t i = 0; i < fx.size(); i++) fprintf(stderr, "h %zu f %.3f dy %.4f\n", i, fx[i], dy[i]);
    if (fx.size() < 8) return 1e9;
    // least-squares quadratic fit via the 3x3 normal equations
    double S[5] = {0, 0, 0, 0, 0}, T[3] = {0, 0, 0};
    for (size_t i = 0; i < fx.size(); i++) {
        double xp = 1;
        for (int j = 0; j < 5; j++) { S[j] += xp; if (j < 3) T[j] += xp * dy[i]; xp *= fx[i]; }
    }
    double M[3][4] = {{S[0], S[1], S[2], T[0]}, {S[1], S[2], S[3], T[1]}, {S[2], S[3], S[4], T[2]}};
    for (int i = 0; i < 3; i++) {
        for (int r = i + 1; r < 3; r++) {
            const double f = M[r][i] / M[i][i];
            for (int c = i; c < 4; c++) M[r][c] -= f * M[i][c];
        }
    }
    double co[3];
    for (int i = 2; i >= 0; i--) {
        double s = M[i][3];
        for (int c = i + 1; c < 3; c++) s -= M[i][c] * co[c];
        co[i] = s / M[i][i];
    }
    double worst = 0;
    for (size_t i = 0; i < fx.size(); i++)
        worst = std::max(worst, std::fabs(dy[i] - (co[0] + co[1] * fx[i] + co[2] * fx[i] * fx[i])));
    return worst;
}

} // namespace driveos_check
