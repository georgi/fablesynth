#pragma once

#include "ParameterSource.h"
#include "../dsp/LfoShape.h"
#include "../dsp/Params.h"
#include "../seq/ui/AutomationEdit.h"

#include <array>
#include <vector>

// Drawn LFO shapes in the editor — C++ port of src/lfoShape.ts. A shape is a
// clip automation lane of kLane steps, so the editor reuses the lane's point,
// bend and paint operations (fable::autoedit). The points live in the flat
// `lfoN.p*` params.
namespace fui::lfoshape {

using Points = std::vector<fable::AutoPoint>;
constexpr int kLane = 64;   // lane length that holds one LFO cycle (256 positions)

// The parameters of one LFO's drawn shape, resolved once.
struct Params {
    juce::RangedAudioParameter* count = nullptr;
    std::array<juce::RangedAudioParameter*, 3 * fable::LFO_MAX_POINTS> points{};
    Params() = default;
    Params(const ParameterSource& source, const juce::String& lfo) {
        count = source.parameter(lfo + ".pn");
        for (int k = 0; k < fable::LFO_MAX_POINTS; ++k) {
            const juce::String pt = lfo + ".p" + juce::String(k + 1);
            points[(size_t)(3 * k)] = source.parameter(pt + ".x");
            points[(size_t)(3 * k + 1)] = source.parameter(pt + ".y");
            points[(size_t)(3 * k + 2)] = source.parameter(pt + ".c");
        }
    }
    bool valid() const {
        if (!count) return false;
        for (auto* p : points) if (!p) return false;
        return true;
    }
};
inline float real(juce::RangedAudioParameter* p) { return p->convertFrom0to1(p->getValue()); }

// The shape as lane points (t in 0..kLane).
inline Points read(const Params& ps) {
    Points out;
    if (!ps.valid()) return out;
    const int n = juce::jlimit(0, fable::LFO_MAX_POINTS, juce::roundToInt(real(ps.count)));
    for (int k = 0; k < n; ++k) {
        const float c = real(ps.points[(size_t)(3 * k + 2)]);
        out.push_back({(double)real(ps.points[(size_t)(3 * k)]) * kLane, (double)real(ps.points[(size_t)(3 * k + 1)]),
                       c > 1.5f ? 0.0 : (double)c, c > 1.5f});
    }
    return out;
}
// Store the points; only changed parameters are written.
inline void write(const Params& ps, const Points& points) {
    if (!ps.valid()) return;
    auto set = [](juce::RangedAudioParameter* p, float value) {
        const float norm = p->convertTo0to1(value);
        if (std::abs(norm - p->getValue()) < 1e-7f) return;
        p->beginChangeGesture(); p->setValueNotifyingHost(norm); p->endChangeGesture();
    };
    const int n = juce::jmin(fable::LFO_MAX_POINTS, (int)points.size());
    for (int k = 0; k < fable::LFO_MAX_POINTS; ++k) {
        const bool used = k < n;
        const fable::AutoPoint pt = used ? points[(size_t)k] : fable::AutoPoint{};
        set(ps.points[(size_t)(3 * k)], used ? (float)(pt.t / kLane) : 0.0f);
        set(ps.points[(size_t)(3 * k + 1)], used ? (float)pt.v : 0.5f);
        set(ps.points[(size_t)(3 * k + 2)], used ? (pt.hold ? fable::LFO_HOLD : (float)pt.c) : 0.0f);
    }
    set(ps.count, (float)n);
}
// Shape value in -1..+1 at a phase in cycles; an empty shape rests at 0.
inline double value(const Points& points, double phase) {
    if (points.empty()) return 0;
    return fable::evalLane(points, kLane, (phase - std::floor(phase)) * kLane) * 2 - 1;
}

// ---- edits: each returns the new points, or the old ones when the shape
// would need more than LFO_MAX_POINTS points ----
inline std::pair<Points, int> addPoint(const Points& points, double x, double y) {
    auto added = fable::autoedit::addPoint(points, x * kLane, y);
    if ((int)added.first.size() > fable::LFO_MAX_POINTS) return {points, -1};
    return added;
}
inline Points movePoint(const Points& points, int i, double x, double y) {
    return fable::autoedit::movePoint(points, kLane, i, x * kLane, y);
}
inline Points bendSegment(const Points& points, int i, double c) { return fable::autoedit::bendSegment(points, i, c); }
inline Points removePoint(const Points& points, int i) { return fable::autoedit::removePoint(points, i); }
// One held step of width 1/steps at the cell that holds x. The rest of the
// shape stays as drawn while the points last; after that the whole shape
// becomes `steps` held cells, so a paint stroke always lands.
inline Points paintStep(const Points& points, double x, double y, int steps) {
    const double grid = (double)kLane / steps;
    auto next = fable::autoedit::paintCell(points, kLane, x * kLane, y, grid);
    if ((int)next.size() <= fable::LFO_MAX_POINTS) return next;
    if (steps > fable::LFO_MAX_POINTS) return points;
    const int cell = juce::jmin(steps - 1, (int)std::floor(juce::jlimit(0.0, 0.999999, x) * steps));
    Points out;
    for (int s = 0; s < steps; ++s)
        out.push_back({s * grid, s == cell ? juce::jlimit(0.0, 1.0, y) : fable::evalLane(points, kLane, (s + 0.5) * grid), 0, true});
    return out;
}
// Index of the segment under phase x: the last point at or before it (wraps).
inline int segmentAt(const Points& points, double x) {
    int i = -1;
    for (int j = 0; j < (int)points.size(); ++j) if (points[(size_t)j].t <= x * kLane) i = j;
    return i < 0 ? (int)points.size() - 1 : i;
}

// A drawn starting point for each standard shape (LFO_SHAPES order), so CUSTOM
// begins as the shape the LFO played before.
inline Points seed(int shape) {
    const double q = 0.294; // bend that puts sin(45 deg) at the middle of a segment
    switch (shape) {
        case 0: return {{0, 0.5, -q}, {16, 1, q}, {32, 0.5, -q}, {48, 0, q}};
        case 2: return {{0, 1}, {kLane - 0.25, 0}};
        case 3: return {{0, 1, 0, true}, {32, 0, 0, true}};
        case 4: {
            const double v[8] = {0.62, 0.2, 0.85, 0.4, 0.05, 0.7, 0.3, 0.95};
            Points out;
            for (int i = 0; i < 8; ++i) out.push_back({i * 8.0, v[i], 0, true});
            return out;
        }
        default: return {{0, 0}, {32, 1}};
    }
}

} // namespace fui::lfoshape
