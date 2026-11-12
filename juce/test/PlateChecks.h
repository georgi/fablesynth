// PLATE reverb checks (dsp/PlateReverb.h and its place in Fx). The same
// measurements as src/engine/plate.test.ts: impulse responses against the
// ROOM comb network, then the wet level and the type switch in the chain.
#pragma once

#include "../source/dsp/Fx.h"
#include "../source/dsp/Params.h"
#include "../source/dsp/PlateReverb.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace platecheck {

using Check = std::function<void(bool, const std::string&, const std::string&)>;
constexpr double kSr = 48000.0;
using Ir = std::vector<double>;

// Impulse response of the ROOM network (left channel), as Fx runs it.
inline Ir roomIr(double size, double seconds) {
    static const int comb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, ap[4] = {556, 441, 341, 225};
    const double scale = kSr / 44100.0;
    fable::FvComb combs[8]; fable::FvAllpass aps[4];
    for (int i = 0; i < 8; i++) {
        combs[i].prepare((int)(comb[i] * scale));
        combs[i].feedback = (float)(0.7 + size * 0.28);
        combs[i].damp1 = (float)(0.4 - size * 0.2); combs[i].damp2 = 1 - combs[i].damp1;
    }
    for (int i = 0; i < 4; i++) { aps[i].prepare((int)(ap[i] * scale)); aps[i].feedback = 0.5f; }
    Ir out((size_t)std::lround(seconds * kSr));
    for (size_t i = 0; i < out.size(); i++) {
        float o = 0;
        for (auto& c : combs) o += c.process(i == 0 ? 1.0f : 0.0f);
        for (auto& a : aps) o = a.process(o);
        out[i] = o;
    }
    return out;
}
inline void plateIr(double size, double damp, double seconds, Ir& l, Ir& r) {
    fable::PlateReverb p; p.prepare(kSr); p.setParams(size, damp);
    const size_t n = (size_t)std::lround(seconds * kSr);
    l.assign(n, 0); r.assign(n, 0);
    for (size_t i = 0; i < n; i++) { p.process(i == 0 ? 1.0 : 0.0); l[i] = p.outL; r[i] = p.outR; }
}
inline double energy(const Ir& x, double t0, double t1) {
    double e = 0;
    for (size_t i = (size_t)(t0 * kSr); i < (size_t)(t1 * kSr); i++) e += x[i] * x[i];
    return e;
}
// Seconds until the backward-integrated energy has dropped 60 dB (-5..-25 dB slope).
inline double rt60(const Ir& x) {
    std::vector<double> edc(x.size());
    double acc = 0;
    for (size_t i = x.size(); i-- > 0;) { acc += x[i] * x[i]; edc[i] = acc; }
    auto at = [&](double db) {
        const double level = edc[0] * std::pow(10.0, db / 10);
        for (size_t i = 0; i < edc.size(); i++) if (edc[i] < level) return (double)i / kSr;
        return (double)edc.size() / kSr;
    };
    return (at(-25) - at(-5)) * 3;
}
// Normalized echo density: 1 for Gaussian noise, lower for sparse echoes.
inline double echoDensity(const Ir& x, double t0, double t1) {
    const size_t a = (size_t)(t0 * kSr), b = (size_t)(t1 * kSr);
    const double sd = std::sqrt(energy(x, t0, t1) / (double)(b - a));
    int outside = 0;
    for (size_t i = a; i < b; i++) if (std::abs(x[i]) > sd) outside++;
    return (double)outside / (double)(b - a) / 0.3173;
}
// RMS of the tail's normalized autocorrelation over 4..80 ms: periodic flutter.
inline double tailRipple(const Ir& x) {
    Ir seg(x.begin() + (long)(0.4 * kSr), x.begin() + (long)(1.4 * kSr));
    const size_t win = 2400; // flatten the decay in 50 ms windows
    for (size_t k = 0; k < seg.size(); k += win) {
        double e = 0;
        for (size_t i = k; i < k + win; i++) e += seg[i] * seg[i];
        const double g = 1 / std::max(1e-12, std::sqrt(e / (double)win));
        for (size_t i = k; i < k + win; i++) seg[i] *= g;
    }
    double e0 = 0, sum = 0; int count = 0;
    for (double v : seg) e0 += v * v;
    for (size_t lag = (size_t)(0.004 * kSr); lag < (size_t)(0.08 * kSr); lag += 3) {
        double s = 0;
        for (size_t i = 0; i + lag < seg.size(); i++) s += seg[i] * seg[i + lag];
        sum += (s / e0) * (s / e0); count++;
    }
    return std::sqrt(sum / count);
}

