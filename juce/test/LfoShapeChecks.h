// Drawn LFO shapes and the one-shot mode. The value checks use the same
// numbers as src/lfoShape.test.ts, so both engines are held to one result.
#pragma once

#include "../source/dsp/Engine.h"
#include "../source/dsp/LfoShape.h"
#include "../source/dsp/Params.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace lfocheck {

using Check = std::function<void(bool, const std::string&, const std::string&)>;
struct Point { float x, y, c; };

// Write a shape into LFO `n`'s drawn-shape params.
inline void setShape(fable::ParamArray& p, int n, const std::vector<Point>& points) {
    const int b = fable::lfoxBase(n);
    p[(size_t)(b + fable::LFOX_COUNT)] = (float)points.size();
    for (size_t k = 0; k < points.size(); k++) {
        p[(size_t)(b + fable::LFOX_POINTS) + 3 * k] = points[k].x;
        p[(size_t)(b + fable::LFOX_POINTS) + 3 * k + 1] = points[k].y;
        p[(size_t)(b + fable::LFOX_POINTS) + 3 * k + 2] = points[k].c;
    }
}
inline double value(const std::vector<Point>& points, double ph) {
    std::vector<float> flat;
    for (auto& pt : points) { flat.push_back(pt.x); flat.push_back(pt.y); flat.push_back(pt.c); }
    return fable::lfoShapeValue(flat.data(), (int)points.size(), ph);
}

inline void runChecks(const std::vector<fable::TablePtr>& tables, const Check& check) {
    using namespace fable;
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    auto str = [](double v) { return std::to_string(v); };

    // ---- params ----
    {
        const auto& info = paramInfo();
        bool ok = LFO_SHAPES.size() == 6 && LFO_SHAPES[(size_t)LFO_CUSTOM] == "CUSTOM";
        for (int n = 1; n <= 4; n++) {
            const std::string l = "lfo" + std::to_string(n);
            const int b = lfoxBase(n);
            ok &= info[(size_t)(b + LFOX_ONESHOT)].pid == l + ".oneshot" && !info[(size_t)(b + LFOX_ONESHOT)].hidden;
            ok &= info[(size_t)(b + LFOX_COUNT)].pid == l + ".pn" && info[(size_t)(b + LFOX_COUNT)].hidden;
            ok &= info[(size_t)(b + LFOX_POINTS)].pid == l + ".p1.x";
            ok &= info[(size_t)(b + LFOX_POINTS + 3 * (LFO_MAX_POINTS - 1) + 2)].pid == l + ".p16.c";
        }
        ok &= lfoxBase(4) + LFOX_NFIELDS - 1 == LFOX_LAST;
        check(ok, "drawn LFO shape params: one-shot, count and 16 points per LFO", "");
    }

    // ---- value: the cases of src/lfoShape.test.ts ----
    {
        const std::vector<Point> tri = {{0, 0, 0}, {0.5f, 1, 0}};
        check(near(value(tri, 0), -1) && near(value(tri, 0.25), 0) && near(value(tri, 0.5), 1) && near(value(tri, 0.75), 0),
              "drawn shape ramps and runs on from the last point to the first", "");
        const std::vector<Point> steps = {{0, 1, LFO_HOLD}, {0.5f, 0.25f, LFO_HOLD}};
        check(near(value(steps, 0.49), 1) && near(value(steps, 0.5), -0.5) && near(value(steps, 0.99), -0.5),
              "drawn shape holds steps", "");
        check(near(value({{0.25f, 0, 0}, {0.75f, 1, 0}}, 0), 0) && near(value({}, 0.3), 0),
              "drawn shape continues before its first point; an empty shape rests at zero", "");
        auto bent = [&](float c) { return value({{0, 0, c}, {0.5f, 1, LFO_HOLD}}, 0.25); };
        check(near(bent(0), 0) && near(bent(1), 2 / (1 + std::exp(3.0)) - 1) && near(bent(-1), -bent(1)),
              "bend: c > 0 starts slow, c < 0 starts fast", str(bent(1)));
    }

    // LFO 1 -> A POS at full depth: vizMod[1] is the LFO value.
    auto engine = [&](const ParamArray& p) {
        auto e = std::make_unique<Engine>();
        e->prepare(48000); e->setTables(tables); e->setParams(p);
        return e;
    };
    ParamArray base = defaultParams();
    base[(size_t)(matBase(1) + MAT_SRC)] = 1; base[(size_t)(matBase(1) + MAT_DST)] = 1; base[(size_t)(matBase(1) + MAT_AMT)] = 1;
    base[(size_t)(LFO1_BASE + LFO_SHAPE)] = (float)LFO_CUSTOM; base[(size_t)(LFO1_BASE + LFO_RATE)] = 2;
    auto collect = [](Engine& e, int blocks) {
        std::vector<float> L(128), R(128); std::vector<double> seen;
        for (int i = 0; i < blocks; i++) { e.render(L.data(), R.data(), 128); if (e.vizModAny) seen.push_back(e.vizMod[1]); }
        return seen;
    };
    {
        ParamArray p = base; setShape(p, 1, {{0, 1, LFO_HOLD}, {0.5f, 0.25f, LFO_HOLD}});
        auto e = engine(p); e->noteOn(60, 1.0);
        const auto seen = collect(*e, 750);
        bool only = !seen.empty(), hi = false, lo = false;
        for (double v : seen) { only &= near(v, 1) || near(v, -0.5); hi |= near(v, 1); lo |= near(v, -0.5); }
        check(only && hi && lo, "the engine plays the drawn steps", std::to_string(seen.size()) + " values");
    }
    {
        const std::vector<Point> ramp = {{0, 0, 0}, {255.0f / 256, 1, LFO_HOLD}};
        ParamArray p = base; setShape(p, 1, ramp); p[(size_t)(lfoxBase(1) + LFOX_ONESHOT)] = 1;
        auto e = engine(p); e->noteOn(60, 1.0);
        const auto seen = collect(*e, 750);
        bool rising = !seen.empty() && seen.front() < -0.7;
        for (size_t i = 1; i < seen.size(); i++) rising &= seen[i] >= seen[i - 1] - 1e-6;
        check(rising && near(seen.back(), 1), "ONE-SHOT runs one cycle, then holds the end of the shape", str(seen.back()));
        ParamArray loop = base; setShape(loop, 1, ramp);
        auto l = engine(loop); l->noteOn(60, 1.0);
        const auto again = collect(*l, 750);
        bool wrapped = false;
        for (size_t i = 1; i < again.size(); i++) wrapped |= again[i] < again[i - 1] - 0.5;
        check(wrapped, "without ONE-SHOT the drawn shape loops", "");
    }
    {
        // TRIG off + ONE-SHOT, standard SAW: every note starts at the top and stops at the bottom.
        ParamArray p = base;
        p[(size_t)(LFO1_BASE + LFO_SHAPE)] = 2; p[(size_t)(LFO1_BASE + LFO_RETRIG)] = 0;
        p[(size_t)(lfoxBase(1) + LFOX_ONESHOT)] = 1; p[(size_t)(ENV1_BASE + 3)] = 0.01f;
        auto e = engine(p);
        bool ok = true;
        for (int note = 0; note < 2; note++) {
            e->noteOn(60, 1.0);
            const auto seen = collect(*e, 400);
            ok &= !seen.empty() && seen.front() > 0.8 && near(seen.back(), -1);
            e->noteOff(60);
            collect(*e, 200);
        }
        check(ok, "ONE-SHOT restarts with each note, also with TRIG off", "");
    }
}

} // namespace lfocheck
