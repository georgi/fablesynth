// C++ port of src/seq/factory.ts — note-for-note transcription so both builds
// ship the same NEON TALE factory session. Clip bytes use the shared packed
// layout (flags: on/acc/duration, note+slide, oct+1) — identical to the web.
// DR-1 pad map (shared by 808, UZU, and hybrid kits): 0 KICK · 2 SNARE · 3 CLAP · 4 RIM · 5 CH HAT ·
// 6 OH HAT · 8..10 TOMS · 12/13 PERC.
#include "SeqFactory.h"
#include "ClipLibrary.gen.h"

#include <algorithm>

namespace fable {
namespace {

// ---------- pattern builders ----------

// Field order is positional so clip literals read like the web NoteStep:
// { s, n, o, a, sl, d, lane }. `sl` (BL-1 slide) precedes `d` (duration) so a
// bare fifth bool still means a slide; durations default to one step.
struct NoteStep {
    int s;
    int n;
    int o = 0;
    bool a = false;
    bool sl = false;   // BL-1 legato slide (note-byte bit 7); WT-1 ignores it
    int d = 1;         // duration in 16th-note steps (1..63)
    int lane = 0;      // WT-1 chord voice
};

ClipData noteClip(Machine machine, std::string name, int bars, std::vector<NoteStep> steps) {
    ClipData c;
    c.name = std::move(name);
    c.bars = bars;
    c.bytes.assign((size_t)(bars * sqBytesPerBar(machine)), 0);
    // One-step duration plus neutral octave for every note slot.
    for (size_t i = 0; i < c.bytes.size(); i += SQ_NOTE_STRIDE) {
        c.bytes[i] = 1 << 2;
        c.bytes[i + 2] = 1;
    }
    const int clipSteps = bars * SQ_STEPS_PER_BAR;
    for (auto& st : steps) {
        const int o = machine == Machine::WT1 ? sqWtNoteIdx(st.s / 16, st.s % 16, st.lane)
                                              : sqNoteIdx(st.s / 16, st.s % 16);
        // duration clamps to 63 and to the remaining clip length (web noteClip)
        const int duration = std::min({ 63, clipSteps - st.s, std::max(1, st.d) });
        c.bytes[(size_t)o]     = (uint8_t)(1 | (st.a ? 2 : 0) | (duration << 2));
        c.bytes[(size_t)o + 1] = (uint8_t)(st.n | (machine == Machine::BL1 && st.sl ? 0x80 : 0));
        c.bytes[(size_t)o + 2] = (uint8_t)(st.o + 1);
    }
    return c;
}

ClipData bassClip(std::string name, int bars, std::vector<NoteStep> steps) {
    return noteClip(Machine::BL1, std::move(name), bars, std::move(steps));
}
ClipData wtClip(std::string name, int bars, std::vector<NoteStep> steps) {
    return noteClip(Machine::WT1, std::move(name), bars, std::move(steps));
}

// A held swell: a single note whose duration spans `span` steps, split only
// at the packed format's 63-step limit (web factory.ts held). Duration — not a
// tie chain — sustains the note across steps.
std::vector<NoteStep> held(int s0, int span, int n, int o = 0, int lane = 0) {
    std::vector<NoteStep> out;
    for (int s = s0, remaining = span; remaining > 0;) {
        const int d = std::min(remaining, 63);
        out.push_back({ s, n, o, false, false, d, lane });
        s += d;
        remaining -= d;
    }
    return out;
}

void append(std::vector<NoteStep>& dst, std::vector<NoteStep> src);

std::vector<NoteStep> chordHeld(int s0, int span, int root, int octave = 0, bool minor = true) {
    std::vector<NoteStep> out;
    const int intervals[3] { 0, minor ? 3 : 4, 7 };
    for (int lane = 0; lane < 3; ++lane) {
        const int absolute = root + intervals[lane];
        append(out, held(s0, span, absolute % 12, octave + absolute / 12, lane));
    }
    return out;
}

void append(std::vector<NoteStep>& dst, std::vector<NoteStep> src) {
    for (auto& s : src) dst.push_back(s);
}

// FOG STABS voicing (web factory.ts FOG_STABS flatMap): each root expands to a
// 3-note chord across lanes 0..2 — root, +3, +7 — with an octave bump when
// the interval crosses a 12-step boundary.
std::vector<NoteStep> fogVoicing(std::vector<NoteStep> roots) {
    std::vector<NoteStep> out;
    for (auto step : roots) {
        out.push_back(step);                                  // lane 0 (root)
        NoteStep v1 = step; v1.lane = 1;
        v1.n = (step.n + 3) % 12; v1.o = step.o + (step.n >= 9 ? 1 : 0);
        out.push_back(v1);
        NoteStep v2 = step; v2.lane = 2;
        v2.n = (step.n + 7) % 12; v2.o = step.o + (step.n >= 5 ? 1 : 0);
        out.push_back(v2);
    }
    return out;
}

// ---------- drum clips ----------

ClipData libraryDrumClip(const char* id) {
    for (const auto& clip : factoryClipLibrary())
        if (clip.machine == Machine::DR1 && clip.id == id)
            return ClipData { clip.name, clip.bars, clip.bytes };
    return {};
}

// ---------- bass clips (BL-1 lanes: 0 = its C, slide bit glides) ----------

ClipData acidCrawl() {
    return bassClip("ACID CRAWL", 2, {
        { 0, 0 }, { 3, 0, 0, false, true }, { 8, 3 }, { 11, 0 },
        { 16, 0 }, { 19, 10, -1 }, { 24, 5 }, { 27, 3, 0, false, true },
    });
}

ClipData acid303() {
    return bassClip("ACID 303", 4, {
        { 0, 0, 0, true }, { 2, 0 }, { 3, 0, 1, false, true }, { 4, 0 },
        { 6, 3, 0, true }, { 7, 5, 0, false, true }, { 8, 0 }, { 10, 10, -1 },
        { 11, 0, 0, false, true }, { 12, 7, 0, true }, { 14, 5 }, { 15, 3, 0, false, true },
        { 16, 10, 0, true }, { 18, 10 }, { 20, 5 }, { 22, 3, 0, false, true },
        { 24, 10 }, { 26, 0, 1 }, { 28, 7 }, { 30, 5, 0, false, true },
        { 32, 7, 0, true }, { 34, 7 }, { 36, 2 }, { 38, 0, 0, false, true },
        { 40, 7 }, { 42, 10, -1 }, { 44, 5 }, { 46, 3, 0, false, true },
        { 48, 5, 0, true }, { 50, 5 }, { 52, 0 }, { 54, 10, -1, false, true },
        { 56, 5 }, { 58, 7 }, { 60, 3 }, { 62, 0, 0, false, true },
    });
}

ClipData acidShift() {
    return bassClip("ACID SHIFT", 4, {
        { 0, 3, 0, true }, { 2, 3 }, { 4, 10, -1 }, { 5, 3, 0, false, true },
        { 7, 7 }, { 8, 3, 0, true }, { 10, 5, 0, false, true }, { 12, 0 },
        { 13, 0, 1, false, true }, { 15, 10, -1 },
        { 16, 0, 0, true }, { 18, 0 }, { 20, 7 }, { 21, 0, 0, false, true },
        { 23, 10 }, { 24, 0, 0, true }, { 26, 3, 0, false, true }, { 28, 5 },
        { 30, 7, -1 }, { 31, 10, 0, false, true },
        { 32, 10, 0, true }, { 34, 10 }, { 36, 5 }, { 37, 10, 0, false, true },
        { 39, 3 }, { 40, 10, 0, true }, { 42, 0, 0, false, true }, { 44, 3 },
        { 46, 5, -1 }, { 47, 7, 0, false, true },
        { 48, 7, 0, true }, { 50, 7 }, { 52, 2 }, { 53, 7, 0, false, true },
        { 55, 0 }, { 56, 7, 0, true }, { 58, 10, 0, false, true }, { 60, 0 },
        { 62, 3, -1 },
    });
}

ClipData subHold() {
    std::vector<NoteStep> steps;
    append(steps, held(0, 16, 0, -1));
    append(steps, held(16, 16, 10, -1));
    append(steps, held(32, 16, 3, -1));
    append(steps, held(48, 12, 5, -1));
    append(steps, held(60, 4, 7, -1));
    return bassClip("SUB HOLD", 4, steps);
}

// ---------- lead / pad clips (WT-1, lanes relative to seq.root C3) ----------

ClipData glassHook() {
    return wtClip("GLASS HOOK", 4, {
        { 0, 0, 1, true }, { 3, 10 }, { 6, 7 }, { 8, 3, 1 },
        { 10, 10 }, { 14, 5 },
        { 16, 0, 1, true }, { 19, 10 }, { 22, 7 }, { 24, 2, 1 },
        { 26, 0, 1 }, { 30, 7 },
        { 32, 7, 1, true }, { 35, 5, 1 }, { 38, 2, 1 }, { 40, 10 },
        { 42, 7, 1 }, { 46, 5 },
        { 48, 5, 1, true }, { 51, 3, 1 }, { 54, 0, 1 }, { 56, 10 },
        { 58, 7 }, { 62, 3, 1 },
    });
}

ClipData glassHookII() {
    return wtClip("GLASS HOOK II", 4, {
        { 0, 3, 1, true }, { 3, 0, 1 }, { 6, 10 }, { 8, 5, 1 },
        { 10, 3, 1 }, { 14, 7 },
        { 16, 3, 1, true }, { 19, 2, 1 }, { 22, 0, 1 }, { 24, 10 },
        { 26, 7 }, { 30, 10 },
        { 32, 10, 1, true }, { 35, 7, 1 }, { 38, 5 }, { 40, 3, 1 },
        { 42, 2, 1 }, { 46, 0, 1 },
        { 48, 7, 1, true }, { 51, 5, 1 }, { 54, 3, 1 }, { 56, 0, 1 },
        { 58, 10 }, { 62, 7 },
    });
}

ClipData glassSolo() {
    return wtClip("GLASS SOLO", 4, {
        { 0, 0, 1 }, { 4, 10 }, { 8, 7 }, { 12, 10 },
        { 16, 3, 1 }, { 20, 2, 1 }, { 24, 0, 1 }, { 28, 10 },
        { 32, 5, 1 }, { 36, 3, 1 }, { 40, 2, 1 }, { 44, 0, 1 },
        { 48, 10, 0, true }, { 52, 7 }, { 56, 3 }, { 60, 0 },
    });
}

ClipData airBed() {
    std::vector<NoteStep> steps;
    append(steps, chordHeld(0, 16, 0));
    append(steps, chordHeld(16, 16, 10, -1, false));
    append(steps, chordHeld(32, 16, 8, -1, false));
    append(steps, chordHeld(48, 16, 3));
    return wtClip("AIR BED", 4, steps);
}

ClipData airBedII() {
    std::vector<NoteStep> steps;
    append(steps, chordHeld(0, 16, 3));
    append(steps, chordHeld(16, 16, 2));
    append(steps, chordHeld(32, 16, 0));
    append(steps, chordHeld(48, 16, 10, -1, false));
    return wtClip("AIR BED II", 4, steps);
}

ClipData fogStabs() {
    static const std::vector<NoteStep> roots = {
        { 0, 0 }, { 2, 0 }, { 7, 3 }, { 10, 5, 0, true }, { 12, 5 },
        { 16, 10, -1 }, { 18, 10, -1 }, { 23, 0 }, { 26, 2 }, { 28, 3 },
        { 32, 7, -1 }, { 34, 7, -1 }, { 39, 10, -1 }, { 42, 2, 0, true }, { 44, 2 },
        { 48, 5, -1 }, { 50, 5, -1 }, { 55, 0 }, { 58, 3 }, { 60, 0 },
    };
    return wtClip("FOG STABS", 4, fogVoicing(roots));
}

ClipData fogSwell() {
    std::vector<NoteStep> steps;
    append(steps, chordHeld(0, 32, 0));
    append(steps, chordHeld(32, 32, 10, -1, false));
    append(steps, chordHeld(64, 32, 5));
    append(steps, chordHeld(96, 32, 3));
    return wtClip("FOG SWELL", 8, steps);
}

ClipData airOut() {
    std::vector<NoteStep> steps;
    append(steps, chordHeld(0, 32, 0));
    append(steps, chordHeld(32, 32, 10, -1, false));
    append(steps, chordHeld(64, 64, 0, -1));
    return wtClip("AIR OUT", 8, steps);
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
    static ClipData SPARSE_KICK = libraryDrumClip("dr1-distant-ticks");
    static ClipData HAT_RISE = libraryDrumClip("dr1-hat-rise");
    static ClipData FULL_KIT_A = libraryDrumClip("dr1-neon-drive");
    static ClipData FULL_KIT_B = libraryDrumClip("dr1-jungle-sparks");
    static ClipData TAIL_KICK = libraryDrumClip("dr1-ghost-shuffle");
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
        { Machine::DR1, "DRUMS", 0xff4de8ffu, 0.8f, { true, 13, {} } }, // 808+UZU HYBRID
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
            const auto calibratedGain = [](size_t track, int program) {
                if (track == 0) return 0.78f; // drums: fixed reference level
                if (track == 1) { // measured BL-1 phrase loudness
                    switch (program) {
                        case 0: return 0.72f; case 2: return 0.73f;
                        case 4: return 0.87f; case 5: return 0.67f;
                        case 7: return 0.71f; case 8: return 0.70f;
                        case 10: return 0.65f; default: return 0.72f;
                    }
                }
                if (track == 2) { // measured WT-1 lead loudness
                    switch (program) {
                        case 3: return 1.00f; case 6: return 0.62f;
                        case 14: return 1.00f; case 15: return 0.95f;
                        case 19: return 0.45f; default: return 0.80f;
                    }
                }
                switch (program) { // measured WT-1 pad loudness
                    case 1: return 0.85f; case 6: return 1.00f;
                    case 11: return 0.65f; case 17: return 0.90f;
                    default: return 0.80f;
                }
            };
            for (size_t t = 0; t < programs.size(); ++t) {
                preset.session.tracks[t].patch = PatchRef { true, programs[t], {} };
                preset.session.tracks[t].gain = calibratedGain(t, programs[t]);
            }

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
            make("NEON CHASE",   "NEON", "CHASE",    5, { "bright", "driving", "wide" }, { 13, 2, 14, 11 }, 1),
            make("GLASS CIRCUIT", "NEON", "GLASS",   2, { "clean", "glassy", "sparse" }, { 12, 5, 6, 1 }, 2),
            make("AFTERGLOW",    "NEON", "SOFT",     2, { "warm", "soft", "wide" }, { 3, 7, 19, 17 }, 3),

            // ACID / WAREHOUSE
            make("WAREHOUSE RAW", "ACID", "RAW",     5, { "hard", "dark", "driving" }, { 13, 4, 14, 11 }, 0),
            make("ACID FLASH",    "ACID", "FLASH",   4, { "acid", "bright", "punchy" }, { 3, 0, 3, 1 }, 1),
            make("STEEL PULSE",   "ACID", "METAL",   4, { "metallic", "tight", "industrial" }, { 12, 2, 19, 17 }, 2),
            make("PEAK SIGNAL",   "ACID", "PEAK",    5, { "distorted", "wide", "peak-time" }, { 13, 5, 6, 11 }, 3),

            // AMBIENT / DEEP
            make("DEEP FOG",      "AMBIENT", "FOG",   1, { "dark", "deep", "slow" }, { 12, 7, 6, 17 }, 0),
            make("GLASS BLOOM",   "AMBIENT", "BLOOM", 2, { "glassy", "clean", "lush" }, { 13, 0, 19, 1 }, 1),
            make("FROZEN BELL",   "AMBIENT", "FROZEN", 2, { "cold", "bell", "sparse" }, { 12, 5, 6, 17 }, 2),
            make("AIR TEMPLE",    "AMBIENT", "TEMPLE", 2, { "warm", "ceremonial", "wide" }, { 3, 7, 15, 1 }, 3),

            // HOUSE / CLUB
            make("DUST HOUSE",    "HOUSE", "DUST",    3, { "dusty", "groovy", "warm" }, { 12, 4, 14, 11 }, 0),
            make("MIDNIGHT FLOOR", "HOUSE", "NIGHT",  4, { "club", "round", "wide" }, { 13, 0, 3, 1 }, 1),
            make("TAPE DISCO",    "HOUSE", "TAPE",    3, { "tape", "soft", "groovy" }, { 3, 7, 15, 17 }, 2),
            make("CLEAN CLUB",    "HOUSE", "CLEAN",   4, { "clean", "tight", "bright" }, { 12, 5, 19, 1 }, 3),

            // LO-FI / RETRO
            make("VHS GARDEN",    "LO-FI", "VHS",     2, { "tape", "dark", "nostalgic" }, { 3, 7, 15, 17 }, 0),
            make("POCKET DUST",   "LO-FI", "POCKET",  2, { "dusty", "small", "warm" }, { 12, 5, 3, 1 }, 1),
            make("TOY PARADE",    "LO-FI", "TOY",     4, { "8-bit", "playful", "broken" }, { 13, 2, 15, 17 }, 2),
            make("WORN SIGNAL",   "LO-FI", "WORN",    3, { "distorted", "dark", "unstable" }, { 3, 0, 19, 1 }, 3),

            // CINEMATIC / EXPERIMENTAL
            make("CHROME CATHEDRAL", "CINEMATIC", "CATHEDRAL", 3, { "large", "metallic", "ceremonial" }, { 13, 7, 6, 17 }, 0),
            make("MACHINE TENSION",  "CINEMATIC", "TENSION",   4, { "industrial", "tense", "dark" }, { 12, 5, 19, 11 }, 1),
            make("VOID MARCH",       "CINEMATIC", "MARCH",     4, { "heavy", "dark", "driving" }, { 3, 4, 6, 17 }, 2),
            make("FINAL HORIZON",    "CINEMATIC", "FINALE",    5, { "epic", "wide", "bright" }, { 13, 8, 19, 11 }, 3),
        };
        // Preserve the hand-authored cross-platform factory session exactly.
        library.front().session = factorySession();
        return library;
    }();
    return presets;
}

} // namespace fable