// White noise through Fx for `held` seconds, then `tail` seconds of silence.
struct ChainRun { std::vector<float> l, r; };
inline ChainRun runChain(fable::ParamArray p, double held, double tail, const std::function<void(fable::Fx&, int)>& atBlock = {}) {
    fable::Fx fx; fx.prepare(kSr); fx.setParams(p);
    const int n = (int)((held + tail) * kSr) / 128 * 128, hold = (int)(held * kSr);
    ChainRun out; out.l.assign((size_t)n, 0.0f); out.r.assign((size_t)n, 0.0f);
    unsigned seed = 7;
    for (int i = 0; i < hold; i++) {
        seed = seed * 1664525u + 1013904223u;
        out.l[(size_t)i] = out.r[(size_t)i] = (float)((double)seed / 2147483648.0 - 1.0) * 0.1f;
    }
    for (int at = 0; at < n; at += 128) {
        if (atBlock) atBlock(fx, at / 128);
        fx.process(out.l.data() + at, out.r.data() + at, 128);
    }
    return out;
}
inline double rmsOf(const std::vector<float>& x, double t0, double t1) {
    double e = 0; const size_t a = (size_t)(t0 * kSr), b = (size_t)(t1 * kSr);
    for (size_t i = a; i < b; i++) e += (double)x[i] * x[i];
    return std::sqrt(e / (double)(b - a));
}

