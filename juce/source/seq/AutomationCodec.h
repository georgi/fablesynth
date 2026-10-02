#pragma once
#include "dsp/SeqModel.h"
#include <juce_core/juce_core.h>
namespace fable {
inline juce::var automationToVar(const std::vector<AutoLane>& lanes) {
    juce::Array<juce::var> values;
    for (const auto& l : lanes) {
        auto* o = new juce::DynamicObject();
        o->setProperty("target", juce::String(l.target)); o->setProperty("enabled", l.enabled);
        auto* time = new juce::DynamicObject();
        time->setProperty("mode", l.time.mode == AutoTime::Grid ? "grid" : l.time.mode == AutoTime::Fit ? "fit" : l.time.mode == AutoTime::Pad ? "pad" : "clip");
        if (l.time.mode == AutoTime::Grid || l.time.mode == AutoTime::Fit) time->setProperty("steps", l.time.steps);
        if (l.time.mode == AutoTime::Fit) time->setProperty("cycleBeats", l.time.cycleBeats);
        o->setProperty("time", juce::var(time));
        juce::Array<juce::var> points;
        for (const auto& p : l.points) {
            auto* point = new juce::DynamicObject(); point->setProperty("t", p.t); point->setProperty("v", p.v);
            if (p.c != 0) point->setProperty("c", p.c);
            if (p.hold) point->setProperty("hold", true);
            points.add(juce::var(point));
        }
        o->setProperty("points", points); values.add(juce::var(o));
    }
    return values;
}
inline bool automationFromVar(const juce::var& value, std::vector<AutoLane>& out, Machine machine) {
    auto number = [](const juce::var& v) { return (v.isDouble() || v.isInt() || v.isInt64()) && std::isfinite((double)v); };
    auto integer = [&](const juce::var& v, int lo, int hi) { return number(v) && (double)v >= lo && (double)v <= hi && std::floor((double)v) == (double)v; };
    const auto* list = value.getArray(); if (!list || list->size() > AUTO_MAX_LANES) return false;
    std::vector<AutoLane> lanes;
    for (const auto& v : *list) {
        if (!v.isObject() || !v["target"].isString() || !v["enabled"].isBool() || !v["time"].isObject()) return false;
        AutoLane l; l.target = v["target"].toString().toStdString(); l.enabled = (bool)v["enabled"];
        const auto time = v["time"]; const auto mode = time["mode"].toString();
        if (mode == "grid" || mode == "fit") {
            if (!integer(time["steps"], 1, AUTO_MAX_STEPS)) return false;
            l.time.mode = mode == "grid" ? AutoTime::Grid : AutoTime::Fit; l.time.steps = (int)time["steps"];
            if (mode == "fit") { if (!integer(time["cycleBeats"], 4, 8)) return false; l.time.cycleBeats = (int)time["cycleBeats"]; }
        } else if (mode == "pad") l.time.mode = AutoTime::Pad;
        else if (mode != "clip") return false;
        const auto* points = v["points"].getArray(); if (!points || points->size() > AUTO_MAX_POINTS) return false;
        for (const auto& p : *points) {
            if (!p.isObject() || !number(p["t"]) || !number(p["v"]) || (p.hasProperty("c") && !number(p["c"])) || (p.hasProperty("hold") && !p["hold"].isBool())) return false;
            l.points.push_back({ (double)p["t"], (double)p["v"], p.hasProperty("c") ? (double)p["c"] : 0, (bool)p["hold"] });
        }
        lanes.push_back(std::move(l));
    }
    if (!validateAutomation(lanes, machine)) return false;
    out = std::move(lanes); return true;
}
}
