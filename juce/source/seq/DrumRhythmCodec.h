#pragma once
#include "dsp/SeqModel.h"
#include <juce_core/juce_core.h>

namespace fable {
inline juce::var drumRhythmToVar(const ClipData& clip) {
    auto* root = new juce::DynamicObject(); root->setProperty("v", 1);
    juce::Array<juce::var> lanes;
    for (int i = 0; i < 16; ++i) {
        const auto& lane = clip.drumRhythm.lanes[(size_t)i];
        if (!(clip.drumConfiguredLanes & (1u << i)) && !lane.scheduled()) { lanes.add(juce::var()); continue; }
        auto* obj = new juce::DynamicObject();
        obj->setProperty("enabled", lane.enabled); obj->setProperty("sourceBar", lane.sourceBar);
        obj->setProperty("steps", lane.steps); obj->setProperty("rotation", lane.rotation);
        auto* timing = new juce::DynamicObject();
        timing->setProperty("mode", lane.mode == DrumRhythmMode::fit ? "fit" : "grid");
        if (lane.mode == DrumRhythmMode::fit) timing->setProperty("cycleBeats", lane.cycleBeats);
        obj->setProperty("timing", juce::var(timing));
        if (lane.micro) {
            obj->setProperty("delayMs", lane.delayMs);
            juce::Array<juce::var> delays;
            int count = 256; while (count && lane.stepDelayMs[(size_t)count - 1] == 0) --count;
            for (int step = 0; step < count; ++step) delays.add(lane.stepDelayMs[(size_t)step]);
            obj->setProperty("stepDelayMs", delays);
        }
        lanes.add(juce::var(obj));
    }
    root->setProperty("lanes", lanes); return juce::var(root);
}
inline bool drumRhythmFromVar(const juce::var& value, ClipData& clip, Machine machine) {
    const auto integer = [](const juce::var& v, int lo, int hi) {
        if (!v.isInt() && !v.isInt64() && !v.isDouble()) return false;
        const double n = (double)v;
        return std::isfinite(n) && std::floor(n) == n && n >= lo && n <= hi;
    };
    if (!value.isObject() || !integer(value["v"], 1, 1)) return false;
    const auto* lanes = value["lanes"].getArray();
    if (!lanes || lanes->size() != 16) return false;
    ClipData next = clip; next.hasDrumRhythm = true; next.drumRhythm = {}; next.drumConfiguredLanes = 0;
    for (int i = 0; i < 16; ++i) {
        const auto& obj = lanes->getReference(i);
        if (obj.isVoid() || obj.isUndefined()) continue;
        if (!obj.isObject() || !obj["enabled"].isBool() || !integer(obj["sourceBar"], 0, clip.bars - 1)
            || !integer(obj["steps"], 1, 16) || !integer(obj["rotation"], 0, (int)obj["steps"] - 1)) return false;
        auto& lane = next.drumRhythm.lanes[(size_t)i]; next.drumConfiguredLanes |= (uint16_t)(1u << i);
        lane.enabled = (bool)obj["enabled"]; lane.sourceBar = (int)obj["sourceBar"];
        lane.steps = (int)obj["steps"]; lane.rotation = (int)obj["rotation"];
        const auto timing = obj["timing"]; if (!timing.isObject()) return false;
        if (timing["mode"].toString() == "fit") {
            if (!integer(timing["cycleBeats"], 4, 8) || ((int)timing["cycleBeats"] != 4 && (int)timing["cycleBeats"] != 8)) return false;
            lane.mode = DrumRhythmMode::fit; lane.cycleBeats = (int)timing["cycleBeats"];
        } else if (timing["mode"].toString() != "grid") return false;
        if (obj.hasProperty("delayMs")) {
            if (!integer(obj["delayMs"], -50, 50)) return false;
            lane.micro = true; lane.delayMs = (int)obj["delayMs"];
        }
        if (obj.hasProperty("stepDelayMs")) {
            const auto* delays = obj["stepDelayMs"].getArray();
            if (!delays || delays->size() > 256) return false;
            lane.micro = true;
            for (int step = 0; step < delays->size(); ++step) {
                if (!integer(delays->getReference(step), -50, 50)) return false;
                lane.stepDelayMs[(size_t)step] = (int)delays->getReference(step);
            }
        }
    }
    if (!validDrumClip(next, machine)) return false;
    clip = std::move(next); return true;
}
}
