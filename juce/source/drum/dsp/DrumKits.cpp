// Transcription of src/drum/kits.ts. Derived kits share the same base helpers
// and rounding/clamps as the web so the 12-program banks stay in lockstep.
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
        { 0, -26, 22, 0.30f }, { 0, -19, 16, 0.24f }, { 1, -12,  5, 0.18f }, { 1,   7,  2, 0.14f },
        { 2,  24,  0, 0.08f }, { 2,  18,  0, 0.04f }, { 2, 12,  0, 0.30f }, { 2,   5,  0, 1.40f },
        { 0, -19, 12, 0.28f }, { 0, -12, 10, 0.24f }, { 0, -5,  8, 0.20f }, { 2,   0,  0, 1.80f },
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
    // Metallic pads: unrelated fixed-Hz carriers prevent their sidebands from
    // collapsing back onto the oscillator's harmonic series.
    set(p, padPid(5, "ring.freq"), 6389); set(p, padPid(5, "ring.mix"), 0.18f);
    set(p, padPid(6, "ring.freq"), 5197); set(p, padPid(6, "ring.mix"), 0.28f);
    set(p, padPid(7, "ring.freq"), 1667); set(p, padPid(7, "ring.mix"), 0.46f);
    set(p, padPid(11, "ring.freq"), 2741); set(p, padPid(11, "ring.mix"), 0.62f);
    set(p, padPid(12, "oscA.table"), 8); set(p, padPid(12, "oscA.tune"), -5);
    set(p, padPid(12, "ring.freq"), 731); set(p, padPid(12, "ring.mix"), 0.78f);
    set(p, padPid(12, "aenv.dec"), 0.16f); set(p, padPid(12, "aenv.curve"), 0.22f);
    set(p, padPid(13, "oscA.table"), 3); set(p, padPid(13, "oscA.pos"), 0.38f);
    set(p, padPid(13, "oscA.tune"), 17); set(p, padPid(13, "noise.level"), 0.18f);
    set(p, padPid(13, "noise.color"), 0.75f); set(p, padPid(13, "ring.freq"), 3271);
    set(p, padPid(13, "ring.mix"), 0.88f); set(p, padPid(13, "aenv.dec"), 0.20f);
    set(p, padPid(13, "flt.on"), 1); set(p, padPid(13, "flt.type"), 3);
    set(p, padPid(13, "flt.cut"), 3600);
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

Overrides classic808Params() {
    Overrides p = trVoidParams();
    set(p, "seq.bpm", 124); set(p, "master.swing", 0.28f); set(p, "fx.reverb.mix", 0.09f);
    set(p, padPid(0, "oscA.tune"), -30); set(p, padPid(0, "penv.amt"), 30);
    set(p, padPid(0, "aenv.dec"), 0.52f); set(p, padPid(1, "oscA.tune"), -22);
    for (const auto [pad, slot] : { std::pair<int, int>{2, 0}, {3, 1}, {5, 2}, {6, 3}, {7, 4}, {11, 4} }) {
        set(p, padPid(pad, "oscA.level"), 0);
        set(p, padPid(pad, "oscB.table"), (float)slot);
        set(p, padPid(pad, "oscB.level"), 0.9f);
    }
    set(p, padPid(2, "noise.level"), 0.12f); set(p, padPid(2, "aenv.dec"), 0.42f);
    set(p, padPid(3, "aenv.dec"), 0.65f);
    return p;
}

Overrides deepDubParams() {
    Overrides p = trVoidParams();
    set(p, "seq.bpm", 112); set(p, "master.swing", 0.38f);
    set(p, "fx.delay.on", 1); set(p, "fx.delay.time", 0.48f);
    set(p, "fx.delay.fb", 0.55f); set(p, "fx.delay.mix", 0.18f);
    set(p, "fx.reverb.size", 0.72f); set(p, "fx.reverb.mix", 0.24f);
    for (int i : { 0, 1, 8, 9, 10 }) {
        set(p, padPid(i, "oscA.tune"), get(p, padPid(i, "oscA.tune"), 0) - 5);
        set(p, padPid(i, "aenv.dec"), std::min(4.0f, get(p, padPid(i, "aenv.dec"), 0.24f) * 1.65f));
    }
    for (int i : { 5, 6 }) set(p, padPid(i, "flt.cut"), i == 5 ? 4300 : 3200);
    return p;
}

Overrides dustHouseParams() {
    Overrides p = roomOneParams();
    set(p, "seq.bpm", 122); set(p, "master.swing", 0.46f);
    set(p, "fx.drive.on", 1); set(p, "fx.drive.amt", 0.28f);
    set(p, "fx.drive.mix", 0.48f); set(p, "fx.reverb.mix", 0.2f);
    for (int i = 0; i < DR_NPADS; ++i) {
        set(p, padPid(i, "noise.level"), i < 4 ? 0.12f : 0.04f);
        set(p, padPid(i, "oscA.pos"), 0.18f + (float)(i % 3) * 0.08f);
        set(p, padPid(i, "aenv.curve"), 0.58f);
    }
    return p;
}

Overrides warehouseParams() {
    Overrides p = trVoidParams();
    set(p, "seq.bpm", 136); set(p, "master.swing", 0.14f);
    set(p, "fx.drive.on", 1); set(p, "fx.drive.amt", 0.72f);
    set(p, "fx.drive.mix", 0.76f); set(p, "fx.comp.thr", -20);
    set(p, "fx.comp.gain", 3); set(p, "fx.reverb.mix", 0.1f);
    for (int i : { 0, 1, 2, 3, 8, 9, 10 }) {
        set(p, padPid(i, "flt.on"), 1); set(p, padPid(i, "flt.type"), 1);
        set(p, padPid(i, "flt.cut"), i < 2 ? 1800 : 5200);
        set(p, padPid(i, "flt.drive"), 0.52f);
    }
    return p;
}

