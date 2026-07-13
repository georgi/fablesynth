// C++ port of src/seq/factory.ts — verbatim clip-for-clip, note-for-note
// transcription so both builds ship the same NEON TALE factory session.
// DR-1 pad map (TR-VOID kit): 0 KICK · 2 SNARE · 3 CLAP · 4 RIM · 5 CH HAT ·
// 6 OH HAT · 8..10 TOMS · 12/13 PERC.
#include "SeqFactory.h"
#include "ClipLibrary.gen.h"

#include <algorithm>
#include <tuple>

namespace fable {
namespace {

// ---------- pattern builders ----------

// One entry per bar; each bar is a list of {pad, steps, accents}.
using DrumHit = std::tuple<int, std::vector<int>, std::vector<int>>;
using DrumBar = std::vector<DrumHit>;

ClipData drumClip(std::string name, std::vector<DrumBar> barsSpec) {
    const int bars = (int)barsSpec.size();
    ClipData c;
    c.name = std::move(name);
    c.bars = bars;
    c.bytes.assign((size_t)(bars * sqBytesPerBar(Machine::DR1)), 0);
    for (int b = 0; b < bars; b++) {
        for (auto& hit : barsSpec[(size_t)b]) {
            int pad = std::get<0>(hit);
            const std::vector<int>& steps = std::get<1>(hit);
            const std::vector<int>& accents = std::get<2>(hit);
            for (int s : steps) {
                bool acc = std::find(accents.begin(), accents.end(), s) != accents.end();
                c.bytes[(size_t)sqDr1Idx(b, pad, s)] = (uint8_t)(acc ? 2 : 1);
            }
        }
    }
    return c;
}

struct NoteStep {
    int s;
    int n;
    int o = 0;
    bool a = false;
    bool t = false;
};

ClipData noteClip(std::string name, int bars, std::vector<NoteStep> steps) {
    ClipData c;
    c.name = std::move(name);
    c.bars = bars;
    c.bytes.assign((size_t)(bars * sqBytesPerBar(Machine::WT1)), 0);
    // oct byte defaults to 1 (= oct 0) so rests read back neutral
    for (size_t i = 2; i < c.bytes.size(); i += SQ_NOTE_STRIDE) c.bytes[i] = 1;
    for (auto& st : steps) {
        int o = sqNoteIdx(st.s / 16, st.s % 16);
        c.bytes[(size_t)o] = (uint8_t)(1 | (st.a ? 2 : 0) | (st.t ? 4 : 0));
        c.bytes[(size_t)o + 1] = (uint8_t)st.n;
        c.bytes[(size_t)o + 2] = (uint8_t)(st.o + 1);
    }
    return c;
}

// A held swell: one attack, then a tie on EVERY following step of the span —
// a tie only sustains when the immediately next step ties in, so gaps in the
// tie chain would gate the note off (and slow-attack pads would never open).
std::vector<NoteStep> held(int s0, int span, int n, int o = 0) {
    std::vector<NoteStep> out;
    out.push_back({ s0, n, o, false, false });
    for (int s = s0 + 1; s < s0 + span; s++) out.push_back({ s, n, o, false, true });
    return out;
}

void append(std::vector<NoteStep>& dst, std::vector<NoteStep> src) {
    for (auto& s : src) dst.push_back(s);
}

// ---------- drum clips ----------

constexpr int KICK = 0, SNARE = 2, CLAP = 3, RIM = 4, CH = 5, OH = 6, TOM_LO = 8, TOM_HI = 10, PERC = 12;

const std::vector<int> four = { 0, 4, 8, 12 };
const std::vector<int> off8 = { 2, 6, 10, 14 };
const std::vector<int> all16 = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

ClipData sparseKick() {
    return drumClip("SPARSE KICK", {
        { { KICK, { 0, 8 }, { 0 } }, { RIM, { 10 }, {} } },
    });
}

ClipData hatRise() {
    return drumClip("HAT RISE", {
        { { KICK, four, { 0 } }, { CH, { 0, 4, 8, 12 }, {} }, { PERC, { 14 }, {} } },
        { { KICK, four, { 0 } }, { CH, all16, { 4, 12 } }, { OH, { 14 }, {} } },
    });
}

ClipData fullKitA() {
    return drumClip("FULL KIT A", {
        { { KICK, four, { 0 } }, { SNARE, { 4, 12 }, {} }, { CH, off8, {} }, { OH, { 10 }, {} }, { CLAP, { 12 }, {} } },
        { { KICK, four, { 0 } }, { SNARE, { 4, 12 }, { 12 } }, { CH, off8, {} }, { OH, { 6, 14 }, {} }, { PERC, { 7, 15 }, {} } },
    });
}

ClipData fullKitB() {
    return drumClip("FULL KIT B", {
        { { KICK, { 0, 4, 7, 8, 12 }, { 0 } }, { SNARE, { 4, 12 }, {} }, { CH, all16, { 2, 6, 10, 14 } }, { CLAP, { 12 }, {} } },
        { { KICK, { 0, 4, 8, 10, 12 }, { 0 } }, { SNARE, { 4, 12, 15 }, { 15 } }, { CH, all16, { 2, 6, 10, 14 } }, { TOM_HI, { 13 }, {} }, { TOM_LO, { 14 }, {} } },
    });
}

ClipData tailKick() {
    return drumClip("TAIL KICK", {
        { { KICK, { 0, 8 }, {} }, { OH, { 8 }, {} }, { PERC, { 12 }, {} } },
    });
}

// ---------- bass clips (BL-1 lanes: 0 = its C, slide bit glides) ----------

ClipData acidCrawl() {
    return noteClip("ACID CRAWL", 2, {
        { 0, 0 }, { 3, 0, 0, false, true }, { 8, 3 }, { 11, 0 },
        { 16, 0 }, { 19, 10, -1 }, { 24, 5 }, { 27, 3, 0, false, true },
    });
}

ClipData acid303() {
    return noteClip("ACID 303", 1, {
        { 0, 0, 0, true }, { 2, 0 }, { 3, 0, 1, false, true }, { 4, 0 },
        { 6, 3, 0, true }, { 7, 5, 0, false, true }, { 8, 0 }, { 10, 10, -1 },
        { 11, 0, 0, false, true }, { 12, 7, 0, true }, { 14, 5 }, { 15, 3, 0, false, true },
    });
}

ClipData acidShift() {
    return noteClip("ACID SHIFT", 1, {
        { 0, 3, 0, true }, { 2, 3 }, { 4, 10, -1 }, { 5, 3, 0, false, true },
        { 7, 7 }, { 8, 3, 0, true }, { 10, 5, 0, false, true }, { 12, 0 },
        { 13, 0, 1, false, true }, { 15, 10, -1 },
    });
}

ClipData subHold() {
    std::vector<NoteStep> steps;
    append(steps, held(0, 16, 0, -1));
    append(steps, held(16, 16, 10, -1));
    append(steps, held(32, 16, 3, -1));
    append(steps, held(48, 12, 5, -1));
    append(steps, held(60, 4, 7, -1));
    return noteClip("SUB HOLD", 4, steps);
}

// ---------- lead / pad clips (WT-1, lanes relative to seq.root C3) ----------

ClipData glassHook() {
    return noteClip("GLASS HOOK", 2, {
        { 0, 0, 1, true }, { 3, 10 }, { 6, 7 }, { 8, 3, 1 },
        { 10, 10, 0, false, true }, { 14, 5 },
        { 16, 0, 1, true }, { 19, 10 }, { 22, 7 }, { 24, 2, 1 },
        { 26, 0, 1, false, true }, { 30, 7, 0, false, true },
    });
}

ClipData glassHookII() {
    return noteClip("GLASS HOOK II", 2, {
        { 0, 3, 1, true }, { 3, 0, 1 }, { 6, 10 }, { 8, 5, 1 },
        { 10, 3, 1, false, true }, { 14, 7 },
        { 16, 3, 1, true }, { 19, 2, 1 }, { 22, 0, 1 }, { 24, 10 },
        { 26, 7, 0, false, true }, { 30, 10, 0, false, true },
    });
}

ClipData glassSolo() {
    return noteClip("GLASS SOLO", 4, {
        { 0, 0, 1 }, { 4, 10, 0, false, true }, { 8, 7, 0, false, true }, { 12, 10, 0, false, true },
        { 16, 3, 1 }, { 20, 2, 1, false, true }, { 24, 0, 1, false, true }, { 28, 10, 0, false, true },
        { 32, 5, 1 }, { 36, 3, 1, false, true }, { 40, 2, 1, false, true }, { 44, 0, 1, false, true },
        { 48, 10, 0, true }, { 52, 7, 0, false, true }, { 56, 3, 0, false, true }, { 60, 0, 0, false, true },
    });
}

ClipData airBed() {
    std::vector<NoteStep> steps;
    append(steps, held(0, 16, 0));
    append(steps, held(16, 16, 10, -1));
    append(steps, held(32, 16, 8, -1));
    append(steps, held(48, 16, 3));
    return noteClip("AIR BED", 4, steps);
}

ClipData airBedII() {
    std::vector<NoteStep> steps;
    append(steps, held(0, 16, 3));
    append(steps, held(16, 16, 2));
    append(steps, held(32, 16, 0));
    append(steps, held(48, 16, 10, -1));
    return noteClip("AIR BED II", 4, steps);
}

ClipData fogStabs() {
    return noteClip("FOG STABS", 2, {
        { 0, 0 }, { 2, 0, 0, false, true }, { 7, 3 }, { 10, 5, 0, true }, { 12, 5, 0, false, true },
        { 16, 10, -1 }, { 18, 10, -1, false, true }, { 23, 0 }, { 26, 2 }, { 28, 3, 0, false, true },
    });
}

ClipData fogSwell() {
    std::vector<NoteStep> steps;
    append(steps, held(0, 32, 0));
    append(steps, held(32, 32, 10, -1));
    append(steps, held(64, 32, 5));
    append(steps, held(96, 32, 3));
    return noteClip("FOG SWELL", 8, steps);
}

ClipData airOut() {
    std::vector<NoteStep> steps;
    append(steps, held(0, 32, 0));
    append(steps, held(32, 32, 10, -1));
    append(steps, held(64, 64, 0, -1));
    return noteClip("AIR OUT", 8, steps);
}

// scene[track] convenience: mark hasClip and store the clip, or leave empty.
SceneData scene(std::string name, std::vector<ClipData*> clips) {
    SceneData sc;
    sc.name = std::move(name);
    sc.clips.resize(clips.size());
    sc.hasClip.resize(clips.size(), false);
    // pass is left empty: the factory session ships no pass-through tracks,
    // so every empty cell is a stop button on scene launch (default).
    for (size_t t = 0; t < clips.size(); t++) {
        if (clips[t] != nullptr) {
            sc.clips[t] = *clips[t];
            sc.hasClip[t] = true;
        }
    }
    return sc;
}

} // namespace

// ---------- the session ----------

SessionData factorySession() {
    static ClipData SPARSE_KICK = sparseKick();
    static ClipData HAT_RISE = hatRise();
    static ClipData FULL_KIT_A = fullKitA();
    static ClipData FULL_KIT_B = fullKitB();
    static ClipData TAIL_KICK = tailKick();
    static ClipData ACID_CRAWL = acidCrawl();
    static ClipData ACID_303 = acid303();
    static ClipData ACID_SHIFT = acidShift();
    static ClipData SUB_HOLD = subHold();
    static ClipData GLASS_HOOK = glassHook();
    static ClipData GLASS_HOOK_II = glassHookII();
    static ClipData GLASS_SOLO = glassSolo();
    static ClipData AIR_BED = airBed();
    static ClipData AIR_BED_II = airBedII();
    static ClipData FOG_STABS = fogStabs();
    static ClipData FOG_SWELL = fogSwell();
    static ClipData AIR_OUT = airOut();

    SessionData s;
    s.name = "NEON TALE";
    s.bpm = 122;
    s.swing = 0;
    s.quant = Quant::Bar;

    s.tracks = {
        { Machine::DR1, "DRUMS", 0xff4de8ffu, 0.8f, { true, 0, {} } }, // TR-VOID
        { Machine::BL1, "BASS",  0xff4dff9eu, 0.75f, { true, 0, {} } }, // ACID LINE
        { Machine::WT1, "LEAD",  0xffffa14du, 0.85f, { true, 3, {} } }, // CRYSTAL PLUCK
        { Machine::WT1, "PADS",  0xffb18cffu, 1.0f, { true, 11, {} } }, // FUTURE CHORD
    };

    s.scenes = {
        scene("INTRO", { &SPARSE_KICK, nullptr, nullptr, &AIR_BED }),
        scene("BUILD", { &HAT_RISE, &ACID_CRAWL, nullptr, &AIR_BED_II }),
        scene("DROP A", { &FULL_KIT_A, &ACID_303, &GLASS_HOOK, &FOG_STABS }),
        scene("DROP B", { &FULL_KIT_B, &ACID_SHIFT, &GLASS_HOOK_II, &FOG_STABS }),
        scene("BREAK", { nullptr, &SUB_HOLD, &GLASS_SOLO, &FOG_SWELL }),
        scene("OUTRO", { &TAIL_KICK, nullptr, nullptr, &AIR_OUT }),
    };

    return s;
}

const std::vector<SessionPreset>& factorySessionLibrary() {
    static const std::vector<SessionPreset> presets = [] {
        auto make = [](const char* name, const char* family, const char* variation,
                       int energy, std::initializer_list<const char*> tags,
                       std::array<int, 4> programs, int variationIndex) {
            SessionPreset preset;
            preset.name = name;
            preset.family = family;
            preset.variation = variation;
            preset.energy = energy;
            for (auto* tag : tags) preset.tags.emplace_back(tag);
            preset.session = factorySession();
            preset.session.name = name;
            preset.session.bpm = 96.0 + energy * 7.0 + variationIndex;
            preset.session.swing = family == std::string("HOUSE") ? 0.12
                                 : family == std::string("LO-FI") ? 0.18 : 0.0;
            for (size_t t = 0; t < programs.size(); ++t)
                preset.session.tracks[t].patch = PatchRef { true, programs[t], {} };

            // Build a six-scene arrangement from the shared factory clip bank.
            // Prefer the session's family, but deliberately fall back to the
            // complete machine pool so every preset remains fully populated.
            const std::string wanted = family == std::string("NEON") ? "techno"
                : family == std::string("ACID") ? "acid"
                : family == std::string("AMBIENT") ? "ambient"
                : family == std::string("HOUSE") ? "house"
                : family == std::string("LO-FI") ? "lo-fi" : "cinematic";
            const auto& clips = factoryClipLibrary();
            for (size_t s = 0; s < preset.session.scenes.size(); ++s) {
                auto& sceneData = preset.session.scenes[s];
                for (size_t t = 0; t < preset.session.tracks.size(); ++t) {
                    const auto machine = preset.session.tracks[t].machine;
                    std::vector<const ClipLibraryEntry*> candidates;
                    for (const auto& clip : clips)
                        if (clip.machine == machine && clip.family == wanted)
                            candidates.push_back(&clip);
                    if (candidates.empty())
                        for (const auto& clip : clips)
                            if (clip.machine == machine) candidates.push_back(&clip);
                    const size_t pick = (s + (size_t)variationIndex * 3 + t * 2)
                                      % candidates.size();
                    const auto& clip = *candidates[pick];
                    sceneData.clips[t] = ClipData { clip.name, clip.bars, clip.bytes };
                    sceneData.hasClip[t] = true;
                }
            }
            return preset;
        };

        // Program order is DR-1, BL-1, WT-1 lead, WT-1 pad. The two WT slots
        // are voiced as complementary roles rather than interchangeable picks.
        auto library = std::vector<SessionPreset> {
            // NEON / SYNTHWAVE
            make("NEON TALE",    "NEON", "ORIGINAL", 3, { "bright", "balanced", "wide" }, { 0, 0, 3, 11 }, 0),
            make("NEON CHASE",   "NEON", "CHASE",    5, { "bright", "driving", "wide" }, { 3, 2, 4, 11 }, 1),
            make("GLASS CIRCUIT", "NEON", "GLASS",   2, { "clean", "glassy", "sparse" }, { 9, 9, 3, 6 }, 2),
            make("AFTERGLOW",    "NEON", "SOFT",     2, { "warm", "soft", "wide" }, { 5, 1, 5, 1 }, 3),

            // ACID / WAREHOUSE
            make("WAREHOUSE RAW", "ACID", "RAW",     5, { "hard", "dark", "driving" }, { 6, 4, 12, 17 }, 0),
            make("ACID FLASH",    "ACID", "FLASH",   4, { "acid", "bright", "punchy" }, { 3, 0, 14, 4 }, 1),
            make("STEEL PULSE",   "ACID", "METAL",   4, { "metallic", "tight", "industrial" }, { 7, 6, 13, 9 }, 2),
            make("PEAK SIGNAL",   "ACID", "PEAK",    5, { "distorted", "wide", "peak-time" }, { 2, 2, 19, 11 }, 3),

            // AMBIENT / DEEP
            make("DEEP FOG",      "AMBIENT", "FOG",   1, { "dark", "deep", "slow" }, { 4, 3, 5, 17 }, 0),
            make("GLASS BLOOM",   "AMBIENT", "BLOOM", 2, { "glassy", "clean", "lush" }, { 11, 11, 3, 1 }, 1),
            make("FROZEN BELL",   "AMBIENT", "FROZEN", 2, { "cold", "bell", "sparse" }, { 9, 11, 6, 17 }, 2),
            make("AIR TEMPLE",    "AMBIENT", "TEMPLE", 2, { "warm", "ceremonial", "wide" }, { 1, 7, 15, 1 }, 3),

            // HOUSE / CLUB
            make("DUST HOUSE",    "HOUSE", "DUST",    3, { "dusty", "groovy", "warm" }, { 5, 5, 14, 1 }, 0),
            make("MIDNIGHT FLOOR", "HOUSE", "NIGHT",  4, { "club", "round", "wide" }, { 3, 1, 13, 11 }, 1),
            make("TAPE DISCO",    "HOUSE", "TAPE",    3, { "tape", "soft", "groovy" }, { 8, 7, 19, 1 }, 2),
            make("CLEAN CLUB",    "HOUSE", "CLEAN",   4, { "clean", "tight", "bright" }, { 9, 11, 14, 4 }, 3),

            // LO-FI / RETRO
            make("VHS GARDEN",    "LO-FI", "VHS",     2, { "tape", "dark", "nostalgic" }, { 8, 7, 16, 17 }, 0),
            make("POCKET DUST",   "LO-FI", "POCKET",  2, { "dusty", "small", "warm" }, { 5, 5, 3, 1 }, 1),
            make("TOY PARADE",    "LO-FI", "TOY",     4, { "8-bit", "playful", "broken" }, { 10, 9, 16, 15 }, 2),
            make("WORN SIGNAL",   "LO-FI", "WORN",    3, { "distorted", "dark", "unstable" }, { 2, 10, 5, 17 }, 3),

            // CINEMATIC / EXPERIMENTAL
            make("CHROME CATHEDRAL", "CINEMATIC", "CATHEDRAL", 3, { "large", "metallic", "ceremonial" }, { 11, 3, 6, 1 }, 0),
            make("MACHINE TENSION",  "CINEMATIC", "TENSION",   4, { "industrial", "tense", "dark" }, { 7, 10, 12, 17 }, 1),
            make("VOID MARCH",       "CINEMATIC", "MARCH",     4, { "heavy", "dark", "driving" }, { 0, 8, 9, 17 }, 2),
            make("FINAL HORIZON",    "CINEMATIC", "FINALE",    5, { "epic", "wide", "bright" }, { 6, 8, 19, 11 }, 3),
        };
        // Preserve the hand-authored cross-platform factory session exactly.
        library.front().session = factorySession();
        return library;
    }();
    return presets;
}

} // namespace fable
