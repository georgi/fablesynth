// Oscillator warp checks (Engine::renderOscWarp). The same measurements as
// src/engine/worklet.warp.test.ts: harmonic magnitudes of a steady A4 (an
// exact bin of the half-second window) and the power between the harmonics,
// which is aliasing.
#pragma once

#include "../source/dsp/Engine.h"
#include "../source/dsp/Params.h"
#include "../source/dsp/Wavetables.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace warpcheck {

using Check = std::function<void(bool, const std::string&, const std::string&)>;
using fable::Engine; using fable::ParamArray; using fable::TablePtr;
constexpr double kSr = 48000.0, kPi = 3.141592653589793;
// 264 and 220 cycles of A4. The long skip lets the voice DC blocker settle:
// a warped waveform has a DC offset, and its decay is not a harmonic.
constexpr int kSkip = 28800, kWin = 24000;

// A dry oscillator path: no filter, full sustain, osc A on the PRIME sine.
inline ParamArray dry() {
    ParamArray p = fable::defaultParams();
    p[(size_t)fable::paramIndex(fable::FILTER1_BASE, fable::FLT_ON)] = 0;
    p[(size_t)fable::ENV1_BASE] = 0.001f; p[(size_t)fable::ENV1_BASE + 2] = 1;
    p[(size_t)fable::paramIndex(fable::OSCA_BASE, fable::OSC_TABLE)] = 0;
    p[(size_t)fable::paramIndex(fable::OSCA_BASE, fable::OSC_POS)] = 0;
    p[(size_t)fable::paramIndex(fable::OSCA_BASE, fable::OSC_LEVEL)] = 0.5f;
    return p;
}
inline void set(ParamArray& p, int id, float v) { p[(size_t)id] = v; }
inline int oscA(fable::OscField f) { return fable::paramIndex(fable::OSCA_BASE, f); }
inline int oscB(fable::OscField f) { return fable::paramIndex(fable::OSCB_BASE, f); }

// Osc B as a silent sine modulator.
inline void sineB(ParamArray& p) {
    set(p, oscB(fable::OSC_ON), 1); set(p, oscB(fable::OSC_TABLE), 0);
    set(p, oscB(fable::OSC_POS), 0); set(p, oscB(fable::OSC_LEVEL), 0);
}

inline std::vector<float> take(Engine& e, int note) {
    const int n = ((kSkip + kWin + 127) / 128) * 128;
    std::vector<float> L((size_t)n), R((size_t)n);
    e.noteOn(note, 1.0);
    for (int at = 0; at < n; at += 128) e.render(L.data() + at, R.data() + at, 128);
    return std::vector<float>(L.begin() + kSkip, L.begin() + kSkip + kWin);
}
inline std::vector<float> renderNote(const std::vector<TablePtr>& tables, const ParamArray& p, int note = 69) {
    Engine e; e.prepare(kSr); e.setTables(tables); e.setParams(p);
    return take(e, note);
}

inline double magAt(const std::vector<float>& x, double hz) {
    const double w = 2 * kPi * hz / kSr;
    double re = 0, im = 0;
    for (size_t i = 0; i < x.size(); i++) { re += x[i] * std::cos(w * (double)i); im -= x[i] * std::sin(w * (double)i); }
    return 2 * std::hypot(re, im) / (double)x.size();
}
// Share of the signal power that is not on a harmonic of f0, in dB.
inline double inharmonicDb(const std::vector<float>& x, double f0) {
    double mean = 0, total = 0, harm = 0;
    for (float v : x) mean += v;
    mean /= (double)x.size();
    for (float v : x) total += (v - mean) * (v - mean);
    total /= (double)x.size();
    for (int k = 1; k * f0 < kSr / 2; k++) { const double m = magAt(x, k * f0); harm += m * m / 2; }
    return 10 * std::log10(std::max(1e-12, (total - harm) / total));
}
inline double maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double m = 0;
    for (size_t i = 0; i < a.size(); i++) m = std::max(m, (double)std::abs(a[i] - b[i]));
    return m;
}
inline double peakOf(const std::vector<float>& x) {
    double m = 0; for (float v : x) m = std::max(m, (double)std::abs(v)); return m;
}
inline double besselJ(int n, double x) {
    double term = std::pow(x / 2, n), sum = 0;
    for (int k = 1; k <= n; k++) term /= k;
    for (int k = 0; k < 30; k++) { sum += term; term *= -(x * x) / 4 / ((k + 1.0) * (k + 1.0 + n)); }
    return sum;
}

