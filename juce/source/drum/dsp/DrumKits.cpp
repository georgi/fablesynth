// Transcription of src/drum/kits.ts:18-113. ROOM ONE and BITCRUSH derive from
// trVoidParams() exactly like the web (same rounding/clamps), so the three
// kits stay lockstep with the reference by construction.
#include "DrumKits.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace fable {
namespace {

const std::array<std::string, DR_NPADS> kPadNames = {
    "KICK", "KICK 2", "SNARE", "CLAP", "RIM", "CH HAT", "OH HAT", "RIDE",
    "TOM LO", "TOM MD", "TOM HI", "CRASH", "PERC 1", "PERC 2", "VOX", "GLITCH",
};

using Overrides = std::vector<std::pair<std::string, float>>;

// Map semantics like the TS Partial<ParamValues>: assigning an existing key
// overwrites it (derived kits re-set keys trVoidParams already wrote).
void set(Overrides& o, const std::string& pid, float v) {
    for (auto& [p, val] : o)
        if (p == pid) { val = v; return; }
    o.emplace_back(pid, v);
}
float get(const Overrides& o, const std::string& pid, float fallback) {
    for (const auto& [p, val] : o)
        if (p == pid) return val;
    return fallback;
}
std::string padPid(int i, const char* field) {
    return "pad" + std::to_string(i) + "." + field;
}

int patIdx(int pat, int padI, int step) {
    return pat * DR_NPADS * DR_STEPS + padI * DR_STEPS + step;
}

// kits.ts trVoidPatterns()
std::vector<uint8_t> trVoidPatterns() {
    std::vector<uint8_t> patterns(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
    auto setSteps = [&](int padI, std::initializer_list<int> steps,
                        std::initializer_list<int> accents = {}) {
        for (int step : steps) {
            bool accent = std::find(accents.begin(), accents.end(), step) != accents.end();
            patterns[(size_t)patIdx(0, padI, step)] = accent ? 2 : 1;
        }
    };
    setSteps(0, { 0, 4, 8, 12 }, { 0 });
    setSteps(2, { 4, 12 }, { 12 });
    setSteps(3, { 4, 12 });
    setSteps(4, { 7 });
    setSteps(5, { 0, 2, 4, 6, 8, 10, 12 }, { 4, 12 });
    setSteps(6, { 14 }, { 14 });
    setSteps(12, { 3, 11 });
    setSteps(14, { 10 });
    return patterns;
}

// kits.ts trVoidParams()
Overrides trVoidParams() {
    Overrides p;
    set(p, "seq.bpm", 126.0f);
    set(p, "master.swing", 0.22f);
    set(p, "fx.comp.on", 1.0f);
    set(p, "fx.reverb.on", 1.0f);
    set(p, "fx.reverb.mix", 0.16f);
    // [oscA.table, oscA.tune, penv.amt, aenv.dec] per pad
    static const float kSounds[DR_NPADS][4] = {
        { 0, -14, 22, 0.30f }, { 0,  -7, 16, 0.24f }, { 1,  0,  5, 0.18f }, { 1,   7,  2, 0.14f },
        { 2,  24,  0, 0.08f }, { 2,  18,  0, 0.04f }, { 2, 12,  0, 0.30f }, { 2,   5,  0, 1.40f },
        { 0, -12, 12, 0.42f }, { 0,  -5, 10, 0.36f }, { 0,  2,  8, 0.30f }, { 2,   0,  0, 1.80f },
        { 1,  12, -5, 0.16f }, { 2,  19,  0, 0.20f }, { 7, -5,  0, 0.48f }, { 3, -12,  9, 0.22f },
    };
    for (int i = 0; i < DR_NPADS; i++) {
        set(p, padPid(i, "oscA.table"), kSounds[i][0]);
        set(p, padPid(i, "oscA.tune"), kSounds[i][1]);
        set(p, padPid(i, "penv.amt"), kSounds[i][2]);
        set(p, padPid(i, "aenv.dec"), kSounds[i][3]);
    }
    set(p, padPid(0, "penv.dec"), 0.06f);
    set(p, padPid(2, "noise.level"), 0.5f);
    set(p, padPid(2, "noise.color"), 0.3f);
    set(p, padPid(3, "noise.level"), 0.35f);
    for (int i : { 5, 6 }) {
        set(p, padPid(i, "choke"), 1.0f);
        set(p, padPid(i, "flt.on"), 1.0f);
        set(p, padPid(i, "flt.type"), 3.0f);
        set(p, padPid(i, "flt.cut"), i == 5 ? 7200.0f : 5200.0f);
    }
    return p;
}

// kits.ts roomOneParams()
Overrides roomOneParams() {
    Overrides p = trVoidParams();
    set(p, "seq.bpm", 116.0f);
    set(p, "master.swing", 0.16f);
    set(p, "fx.reverb.mix", 0.3f);
    set(p, "fx.reverb.size", 0.62f);
    set(p, "fx.chorus.on", 1.0f);
    set(p, "fx.chorus.mix", 0.14f);
    for (int i = 0; i < DR_NPADS; i++) {
        set(p, padPid(i, "penv.amt"),
            std::round(get(p, padPid(i, "penv.amt"), 0.0f) * 0.55f));
        set(p, padPid(i, "aenv.hold"), i < 4 ? 0.025f : 0.015f);
        set(p, padPid(i, "aenv.dec"),
            std::min(4.0f, get(p, padPid(i, "aenv.dec"), 0.24f) * 1.25f));
    }
    return p;
}

// kits.ts bitcrushParams()
Overrides bitcrushParams() {
    Overrides p = trVoidParams();
    set(p, "seq.bpm", 140.0f);
    set(p, "master.swing", 0.08f);
    set(p, "fx.drive.on", 1.0f);
    set(p, "fx.drive.amt", 0.6f);
    set(p, "fx.drive.mix", 0.9f);
    set(p, "fx.delay.on", 1.0f);
    set(p, "fx.delay.time", 0.18f);
    set(p, "fx.delay.fb", 0.42f);
    set(p, "fx.delay.mix", 0.22f);
    for (int i = 0; i < DR_NPADS; i++) {
        set(p, padPid(i, "oscA.table"), 3.0f);
        set(p, padPid(i, "oscA.pos"), (float)(i % 4) / 4.0f);
        set(p, padPid(i, "aenv.dec"),
            std::max(0.04f, std::min(0.65f, get(p, padPid(i, "aenv.dec"), 0.24f))));
    }
    return p;
}

} // namespace

const std::vector<DrumKit>& factoryKits() {
    static const std::vector<DrumKit> kits = [] {
        const std::vector<uint8_t> patterns = trVoidPatterns();
        std::vector<DrumKit> out;
        out.push_back({ "TR-VOID", trVoidParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "ROOM ONE", roomOneParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "BITCRUSH", bitcrushParams(), kPadNames, patterns, { 0 } });
        return out;
    }();
    return kits;
}

DrumParamArray applyKit(const DrumKit& kit) {
    DrumParamArray p = defaultDrumParams();
    for (const auto& [pid, v] : kit.params) {
        int id = drumIdFromString(pid);
        if (id >= 0) p[(size_t)id] = v;
    }
    return p;
}

} // namespace fable
