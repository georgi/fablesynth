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
    s->setProperty("input", a.chords ? "chords" : a.keys ? "keys" : "stored"); s->setProperty("latch", a.latch);
    juce::Array<juce::var> chords;
    for (int i = 0; i < std::clamp(a.chordCount, 0, kMaxArpChords); ++i) {
        const auto& c = a.chordList[(size_t)i]; auto* o = new juce::DynamicObject;
        o->setProperty("start", (int)c.start); o->setProperty("length", (int)c.length); o->setProperty("root", (int)c.root);
        o->setProperty("quality", chordQualities()[(size_t)std::min<int>(c.quality, kChordQualities - 1)].key);
        o->setProperty("inversion", (int)c.inversion); chords.add(juce::var(o));
    }
    s->setProperty("chords", chords); s->setProperty("chordKey", a.chordKey); s->setProperty("chordOctave", a.chordOctave);
    if (a.chordBars != 4) s->setProperty("chordBars", a.chordBars);
    if (a.chordLink) s->setProperty("chordLink", true);
    juce::Array<juce::var> comp, ties;
    for (auto m : a.compMask) comp.add((int)m);
    for (auto t : a.ties) ties.add(t);
    s->setProperty("style", a.comp ? "comp" : "arp"); s->setProperty("comp", comp); s->setProperty("ties", ties);
    static const char* dirs[] = {"up", "down", "alt"};
    static const char* voicings[] = {"close", "drop2", "drop24", "open"};
    s->setProperty("strum", a.strum); s->setProperty("strumDir", dirs[std::clamp(a.strumDir, 0, 2)]);
    s->setProperty("voicing", voicings[std::clamp(a.voicing, 0, 3)]);
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
    const auto input = s["input"].toString(); if (input != "stored" && input != "keys" && input != "chords") return false;
    if (!s["latch"].isBool()) return false;
    a.keys = input == "keys"; a.chords = input == "chords"; a.latch = (bool)s["latch"];
    if (hosted && (a.keys || a.latch)) return false;
    // Chord-track fields are optional: older states keep the defaults.
    auto whole = [&](const juce::var& n, int lo, int hi, int& out) {
        if (!numeric(n)) return false;
        const double d = (double)n; if (!std::isfinite(d) || d != std::floor(d) || d < lo || d > hi) return false;
        out = (int)d; return true;
    };
    if (!s["chordKey"].isVoid() && !whole(s["chordKey"], 0, 11, a.chordKey)) return false;
    if (!s["chordOctave"].isVoid() && !whole(s["chordOctave"], 0, 6, a.chordOctave)) return false;
    if (!s["chordBars"].isVoid() && !whole(s["chordBars"], 1, 4, a.chordBars)) return false;
    if (!s["chordLink"].isVoid()) { if (!s["chordLink"].isBool()) return false; a.chordLink = (bool)s["chordLink"]; }
    if (!s["chords"].isVoid()) {
        const auto* chords = s["chords"].getArray(); if (!chords || chords->size() > kMaxArpChords) return false;
        a.chordCount = chords->size();
        for (int i = 0; i < a.chordCount; ++i) {
            const auto& c = (*chords)[i]; int start, length, root, inversion;
            if (!whole(c["start"], 0, 63, start) || !whole(c["length"], 1, 64, length) || start + length > 64
                || !whole(c["root"], 0, 11, root) || !whole(c["inversion"], 0, 5, inversion)) return false;
            const auto key = c["quality"].toString(); int quality = -1;
            for (int k = 0; k < kChordQualities; ++k) if (key == chordQualities()[(size_t)k].key) quality = k;
            if (quality < 0) return false;
            a.chordList[(size_t)i] = {(uint8_t)start, (uint8_t)length, (uint8_t)root, (uint8_t)quality, (uint8_t)inversion};
        }
    }
    // Comp fields are optional: older states keep the arp style.
    if (!s["style"].isVoid()) {
        const auto style = s["style"].toString(); if (style != "arp" && style != "comp") return false;
        a.comp = style == "comp";
    }
    if (!lane("ties", a.ties, true)) return false;
    if (!s["comp"].isVoid()) {
        const auto* masks = s["comp"].getArray(); if (!masks || masks->size() != 16) return false;
        for (int i = 0; i < 16; ++i) { int m; if (!whole((*masks)[i], 0, 15, m)) return false; a.compMask[(size_t)i] = (uint8_t)m; }
    }
    if (!s["strum"].isVoid() && !whole(s["strum"], 0, kStrumMaxMs, a.strum)) return false;
    if (!s["strumDir"].isVoid()) { a.strumDir = juce::StringArray {"up", "down", "alt"}.indexOf(s["strumDir"].toString()); if (a.strumDir < 0) return false; }
    if (!s["voicing"].isVoid()) { a.voicing = juce::StringArray {"close", "drop2", "drop24", "open"}.indexOf(s["voicing"].toString()); if (a.voicing < 0) return false; }
    out = a; return true;
}
}