inline void runChecks(const Check& check) {
    using namespace fable;
    auto str = [](double v) { return std::to_string(v); };

    // ---- the network ----
    const Ir room = roomIr(0.84, 2);
    Ir pl, pr; plateIr(0.84, 0.5, 2, pl, pr);
    {
        bool denser = true; std::string detail;
        const double spans[4][2] = {{0.02, 0.07}, {0.07, 0.15}, {0.15, 0.3}, {0.4, 0.6}};
        for (auto& s : spans) {
            const double a = echoDensity(pl, s[0], s[1]), b = echoDensity(room, s[0], s[1]);
            denser &= a > b; detail += str(a).substr(0, 4) + "/" + str(b).substr(0, 4) + " ";
        }
        check(denser && echoDensity(pl, 0.4, 0.6) > 0.9, "PLATE is denser than the ROOM network from the first reflections on", detail);
        const double ripple = tailRipple(pl) / tailRipple(room);
        check(ripple < 0.6, "PLATE has less periodic ripple in the tail", "ratio " + str(ripple));
        double cross = 0;
        for (size_t i = 0; i < pl.size(); i++) cross += pl[i] * pr[i];
        const double corr = cross / std::sqrt(energy(pl, 0, 2) * energy(pr, 0, 2));
        check(std::abs(corr) < 0.1, "PLATE decorrelates left and right", str(corr));
    }
    {
        bool ok = true; std::string detail;
        for (double size : {0.3, 0.6, 0.84}) {
            Ir l, r; plateIr(size, 0.5, 6, l, r);
            const double ratio = rt60(l) / rt60(roomIr(size, 6));
            ok &= ratio > 0.75 && ratio < 1.3; detail += str(ratio).substr(0, 4) + " ";
        }
        check(ok && plate::decay(1) < 0.9, "PLATE SIZE sets the same decay time as the ROOM network", detail);
    }
    {
        auto hf = [](const Ir& x) { // energy of the first difference: a high-pass
            double e = 0;
            for (size_t i = (size_t)(0.5 * kSr) + 1; i < x.size(); i++) e += (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
            return e / energy(x, 0.5, 2);
        };
        Ir bl, br, dl, dr; plateIr(0.84, 0, 2, bl, br); plateIr(0.84, 1, 2, dl, dr);
        check(hf(dl) < 0.1 * hf(bl), "PLATE DAMP shortens the high-frequency tail", str(hf(dl) / hf(bl)));
    }
    {
        PlateReverb p; p.prepare(kSr); p.setParams(1, 0);
        unsigned seed = 7; double peak = 0;
        for (int i = 0; i < (int)kSr * 20; i++) {
            seed = seed * 1664525u + 1013904223u;
            p.process((double)seed / 2147483648.0 - 1.0);
            peak = std::max(peak, std::max(std::abs(p.outL), std::abs(p.outR)));
        }
        check(std::isfinite(p.outL) && peak < 20, "PLATE stays bounded at full size", str(peak));
    }
    {
        // Lengths scale with the sample rate: the same decay time at 96 kHz.
        // The excitation is a 0.5 ms Hann pulse, the same band at both rates
        // (an impulse has an octave more treble at 96 kHz, which DAMP removes
        // first and so steepens the early slope).
        auto pulseRt = [](double sr) {
            PlateReverb p; p.prepare(sr); p.setParams(0.84, 0.5);
            Ir y((size_t)(sr * 6));
            const int len = (int)(sr * 0.0005);
            for (size_t i = 0; i < y.size(); i++) {
                p.process((int)i < len ? 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * (double)i / len) : 0.0);
                y[i] = p.outL;
            }
            return rt60(y) * kSr / sr; // rt60() counts samples at kSr
        };
        const double ratio = pulseRt(96000) / pulseRt(48000);
        check(ratio > 0.93 && ratio < 1.07, "PLATE decay time is the same at 96 kHz", str(ratio));
    }

    // ---- in the FX chain ----
    ParamArray base = defaultParams();
    base[(size_t)FXCOMP_ON] = 0; base[(size_t)FXREVERB_ON] = 1;
    base[(size_t)FXREVERB_SIZE] = 0.8f; base[(size_t)FXREVERB_MIX] = 0.5f;
    check(paramInfo()[FXREVERB_TYPE].pid == "fx.reverb.type" && paramInfo()[FXREVERB_DAMP].pid == "fx.reverb.damp"
          && REVERB_TYPES.size() == 2, "reverb type and damp params are appended", "");
    const ChainRun roomRun = runChain(base, 2, 1);
    {
        ParamArray p = base; p[(size_t)FXREVERB_DAMP] = 1;
        const ChainRun again = runChain(p, 2, 1);
        check(again.l == roomRun.l && again.r == roomRun.r, "ROOM stays the default and ignores DAMP", "");
    }
    ParamArray plateParams = base; plateParams[(size_t)FXREVERB_TYPE] = 1;
    {
        const ChainRun plateRun = runChain(plateParams, 2, 1);
        const double held = 20 * std::log10(rmsOf(plateRun.l, 1, 2) / rmsOf(roomRun.l, 1, 2));
        const double tail = 20 * std::log10(rmsOf(plateRun.l, 2.05, 2.25) / rmsOf(roomRun.l, 2.05, 2.25));
        check(std::abs(held) < 1.5 && std::abs(tail) < 1.5, "PLATE returns a wet level within 1.5 dB of ROOM",
              "held " + str(held) + " dB, tail " + str(tail) + " dB");
        double cross = 0, l = 0, r = 0;
        for (size_t i = (size_t)(2.05 * kSr); i < plateRun.l.size(); i++) {
            cross += (double)plateRun.l[i] * plateRun.r[i]; l += (double)plateRun.l[i] * plateRun.l[i]; r += (double)plateRun.r[i] * plateRun.r[i];
        }
        check(std::abs(cross / std::sqrt(l * r)) < 0.2, "PLATE has a wide tail", str(cross / std::sqrt(l * r)));
    }
    {
        // PLATE tail -> ROOM -> PLATE again: the old tail must be gone.
        ParamArray roomP = plateParams; roomP[(size_t)FXREVERB_TYPE] = 0;
        const int offBlock = (int)(1.05 * kSr) / 128;
        const ChainRun run = runChain(plateParams, 1, 0.5, [&](Fx& fx, int block) {
            if (block == offBlock) fx.setParams(roomP);
            if (block == offBlock + 40) fx.setParams(plateParams);
        });
        const double back = rmsOf(run.l, 1.05 + 60 * 128 / kSr, 1.05 + 80 * 128 / kSr);
        check(back < 1e-5, "a reverb type change starts from silence", str(back));
    }
}

} // namespace platecheck
