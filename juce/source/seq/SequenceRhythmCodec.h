#pragma once
#include "dsp/SeqModel.h"
#include <juce_core/juce_core.h>

namespace fable {
inline juce::var sequenceRhythmToVar(const SequenceRhythm& r) {
    auto* o = new juce::DynamicObject();
    o->setProperty("v", 1); o->setProperty("enabled", r.enabled);
    o->setProperty("mode", r.fit ? "fit" : "grid");
    o->setProperty("steps", r.steps); o->setProperty("rotation", r.rotation);
    o->setProperty("cycleBeats", r.cycleBeats);
    return juce::var(o);
}
inline bool sequenceRhythmFromVar(const juce::var& value, SequenceRhythm& out) {
    const auto integer = [](const juce::var& v, int lo, int hi) {
        if (!v.isInt() && !v.isInt64() && !v.isDouble()) return false;
        const double n = (double)v;
        return std::isfinite(n) && std::floor(n) == n && n >= lo && n <= hi;
    };
    if (!value.isObject() || !integer(value["v"], 1, 1) || !value["enabled"].isBool()) return false;
    const auto mode = value["mode"].toString();
    if (mode != "grid" && mode != "fit") return false;
    if (!integer(value["steps"], 1, 16) || !integer(value["rotation"], 0, (int)value["steps"] - 1)) return false;
    if (!integer(value["cycleBeats"], 4, 8) || ((int)value["cycleBeats"] != 4 && (int)value["cycleBeats"] != 8)) return false;
    out.enabled = (bool)value["enabled"]; out.fit = mode == "fit";
    out.steps = (int)value["steps"]; out.rotation = (int)value["rotation"];
    out.cycleBeats = (int)value["cycleBeats"];
    return true;
}
} // namespace fable
