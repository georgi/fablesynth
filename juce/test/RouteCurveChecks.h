// Response curve per modulation route (mat<n>.crv). The same numbers as
// src/engine/worklet.curve.test.ts.
#pragma once

#include "../source/dsp/Engine.h"
#include "../source/dsp/Params.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace routecurvecheck {

using Check = std::function<void(bool, const std::string&, const std::string&)>;

inline void runChecks(const std::vector<fable::TablePtr>& tables, const Check& check) {
    using namespace fable;
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-5; };
    const double slow = 1 / (1 + std::exp(3.0)); // bend(0.5, +1)

    check(paramInfo()[MATCRV1].pid == "mat1.crv" && paramInfo()[MATCRV_LAST].pid == "mat16.crv"
          && !paramInfo()[MATCRV1].hidden, "response curve params mat1.crv..mat16.crv", "");
    check(near(routeCurve(0.5, 0, false), 0.5) && near(routeCurve(0.5, 1, false), slow) && near(routeCurve(0.5, -1, false), 1 - slow)
          && near(routeCurve(0, 1, false), 0) && near(routeCurve(1, 1, false), 1) && near(routeCurve(1.7, -1, false), 1),
          "routeCurve bends a unipolar value and keeps both ends", "");
    check(near(routeCurve(-0.5, 1, true), -slow) && near(routeCurve(0.5, 1, true), slow) && near(routeCurve(-1, -0.4, true), -1),
          "routeCurve keeps the sign of a bipolar value", "");

    // VELO (0.5) -> A POS at full depth: vizMod[1] is the curved source value.
    auto routed = [&](int src, float curve, int note, double vel) {
        Engine e; e.prepare(48000); e.setTables(tables);
        ParamArray p = defaultParams();
        p[(size_t)(matBase(1) + MAT_SRC)] = (float)src; p[(size_t)(matBase(1) + MAT_DST)] = 1; p[(size_t)(matBase(1) + MAT_AMT)] = 1;
        p[(size_t)MATCRV1] = curve;
        e.setParams(p);
        std::vector<float> L(128), R(128);
        e.noteOn(note, vel);
        e.render(L.data(), R.data(), 128);
        return e.vizMod[1];
    };
    check(near(routed(4, 0, 60, 0.5), 0.5) && near(routed(4, 1, 60, 0.5), slow) && near(routed(4, -1, 60, 0.5), 1 - slow),
          "the engine applies the curve to a unipolar source", std::to_string(routed(4, 1, 60, 0.5)));
    // NOTE is bipolar: C3 is -0.5.
    check(near(routed(5, 0, 48, 1), -0.5) && near(routed(5, 1, 48, 1), -slow),
          "the engine applies the curve to the size of a bipolar source", std::to_string(routed(5, 1, 48, 1)));
}

} // namespace routecurvecheck
