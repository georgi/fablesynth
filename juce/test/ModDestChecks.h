// The modulation destinations from 30 up: LFO rates, envelope times, FX
// controls and the amount of another route. The same cases as
// src/engine/worklet.dests.test.ts.
#pragma once

#include "../source/dsp/Engine.h"
#include "../source/dsp/Fx.h"
#include "../source/dsp/Params.h"

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace moddestcheck {

using Check = std::function<void(bool, const std::string&, const std::string&)>;
constexpr int kMacro1 = 9, kVelo = 4, kLfo1 = 1;

inline void route(fable::ParamArray& p, int slot, int src, int dst, float amt) {
    p[(size_t)(fable::matBase(slot) + fable::MAT_SRC)] = (float)src;
    p[(size_t)(fable::matBase(slot) + fable::MAT_DST)] = (float)dst;
    p[(size_t)(fable::matBase(slot) + fable::MAT_AMT)] = amt;
}
inline std::unique_ptr<fable::Engine> engine(const std::vector<fable::TablePtr>& tables, const fable::ParamArray& p) {
    auto e = std::make_unique<fable::Engine>();
    e->prepare(48000); e->setTables(tables); e->setParams(p);
    return e;
}
inline double rmsOf(const std::vector<float>& x, size_t from, size_t to) {
    double e = 0;
    for (size_t i = from; i < to; i++) e += (double)x[i] * x[i];
    return std::sqrt(e / (double)(to - from));
}

inline void runChecks(const std::vector<fable::TablePtr>& tables, const Check& check) {
    using namespace fable;
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-5; };
    auto str = [](double v) { return std::to_string(v); };

    // ---- table ----
    {
        bool ok = (int)MOD_DESTS.size() == NUM_MOD_DESTS && MOD_DESTS[30] == "LFO1 RATE" && MOD_DESTS[48] == "REVERB MIX"
            && MOD_DESTS[(size_t)DST_ROUTE_AMT] == "MOD 1 AMT" && MOD_DESTS[64] == "MOD 16 AMT";
        ok &= dstTarget(30) == paramIndex(LFO1_BASE, LFO_RATE) && dstTarget(33) == paramIndex(LFO4_BASE, LFO_RATE)
            && dstTarget(35) == ENV1_BASE + 1 && dstTarget(39) == ENV2_BASE + 3 && dstTarget(40) == ENV3_BASE
            && dstTarget(43) == FXDRIVE_AMT && dstTarget(46) == FXDELAY_FB && dstTarget(48) == FXREVERB_MIX
            && dstTarget(49) == matBase(1) + MAT_AMT && dstTarget(64) == matBase(16) + MAT_AMT && dstTarget(65) == DST_NONE;
        ok &= dstRouteSlot(49) == 1 && dstRouteSlot(64) == 16 && dstRouteSlot(48) == 0 && dstRouteSlot(65) == 0;
        check(ok, "destinations 30..64: LFO rates, envelope times, FX controls, route amounts", "");
    }

    // ---- LFO rate: LFO 1 (saw, 1 Hz) -> A POS shows the LFO; a macro on LFO1 RATE speeds it up ----
    auto wraps = [&](float macro, float amt, bool retrig) {
        ParamArray p = defaultParams();
        p[(size_t)(LFO1_BASE + LFO_SHAPE)] = 2; p[(size_t)(LFO1_BASE + LFO_RATE)] = 1; p[(size_t)(LFO1_BASE + LFO_RETRIG)] = retrig ? 1 : 0;
        route(p, 1, kLfo1, 1, 1); route(p, 2, kMacro1, 30, amt); p[(size_t)MACRO1] = macro;
        auto e = engine(tables, p); e->noteOn(60, 1.0);
        std::vector<float> L(128), R(128);
        int count = 0; double prev = 0; bool have = false;
        for (int i = 0; i < 375; i++) {
            e->render(L.data(), R.data(), 128);
            const double v = e->vizMod[1];
            if (have && v > prev + 1) count++;
            prev = v; have = true;
        }
        return count;
    };
    check(wraps(0, 0.2f, true) == 0 && wraps(1, 0.2f, true) == 1 && wraps(1, 0.4f, true) == 3,
          "LFO1 RATE scales a per-voice LFO by 2^(5x)", std::to_string(wraps(1, 0.4f, true)));
    check(wraps(1, 0.4f, false) >= 3 && wraps(0, 0.4f, false) <= 1, "LFO1 RATE scales a free-running LFO too",
          std::to_string(wraps(1, 0.4f, false)));

    // ---- envelope times ----
    auto late = [&](float decay, float macro, int dst, float amt) {
        ParamArray p = defaultParams();
        p[(size_t)paramIndex(FILTER1_BASE, FLT_ON)] = 0; p[(size_t)ENV1_BASE] = 0.001f; p[(size_t)(ENV1_BASE + 1)] = decay; p[(size_t)(ENV1_BASE + 2)] = 0;
        if (dst) { route(p, 1, kMacro1, dst, amt); p[(size_t)MACRO1] = macro; }
        auto e = engine(tables, p); e->noteOn(60, 1.0);
        std::vector<float> L(10240), R(10240);
        for (int at = 0; at < 10240; at += 128) e->render(L.data() + at, R.data() + at, 128);
        return rmsOf(L, 7200, 9600); // 150..200 ms after the note start
    };
    {
        const double plain = late(0.05f, 0, 0, 0), longer = late(0.05f, 1, 35, 0.4f), knob = late(0.2f, 0, 0, 0);
        check(longer > plain * 20 && std::abs(longer - knob) < knob * 0.01, "AMP DEC follows its route like the knob",
              str(longer) + " vs " + str(knob));
        check(std::isfinite(late(0.05f, 1, 34, -1)), "a routed envelope time stays inside the knob range", "");
    }

    // ---- route amount (mod of mod): VELO -> A POS 0.2, MACRO 1 -> MOD 1 AMT ----
    auto pos = [&](float macro, float amt2, bool swapped, bool third) {
        ParamArray p = defaultParams();
        if (swapped) { route(p, 1, kMacro1, DST_ROUTE_AMT + 1, amt2); route(p, 2, kVelo, 1, 0.2f); }
        else { route(p, 1, kVelo, 1, 0.2f); route(p, 2, kMacro1, DST_ROUTE_AMT, amt2); }
        if (third) route(p, 3, kVelo, DST_ROUTE_AMT + 1, 1);
        p[(size_t)MACRO1] = macro;
        auto e = engine(tables, p); e->noteOn(60, 1.0);
        std::vector<float> L(128), R(128);
        e->render(L.data(), R.data(), 128);
        return e->vizMod[1];
    };
    check(near(pos(0, 0.25f, false, false), 0.2) && near(pos(1, 0.25f, false, false), 0.7)
          && near(pos(1, 1, false, false), 1) && near(pos(1, -1, false, false), -1),
          "MOD 1 AMT moves the amount of route 1, inside -1..1", str(pos(1, 0.25f, false, false)));
    check(near(pos(1, 0.25f, true, false), 0.7), "a route amount route does not depend on the slot order", "");
    check(near(pos(1, 0.25f, false, true), 0.7), "a route on a route amount is not itself modulated by a third", "");

    // ---- FX: a macro opens the delay mix (engine -> Fx::setMod) ----
    auto echo = [&](float macro, float mix, bool routed) {
        ParamArray p = defaultParams();
        p[(size_t)paramIndex(FILTER1_BASE, FLT_ON)] = 0; p[(size_t)FXCOMP_ON] = 0;
        p[(size_t)ENV1_BASE] = 0.001f; p[(size_t)(ENV1_BASE + 2)] = 1; p[(size_t)(ENV1_BASE + 3)] = 0.005f;
        p[(size_t)FXDELAY_ON] = 1; p[(size_t)FXDELAY_TIME] = 0.25f; p[(size_t)FXDELAY_FB] = 0; p[(size_t)FXDELAY_MIX] = mix;
        if (routed) route(p, 1, kMacro1, 47, 1);
        p[(size_t)MACRO1] = macro;
        auto e = engine(tables, p);
        Fx fx; fx.prepare(48000); fx.setParams(p);
        std::vector<float> L(16896), R(16896);
        e->noteOn(60, 1.0);
        for (int at = 0; at < 16896; at += 128) {
            if (at == 38 * 128) e->noteOff(60);
            e->render(L.data() + at, R.data() + at, 128);
            fx.setMod(e->fxMod());
            fx.process(L.data() + at, R.data() + at, 128);
        }
        return rmsOf(L, 38 * 128 + 7200, 38 * 128 + 12000); // around the first echo
    };
    {
        const double dry = echo(0, 0, true), wet = echo(0.6f, 0, true), knob = echo(0, 0.6f, false);
        check(dry < 1e-4 && wet > 0.005 && std::abs(wet - knob) < knob * 0.02, "a macro opens the delay mix like the knob",
              str(wet) + " vs " + str(knob));
    }
    {
        // The macro still moves the mix after the last voice ended.
        ParamArray p = defaultParams();
        p[(size_t)paramIndex(FILTER1_BASE, FLT_ON)] = 0; p[(size_t)FXCOMP_ON] = 0;
        p[(size_t)ENV1_BASE] = 0.001f; p[(size_t)(ENV1_BASE + 2)] = 1; p[(size_t)(ENV1_BASE + 3)] = 0.005f;
        p[(size_t)FXDELAY_ON] = 1; p[(size_t)FXDELAY_TIME] = 0.25f; p[(size_t)FXDELAY_FB] = 0.8f; p[(size_t)FXDELAY_MIX] = 0;
        route(p, 1, kMacro1, 47, 1);
        auto e = engine(tables, p);
        Fx fx; fx.prepare(48000); fx.setParams(p);
        std::vector<float> L(128), R(128);
        auto run = [&](int blocks) {
            double sum = 0;
            for (int i = 0; i < blocks; i++) {
                e->render(L.data(), R.data(), 128);
                fx.setMod(e->fxMod());
                fx.process(L.data(), R.data(), 128);
                for (float v : L) sum += (double)v * v;
            }
            return std::sqrt(sum / (blocks * 128.0));
        };
        e->noteOn(60, 1.0); run(38); e->noteOff(60); run(60);
        const double before = run(150);
        e->setParam(MACRO1, 0.7f);
        run(20);
        const double after = run(150);
        check(before < 1e-4 && after > 0.002, "the macro still moves the delay mix after the last voice ended",
              str(before) + " -> " + str(after));
    }
}

} // namespace moddestcheck
