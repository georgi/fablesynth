#pragma once
#include <juce_core/juce_core.h>
#include "../dsp/Arp.h"

namespace fable {
inline juce::var arpToVar(const ArpSettings& a) {
    auto* root = new juce::DynamicObject; root->setProperty("enabled", a.enabled);
    auto* s = new juce::DynamicObject;
    juce::Array<juce::var> notes, hits, accents, slides;
    for (int i = 0; i < a.count; ++i) notes.add(a.notes[i]);
    for (auto v : a.hits) hits.add(v);
    for (auto v : a.accents) accents.add(v);
    for (auto v : a.slides) slides.add(v);
    static const char* orders[] = {"up", "down", "updown", "played", "random"};
    s->setProperty("notes", notes); s->setProperty("hits", hits); s->setProperty("accents", accents); s->setProperty("slides", slides);
    s->setProperty("order", orders[std::clamp(a.order, 0, 4)]); s->setProperty("rate", a.rate);
    s->setProperty("octaves", a.octaves); s->setProperty("gate", a.gate); s->setProperty("seed", (juce::int64)a.seed);
    s->setProperty("input", a.keys ? "keys" : "stored"); s->setProperty("latch", a.latch);
    root->setProperty("settings", juce::var(s)); return juce::var(root);
}
inline bool arpFromVar(const juce::var& v, ArpSettings& out, bool hosted = false) {
    if (!v.isObject() || !v["enabled"].isBool()) return false;
    const auto s = v["settings"]; if (!s.isObject()) return false;
    auto numeric = [](const juce::var& n) { return n.isInt() || n.isInt64() || n.isDouble(); };
    ArpSettings a; a.enabled = (bool)v["enabled"];
    const auto* notes = s["notes"].getArray(); if (!notes || notes->size() > 128) return false;
    a.count = notes->size();
    for (int i = 0; i < a.count; ++i) {
        if (!numeric((*notes)[i])) return false;
        const double n = (double)(*notes)[i]; if (!std::isfinite(n) || n != std::floor(n) || n < 0 || n > 127) return false;
        a.notes[i] = (int)n;
    }
    auto lane = [&](const char* key, auto& target, bool optional = false) {
        if (optional && s[key].isVoid()) return true;
        const auto* values = s[key].getArray(); if (!values || values->size() != 16) return false;
        for (int i = 0; i < 16; ++i) { if (!(*values)[i].isBool()) return false; target[i] = (bool)(*values)[i]; }
        return true;
    };
    if (!lane("hits", a.hits) || !lane("accents", a.accents) || !lane("slides", a.slides, true)) return false;
    const juce::StringArray orders {"up", "down", "updown", "played", "random"};
    a.order = orders.indexOf(s["order"].toString()); if (a.order < 0) return false;
    for (const char* k : {"rate", "gate", "octaves", "seed"}) if (!numeric(s[k])) return false;
    a.rate = (double)s["rate"]; a.gate = (double)s["gate"];
    const std::array<double, 8> rates {{.125, .25, .5, 1, 1.0/3, 1.0/6, .375, .75}};
    if (std::find(rates.begin(), rates.end(), a.rate) == rates.end() || !std::isfinite(a.gate) || a.gate < .05 || a.gate > .95) return false;
    const double oct = (double)s["octaves"], seed = (double)s["seed"];
    if (oct != std::floor(oct) || oct < 1 || oct > 3 || !std::isfinite(seed) || seed != std::floor(seed)) return false;
    a.octaves = (int)oct;
    double reduced = std::fmod(seed, 4294967296.0); if (reduced < 0) reduced += 4294967296.0;
    a.seed = (uint32_t)reduced;
    const auto input = s["input"].toString(); if (input != "stored" && input != "keys") return false;
    if (!s["latch"].isBool()) return false;
    a.keys = input == "keys"; a.latch = (bool)s["latch"];
    if (hosted && (a.keys || a.latch)) return false;
    out = a; return true;
}
}
