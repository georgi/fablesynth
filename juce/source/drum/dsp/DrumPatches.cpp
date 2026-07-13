// Transcription of src/drum/patches.ts FACTORY_PATCHES + applyPatchToParams.
// Table indices follow DRUM_TABLE_NAMES: THUD 0, CRACK 1, TINE 2, GRIT 3,
// PRIME 4, BLOOM 5, PULSE 6, VOX 7, CHIME 8, GLITCH 9, 808SD 10, 808CP 11,
// 808CH 12, 808OH 13, 808CY 14. Filter types: LP12 0, LP24 1, BP12 2, HP12 3.
#include "DrumPatches.h"

#include <array>
#include <initializer_list>

namespace fable {
namespace {

PadPatch fp(const char* name,
            std::initializer_list<std::pair<const char*, float>> params) {
    PadPatch p;
    p.name = name;
    for (const auto& [rel, v] : params) p.params.emplace_back(rel, v);
    return p;
}

} // namespace

const std::vector<PadPatch>& factoryPatches() {
    static const std::vector<PadPatch> bank = {
        // Kicks — THUD body, pitch envelope does the punch.
        fp("BD DEEP", {
            { "oscA.table", 0 }, { "oscA.tune", -26 }, { "penv.amt", 24 }, { "penv.dec", 0.05f },
            { "aenv.dec", 0.42f }, { "aenv.curve", 0.45f }, { "lvl", 0.9f },
        }),
        fp("BD PUNCH", {
            { "oscA.table", 0 }, { "oscA.tune", -19 }, { "penv.amt", 32 }, { "penv.dec", 0.028f },
            { "aenv.dec", 0.2f }, { "aenv.curve", 0.5f }, { "lvl", 0.9f },
        }),
        fp("BD SUB", {
            { "oscA.table", 0 }, { "oscA.tune", -34 }, { "penv.amt", 20 }, { "penv.dec", 0.06f },
            { "aenv.dec", 0.95f }, { "aenv.hold", 0.02f }, { "aenv.curve", 0.3f }, { "lvl", 0.92f },
        }),
        fp("BD 808", {
            { "oscA.table", 0 }, { "oscA.tune", -24 }, { "penv.amt", 26 }, { "penv.dec", 0.075f },
            { "aenv.dec", 0.65f }, { "aenv.curve", 0.35f }, { "flt.on", 1 }, { "flt.type", 0 },
            { "flt.cut", 900 }, { "flt.drive", 0.35f }, { "lvl", 0.92f },
        }),
        // Snares — tonal crack plus a bright noise layer.
        fp("SD CRACK", {
            { "oscA.table", 1 }, { "oscA.tune", -12 }, { "penv.amt", 5 }, { "penv.dec", 0.03f },
            { "noise.level", 0.5f }, { "noise.color", 0.3f }, { "aenv.dec", 0.18f }, { "lvl", 0.85f },
        }),
        fp("SD 808", {
            { "oscA.level", 0 }, { "oscB.table", 0 }, { "oscB.level", 0.9f },
            { "noise.level", 0.12f }, { "noise.color", 0.45f }, { "aenv.dec", 0.42f }, { "lvl", 0.85f },
        }),
        fp("SD RIM", {
            { "oscA.table", 1 }, { "oscA.tune", 0 }, { "penv.amt", 2 }, { "penv.dec", 0.015f },
            { "noise.level", 0.18f }, { "noise.color", 0.6f }, { "aenv.dec", 0.08f }, { "lvl", 0.8f },
        }),
        // Clap — sampled 808 clap with a room-friendly tail.
        fp("CP 808", {
            { "oscA.level", 0 }, { "oscB.table", 1 }, { "oscB.level", 0.9f },
            { "aenv.hold", 0.02f }, { "aenv.dec", 0.65f }, { "aenv.curve", 0.4f }, { "lvl", 0.85f },
        }),
        // Hats — sampled 808 hats, high-passed to sit above the kit.
        fp("HH 808", {
            { "oscA.level", 0 }, { "oscB.table", 2 }, { "oscB.level", 0.9f }, { "aenv.dec", 0.12f }, { "flt.on", 1 }, { "flt.type", 3 },
            { "flt.cut", 7200 }, { "ring.freq", 6389 }, { "ring.mix", 0.18f }, { "lvl", 0.7f },
        }),
        fp("HH TIGHT", {
            { "oscA.level", 0 }, { "oscB.table", 2 }, { "oscB.level", 0.9f }, { "aenv.dec", 0.055f }, { "flt.on", 1 }, { "flt.type", 3 },
            { "flt.cut", 9200 }, { "ring.freq", 7919 }, { "ring.mix", 0.22f }, { "lvl", 0.65f },
        }),
        fp("OH 808", {
            { "oscA.level", 0 }, { "oscB.table", 3 }, { "oscB.level", 0.9f }, { "aenv.dec", 0.55f }, { "flt.on", 1 }, { "flt.type", 3 },
            { "flt.cut", 5200 }, { "ring.freq", 5197 }, { "ring.mix", 0.28f }, { "lvl", 0.7f },
        }),
        // Cymbal — long sizzle.
        fp("CY 808", {
            { "oscA.level", 0 }, { "oscB.table", 4 }, { "oscB.level", 0.9f },
            { "aenv.dec", 1.7f }, { "aenv.curve", 0.25f }, { "flt.on", 1 },
            { "flt.type", 3 }, { "flt.cut", 3800 }, { "ring.freq", 2741 }, { "ring.mix", 0.62f },
            { "lvl", 0.72f },
        }),
        // Toms — THUD tuned across the range with a modest pitch sweep.
        fp("TM LO", {
            { "oscA.table", 0 }, { "oscA.tune", -19 }, { "penv.amt", 12 }, { "penv.dec", 0.07f },
            { "aenv.dec", 0.28f }, { "lvl", 0.85f },
        }),
        fp("TM MID", {
            { "oscA.table", 0 }, { "oscA.tune", -12 }, { "penv.amt", 10 }, { "penv.dec", 0.06f },
            { "aenv.dec", 0.24f }, { "lvl", 0.85f },
        }),
        fp("TM HI", {
            { "oscA.table", 0 }, { "oscA.tune", -5 }, { "penv.amt", 8 }, { "penv.dec", 0.05f },
            { "aenv.dec", 0.2f }, { "lvl", 0.85f },
        }),
        // Perc / vox / glitch flavors. Fixed-Hz ring modulation supplies the
        // inharmonic sidebands that the pitched procedural tables lack.
        fp("PC TINE", {
            { "oscA.table", 2 }, { "oscA.tune", 7 }, { "oscA.fine", -5 }, { "ring.freq", 1187 },
            { "ring.mix", 0.62f }, { "aenv.dec", 0.42f }, { "aenv.curve", 0.28f }, { "lvl", 0.75f },
        }),
        fp("PC BELL", {
            { "oscA.table", 8 }, { "oscA.tune", -5 }, { "oscA.level", 0.7f }, { "ring.freq", 731 },
            { "ring.mix", 0.78f }, { "aenv.att", 0.0005f }, { "aenv.hold", 0.018f },
            { "aenv.dec", 1.35f }, { "aenv.curve", 0.2f }, { "flt.on", 1 }, { "flt.type", 2 },
            { "flt.cut", 2400 }, { "flt.res", 0.35f }, { "lvl", 0.78f },
        }),
        fp("PC CYMBAL", {
            { "oscA.table", 3 }, { "oscA.pos", 0.38f }, { "oscA.tune", 17 }, { "noise.level", 0.22f },
            { "noise.color", 0.75f }, { "ring.freq", 3271 }, { "ring.mix", 0.88f },
            { "aenv.hold", 0.014f }, { "aenv.dec", 1.6f }, { "aenv.curve", 0.24f },
            { "flt.on", 1 }, { "flt.type", 3 }, { "flt.cut", 3600 }, { "lvl", 0.72f },
        }),
        fp("PC VOX", {
            { "oscA.table", 7 }, { "oscA.tune", -5 }, { "aenv.dec", 0.48f }, { "aenv.curve", 0.3f },
            { "lvl", 0.78f },
        }),
        fp("PC GLITCH", {
            { "oscA.table", 3 }, { "oscA.tune", -12 }, { "oscA.pos", 0.5f }, { "penv.amt", 9 },
            { "penv.dec", 0.04f }, { "aenv.dec", 0.22f }, { "lvl", 0.78f },
        }),
    };
    return bank;
}

std::vector<std::pair<int, float>> applyPatchToPad(int padI, const PadPatch& patch) {
    // Resolve the relative override ids against pad 0 (whose flat ids equal
    // the field offsets), mirroring patches.ts applyPatchToParams.
    std::array<float, DPAD_NFIELDS> vals{};
    std::array<bool, DPAD_NFIELDS> has{};
    for (const auto& [rel, v] : patch.params) {
        const int f = drumIdFromString("pad0." + rel);
        if (f >= 0 && f < DPAD_NFIELDS) { vals[(size_t)f] = v; has[(size_t)f] = true; }
    }
    static const DrumParamArray defs = defaultDrumParams();
    std::vector<std::pair<int, float>> out;
    out.reserve(DPAD_NFIELDS - 2);
    for (int f = 0; f < DPAD_NFIELDS; f++) {
        if (f == DP_CHOKE || f == DP_OUT) continue;   // routing stays untouched
        const int id = dpid(padI, f);
        out.emplace_back(id, has[(size_t)f] ? vals[(size_t)f] : defs[(size_t)id]);
    }
    return out;
}

} // namespace fable
