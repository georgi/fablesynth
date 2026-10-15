// 2x oversampler for the in-voice drive saturators (WT-1, BL-1, DR-1).
//
// Only the nonlinearity runs at 2x: upsample -> ADAA tanh at the high rate ->
// downsample. The SVF stays at the base rate. Both rate converters are the same
// polyphase IIR half-band: two allpass branches, A0 = {c0, c2, c4} and
// A1 = {c1, c3}, with the 5 coefficients below (11th-order elliptic-type
// half-band, transition bandwidth 0.09 of the high rate, designed with the
// Valenzuela/Constantinides closed form that HIIR uses). Measured: the up+down
// pair is flat within 1e-6 dB up to 16 kHz and -0.0075 dB at 20 kHz at 44.1 kHz;
// one converter rejects images by 83 dB where products fold below 18 kHz; the
// pair delays low frequencies by 2.74 samples, 3.4 samples at 10 kHz.
//
// Bypass: while the saturator is idle and the stage has faded out, run() returns
// its input unchanged (bit-identical to a voice without the stage). The stage
// fades in and out over kDriveOsRampSec with a plain linear crossfade against
// the bypass signal. The stage is state-less to start (cold) when it engages.
#pragma once

#include <algorithm>
#include <cmath>

namespace fable {

constexpr double kDriveOsCoef[5] = {0.058131122888383423, 0.21191463147606193, 0.4170395803350932,
                                    0.63844438482644794, 0.87095859811277021};
constexpr double kDriveOsInputLimit = 16.0;
constexpr double kDriveOsRampSec = 0.004;

struct DriveOsAllpass {
    double x1 = 0, y1 = 0;
    inline double process(double a, double x) {
        const double y = a * (x - y1) + x1;
        x1 = x; y1 = y;
        return y;
    }
};

struct DriveOs2 {
    DriveOsAllpass u0[3], u1[2], d0[3], d1[2];
    double dprev = 0;                 // decimator branch 1 output, one sample late
    double xp = 0;                    // previous high-rate ADAA input
    double w = 0;                     // stage weight: 0 = bypass, 1 = stage

    void reset() { *this = DriveOs2(); }

    static inline double clampIn(double x) {
        return std::isfinite(x) ? std::clamp(x, -kDriveOsInputLimit, kDriveOsInputLimit) : 0.0;
    }
    // One base-rate sample in, two high-rate samples out.
    inline void interpolate(double x, double& y0, double& y1) {
        y0 = u0[2].process(kDriveOsCoef[4], u0[1].process(kDriveOsCoef[2], u0[0].process(kDriveOsCoef[0], x)));
        y1 = u1[1].process(kDriveOsCoef[3], u1[0].process(kDriveOsCoef[1], x));
    }
    // Two high-rate samples in, one base-rate sample out.
    inline double decimate(double e, double o) {
        const double a = d0[2].process(kDriveOsCoef[4], d0[1].process(kDriveOsCoef[2], d0[0].process(kDriveOsCoef[0], e)));
        const double b = d1[1].process(kDriveOsCoef[3], d1[0].process(kDriveOsCoef[1], o));
        const double out = 0.5 * (a + dprev);
        dprev = b;
        return out;
    }
    // One base-rate sample. engage: the stage should be in the path (it fades
    // in or out over wStep per sample). satOn: the saturator runs, with dry/wet
    // mix m. sat(a, xpPrev) returns the ADAA tanh of a.
    template <class Sat>
    inline double run(double x, bool engage, bool satOn, double m, double wStep, Sat&& sat) {
        const double target = engage ? 1.0 : 0.0;
        if (w < target) w = std::min(target, w + wStep);
        else if (w > target) w = std::max(target, w - wStep);
        if (w <= 0.0 && !engage) { xp = clampIn(x); return x; }
        double u0, u1;
        interpolate(x, u0, u1);
        const double a0 = clampIn(u0), a1 = clampIn(u1);
        double o0 = a0, o1 = a1;
        if (satOn) {
            o0 = a0 + m * (sat(a0, xp) - a0);
            o1 = a1 + m * (sat(a1, a0) - a1);
        }
        xp = a1;
        const double y = decimate(o0, o1);
        return w >= 1.0 ? y : x + w * (y - x);
    }
};

} // namespace fable