inline void runChecks(const std::vector<TablePtr>& tables, const Check& check) {
    using namespace fable;
    auto str = [](double v) { return std::to_string(v); };

    check(paramInfo()[OSCA_WARP].pid == "oscA.warp" && paramInfo()[OSCA_WARPAMT].pid == "oscA.warpamt"
          && paramInfo()[OSCB_WARP].pid == "oscB.warp" && paramInfo()[OSCB_WARPAMT].pid == "oscB.warpamt"
          && warpBase(1) == OSCB_WARP, "warp params are appended per oscillator", "");

    // Neutral settings leave the waveform alone.
    {
        ParamArray base = dry(); set(base, oscA(OSC_POS), 0.6f);
        const auto off = renderNote(tables, base);
        ParamArray amt = base; set(amt, OSCA_WARPAMT, 0.8f);
        check(maxDiff(renderNote(tables, amt), off) == 0, "warp amount has no effect while the mode is OFF", "");
        const std::pair<int, float> neutral[] = {{WARP_BEND, 0.5f}, {WARP_ASYM, 0.5f}, {WARP_PWM, 0}, {WARP_SYNC, 0}};
        for (auto [mode, a] : neutral) {
            ParamArray p = base; set(p, OSCA_WARP, (float)mode); set(p, OSCA_WARPAMT, a);
            const double d = maxDiff(renderNote(tables, p), off);
            check(d < 2e-3, WARP_MODES[(size_t)mode] + " at its neutral amount keeps the waveform", str(d));
            check(std::abs(a - warpNeutral(mode)) < 1e-9, WARP_MODES[(size_t)mode] + " neutral amount matches warpNeutral()", "");
        }
    }

    // Hard sync: band-limited reset and a slave pitch ceiling.
    {
        double worst = -200;
        for (float a : {0.2f, 0.35f, 0.62f, 0.9f}) {
            ParamArray p = dry(); set(p, oscA(OSC_POS), 0.66f); set(p, OSCA_WARP, WARP_SYNC); set(p, OSCA_WARPAMT, a);
            worst = std::max(worst, inharmonicDb(renderNote(tables, p), 440));
        }
        check(worst < -50, "hard SYNC of a saw keeps aliasing low", str(worst) + " dB");
        ParamArray p = dry(); set(p, oscA(OSC_POS), 0.66f); set(p, OSCA_WARP, WARP_SYNC); set(p, OSCA_WARPAMT, 1);
        const double top = inharmonicDb(renderNote(tables, p, 93), 1760);
        check(top < -40, "hard SYNC holds the slave below a quarter of the sample rate", str(top) + " dB");
        set(p, OSCA_WARP, WARP_SYNCWIN); set(p, OSCA_WARPAMT, 0.55f);
        const double win = inharmonicDb(renderNote(tables, p), 440);
        check(win < -60, "SYNC WIN stays band-limited", str(win) + " dB");
    }

    // Phase maps reshape a sine: new harmonics, no step.
    for (int mode : {WARP_BEND, WARP_ASYM, WARP_PWM, WARP_MIRROR}) {
        bool ok = true; double worst = -200;
        for (float a : {0.1f, 0.9f}) {
            ParamArray p = dry(); set(p, OSCA_WARP, (float)mode); set(p, OSCA_WARPAMT, a);
            const auto x = renderNote(tables, p);
            double upper = 0;
            for (int k = 2; k <= 6; k++) upper = std::max(upper, magAt(x, 440.0 * k));
            ok &= upper > 0.02 * magAt(x, 440);
            worst = std::max(worst, inharmonicDb(x, 440));
        }
        check(ok && worst < -38, WARP_MODES[(size_t)mode] + " reshapes a sine without a step", str(worst) + " dB");
    }

    // FM from osc B (level 0): the phase-modulation series of two sines.
    {
        ParamArray p = dry(); sineB(p); set(p, OSCA_WARP, WARP_FM); set(p, OSCA_WARPAMT, 0.5f);
        const auto x = renderNote(tables, p);
        const double carrier = magAt(renderNote(tables, dry()), 440);
        double sinePeak = 0;
        for (int i = 0; i < SIZE; i++) sinePeak = std::max(sinePeak, (double)std::abs(tables[0]->data[(size_t)i]));
        const double index = 0.5 * 0.5 * 2 * 2 * kPi * sinePeak;
        double err = 0;
        for (int n = 1; n <= 5; n++) {
            const double want = std::abs(besselJ(n - 1, index) + (n % 2 ? -1 : 1) * besselJ(n + 1, index));
            err = std::max(err, std::abs(magAt(x, 440.0 * n) / carrier - want));
        }
        check(err < 0.005, "FM matches the phase-modulation series of two sines", "max error " + str(err));

        // Every note has the same carrier/modulator phase relation.
        Engine e; e.prepare(kSr); e.setTables(tables);
        set(p, OSCA_WARPAMT, 0.6f); set(p, ENV1_BASE + 3, 0.01f);
        e.setParams(p);
        auto spectrum = [&] {
            const auto y = take(e, 69);
            e.noteOff(69);
            std::vector<float> L(12800), R(12800);
            for (int at = 0; at < 12800; at += 128) e.render(L.data() + at, R.data() + at, 128);
            std::vector<double> h;
            for (int k = 1; k <= 6; k++) h.push_back(magAt(y, 440.0 * k));
            return h;
        };
        const auto a = spectrum(), b = spectrum();
        double d = 0;
        for (size_t k = 0; k < a.size(); k++) d = std::max(d, std::abs(a[k] - b[k]));
        check(d < 1e-4, "FM gives every note the same timbre", str(d));

        // Without osc B the warp is off.
        ParamArray solo = dry(); set(solo, OSCA_WARP, WARP_FM); set(solo, OSCA_WARPAMT, 0.8f);
        const auto s = renderNote(tables, solo);
        check(magAt(s, 880) < 1e-3 * magAt(s, 440), "FM is off when osc B is off", "");
    }

    // RM and AM with the modulator an octave up.
    {
        ParamArray p = dry(); sineB(p); set(p, oscB(OSC_OCT), 1); set(p, OSCA_WARP, WARP_RM); set(p, OSCA_WARPAMT, 1);
        auto x = renderNote(tables, p);
        const double rm = magAt(x, 1320) / magAt(x, 440);
        check(std::abs(rm - 1) < 0.05 && magAt(x, 880) < 0.02 * magAt(x, 440),
              "RM of two sines an octave apart leaves equal first and third harmonics", str(rm));
        set(p, OSCA_WARP, WARP_AM);
        x = renderNote(tables, p);
        const double am = magAt(x, 1320) / magAt(x, 440);
        check(am > 0.2 && am < 0.8, "AM keeps the carrier and adds the sidebands", str(am));
    }

    // A WARP is a modulation destination.
    {
        ParamArray direct = dry(); set(direct, oscA(OSC_POS), 0.66f); set(direct, OSCA_WARP, WARP_SYNC); set(direct, OSCA_WARPAMT, 0.5f);
        ParamArray routed = direct; set(routed, OSCA_WARPAMT, 0); set(routed, MACRO1, 0.5f);
        set(routed, matBase(1) + MAT_SRC, 9); set(routed, matBase(1) + MAT_DST, 28); set(routed, matBase(1) + MAT_AMT, 1);
        check(dstTarget(28) == OSCA_WARPAMT && dstTarget(29) == OSCB_WARPAMT, "A WARP / B WARP destinations map to the warp amounts", "");
        const double d = maxDiff(renderNote(tables, direct), renderNote(tables, routed));
        check(d < 1e-6, "a routed warp amount equals the same direct amount", str(d));
    }

    // A warp mode change crossfades like a table switch.
    {
        ParamArray p = dry(); set(p, oscA(OSC_POS), 0.66f);
        Engine e; e.prepare(kSr); e.setTables(tables); e.setParams(p);
        std::vector<float> L(19200), R(19200);
        e.noteOn(45, 1.0);
        for (int at = 0; at < 9600; at += 128) e.render(L.data() + at, R.data() + at, 128);
        e.setParam(OSCA_WARP, WARP_MIRROR);
        for (int at = 9600; at < 19200; at += 128) e.render(L.data() + at, R.data() + at, 128);
        // The saw's own step is the largest sample-to-sample move before the switch.
        double before = 0, at = 0;
        for (int i = 4800; i < 9600; i++) before = std::max(before, (double)std::abs(L[(size_t)i] - L[(size_t)i - 1]));
        for (int i = 9600; i < 9600 + 480; i++) at = std::max(at, (double)std::abs(L[(size_t)i] - L[(size_t)i - 1]));
        check(at < before * 1.5, "a warp mode change does not click", str(at) + " vs " + str(before));
    }

    // Every mode stays finite and bounded at the extremes.
    {
        bool ok = true; std::string bad;
        for (int mode = 1; mode < (int)WARP_MODES.size(); mode++)
            for (float a : {0.0f, 1.0f})
                for (int note : {24, 108}) {
                    ParamArray p = dry();
                    set(p, oscA(OSC_POS), 0.66f); set(p, oscA(OSC_UNISON), 16); set(p, oscA(OSC_DETUNE), 1);
                    set(p, oscB(OSC_ON), 1); set(p, oscB(OSC_TABLE), 5); set(p, oscB(OSC_POS), 1); set(p, oscB(OSC_OCT), 3);
                    set(p, OSCA_WARP, (float)mode); set(p, OSCA_WARPAMT, a); set(p, OSCB_WARP, (float)mode); set(p, OSCB_WARPAMT, a);
                    const auto x = renderNote(tables, p, note);
                    bool fin = true;
                    for (float v : x) fin &= std::isfinite(v);
                    if (!fin || peakOf(x) >= 4.0) { ok = false; bad = WARP_MODES[(size_t)mode] + " note " + std::to_string(note); }
                }
        check(ok, "every warp mode stays finite and bounded at the extremes", bad);
    }
}

} // namespace warpcheck