Overrides metalWorkParams() {
    Overrides p = trVoidParams();
    set(p, "seq.bpm", 132); set(p, "master.swing", 0.2f);
    set(p, "fx.chorus.on", 1); set(p, "fx.chorus.rate", 1.8f);
    set(p, "fx.chorus.depth", 0.52f); set(p, "fx.chorus.mix", 0.24f);
    set(p, "fx.reverb.size", 0.8f); set(p, "fx.reverb.mix", 0.28f);
    for (int i = 0; i < DR_NPADS; ++i) {
        set(p, padPid(i, "oscA.table"), i % 3 == 0 ? 8 : 2);
        set(p, padPid(i, "oscA.tune"), -12 + (i % 6) * 7);
        set(p, padPid(i, "oscA.fine"), i % 2 ? 11 : -9);
        set(p, padPid(i, "aenv.dec"), std::min(2.2f, 0.12f + (float)(i % 5) * 0.16f));
    }
    return p;
}

Overrides tapeKitParams() {
    Overrides p = roomOneParams();
    set(p, "seq.bpm", 98); set(p, "master.swing", 0.52f);
    set(p, "fx.chorus.on", 1); set(p, "fx.chorus.rate", 0.22f);
    set(p, "fx.chorus.depth", 0.24f); set(p, "fx.chorus.mix", 0.18f);
    set(p, "fx.drive.on", 1); set(p, "fx.drive.amt", 0.18f); set(p, "fx.drive.mix", 0.32f);
    for (int i = 0; i < DR_NPADS; ++i) {
        set(p, padPid(i, "oscA.fine"), (i % 5) * 3 - 6);
        set(p, padPid(i, "flt.on"), 1); set(p, padPid(i, "flt.type"), 0);
        set(p, padPid(i, "flt.cut"), i < 4 ? 4800 : 7600);
    }
    return p;
}

Overrides minimalParams() {
    Overrides p = trVoidParams();
    set(p, "seq.bpm", 128); set(p, "master.swing", 0.12f);
    set(p, "fx.reverb.mix", 0.07f); set(p, "fx.comp.gain", 2);
    for (int i = 0; i < DR_NPADS; ++i) {
        set(p, padPid(i, "aenv.dec"),
            std::max(0.025f, std::min(0.22f, get(p, padPid(i, "aenv.dec"), 0.24f) * 0.52f)));
        const bool main = i == 0 || i == 2 || i == 5 || i == 6;
        set(p, padPid(i, "lvl"), main ? 0.82f : 0.5f);
    }
    return p;
}

Overrides brokenToysParams() {
    Overrides p = bitcrushParams();
    set(p, "seq.bpm", 150); set(p, "master.swing", 0.34f);
    set(p, "fx.delay.time", 0.11f); set(p, "fx.delay.fb", 0.64f); set(p, "fx.delay.mix", 0.3f);
    for (int i = 0; i < DR_NPADS; ++i) {
        set(p, padPid(i, "oscA.table"), i % 2 ? 9 : 7);
        set(p, padPid(i, "oscA.tune"), -24 + (i * 11) % 47);
        set(p, padPid(i, "pan"), (float)((i % 5) - 2) * 0.28f);
        set(p, padPid(i, "mod1.src"), 3); set(p, padPid(i, "mod1.dst"), 1);
        set(p, padPid(i, "mod1.amt"), 0.22f);
    }
    return p;
}

Overrides liveRoomParams() {
    Overrides p = classic808Params();
    set(p, "seq.bpm", 110); set(p, "master.swing", 0.2f);
    set(p, "fx.reverb.size", 0.88f); set(p, "fx.reverb.mix", 0.36f);
    set(p, "fx.comp.thr", -12); set(p, "fx.comp.gain", 2);
    for (int i = 0; i < DR_NPADS; ++i) {
        set(p, padPid(i, "aenv.hold"), i < 4 ? 0.04f : 0.02f);
        set(p, padPid(i, "aenv.dec"), std::min(4.0f, get(p, padPid(i, "aenv.dec"), 0.24f) * 1.35f));
        set(p, padPid(i, "v2l"), 0.82f);
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
        out.push_back({ "808 CLASSIC", classic808Params(), kPadNames, patterns, { 0 } });
        out.push_back({ "DEEP DUB", deepDubParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "DUST HOUSE", dustHouseParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "WAREHOUSE", warehouseParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "METAL WORK", metalWorkParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "TAPE KIT", tapeKitParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "MINIMAL", minimalParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "BROKEN TOYS", brokenToysParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "LIVE ROOM", liveRoomParams(), kPadNames, patterns, { 0 } });
        return out;
    }();
    return kits;
}

DrumParamArray applyKit(const DrumKit& kit) {
    DrumParamArray p = defaultDrumParams();
    for (const auto& [pid, v] : kit.params) {
        int id = drumIdFromString(pid);
        if (id >= 0) {
            p[(size_t)id] = v;
        } else if (const int field = legacyDrumFxField(pid); field >= 0) {
            for (int pad = 0; pad < DR_NPADS; ++pad) p[(size_t)dpid(pad, field)] = v;
        }
    }
    return p;
}

} // namespace fable
