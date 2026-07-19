// Transcription of src/drum/kits.ts. Derived kits share the same base helpers
// and rounding/clamps as the web so the factory program banks stay in lockstep.
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

// kits.ts buildPatterns(): fills pattern slots [groove, groove, groove, fill]
// so chain [0,1,2,3] plays a 4-bar A A A B loop. Each spec entry names a pad,
// its active steps, and the subset of those steps that hit accented (value 2).
struct PadPat {
    int pad;
    std::vector<int> on;
    std::vector<int> acc;
};
using PatternSpec = std::vector<PadPat>;

std::vector<uint8_t> buildPatterns(const PatternSpec& groove, const PatternSpec& fill) {
    std::vector<uint8_t> patterns(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
    const PatternSpec* specs[4] = { &groove, &groove, &groove, &fill };
    for (int patI = 0; patI < 4; ++patI) {
        for (const auto& pp : *specs[patI]) {
            for (int step : pp.on) {
                const bool accent =
                    std::find(pp.acc.begin(), pp.acc.end(), step) != pp.acc.end();
                patterns[(size_t)patIdx(patI, pp.pad, step)] = accent ? 2 : 1;
            }
        }
    }
    return patterns;
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
    // The off-beat 3/11 pokes sit on TOM LO rather than PERC 1 — the tuned tom
    // reads as part of the kit's low end where the perc hit sat on top of it.
    setSteps(8, { 3, 11 });
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
    set(p, "fx.drive.mix", 0.2f);
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
    static const int slots[DR_NPADS] = { 5, 5, 0, 1, 6, 2, 3, 4, 13, 14, 15, 4, 7, 9, 8, 12 };
    static const float decays[DR_NPADS] = { .8f, .55f, .5f, .7f, .2f, .1f, .6f, 2, .7f, .65f, .6f, 2, .8f, .25f, .15f, .6f };
    for (int pad = 0; pad < DR_NPADS; ++pad) {
        set(p, padPid(pad, "oscA.level"), 0.0f);
        set(p, padPid(pad, "oscB.table"), (float)slots[pad]);
        set(p, padPid(pad, "oscB.level"), 0.9f);
        set(p, padPid(pad, "aenv.dec"), decays[pad]);
    }
    set(p, padPid(1, "oscB.tune"), 3.0f);
    set(p, padPid(2, "noise.level"), 0.12f);
    return p;
}

Overrides uzuParams() {
    Overrides p = trVoidParams();
    set(p, "seq.bpm", 128); set(p, "master.swing", 0.18f); set(p, "fx.reverb.mix", 0.12f);
    static const float decays[DR_NPADS] = { .4f, 2.4f, .6f, .6f, .2f, .45f, 1.8f, 1.1f, 1, .6f, .55f, 1, .35f, .1f, .6f, .2f };
    for (int i = 0; i < DR_NPADS; ++i) {
        set(p, padPid(i, "oscA.level"), 0.0f);
        set(p, padPid(i, "oscB.table"), (float)(16 + i));
        set(p, padPid(i, "oscB.level"), 0.92f);
        set(p, padPid(i, "noise.level"), 0.0f);
        set(p, padPid(i, "ring.mix"), 0.0f);
        set(p, padPid(i, "penv.amt"), 0.0f);
        set(p, padPid(i, "aenv.dec"), decays[i]);
    }
    return p;
}

Overrides hybridParams() {
    Overrides p = trVoidParams();
    set(p, "seq.bpm", 126); set(p, "master.swing", 0.24f);
    set(p, "fx.drive.on", 1); set(p, "fx.drive.amt", 0.16f); set(p, "fx.drive.mix", 0.2f);
    set(p, "fx.reverb.mix", 0.14f);
    static const int samples[DR_NPADS] = { 16, 5, 18, 1, 20, 21, 3, 23, 13, 25, 15, 27, 7, 28, 30, 31 };
    static const float oscLevels[DR_NPADS] = { .50f, .45f, .42f, .25f, .40f, .18f, .16f, .12f, .45f, .42f, .40f, .12f, .25f, .22f, .30f, .35f };
    static const float sampleLevels[DR_NPADS] = { .72f, .68f, .70f, .76f, .65f, .76f, .76f, .72f, .66f, .68f, .68f, .72f, .62f, .70f, .66f, .68f };
    static const float decays[DR_NPADS] = { .60f, .70f, .50f, .70f, .20f, .15f, .70f, 1.20f, .60f, .60f, .60f, 1.20f, .50f, .35f, .60f, .25f };
    for (int i = 0; i < DR_NPADS; ++i) {
        set(p, padPid(i, "oscA.level"), oscLevels[i]);
        set(p, padPid(i, "oscB.table"), (float)samples[i]);
        set(p, padPid(i, "oscB.level"), sampleLevels[i]);
        set(p, padPid(i, "aenv.dec"), decays[i]);
    }
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
    // Snare: the sample layer (808SD, the default) carries the crack at full
    // level while the oscillator drops two octaves and sits half back, so it
    // reads as body under the sample rather than as a second pitched hit. A
    // 100 ms decay keeps it short enough for the delay to do the dub work.
    set(p, padPid(2, "oscA.tune"), -24);
    set(p, padPid(2, "oscA.level"), 0.5f);
    set(p, padPid(2, "oscB.level"), 1);
    set(p, padPid(2, "aenv.dec"), 0.1f);
    return p;
}

Overrides dustHouseParams() {
    Overrides p = roomOneParams();
    set(p, "seq.bpm", 122); set(p, "master.swing", 0.46f);
    set(p, "fx.drive.on", 1); set(p, "fx.drive.amt", 0.28f);
    set(p, "fx.drive.mix", 0.2f); set(p, "fx.reverb.mix", 0.2f);
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
    set(p, "fx.drive.mix", 0.2f); set(p, "fx.comp.thr", -20);
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
    set(p, "fx.drive.on", 1); set(p, "fx.drive.amt", 0.18f); set(p, "fx.drive.mix", 0.2f);
    for (int i = 0; i < DR_NPADS; ++i) {
        set(p, padPid(i, "oscA.fine"), (i % 5) * 3 - 6);
        set(p, padPid(i, "flt.on"), 1); set(p, padPid(i, "flt.type"), 0);
        set(p, padPid(i, "flt.cut"), i < 4 ? 4800 : 7600);
    }
    return p;
}

// KICK, KICK 2, TOM LO, TOM MD, TOM HI.
constexpr int kMinimalDrivenPads[] = { 0, 1, 8, 9, 10 };

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
    // Kicks and toms drive at 50% wet: the clipped harmonics give the short,
    // heavily-damped bodies enough edge to cut, while the dry half keeps the
    // fundamental intact so the low end stays as clean as the kit's name asks.
    for (int i : kMinimalDrivenPads) {
        set(p, padPid(i, "fx.drive.on"), 1.0f);
        set(p, padPid(i, "fx.drive.mix"), 0.5f);
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

// -- Authored kits ------------------------------------------------------------
// The kits below are written from scratch (not derived from TR-VOID). Each ships
// a main groove in pattern slots 1-3 and a fill in slot 4; chain [0,1,2,3] plays
// a 4-bar A A A B loop. Unlike the derived kits, these start from empty
// Overrides (matching the TS `Partial<ParamValues>` literals) and applyKit()
// fills the rest from defaultDrumParams().

// NEON GRID — 118 BPM synthwave / electro. Wavetable kick with a sampled UZU
// transient glued on top, PULSE synth-toms, pitch-envelope zaps, and a
// mod-env noise sweep on the last pad.
const std::array<std::string, DR_NPADS> kNeonGridPads = {
    "KICK", "SUB KICK", "SNARE", "CLAP", "RIM ZAP", "CH HAT", "OH HAT", "RIDE",
    "SYN TOM L", "SYN TOM M", "SYN TOM H", "CRASH", "ZAP DOWN", "ZAP UP", "VOX", "SWEEP",
};

// kits.ts neonGridParams()
Overrides neonGridParams() {
    Overrides p;
    set(p, "seq.bpm", 118.0f);
    set(p, "master.swing", 0.1f);
    set(p, "fx.comp.on", 1.0f);
    set(p, "fx.chorus.on", 1.0f); set(p, "fx.chorus.rate", 0.8f);
    set(p, "fx.chorus.depth", 0.35f); set(p, "fx.chorus.mix", 0.16f);
    set(p, "fx.reverb.on", 1.0f); set(p, "fx.reverb.size", 0.58f); set(p, "fx.reverb.mix", 0.2f);
    // Kick: THUD body + UZU BD2 sample click, dry so the low end stays tight.
    set(p, padPid(0, "oscA.tune"), -22.0f); set(p, padPid(0, "penv.amt"), 30.0f); set(p, padPid(0, "penv.dec"), 0.03f);
    set(p, padPid(0, "oscB.table"), 17.0f); set(p, padPid(0, "oscB.level"), 0.55f);
    set(p, padPid(0, "aenv.dec"), 0.34f); set(p, padPid(0, "lvl"), 0.9f); set(p, padPid(0, "fx.reverb.on"), 0.0f);
    set(p, padPid(1, "oscA.tune"), -34.0f); set(p, padPid(1, "penv.amt"), 10.0f); set(p, padPid(1, "penv.dec"), 0.06f);
    set(p, padPid(1, "aenv.dec"), 1.3f); set(p, padPid(1, "lvl"), 0.85f); set(p, padPid(1, "fx.reverb.on"), 0.0f);
    // Snare: CRACK + noise with an 808SD layer underneath, gated tail.
    set(p, padPid(2, "oscA.table"), 1.0f); set(p, padPid(2, "oscA.tune"), -8.0f);
    set(p, padPid(2, "penv.amt"), 6.0f); set(p, padPid(2, "penv.dec"), 0.03f);
    set(p, padPid(2, "noise.level"), 0.4f); set(p, padPid(2, "noise.color"), 0.5f);
    set(p, padPid(2, "oscB.table"), 0.0f); set(p, padPid(2, "oscB.level"), 0.35f);
    set(p, padPid(2, "aenv.dec"), 0.24f); set(p, padPid(2, "aenv.curve"), 0.7f);
    set(p, padPid(3, "oscA.level"), 0.0f); set(p, padPid(3, "oscB.table"), 1.0f); set(p, padPid(3, "oscB.level"), 0.85f);
    set(p, padPid(3, "aenv.hold"), 0.02f); set(p, padPid(3, "aenv.dec"), 0.6f); set(p, padPid(3, "fx.reverb.mix"), 0.34f);
    // Rim zap: PULSE with a fast steep pitch drop — pure electro.
    set(p, padPid(4, "oscA.table"), 6.0f); set(p, padPid(4, "oscA.tune"), 14.0f);
    set(p, padPid(4, "penv.amt"), 40.0f); set(p, padPid(4, "penv.dec"), 0.02f); set(p, padPid(4, "aenv.dec"), 0.09f);
    set(p, padPid(7, "oscA.level"), 0.0f); set(p, padPid(7, "oscB.table"), 23.0f); set(p, padPid(7, "oscB.level"), 0.8f);
    set(p, padPid(7, "aenv.dec"), 1.1f);
    set(p, padPid(11, "oscA.level"), 0.0f); set(p, padPid(11, "oscB.table"), 27.0f); set(p, padPid(11, "oscB.level"), 0.8f);
    set(p, padPid(11, "aenv.dec"), 2.0f);
    // Zap pair: GRIT lasers, one falling and one rising, panned apart.
    set(p, padPid(12, "oscA.table"), 3.0f); set(p, padPid(12, "oscA.tune"), 18.0f);
    set(p, padPid(12, "penv.amt"), 48.0f); set(p, padPid(12, "penv.dec"), 0.05f);
    set(p, padPid(12, "aenv.dec"), 0.16f); set(p, padPid(12, "pan"), -0.25f);
    set(p, padPid(13, "oscA.table"), 3.0f); set(p, padPid(13, "oscA.tune"), 6.0f);
    set(p, padPid(13, "penv.amt"), -40.0f); set(p, padPid(13, "penv.dec"), 0.09f);
    set(p, padPid(13, "aenv.dec"), 0.2f); set(p, padPid(13, "pan"), 0.25f);
    set(p, padPid(14, "oscA.table"), 7.0f); set(p, padPid(14, "oscA.tune"), -5.0f);
    set(p, padPid(14, "aenv.dec"), 0.4f); set(p, padPid(14, "fx.chorus.mix"), 0.3f);
    // Sweep: pure noise through a band-pass whose cutoff rides the mod env.
    set(p, padPid(15, "oscA.level"), 0.0f); set(p, padPid(15, "noise.level"), 0.85f); set(p, padPid(15, "noise.color"), 0.2f);
    set(p, padPid(15, "flt.on"), 1.0f); set(p, padPid(15, "flt.type"), 2.0f); set(p, padPid(15, "flt.cut"), 900.0f);
    set(p, padPid(15, "flt.res"), 0.55f); set(p, padPid(15, "mod1.src"), 1.0f); set(p, padPid(15, "mod1.dst"), 4.0f);
    set(p, padPid(15, "mod1.amt"), 0.85f); set(p, padPid(15, "modenv.dec"), 0.9f); set(p, padPid(15, "aenv.dec"), 1.4f);
    // 808 hats + disco PULSE toms panned across the field.
    // [padI, sample, dec, cut]
    static const float kHats[2][4] = { { 5, 2, 0.06f, 7500 }, { 6, 3, 0.42f, 5600 } };
    for (const auto& h : kHats) {
        const int padI = (int)h[0];
        set(p, padPid(padI, "oscA.level"), 0.0f);
        set(p, padPid(padI, "oscB.table"), h[1]);
        set(p, padPid(padI, "oscB.level"), 0.8f);
        set(p, padPid(padI, "aenv.dec"), h[2]);
        set(p, padPid(padI, "flt.on"), 1.0f);
        set(p, padPid(padI, "flt.type"), 3.0f);
        set(p, padPid(padI, "flt.cut"), h[3]);
        set(p, padPid(padI, "choke"), 1.0f);
    }
    // [tune, pan] for PULSE toms on pads 8..10; decay 0.3 - i*0.04.
    static const float kToms[3][2] = { { -9, -0.4f }, { -2, 0 }, { 5, 0.4f } };
    for (int i = 0; i < 3; ++i) {
        set(p, padPid(8 + i, "oscA.table"), 6.0f);
        set(p, padPid(8 + i, "oscA.tune"), kToms[i][0]);
        set(p, padPid(8 + i, "penv.amt"), 16.0f);
        set(p, padPid(8 + i, "penv.dec"), 0.05f);
        set(p, padPid(8 + i, "aenv.dec"), 0.3f - (float)i * 0.04f);
        set(p, padPid(8 + i, "pan"), kToms[i][1]);
    }
    return p;
}

std::vector<uint8_t> neonGridPatterns() {
    return buildPatterns(
        {
            { 0, { 0, 7, 10 }, { 0 } },
            { 1, { 0, 8 }, {} },
            { 2, { 4, 12 }, { 12 } },
            { 3, { 12 }, {} },
            { 4, { 3 }, {} },
            { 5, { 0, 2, 4, 6, 8, 10, 12, 14 }, { 2, 6, 10, 14 } },
            { 6, { 14 }, {} },
            { 12, { 6 }, {} },
            { 13, { 13 }, {} },
            { 15, { 8 }, {} },
        },
        {
            { 0, { 0, 7, 10 }, { 0 } },
            { 2, { 4, 12, 15 }, { 12 } },
            { 3, { 4, 12 }, {} },
            { 5, { 0, 2, 4, 6, 8, 10 }, { 2, 6 } },
            { 8, { 8 }, {} },
            { 9, { 9, 10 }, {} },
            { 10, { 11 }, { 11 } },
            { 11, { 0 }, {} },
            { 12, { 6, 14 }, {} },
            { 13, { 3, 13 }, {} },
            { 14, { 12 }, {} },
        });
}

// ACID CAVE — 138 BPM dark techno. A dry punch kick over a cavernous rumble
// pad, squelching GRIT blips whose filter rides the mod env, and metallic
// ring-mod percussion.
const std::array<std::string, DR_NPADS> kAcidCavePads = {
    "KICK", "RUMBLE", "SNARE", "CLAP", "RIM", "CH HAT", "OH HAT", "RIDE",
    "BLIP LO", "BLIP MD", "BLIP HI", "CRASH", "TINE HIT", "PERC", "STAB", "GLITCH",
};

// kits.ts acidCaveParams()
Overrides acidCaveParams() {
    Overrides p;
    set(p, "seq.bpm", 138.0f);
    set(p, "master.swing", 0.04f);
    set(p, "fx.comp.on", 1.0f); set(p, "fx.comp.thr", -22.0f); set(p, "fx.comp.gain", 4.0f);
    set(p, "fx.drive.on", 1.0f); set(p, "fx.drive.amt", 0.5f); set(p, "fx.drive.mix", 0.2f);
    set(p, "fx.delay.on", 1.0f); set(p, "fx.delay.time", 0.33f); set(p, "fx.delay.fb", 0.5f); set(p, "fx.delay.mix", 0.14f);
    set(p, "fx.reverb.on", 1.0f); set(p, "fx.reverb.size", 0.7f); set(p, "fx.reverb.mix", 0.12f);
    set(p, padPid(0, "oscA.tune"), -25.0f); set(p, padPid(0, "penv.amt"), 28.0f); set(p, padPid(0, "penv.dec"), 0.035f);
    set(p, padPid(0, "aenv.dec"), 0.3f); set(p, padPid(0, "aenv.curve"), 0.5f);
    set(p, padPid(0, "lvl"), 0.92f); set(p, padPid(0, "fx.reverb.on"), 0.0f);
    // Rumble: same THUD an octave under the kick, low-passed and drowned in a
    // huge per-pad reverb — the classic sub-rumble trick.
    set(p, padPid(1, "oscA.tune"), -25.0f); set(p, padPid(1, "penv.amt"), 8.0f); set(p, padPid(1, "penv.dec"), 0.08f);
    set(p, padPid(1, "aenv.dec"), 2.6f); set(p, padPid(1, "lvl"), 0.55f);
    set(p, padPid(1, "flt.on"), 1.0f); set(p, padPid(1, "flt.type"), 1.0f); set(p, padPid(1, "flt.cut"), 300.0f);
    set(p, padPid(1, "fx.reverb.size"), 0.85f); set(p, padPid(1, "fx.reverb.mix"), 0.55f);
    set(p, padPid(2, "oscA.table"), 3.0f); set(p, padPid(2, "oscA.tune"), -7.0f);
    set(p, padPid(2, "noise.level"), 0.55f); set(p, padPid(2, "noise.color"), 0.1f); set(p, padPid(2, "aenv.dec"), 0.16f);
    set(p, padPid(3, "oscA.level"), 0.0f); set(p, padPid(3, "oscB.table"), 19.0f); set(p, padPid(3, "oscB.tune"), -3.0f);
    set(p, padPid(3, "oscB.level"), 0.8f); set(p, padPid(3, "noise.level"), 0.2f); set(p, padPid(3, "aenv.dec"), 0.4f);
    set(p, padPid(4, "oscA.level"), 0.0f); set(p, padPid(4, "oscB.table"), 20.0f); set(p, padPid(4, "oscB.tune"), -2.0f);
    set(p, padPid(4, "oscB.level"), 0.75f); set(p, padPid(4, "aenv.dec"), 0.08f);
    set(p, padPid(11, "oscA.level"), 0.0f); set(p, padPid(11, "oscB.table"), 4.0f); set(p, padPid(11, "oscB.level"), 0.7f);
    set(p, padPid(11, "aenv.dec"), 2.2f); set(p, padPid(11, "flt.on"), 1.0f); set(p, padPid(11, "flt.type"), 3.0f);
    set(p, padPid(11, "flt.cut"), 5000.0f);
    set(p, padPid(12, "oscA.table"), 2.0f); set(p, padPid(12, "oscA.tune"), 7.0f);
    set(p, padPid(12, "ring.freq"), 3907.0f); set(p, padPid(12, "ring.mix"), 0.5f); set(p, padPid(12, "aenv.dec"), 0.12f);
    set(p, padPid(13, "oscA.level"), 0.0f); set(p, padPid(13, "oscB.table"), 28.0f); set(p, padPid(13, "oscB.tune"), 4.0f);
    set(p, padPid(13, "oscB.level"), 0.7f); set(p, padPid(13, "aenv.dec"), 0.15f); set(p, padPid(13, "pan"), 0.3f);
    // Stab: VOX through a resonant band-pass, pitch jittered per hit.
    set(p, padPid(14, "oscA.table"), 7.0f); set(p, padPid(14, "oscA.tune"), -12.0f); set(p, padPid(14, "aenv.dec"), 0.18f);
    set(p, padPid(14, "flt.on"), 1.0f); set(p, padPid(14, "flt.type"), 2.0f); set(p, padPid(14, "flt.cut"), 1200.0f);
    set(p, padPid(14, "flt.res"), 0.5f); set(p, padPid(14, "mod1.src"), 3.0f); set(p, padPid(14, "mod1.dst"), 5.0f);
    set(p, padPid(14, "mod1.amt"), 0.3f);
    set(p, padPid(15, "oscA.table"), 9.0f); set(p, padPid(15, "aenv.dec"), 0.1f);
    set(p, padPid(15, "mod1.src"), 3.0f); set(p, padPid(15, "mod1.dst"), 1.0f); set(p, padPid(15, "mod1.amt"), 0.6f);
    // Hats/ride: UZU metals, high-passed thin. [padI, sample, dec, cut]
    static const float kMetals[3][4] = {
        { 5, 21, 0.05f, 8000 }, { 6, 22, 0.3f, 6000 }, { 7, 23, 0.9f, 4500 },
    };
    for (const auto& m : kMetals) {
        const int padI = (int)m[0];
        set(p, padPid(padI, "oscA.level"), 0.0f);
        set(p, padPid(padI, "oscB.table"), m[1]);
        set(p, padPid(padI, "oscB.level"), 0.75f);
        set(p, padPid(padI, "aenv.dec"), m[2]);
        set(p, padPid(padI, "flt.on"), 1.0f);
        set(p, padPid(padI, "flt.type"), 3.0f);
        set(p, padPid(padI, "flt.cut"), m[3]);
        if (padI < 7) set(p, padPid(padI, "choke"), 1.0f);
    }
    // Acid blips: 303-ish squelch — resonant LP24 swept hard by the mod env.
    static const float kBlipTunes[3] = { -17, -12, -5 };
    for (int i = 0; i < 3; ++i) {
        set(p, padPid(8 + i, "oscA.table"), 3.0f);
        set(p, padPid(8 + i, "oscA.pos"), 0.3f);
        set(p, padPid(8 + i, "oscA.tune"), kBlipTunes[i]);
        set(p, padPid(8 + i, "aenv.dec"), 0.14f);
        set(p, padPid(8 + i, "flt.on"), 1.0f);
        set(p, padPid(8 + i, "flt.type"), 1.0f);
        set(p, padPid(8 + i, "flt.cut"), 700.0f);
        set(p, padPid(8 + i, "flt.res"), 0.72f);
        set(p, padPid(8 + i, "mod1.src"), 1.0f);
        set(p, padPid(8 + i, "mod1.dst"), 4.0f);
        set(p, padPid(8 + i, "mod1.amt"), 0.9f);
        set(p, padPid(8 + i, "modenv.dec"), 0.12f);
    }
    return p;
}

std::vector<uint8_t> acidCavePatterns() {
    return buildPatterns(
        {
            { 0, { 0, 4, 8, 12 }, { 0, 4, 8, 12 } },
            { 1, { 0 }, {} },
            { 3, { 4, 12 }, {} },
            { 5, { 0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15 }, {} },
            { 6, { 2, 6, 10, 14 }, { 2, 10 } },
            { 8, { 3, 11 }, {} },
            { 9, { 6, 14 }, {} },
            { 10, { 7 }, {} },
            { 15, { 15 }, {} },
        },
        {
            { 0, { 0, 4, 8, 12 }, { 0, 4, 8, 12 } },
            { 1, { 0, 8 }, {} },
            { 2, { 12 }, {} },
            { 3, { 4 }, {} },
            { 6, { 2, 6, 10, 14 }, { 2, 10 } },
            { 7, { 0, 2, 4, 6, 8, 10, 12, 14 }, {} },
            { 8, { 3, 7, 11 }, {} },
            { 9, { 6, 10, 14 }, {} },
            { 10, { 7, 15 }, { 15 } },
            { 13, { 5, 13 }, {} },
            { 14, { 2, 10 }, {} },
        });
}

// BOOM BAP — 90 BPM hip-hop. Sample-forward with wavetable glue under the
// kick, every sample capped by a lazy low-pass for dust, RAND modulation for
// MPC-style humanization, and a reversed UZU MOD transition pad.
const std::array<std::string, DR_NPADS> kBoomBapPads = {
    "KICK", "KICK 808", "SNARE", "CLAP", "RIM", "CH HAT", "OH HAT", "SHAKER",
    "TOM LO", "TOM MD", "TOM HI", "CRASH", "COWBELL", "MARACAS", "VOX", "REVERSE",
};

// kits.ts boomBapParams()
Overrides boomBapParams() {
    Overrides p;
    set(p, "seq.bpm", 90.0f);
    set(p, "master.swing", 0.56f);
    set(p, "fx.comp.on", 1.0f); set(p, "fx.comp.thr", -14.0f);
    set(p, "fx.drive.on", 1.0f); set(p, "fx.drive.amt", 0.35f); set(p, "fx.drive.mix", 0.2f);
    set(p, "fx.reverb.on", 1.0f); set(p, "fx.reverb.size", 0.35f); set(p, "fx.reverb.mix", 0.1f);
    // Sampled backbone: [pad, sample, tune, level, decay].
    static const float kSamples[15][5] = {
        { 0, 16, -2, 0.85f, 0.4f }, { 1, 5, -5, 0.85f, 0.55f }, { 2, 18, -4, 0.8f, 0.3f },
        { 3, 19, -6, 0.8f, 0.5f }, { 4, 6, 0, 0.75f, 0.12f }, { 5, 2, -7, 0.75f, 0.07f },
        { 6, 3, -5, 0.75f, 0.35f }, { 7, 29, 0, 0.7f, 0.14f }, { 8, 13, -3, 0.75f, 0.4f },
        { 9, 14, -3, 0.75f, 0.38f }, { 10, 15, -3, 0.75f, 0.36f }, { 11, 27, -4, 0.7f, 1.8f },
        { 12, 7, -7, 0.6f, 0.2f }, { 13, 8, 0, 0.7f, 0.1f }, { 15, 31, 0, 0.75f, 0.8f },
    };
    for (const auto& s : kSamples) {
        const int padI = (int)s[0];
        set(p, padPid(padI, "oscA.level"), 0.0f);
        set(p, padPid(padI, "oscB.table"), s[1]);
        set(p, padPid(padI, "oscB.tune"), s[2]);
        set(p, padPid(padI, "oscB.level"), s[3]);
        set(p, padPid(padI, "aenv.dec"), s[4]);
        // The dust: cap everything with a dull low-pass like a worn 12-bit sampler.
        set(p, padPid(padI, "flt.on"), 1.0f);
        set(p, padPid(padI, "flt.type"), 1.0f);
        set(p, padPid(padI, "flt.cut"), 8500.0f);
    }
    // THUD glue under the sampled kick so the low end hits like a sub.
    set(p, padPid(0, "oscA.tune"), -24.0f); set(p, padPid(0, "oscA.level"), 0.5f);
    set(p, padPid(0, "penv.amt"), 18.0f); set(p, padPid(0, "penv.dec"), 0.04f);
    set(p, padPid(0, "lvl"), 0.9f); set(p, padPid(0, "fx.reverb.on"), 0.0f);
    set(p, padPid(1, "fx.reverb.on"), 0.0f);
    set(p, padPid(2, "noise.level"), 0.25f); set(p, padPid(2, "noise.color"), 0.35f);
    set(p, padPid(2, "mod1.src"), 3.0f); set(p, padPid(2, "mod1.dst"), 2.0f); set(p, padPid(2, "mod1.amt"), 0.15f);
    set(p, padPid(5, "mod1.src"), 3.0f); set(p, padPid(5, "mod1.dst"), 7.0f); set(p, padPid(5, "mod1.amt"), 0.2f);
    set(p, padPid(5, "choke"), 1.0f); set(p, padPid(6, "choke"), 1.0f);
    set(p, padPid(7, "pan"), 0.3f); set(p, padPid(13, "pan"), -0.3f);
    // Dusty vocal chop on the wavetable side.
    set(p, padPid(14, "oscA.table"), 7.0f); set(p, padPid(14, "oscA.tune"), -10.0f); set(p, padPid(14, "aenv.dec"), 0.5f);
    set(p, padPid(14, "flt.on"), 1.0f); set(p, padPid(14, "flt.type"), 0.0f); set(p, padPid(14, "flt.cut"), 3500.0f);
    // Reversed sample sweep into the downbeat.
    set(p, padPid(15, "oscB.phase"), 1.0f);
    return p;
}

std::vector<uint8_t> boomBapPatterns() {
    return buildPatterns(
        {
            { 0, { 0, 7, 10 }, { 0 } },
            { 2, { 4, 12 }, { 4, 12 } },
            { 5, { 0, 2, 4, 6, 8, 10, 12, 14 }, { 0, 8 } },
            { 7, { 3, 11 }, {} },
            { 4, { 14 }, {} },
        },
        {
            { 0, { 0, 5, 10, 11 }, { 0 } },
            { 1, { 8 }, {} },
            { 2, { 4, 12, 14 }, { 4, 12 } },
            { 5, { 0, 2, 4, 6, 8, 10, 14 }, { 0, 8 } },
            { 6, { 12 }, {} },
            { 7, { 3, 11 }, {} },
            { 12, { 7 }, {} },
            { 14, { 6 }, {} },
            { 15, { 12 }, {} },
        });
}

// PIRATE RADIO — 133 BPM UK garage 2-step. Bright UZU kit swung hard, a THUD
// sub for basslines, a mod-env wobble stab, random-pitch vox chops, and a
// detuned BLOOM organ stab.
const std::array<std::string, DR_NPADS> kPirateRadioPads = {
    "KICK", "SUB BASS", "SNARE", "CLAP", "RIM", "CH HAT", "OH HAT", "SHAKER",
    "PERC L", "PERC M", "PERC H", "CRASH", "WOBBLE", "TAMB", "VOX CHOP", "ORGAN",
};

// kits.ts pirateRadioParams()
Overrides pirateRadioParams() {
    Overrides p;
    set(p, "seq.bpm", 133.0f);
    set(p, "master.swing", 0.58f);
    set(p, "fx.comp.on", 1.0f);
    set(p, "fx.chorus.on", 1.0f); set(p, "fx.chorus.rate", 0.5f); set(p, "fx.chorus.depth", 0.3f); set(p, "fx.chorus.mix", 0.12f);
    set(p, "fx.delay.on", 1.0f); set(p, "fx.delay.time", 0.34f); set(p, "fx.delay.fb", 0.45f); set(p, "fx.delay.mix", 0.16f);
    set(p, "fx.reverb.on", 1.0f); set(p, "fx.reverb.size", 0.5f); set(p, "fx.reverb.mix", 0.18f);
    // Kick: tight UZU BD2 with a THUD knock on top.
    set(p, padPid(0, "oscA.tune"), -20.0f); set(p, padPid(0, "oscA.level"), 0.4f);
    set(p, padPid(0, "penv.amt"), 22.0f); set(p, padPid(0, "penv.dec"), 0.025f);
    set(p, padPid(0, "oscB.table"), 17.0f); set(p, padPid(0, "oscB.level"), 0.85f);
    set(p, padPid(0, "aenv.dec"), 0.28f); set(p, padPid(0, "lvl"), 0.9f); set(p, padPid(0, "fx.reverb.on"), 0.0f);
    // Sub: long THUD for one-finger basslines between the drums.
    set(p, padPid(1, "oscA.tune"), -31.0f); set(p, padPid(1, "penv.amt"), 6.0f); set(p, padPid(1, "penv.dec"), 0.05f);
    set(p, padPid(1, "aenv.dec"), 1.2f); set(p, padPid(1, "lvl"), 0.9f); set(p, padPid(1, "fx.reverb.on"), 0.0f);
    set(p, padPid(2, "oscA.level"), 0.0f); set(p, padPid(2, "oscB.table"), 18.0f); set(p, padPid(2, "oscB.tune"), 3.0f);
    set(p, padPid(2, "oscB.level"), 0.8f); set(p, padPid(2, "noise.level"), 0.2f); set(p, padPid(2, "noise.color"), 0.6f);
    set(p, padPid(2, "aenv.dec"), 0.22f);
    set(p, padPid(3, "oscA.level"), 0.0f); set(p, padPid(3, "oscB.table"), 19.0f); set(p, padPid(3, "oscB.tune"), 2.0f);
    set(p, padPid(3, "oscB.level"), 0.8f); set(p, padPid(3, "aenv.dec"), 0.45f); set(p, padPid(3, "fx.reverb.mix"), 0.3f);
    // Rim: pitch jitters per hit so the skippy 2-step rims never repeat.
    set(p, padPid(4, "oscA.level"), 0.0f); set(p, padPid(4, "oscB.table"), 20.0f); set(p, padPid(4, "oscB.tune"), 6.0f);
    set(p, padPid(4, "oscB.level"), 0.8f); set(p, padPid(4, "aenv.dec"), 0.09f); set(p, padPid(4, "pan"), -0.2f);
    set(p, padPid(4, "mod1.src"), 3.0f); set(p, padPid(4, "mod1.dst"), 5.0f); set(p, padPid(4, "mod1.amt"), 0.15f);
    set(p, padPid(7, "oscA.level"), 0.0f); set(p, padPid(7, "oscB.table"), 29.0f); set(p, padPid(7, "oscB.tune"), 4.0f);
    set(p, padPid(7, "oscB.level"), 0.75f); set(p, padPid(7, "aenv.dec"), 0.12f);
    set(p, padPid(11, "oscA.level"), 0.0f); set(p, padPid(11, "oscB.table"), 27.0f); set(p, padPid(11, "oscB.level"), 0.75f);
    set(p, padPid(11, "aenv.dec"), 1.6f);
    // Wobble: GRIT sub stab, resonant LP24 pumped by the mod env.
    set(p, padPid(12, "oscA.table"), 3.0f); set(p, padPid(12, "oscA.tune"), -24.0f);
    set(p, padPid(12, "aenv.dec"), 0.5f); set(p, padPid(12, "flt.on"), 1.0f); set(p, padPid(12, "flt.type"), 1.0f);
    set(p, padPid(12, "flt.cut"), 500.0f); set(p, padPid(12, "flt.res"), 0.6f);
    set(p, padPid(12, "mod1.src"), 1.0f); set(p, padPid(12, "mod1.dst"), 4.0f); set(p, padPid(12, "mod1.amt"), 0.7f);
    set(p, padPid(12, "modenv.dec"), 0.3f);
    set(p, padPid(13, "oscA.level"), 0.0f); set(p, padPid(13, "oscB.table"), 30.0f); set(p, padPid(13, "oscB.tune"), 3.0f);
    set(p, padPid(13, "oscB.level"), 0.7f); set(p, padPid(13, "aenv.dec"), 0.15f); set(p, padPid(13, "pan"), 0.3f);
    // Vox chop: pitch dives in and lands somewhere new every hit.
    set(p, padPid(14, "oscA.table"), 7.0f); set(p, padPid(14, "oscA.tune"), 7.0f);
    set(p, padPid(14, "penv.amt"), -12.0f); set(p, padPid(14, "penv.dec"), 0.06f); set(p, padPid(14, "aenv.dec"), 0.22f);
    set(p, padPid(14, "fx.chorus.mix"), 0.25f);
    set(p, padPid(14, "mod1.src"), 3.0f); set(p, padPid(14, "mod1.dst"), 5.0f); set(p, padPid(14, "mod1.amt"), 0.4f);
    // Organ stab: detuned BLOOM unison, low-passed warm.
    set(p, padPid(15, "oscA.table"), 5.0f); set(p, padPid(15, "oscA.unison"), 3.0f); set(p, padPid(15, "oscA.detune"), 0.3f);
    set(p, padPid(15, "aenv.dec"), 0.3f); set(p, padPid(15, "flt.on"), 1.0f); set(p, padPid(15, "flt.type"), 0.0f);
    set(p, padPid(15, "flt.cut"), 4000.0f);
    // Shuffled UZU hats + pitched-up perc toms. [padI, sample, dec, cut]
    static const float kHats[2][4] = { { 5, 21, 0.05f, 8500 }, { 6, 22, 0.35f, 6000 } };
    for (const auto& h : kHats) {
        const int padI = (int)h[0];
        set(p, padPid(padI, "oscA.level"), 0.0f);
        set(p, padPid(padI, "oscB.table"), h[1]);
        set(p, padPid(padI, "oscB.tune"), 2.0f);
        set(p, padPid(padI, "oscB.level"), 0.75f);
        set(p, padPid(padI, "aenv.dec"), h[2]);
        set(p, padPid(padI, "flt.on"), 1.0f);
        set(p, padPid(padI, "flt.type"), 3.0f);
        set(p, padPid(padI, "flt.cut"), h[3]);
        set(p, padPid(padI, "choke"), 1.0f);
        set(p, padPid(padI, "v2l"), 0.9f);
    }
    // Perc toms on pads 8..10, panned (i-1)*0.35.
    static const float kPercSamples[3] = { 24, 25, 26 };
    for (int i = 0; i < 3; ++i) {
        set(p, padPid(8 + i, "oscA.level"), 0.0f);
        set(p, padPid(8 + i, "oscB.table"), kPercSamples[i]);
        set(p, padPid(8 + i, "oscB.tune"), 2.0f);
        set(p, padPid(8 + i, "oscB.level"), 0.75f);
        set(p, padPid(8 + i, "aenv.dec"), 0.3f);
        set(p, padPid(8 + i, "pan"), (float)(i - 1) * 0.35f);
    }
    return p;
}

std::vector<uint8_t> pirateRadioPatterns() {
    return buildPatterns(
        {
            { 0, { 0, 10 }, { 0 } },
            { 1, { 0, 7 }, {} },
            { 2, { 4, 12 }, { 12 } },
            { 4, { 7, 15 }, {} },
            { 5, { 2, 3, 6, 7, 11, 14 }, { 2, 6 } },
            { 6, { 10 }, {} },
            { 14, { 13 }, {} },
            { 15, { 8 }, {} },
        },
        {
            { 0, { 0, 7, 10 }, { 0 } },
            { 1, { 0, 7, 11 }, {} },
            { 2, { 4, 12 }, { 12 } },
            { 3, { 12 }, {} },
            { 4, { 7, 13, 15 }, {} },
            { 5, { 2, 3, 6, 7, 11, 14 }, { 2, 6 } },
            { 6, { 10 }, {} },
            { 8, { 5 }, {} },
            { 10, { 14 }, {} },
            { 12, { 6, 14 }, {} },
            { 14, { 3, 13 }, {} },
            { 15, { 8, 11 }, {} },
        });
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
        out.push_back({ "UZU", uzuParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "808+UZU HYBRID", hybridParams(), kPadNames, patterns, { 0 } });
        out.push_back({ "NEON GRID", neonGridParams(), kNeonGridPads, neonGridPatterns(), { 0, 1, 2, 3 } });
        out.push_back({ "ACID CAVE", acidCaveParams(), kAcidCavePads, acidCavePatterns(), { 0, 1, 2, 3 } });
        out.push_back({ "BOOM BAP", boomBapParams(), kBoomBapPads, boomBapPatterns(), { 0, 1, 2, 3 } });
        out.push_back({ "PIRATE RADIO", pirateRadioParams(), kPirateRadioPads, pirateRadioPatterns(), { 0, 1, 2, 3 } });
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
