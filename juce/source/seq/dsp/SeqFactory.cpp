// C++ port of src/seq/factory.ts — note-for-note transcription so both builds
// ship the same NEON TALE factory session. The session *library* generator
// (factorySessionLibrary) is likewise a transcription of
// src/seq/sessionPresets.ts, so the 40 presets match the web byte-for-byte.
// Clip bytes use the shared packed
// layout (flags: on/acc/duration, note+slide, oct+1) — identical to the web.
// DR-1 pad map (shared by 808, UZU, and hybrid kits): 0 KICK · 2 SNARE · 3 CLAP · 4 RIM · 5 CH HAT ·
// 6 OH HAT · 8..10 TOMS · 12/13 PERC.
#include "SeqFactory.h"
#include "ClipLibrary.gen.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <utility>

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

// A held chord close-voiced in the octave below the root (web chordHeldLow):
// each tone at its pitch class − 12.
std::vector<NoteStep> chordHeldLow(int s0, int span, int root, bool minor = true) {
    std::vector<NoteStep> out;
    const int intervals[3] { 0, minor ? 3 : 4, 7 };
    for (int lane = 0; lane < 3; ++lane)
        append(out, held(s0, span, ((root + intervals[lane]) % 12 + 12) % 12, -1, lane));
    return out;
}

void append(std::vector<NoteStep>& dst, std::vector<NoteStep> src) {
    for (auto& s : src) dst.push_back(s);
}

// FOG STABS voicing (web factory.ts FOG_STABS flatMap): each root expands to a
// close-voiced 3-note chord across lanes 0..2 — root, +3, +7 as pitch classes
// in the root octave (0..11), clear of the bass register and under the lead.
std::vector<NoteStep> fogVoicing(std::vector<NoteStep> roots) {
    std::vector<NoteStep> out;
    for (auto step : roots) {
        out.push_back(step);                                  // lane 0 (root)
        NoteStep v1 = step; v1.lane = 1; v1.n = (step.n + 3) % 12;
        out.push_back(v1);
        NoteStep v2 = step; v2.lane = 2; v2.n = (step.n + 7) % 12;
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
        { 0, 0, 0 }, { 2, 0, 0 }, { 7, 3, 0 }, { 10, 5, 0, true }, { 12, 5, 0 },
        { 16, 10, 0 }, { 18, 10, 0 }, { 23, 0, 0 }, { 26, 2, 0 }, { 28, 3, 0 },
        { 32, 7, 0 }, { 34, 7, 0 }, { 39, 10, 0 }, { 42, 2, 0, true }, { 44, 2, 0 },
        { 48, 5, 0 }, { 50, 5, 0 }, { 55, 0, 0 }, { 58, 5, 0 }, { 60, 0, 0 },
    };
    return wtClip("FOG STABS", 4, fogVoicing(roots));
}

ClipData fogSwell() {
    std::vector<NoteStep> steps;
    append(steps, chordHeldLow(0, 32, 0));
    append(steps, chordHeldLow(32, 32, 10, false));
    append(steps, chordHeldLow(64, 32, 5));
    append(steps, chordHeldLow(96, 32, 3));
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

// ---------- procedural session generator (port of src/seq/sessionPresets.ts) ----------

struct Harmony { std::array<int, 4> roots; std::array<bool, 4> minor; };

struct PresetSpec {
    const char* name; const char* family; const char* variation;
    int energy; std::vector<std::string> tags;
    std::array<int, 4> programs; int variationIndex;
};

Harmony harmonyFor(const PresetSpec& spec) {
    const auto tonic = [&]() -> int {
        const std::string f = spec.family;
        if (f == "NEON") return 0;
        if (f == "ACID") return 2;
        if (f == "AMBIENT") return 9;
        if (f == "HOUSE") return 5;
        if (f == "LO-FI") return 7;
        if (f == "MINIMAL") return 1;
        if (f == "FUTURE BASS") return 6;
        if (f == "TRIP HOP") return 10;
        if (f == "DUB") return 3;
        return 4; // CINEMATIC
    }();
    if (spec.family == std::string("MINIMAL")) {
        // AAAB, not a cadence: three bars locked on i, then one late move — the
        // hypnosis comes from the lock, and the single change lands harder for it.
        static const std::pair<int, bool> moves[4] = {
            { 10, false }, // …VII
            { 5, true },   // …iv
            { 8, false },  // …VI
            { 7, true },   // …v
        };
        const auto& move = moves[spec.variationIndex];
        Harmony h { { 0, 0, 0, move.first }, { true, true, true, move.second } };
        for (auto& root : h.roots) root = (root + tonic) % 12;
        return h;
    }
    static const Harmony plans[4] = {
        { { 0, 8, 3, 10 }, { true, false, false, false } }, // i–VI–III–VII
        { { 0, 5, 8, 7 },  { true, true, false, false } },  // i–iv–VI–V
        { { 0, 3, 10, 5 }, { true, false, false, true } },  // i–III–VII–iv
        { { 0, 7, 5, 10 }, { true, false, true, false } },  // i–V–iv–VII
    };
    Harmony h = plans[spec.variationIndex];
    for (auto& root : h.roots) root = (root + tonic) % 12;
    return h;
}

void putNote(std::vector<uint8_t>& bytes, int offset, int absolute, int duration = 1, bool accent = false) {
    const int pitchClass = ((absolute % 12) + 12) % 12;
    bytes[(size_t)offset] = (uint8_t)(1 | (accent ? 2 : 0) | (std::min(63, std::max(1, duration)) << 2));
    bytes[(size_t)offset + 1] = (uint8_t)pitchClass;
    bytes[(size_t)offset + 2] = (uint8_t)std::max(0, std::min(2, (absolute - pitchClass) / 12 + 1));
}

ClipData bassProgression(const Harmony& harmony, const PresetSpec& spec) {
    const std::string variation = spec.variation;
    if (spec.family == std::string("NEON")) {
        // Synthwave engine room: staccato 16ths pumping the low root, one 16th
        // of every beat lifted an octave (rotated per variation so sibling
        // songs pump differently), accents on the quarters, and a pickup into
        // each new bar.
        ClipData clip { variation + " DRIVE · 4 BAR", 4, sqEmptyClip(Machine::BL1, 4) };
        const int lift = 2 + (spec.variationIndex % 2);
        for (int bar = 0; bar < 4; ++bar) {
            const int root = harmony.roots[(size_t)bar];
            const int next = harmony.roots[(size_t)((bar + 1) % 4)];
            for (int step = 0; step < 16; ++step) {
                const int pitch = step == 15 ? next - 12 : step % 4 == lift ? root : root - 12;
                putNote(clip.bytes, sqNoteIdx(bar, step), pitch, 1, step % 4 == 0);
            }
        }
        return clip;
    }
    if (spec.family == std::string("MINIMAL")) {
        // Offbeat eighth stabs — the kick owns the downbeats and the bass
        // answers between them; per variation one offbeat lifts to the fifth
        // so sibling songs rock on a different beat.
        ClipData clip { variation + " PULSE · 4 BAR", 4, sqEmptyClip(Machine::BL1, 4) };
        const int lifted = 2 + spec.variationIndex * 4;
        for (int bar = 0; bar < 4; ++bar) {
            const int root = harmony.roots[(size_t)bar];
            for (int step : { 2, 6, 10, 14 })
                putNote(clip.bytes, sqNoteIdx(bar, step), step == lifted ? root - 5 : root - 12, 1, step == 2);
        }
        return clip;
    }
    if (spec.family == std::string("FUTURE BASS")) {
        // Half-time sub bed: one long low root under each chord, an octave pop
        // on beat 4, and a two-step slide into the next bar's root.
        ClipData clip { variation + " SUB · 4 BAR", 4, sqEmptyClip(Machine::BL1, 4) };
        for (int bar = 0; bar < 4; ++bar) {
            const int root = harmony.roots[(size_t)bar];
            const int next = harmony.roots[(size_t)((bar + 1) % 4)];
            putNote(clip.bytes, sqNoteIdx(bar, 0), root - 12, 10, true);
            putNote(clip.bytes, sqNoteIdx(bar, 12), root, 2);
            putNote(clip.bytes, sqNoteIdx(bar, 14), next - 12, 2);
        }
        return clip;
    }
    if (spec.family == std::string("TRIP HOP")) {
        // Slow head-nod line: root anchored on the one, a late off-beat push,
        // the fifth answering on beat 3's tail, and a pickup dragging into the
        // next bar.
        ClipData clip { variation + " NOD · 4 BAR", 4, sqEmptyClip(Machine::BL1, 4) };
        for (int bar = 0; bar < 4; ++bar) {
            const int root = harmony.roots[(size_t)bar];
            const int next = harmony.roots[(size_t)((bar + 1) % 4)];
            putNote(clip.bytes, sqNoteIdx(bar, 0), root - 12, 6, true);
            putNote(clip.bytes, sqNoteIdx(bar, 7), root - 12, 2);
            putNote(clip.bytes, sqNoteIdx(bar, 10), root - 5, 3);
            putNote(clip.bytes, sqNoteIdx(bar, 14), next - 12, 2);
        }
        return clip;
    }
    if (spec.family == std::string("DUB")) {
        // Steppers bassline: a tight push on the one, a rest where the skank
        // breathes, then a syncopated answer through the fifth below.
        ClipData clip { variation + " STEP · 4 BAR", 4, sqEmptyClip(Machine::BL1, 4) };
        for (int bar = 0; bar < 4; ++bar) {
            const int root = harmony.roots[(size_t)bar];
            putNote(clip.bytes, sqNoteIdx(bar, 0), root - 12, 3, true);
            putNote(clip.bytes, sqNoteIdx(bar, 3), root - 12, 2);
            putNote(clip.bytes, sqNoteIdx(bar, 8), root - 12, 2);
            putNote(clip.bytes, sqNoteIdx(bar, 10), root - 5, 2);
            putNote(clip.bytes, sqNoteIdx(bar, 13), root - 12, 2);
        }
        return clip;
    }
    ClipData clip { variation + " ROOTS · 4 BAR", 4, sqEmptyClip(Machine::BL1, 4) };
    for (int bar = 0; bar < 4; ++bar) {
        // One low root, then one fifth: deliberate space for the drums and pad.
        putNote(clip.bytes, sqNoteIdx(bar, 0), harmony.roots[(size_t)bar] - 12, 8, true);
        putNote(clip.bytes, sqNoteIdx(bar, 8), harmony.roots[(size_t)bar] - 5, 8);
    }
    return clip;
}

ClipData padProgression(const Harmony& harmony, const PresetSpec& spec) {
    const std::string variation = spec.variation;
    if (spec.family == std::string("MINIMAL")) {
        // No chords at all: a bare-fifth drone (no third) so the pad is texture,
        // not harmony — it only shifts when the AAAB plan moves in bar 4.
        ClipData clip { variation + " DRONE · 4 BAR", 4, sqEmptyClip(Machine::WT1, 4) };
        for (int bar = 0; bar < 4; ++bar) {
            const int root = harmony.roots[(size_t)bar];
            const int dyad[2] { root, root + 7 };
            for (int lane = 0; lane < 2; ++lane)
                putNote(clip.bytes, sqWtNoteIdx(bar, 0, lane), ((dyad[lane] % 12) + 12) % 12, 16);
        }
        return clip;
    }
    ClipData clip { variation + " CHORDS · 4 BAR", 4, sqEmptyClip(Machine::WT1, 4) };
    for (int bar = 0; bar < 4; ++bar) {
        // Close-voice the triad as pitch classes inside the root octave (+0..+11):
        // the pad bed stays strictly below the +12..+23 lead band for every root.
        const int root = harmony.roots[(size_t)bar];
        const int chord[3] { root, root + (harmony.minor[(size_t)bar] ? 3 : 4), root + 7 };
        for (int lane = 0; lane < 3; ++lane)
            putNote(clip.bytes, sqWtNoteIdx(bar, 0, lane), ((chord[lane] % 12) + 12) % 12, 16);
    }
    return clip;
}

// Hand-composed lead phrases — one per song. The strings are copied verbatim
// from src/seq/sessionPresets.ts LEAD_PHRASES (see there for the
// compositional notes): four bars separated by '|', a note is
// `step,duration,semitones` above the session tonic, '!' marks an accent.
const std::array<const char*, 4>& leadPhrasesFor(const std::string& family) {
    static const std::map<std::string, std::array<const char*, 4>> phrases = {
        { "NEON", {
            "0,2,0! 2,2,3 4,2,7 8,3,10 12,2,7 14,2,3 | 0,3,8! 4,2,0 8,4,10 14,2,7 | 0,2,7! 2,2,10 4,2,0 6,2,2 8,2,3 10,2,7 12,4,10 | 0,4,5! 4,2,2 8,8,0",
            "0,1,0! 2,1,0 4,2,3 6,2,7 8,2,10 11,2,7 14,2,8 | 0,2,5! 3,2,8 6,2,5 8,3,3 12,4,0 | 0,2,8! 2,2,10 4,2,0 8,2,3 10,2,0 12,2,10 14,2,8 | 0,3,7! 4,3,11 8,2,7 10,6,2",
            "0,3,7! 4,3,3 8,2,5 12,4,7 | 0,3,10! 4,2,7 7,2,5 10,2,3 12,4,2 | 0,2,2! 2,2,5 5,2,7 8,3,10 12,4,7 | 0,4,8! 6,2,7 8,8,5",
            "0,4,3! 4,3,2 8,4,0 13,3,7 | 0,4,2! 4,2,11 6,2,7 8,4,2 12,4,11 | 0,3,0! 3,3,8 8,2,5 10,2,3 12,4,5 | 0,4,2! 4,2,3 6,2,2 8,8,0" } },
        { "ACID", {
            "0,1,0! 1,1,0 3,1,3 4,2,0 8,2,10 11,1,8! 12,4,7 | 0,2,8! 3,1,7 4,2,8 8,2,0 10,1,10 12,3,8 | 0,1,3! 1,1,3 3,1,5 4,2,7 8,2,10 10,2,7 13,3,3 | 0,2,10! 4,2,7 8,2,5 12,4,2",
            "0,1,0! 2,1,3 4,1,0 6,2,7 8,1,5 10,2,3 13,3,0 | 0,2,8! 2,2,5 5,1,3 8,2,8 10,2,10 13,3,5 | 0,1,0! 1,1,0 4,2,8 6,2,10 8,2,8 12,4,3 | 0,2,11! 3,2,7 8,2,2 10,6,7",
            "0,2,7! 3,1,5 4,2,7 8,2,3 10,2,0 14,2,7 | 0,1,10! 1,1,10 4,2,7 6,2,3 8,3,10 12,4,7 | 0,2,5! 2,2,7 4,1,5 8,2,2 10,2,0 12,2,10 14,2,5 | 0,3,8! 4,2,5 8,8,0",
            "0,1,7! 1,1,7 3,1,10 4,2,7 8,2,0 10,2,10 13,3,7 | 0,2,2! 2,1,0 4,2,11 8,2,7 10,2,2 13,3,11 | 0,1,8! 2,1,8 4,2,5 6,2,8 8,2,10 12,4,5 | 0,2,10! 4,2,5 6,2,7 8,8,10" } },
        { "AMBIENT", {
            "0,8,0! 8,4,3 12,4,5 | 0,6,3! 6,6,2 12,4,0 | 0,8,10! 8,4,7 12,4,8 | 0,6,2! 6,10,0",
            "0,4,7! 4,4,8 8,4,7 12,4,10 | 0,6,8! 6,4,7 10,6,5 | 0,4,0! 4,4,10 8,8,8 | 0,4,11! 4,4,7 8,8,2",
            "0,4,7! 4,4,3 8,8,10 | 0,6,10! 6,10,7 | 0,8,2! 8,8,0 | 0,4,5! 4,12,3",
            "0,6,0! 6,4,2 10,6,3 | 0,4,7! 4,4,8 8,8,11 | 0,6,8! 6,4,7 10,6,5 | 0,4,10! 4,12,7" } },
        { "HOUSE", {
            "0,2,0! 3,2,3 7,2,7 10,2,3 14,2,0 | 0,2,8! 3,2,10 7,2,8 11,2,7 14,2,5 | 0,2,3! 3,2,7 7,3,10 11,2,7 14,2,10 | 0,2,2! 3,2,5 7,6,0",
            "0,2,7! 3,2,10 6,1,7 8,2,3 11,2,5 14,2,7 | 0,2,8! 3,2,7 7,2,5 10,2,8 14,2,10 | 0,2,0! 3,2,10 7,2,8 10,2,7 14,2,8 | 0,2,11! 3,2,7 7,3,2 12,4,7",
            "0,2,3! 4,2,0 7,2,3 10,2,7 13,3,5 | 0,2,7! 4,2,10 7,2,0 10,2,10 13,3,7 | 0,2,10! 4,2,2 7,2,10 10,2,8 13,3,7 | 0,2,8! 4,2,7 7,2,5 10,6,3",
            "0,1,0! 3,1,0 6,2,7 8,1,5 11,2,3 14,2,0 | 0,1,2! 3,1,2 6,2,11 8,2,7 11,2,2 14,2,11 | 0,1,5! 3,1,5 6,2,8 8,2,0 11,2,8 14,2,5 | 0,2,2! 3,2,5 6,2,7 8,8,10" } },
        { "LO-FI", {
            "0,3,3! 4,3,7 9,2,5 12,4,3 | 0,3,0! 4,3,10 9,5,8 | 0,3,7! 4,3,5 9,2,7 12,4,10 | 0,3,5! 4,2,3 7,2,2 9,7,0",
            "0,2,0! 3,3,3 8,2,2 10,2,3 13,3,0 | 0,3,8! 4,2,5 8,3,3 12,4,5 | 0,3,3! 4,2,0 8,4,10 13,3,8 | 0,3,7! 4,2,5 7,2,3 9,7,2",
            "0,1,0! 2,1,3 4,1,7 6,1,3 8,2,10 11,1,7 13,3,0 | 0,1,7! 2,1,10 4,1,7 6,1,3 8,3,2 12,4,3 | 0,1,10! 2,1,2 4,1,5 6,1,2 8,2,7 11,1,5 13,3,10 | 0,2,8! 3,1,7 5,2,5 8,8,0",
            "0,4,0! 5,2,10 8,3,8 12,4,7 | 0,4,11! 5,2,7 8,4,2 13,3,7 | 0,4,8! 5,2,7 8,3,5 12,4,8 | 0,4,5! 5,2,3 8,8,2" } },
        { "CINEMATIC", {
            "0,16,0! | 0,8,8! 8,8,3 | 0,16,7! | 0,8,10! 8,8,5",
            "0,8,0! 8,8,3 | 0,16,5! | 0,8,8! 8,8,0 | 0,16,7!",
            "0,16,0! | 0,8,3! 8,8,7 | 0,8,10! 8,8,2 | 0,16,5!",
            "0,8,0! 8,8,7 | 0,16,7! | 0,8,5! 8,8,8 | 0,16,10!" } },
        { "MINIMAL", {
            "0,1,0! 4,1,0 8,1,0 12,1,0 | 0,1,0! 4,1,0 8,1,0 | 0,1,0! 4,1,0 8,1,0 12,1,0 | 0,1,10! 4,1,10 8,8,10",
            "0,1,0! 2,1,0 8,1,0 10,1,0 | 0,1,0! 2,1,0 8,1,0 | 0,1,0! 2,1,0 8,1,0 10,1,0 14,1,0 | 0,1,5! 2,1,5 8,8,5",
            "0,1,7! 4,1,7 8,1,7 12,1,7 | 0,1,7! 4,1,7 12,1,7 | 0,1,7! 4,1,7 8,1,7 12,1,7 | 0,1,8! 4,1,8 8,8,8",
            "0,1,0! 4,1,0 8,1,0 12,1,0 | 0,1,0! 4,1,0 8,1,0 12,1,0 14,1,0 | 0,1,0! 4,1,0 8,1,0 12,1,0 | 0,1,7! 4,1,7 8,8,7" } },
        { "FUTURE BASS", {
            "0,2,0! 3,2,3 6,2,7 10,3,10 14,2,7 | 0,2,8! 3,2,7 6,4,3 11,2,5 14,2,8 | 0,2,7! 3,2,10 6,4,7 12,4,3 | 0,3,10! 4,2,7 6,2,5 8,8,2",
            "0,2,0! 3,2,3 6,2,5 8,3,7 12,4,3 | 0,2,5! 3,2,8 6,4,5 12,4,0 | 0,2,8! 3,2,10 6,2,8 8,4,7 13,3,5 | 0,2,7! 3,2,10 6,2,7 8,8,2",
            "0,2,3! 3,2,7 6,4,10 11,2,7 14,2,5 | 0,2,10! 3,2,7 6,4,3 12,4,7 | 0,2,2! 3,2,5 6,2,10 8,4,7 13,3,5 | 0,2,8! 4,2,5 8,8,0",
            "0,2,7! 3,2,3 6,4,0 11,2,3 14,2,7 | 0,2,11! 3,2,7 6,4,2 12,4,7 | 0,2,5! 3,2,8 6,2,0 8,4,10 13,3,8 | 0,2,10! 4,2,7 8,8,5" } },
        { "TRIP HOP", {
            "0,3,0! 5,2,3 8,3,5 13,3,3 | 0,3,8! 5,2,7 9,4,5 | 0,3,3! 5,2,5 8,3,7 13,3,10 | 0,3,5! 5,2,3 9,7,2",
            "0,3,3! 4,2,2 8,3,0 13,3,3 | 0,3,5! 5,2,8 9,4,7 | 0,3,0! 4,2,10 8,3,8 13,3,5 | 0,3,7! 5,2,5 9,7,2",
            "0,2,0! 3,2,3 8,2,5 11,2,3 14,2,0 | 0,3,10! 5,2,7 9,4,3 | 0,2,2! 3,2,5 8,2,7 11,2,5 14,2,2 | 0,3,8! 5,2,7 9,7,5",
            "0,3,7! 5,2,8 8,3,7 13,3,5 | 0,3,2! 5,2,0 9,4,10 | 0,3,5! 4,2,3 8,3,2 13,3,0 | 0,3,10! 5,2,8 9,7,7" } },
        { "DUB", {
            "0,4,0! 6,2,10 8,4,7 14,2,5 | 0,4,8! 6,2,7 8,6,5 | 0,4,7! 6,2,5 8,4,3 13,3,2 | 0,3,5! 4,2,3 8,8,2",
            "0,4,3! 6,2,5 8,4,7 14,2,10 | 0,4,5! 6,2,3 8,6,0 | 0,4,8! 6,2,10 8,4,8 13,3,7 | 0,3,7! 4,2,5 8,8,3",
            "0,4,7! 6,2,8 8,4,10 14,2,7 | 0,4,10! 6,2,8 8,6,7 | 0,4,5! 6,2,3 8,4,2 13,3,0 | 0,3,8! 4,2,7 8,8,5",
            "0,4,0! 6,2,2 8,4,3 14,2,5 | 0,4,7! 6,2,5 8,6,2 | 0,4,5! 6,2,7 8,4,8 13,3,7 | 0,3,2! 4,2,3 8,8,10" } },
    };
    const auto it = phrases.find(family);
    return it != phrases.end() ? it->second : phrases.at("NEON");
}

ClipData leadProgression(const Harmony& harmony, const PresetSpec& spec) {
    ClipData clip { std::string(spec.variation) + " MELODY · 4 BAR", 4, sqEmptyClip(Machine::WT1, 4) };
    const int tonic = harmony.roots[0];
    const char* p = leadPhrasesFor(spec.family)[(size_t)spec.variationIndex];
    int bar = 0;
    while (*p != '\0') {
        if (*p == ' ') { ++p; continue; }
        if (*p == '|') { ++bar; ++p; continue; }
        char* end = nullptr;
        const int step = (int)std::strtol(p, &end, 10);
        const int duration = (int)std::strtol(end + 1, &end, 10);
        const int pitch = (int)std::strtol(end + 1, &end, 10);
        const bool accent = *end == '!';
        p = accent ? end + 1 : end;
        // Voice the melody strictly one octave above the pad bed (+12..+23).
        putNote(clip.bytes, sqWtNoteIdx(bar, step, 0), (tonic + pitch) % 12 + 12, duration, accent);
    }
    return clip;
}

namespace drum {
constexpr int KICK = 0, SNARE = 2, CLAP = 3, RIM = 4, CH = 5, OH = 6,
              TOM_LO = 8, TOM_MID = 9, TOM_HI = 10, PERC_A = 12, PERC_B = 13;
}

struct DrumVoice { int pad; std::vector<int> steps; std::vector<int> accents; };
struct DrumArchetype { DrumVoice kick, back, hat, open; std::vector<DrumVoice> perc; };
struct DrumFillHit { int pad; int step; bool accent; };

const DrumArchetype& drumArchetypeFor(const std::string& family) {
    using namespace drum;
    static const std::map<std::string, DrumArchetype> archetypes = {
        { "NEON", { { KICK, { 0, 4, 8, 12 }, { 0 } }, { CLAP, { 4, 12 }, {} },
                    { CH, { 2, 6, 10, 14 }, { 2, 10 } }, { OH, { 2, 10 }, {} }, {} } },
        { "ACID", { { KICK, { 0, 4, 8, 12, 14 }, { 0 } }, { SNARE, { 4, 12 }, { 4, 12 } },
                    { CH, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 2, 6, 10, 14 } },
                    { OH, { 7 }, {} }, {} } },
        { "AMBIENT", { { KICK, { 0, 8 }, {} }, { RIM, { 8 }, {} },
                       { CH, { 4, 12 }, {} }, { OH, {}, {} }, {} } },
        { "HOUSE", { { KICK, { 0, 4, 8, 12 }, {} }, { CLAP, { 4, 12 }, {} },
                     { CH, { 2, 6, 10, 14 }, {} }, { OH, { 2, 6, 10, 14 }, { 6, 14 } }, {} } },
        { "LO-FI", { { KICK, { 0, 7, 10 }, { 0 } }, { SNARE, { 4, 12 }, {} },
                     { CH, { 0, 3, 6, 8, 11, 14 }, {} }, { OH, { 14 }, {} },
                     { { PERC_B, { 6 }, {} } } } },
        { "CINEMATIC", { { KICK, { 0, 10 }, { 0 } }, { SNARE, { 8 }, { 8 } },
                         { CH, {}, {} }, { OH, {}, {} },
                         { { TOM_LO, { 13 }, {} } } } },
        { "MINIMAL", { { KICK, { 0, 4, 8, 12 }, { 0 } }, { RIM, { 4, 12 }, {} },
                       { CH, { 2, 6, 10, 14 }, { 2, 10 } }, { OH, { 10 }, {} },
                       { { PERC_A, { 7 }, {} } } } },
        { "FUTURE BASS", { { KICK, { 0, 6, 10 }, { 0 } }, { SNARE, { 8 }, { 8 } },
                           { CH, { 0, 2, 4, 6, 8, 10, 12, 14 }, { 4, 12 } },
                           { OH, { 6 }, {} }, {} } },
        { "TRIP HOP", { { KICK, { 0, 3, 10 }, { 0 } }, { SNARE, { 4, 12 }, { 12 } },
                        { CH, { 0, 4, 6, 10, 14 }, { 6, 14 } }, { OH, { 7 }, {} },
                        { { PERC_B, { 11 }, {} } } } },
        { "DUB", { { KICK, { 8 }, { 8 } }, { SNARE, { 8 }, {} },
                   { CH, { 2, 6, 10, 14 }, { 6, 14 } }, { OH, { 12 }, {} },
                   { { PERC_B, { 3, 11 }, {} } } } },
    };
    const auto it = archetypes.find(family);
    return it != archetypes.end() ? it->second : archetypes.at("NEON");
}

const std::vector<DrumFillHit>& drumFillFor(const std::string& family) {
    using namespace drum;
    static const std::map<std::string, std::vector<DrumFillHit>> fills = {
        { "NEON", { { TOM_HI, 10, false }, { TOM_MID, 12, false }, { TOM_LO, 14, true } } },
        { "ACID", { { SNARE, 13, false }, { SNARE, 14, false }, { SNARE, 15, true } } },
        { "AMBIENT", { { OH, 12, false } } },
        { "HOUSE", { { CLAP, 13, false }, { CLAP, 15, true } } },
        { "LO-FI", { { PERC_B, 13, false }, { PERC_B, 15, false } } },
        { "CINEMATIC", { { TOM_HI, 8, false }, { TOM_MID, 10, false }, { TOM_LO, 12, true }, { TOM_LO, 14, true } } },
        { "MINIMAL", { { PERC_A, 12, false }, { PERC_A, 14, true } } },
        { "FUTURE BASS", { { SNARE, 10, false }, { SNARE, 12, false }, { SNARE, 14, true } } },
        { "TRIP HOP", { { SNARE, 10, false }, { RIM, 13, false }, { SNARE, 15, true } } },
        { "DUB", { { TOM_MID, 11, false }, { TOM_LO, 13, false }, { SNARE, 15, true } } },
    };
    const auto it = fills.find(family);
    return it != fills.end() ? it->second : fills.at("NEON");
}

ClipData drumProgression(const PresetSpec& spec, int scene) {
    using namespace drum;
    const auto& family = drumArchetypeFor(spec.family);
    ClipData clip;
    clip.bars = 4;
    clip.bytes.assign(4 * 256, 0);
    const auto hit = [&](int bar, int pad, int step, bool accent = false) {
        clip.bytes[(size_t)sqDr1Idx(bar, pad, step)] = (uint8_t)(accent ? 2 : 1);
    };
    const auto contains = [](const std::vector<int>& v, int x) {
        return std::find(v.begin(), v.end(), x) != v.end();
    };
    const bool intro = scene == 0, build = scene == 1, outro = scene == 5;

    std::vector<int> hatSteps;
    if (spec.energy >= 4) for (int s = 0; s < 16; ++s) hatSteps.push_back(s);
    else if (spec.energy <= 2) {
        for (size_t i = 0; i < family.hat.steps.size(); i += 2) hatSteps.push_back(family.hat.steps[i]);
    } else hatSteps = family.hat.steps;
    const int shift = spec.variationIndex + (scene == 3 ? 1 : 0);
    std::vector<int> hatAccents;
    for (size_t k = 0; k < family.hat.accents.size(); ++k)
        hatAccents.push_back(hatSteps.empty() ? -1 : hatSteps[((size_t)((int)k * 2 + shift)) % hatSteps.size()]);
    static const std::array<std::pair<int, int>, 4> ghosts { { { -1, -1 }, { KICK, 14 }, { SNARE, 11 }, { PERC_B, 3 } } };
    // AMBIENT keeps its ghosts on the eighth grid with soft voices — off-grid
    // kick/snare pokes read as glitches in so sparse a field. Each variation
    // still gets a distinct hit so sibling songs stay unique.
    static const std::array<std::pair<int, int>, 4> ambientGhosts { { { -1, -1 }, { CH, 10 }, { RIM, 12 }, { PERC_B, 6 } } };
    const auto ghost = (spec.family == std::string("AMBIENT") ? ambientGhosts : ghosts)[(size_t)spec.variationIndex];

    for (int bar = 0; bar < 4; ++bar) {
        const bool lastBar = bar == 3;
        std::vector<int> kickSteps;
        for (int s : family.kick.steps) if (!(intro || outro) || s % 8 == 0) kickSteps.push_back(s);
        for (int s : kickSteps) hit(bar, family.kick.pad, s, contains(family.kick.accents, s));
        if (!intro && !outro) {
            for (int s : family.back.steps) hit(bar, family.back.pad, s, contains(family.back.accents, s));
            if (!build && ghost.first >= 0) hit(bar, ghost.first, ghost.second);
        }
        if (!outro) {
            std::vector<int> steps;
            if (intro) {
                for (size_t i = 0; i < family.hat.steps.size(); ++i)
                    if (((int)i + spec.variationIndex) % 2 == 0) steps.push_back(family.hat.steps[i]);
            } else steps = hatSteps;
            for (int s : steps) hit(bar, family.hat.pad, s, !intro && contains(hatAccents, s));
            if (!intro) for (int s : family.open.steps) hit(bar, family.open.pad, s, contains(family.open.accents, s));
        }
        if (!intro) for (const auto& voice : family.perc) for (int s : voice.steps) hit(bar, voice.pad, s, contains(voice.accents, s));
        if ((intro || outro) && lastBar && ghost.first >= 0 && spec.variationIndex >= 2) hit(bar, ghost.first, ghost.second);
        // Builds climb into the drop on their own backbeat voice (snare/clap/rim)
        // rather than a perc pad — perc voices vary too wildly across kits.
        if (build && lastBar) for (int s : { 12, 13, 14, 15 }) hit(bar, family.back.pad, s, s == 15);
        if (!intro && !build && !outro && (lastBar || (scene == 3 && bar == 1)))
            for (const auto& f : drumFillFor(spec.family)) hit(bar, f.pad, f.step, f.accent);
    }
    const char* role = intro ? "INTRO" : build ? "BUILD" : outro ? "TAIL" : scene == 3 ? "DRIVE II" : "DRIVE";
    clip.name = std::string(spec.variation) + " " + role + " · 4 BAR";
    return clip;
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
            // Slow families anchor below the shared 96 base so energy still
            // spreads the tempo without pushing trip hop or dub into house
            // territory.
            const double bpmBase = family == std::string("TRIP HOP") ? 62.0
                                 : family == std::string("DUB") ? 56.0 : 96.0;
            preset.session.bpm = bpmBase + energy * 7.0 + variationIndex;
            preset.session.swing = family == std::string("HOUSE") ? 0.12
                                 : family == std::string("LO-FI") ? 0.18
                                 : family == std::string("TRIP HOP") ? 0.16
                                 : family == std::string("DUB") ? 0.08 : 0.0;
            // Full-chain, in-context track faders — measured by
            // test/measure_track_levels.cpp, which renders every song's four
            // tracks through their real engine+FX (incl. WT-1's leveling comp)
            // and balances each to the mean drum-bus RMS with perceptual
            // per-role offsets (bass +4 dB, pad +2 dB). Fader curve is
            // gain² × 1.4. Tables carry only currently-used programs; defaults
            // cover future picks until the next measure_track_levels run.
            const auto calibratedGain = [](size_t track, int program) {
                if (track == 0) { // DR-1 kits, leveled to the drum-bus mean
                    switch (program) {
                        case 0: return 1.00f; case 2: return 0.71f;
                        case 3: return 0.74f; case 4: return 1.00f;
                        case 6: return 0.48f; case 8: return 0.80f;
                        case 9: return 1.00f; case 12: return 0.74f;
                        case 13: return 0.80f; case 15: return 0.58f;
                        case 16: return 0.87f;
                        default: return 0.78f;
                    }
                }
                if (track == 1) { // BL-1 bass (+4 dB role offset)
                    switch (program) {
                        case 0: return 0.43f; case 2: return 0.46f;
                        case 3: return 0.44f; case 4: return 0.44f;
                        case 5: return 0.42f; case 7: return 0.43f;
                        case 8: return 0.41f; case 9: return 0.74f;
                        case 10: return 0.45f; case 11: return 0.50f;
                        case 12: return 0.42f; case 13: return 0.41f;
                        case 14: return 0.41f; case 15: return 0.41f;
                        case 16: return 0.71f; case 17: return 0.38f;
                        case 18: return 0.40f; case 19: return 0.42f;
                        case 20: return 0.41f; case 21: return 0.50f;
                        default: return 0.45f;
                    }
                }
                if (track == 2) { // WT-1 lead (at drum target)
                    switch (program) {
                        case 3: return 0.59f; case 4: return 0.45f;
                        case 6: return 0.43f; case 15: return 0.56f;
                        case 20: return 0.64f; case 21: return 0.48f;
                        case 22: return 0.51f; case 28: return 0.79f;
                        case 29: return 0.89f; case 33: return 0.45f;
                        case 35: return 0.47f; case 36: return 0.68f;
                        case 44: return 0.43f; case 45: return 0.70f;
                        case 46: return 0.31f; case 49: return 0.48f;
                        case 51: return 0.43f; case 52: return 0.46f;
                        case 53: return 0.43f; case 54: return 0.43f;
                        case 55: return 0.71f; case 58: return 0.44f;
                        case 59: return 0.80f; case 60: return 0.78f;
                        case 61: return 0.87f;
                        default: return 0.65f;
                    }
                }
                switch (program) { // WT-1 pad (+2 dB role offset)
                    case 1: return 0.59f; case 11: return 0.47f;
                    case 17: return 0.76f; case 22: return 0.47f;
                    case 24: return 0.56f; case 25: return 0.55f;
                    case 27: return 0.55f; case 32: return 1.00f;
                    case 34: return 0.52f; case 35: return 0.53f;
                    case 38: return 0.73f; case 40: return 0.53f;
                    case 42: return 0.72f; case 56: return 1.00f;
                    default: return 0.59f;
                }
            };
            for (size_t t = 0; t < programs.size(); ++t) {
                preset.session.tracks[t].patch = PatchRef { true, programs[t], {} };
                preset.session.tracks[t].gain = calibratedGain(t, programs[t]);
            }

            // One harmonic world per song (port of sessionPresets.ts buildSession).
            const PresetSpec spec { name, family, variation, energy, preset.tags,
                                    programs, variationIndex };
            const Harmony harmony = harmonyFor(spec);
            const ClipData bass = bassProgression(harmony, spec);
            const ClipData lead = leadProgression(harmony, spec);
            const ClipData pads = padProgression(harmony, spec);
            for (size_t s = 0; s < preset.session.scenes.size(); ++s) {
                auto& sceneData = preset.session.scenes[s];
                const ClipData drums = drumProgression(spec, (int)s);
                // Arrange density intentionally; the tonal parts keep one
                // progression whenever they enter, so every scene is one song.
                const std::array<const ClipData*, 4> picks =
                    s == 0 ? std::array<const ClipData*, 4> { &drums, nullptr, nullptr, &pads }
                    : s == 1 ? std::array<const ClipData*, 4> { &drums, &bass, nullptr, &pads }
                    : s == 4 ? std::array<const ClipData*, 4> { nullptr, &bass, &lead, &pads }
                    : s == 5 ? std::array<const ClipData*, 4> { &drums, nullptr, nullptr, &pads }
                    : std::array<const ClipData*, 4> { &drums, &bass, &lead, &pads };
                for (size_t t = 0; t < 4; ++t) {
                    sceneData.hasClip[t] = picks[t] != nullptr;
                    sceneData.clips[t] = picks[t] ? *picks[t] : ClipData {};
                }
            }
            return preset;
        };

        // Program order is DR-1, BL-1, WT-1 lead, WT-1 pad. The two WT slots
        // are voiced as complementary roles rather than interchangeable picks.
        auto library = std::vector<SessionPreset> {
            // NEON / SYNTHWAVE
            make("NEON TALE",    "NEON", "ORIGINAL", 3, { "bright", "balanced", "wide" }, { 13, 0, 3, 11 }, 0), // CRYSTAL PLUCK / FUTURE CHORD
            make("NEON CHASE",   "NEON", "CHASE",    5, { "bright", "driving", "wide" }, { 13, 2, 3, 40 }, 1),   // CRYSTAL PLUCK / PUMP PAD
            make("GLASS CIRCUIT", "NEON", "GLASS",   2, { "clean", "glassy", "sparse" }, { 12, 9, 45, 35 }, 2),  // FANTA BELLS / TWIN SKY
            make("AFTERGLOW",    "NEON", "SOFT",     2, { "warm", "soft", "wide" }, { 3, 5, 21, 24 }, 3),        // DYNO EPIANO / ANALOG STRINGS

            // ACID / WAREHOUSE
            make("WAREHOUSE RAW", "ACID", "RAW",     5, { "hard", "dark", "driving" }, { 13, 4, 33, 40 }, 0),
            make("ACID FLASH",    "ACID", "FLASH",   4, { "acid", "bright", "punchy" }, { 3, 0, 49, 1 }, 1),
            make("STEEL PULSE",   "ACID", "METAL",   4, { "metallic", "tight", "industrial" }, { 12, 2, 29, 17 }, 2),
            make("PEAK SIGNAL",   "ACID", "PEAK",    5, { "distorted", "wide", "peak-time" }, { 13, 5, 6, 40 }, 3),

            // AMBIENT / DEEP
            make("DEEP FOG",      "AMBIENT", "FOG",   1, { "dark", "deep", "slow" }, { 15, 12, 51, 34 }, 0),      // SOFT HORIZON · FOG LIGHT
            make("GLASS BLOOM",   "AMBIENT", "BLOOM", 2, { "glassy", "clean", "lush" }, { 15, 7, 52, 25 }, 1),      // TAPE BASS · GLASS RIBBON
            make("FROZEN BELL",   "AMBIENT", "FROZEN", 2, { "cold", "bell", "sparse" }, { 15, 11, 53, 32 }, 2),     // CLEAN SUB · NORTH WIRE
            make("AIR TEMPLE",    "AMBIENT", "TEMPLE", 2, { "warm", "ceremonial", "wide" }, { 15, 12, 54, 27 }, 3), // SOFT HORIZON · TEMPLE BREATH

            // HOUSE / CLUB
            make("DUST HOUSE",    "HOUSE", "DUST",    3, { "dusty", "groovy", "warm" }, { 12, 13, 36, 42 }, 0),   // HOUSE ORGAN
            make("MIDNIGHT FLOOR", "HOUSE", "NIGHT",  4, { "club", "round", "wide" }, { 13, 4, 21, 40 }, 1),      // WAREHOUSE
            make("TAPE DISCO",    "HOUSE", "TAPE",    3, { "tape", "soft", "groovy" }, { 3, 13, 28, 27 }, 2),     // HOUSE ORGAN
            make("CLEAN CLUB",    "HOUSE", "CLEAN",   4, { "clean", "tight", "bright" }, { 12, 5, 22, 42 }, 3),   // ROUNDHOUSE

            // LO-FI / RETRO
            make("VHS GARDEN",    "LO-FI", "VHS",     2, { "tape", "dark", "nostalgic" }, { 3, 14, 36, 34 }, 0),  // DUSTY FELT
            make("POCKET DUST",   "LO-FI", "POCKET",  2, { "dusty", "small", "warm" }, { 12, 7, 20, 27 }, 1),     // TAPE BASS
            make("TOY PARADE",    "LO-FI", "TOY",     4, { "8-bit", "playful", "broken" }, { 13, 14, 29, 42 }, 2), // DUSTY FELT
            make("WORN SIGNAL",   "LO-FI", "WORN",    3, { "distorted", "dark", "unstable" }, { 3, 4, 33, 17 }, 3), // WAREHOUSE

            // CINEMATIC / EXPERIMENTAL
            make("CHROME CATHEDRAL", "CINEMATIC", "CATHEDRAL", 3, { "large", "metallic", "ceremonial" }, { 13, 15, 58, 25 }, 0), // CINEMA SUB · CINEMA LEAD
            make("MACHINE TENSION",  "CINEMATIC", "TENSION",   4, { "industrial", "tense", "dark" }, { 12, 10, 35, 38 }, 1),     // DARK CURRENT · TWIN SKY
            make("VOID MARCH",       "CINEMATIC", "MARCH",     4, { "heavy", "dark", "driving" }, { 3, 15, 44, 25 }, 2),         // CINEMA SUB
            make("FINAL HORIZON",    "CINEMATIC", "FINALE",    5, { "epic", "wide", "bright" }, { 13, 8, 46, 38 }, 3),           // REESE MONO · TAURUS PEDAL

            // MINIMAL / TECHNO
            make("GRAY ROOM",   "MINIMAL", "ROOM",  4, { "hypnotic", "dry", "tight" }, { 9, 16, 59, 17 }, 0),     // SUB STAB · DEEP TICK / DARK DRONE
            make("CLICK FIELD", "MINIMAL", "CLICK", 4, { "clicky", "sparse", "precise" }, { 9, 21, 61, 17 }, 1),  // TECHNO SUB · CELLAR BLIP / DARK DRONE
            make("COLD ROTOR",  "MINIMAL", "ROTOR", 5, { "dark", "driving", "hypnotic" }, { 6, 21, 60, 35 }, 2),  // TECHNO SUB · ROOM KNOCK / TWIN SKY
            make("NIGHT GRID",  "MINIMAL", "GRID",  4, { "deep", "rolling", "late" }, { 9, 16, 59, 34 }, 3),      // SUB STAB · DEEP TICK / OCEAN AIR

            // FUTURE BASS
            make("SUGAR RUSH", "FUTURE BASS", "RUSH",   5, { "bright", "bouncy", "wide" }, { 0, 17, 4, 11 }, 0),   // 808 GLIDE · HYPER SAW / FUTURE CHORD
            make("PASTEL SKY", "FUTURE BASS", "PASTEL", 4, { "soft", "lush", "wide" }, { 2, 18, 15, 42 }, 1),      // GROWL WIDE · TRAP BELL / JUNO DREAM
            make("STARBURST",  "FUTURE BASS", "BURST",  5, { "euphoric", "punchy", "bright" }, { 0, 17, 45, 40 }, 2), // 808 GLIDE · FANTA BELLS / PUMP PAD
            make("HEART WIRE", "FUTURE BASS", "WIRE",   4, { "emotive", "glassy", "wide" }, { 2, 18, 52, 38 }, 3),  // GROWL WIDE · GLASS RIBBON / AURORA RISER

            // TRIP HOP
            make("VELVET SMOKE", "TRIP HOP", "SMOKE", 2, { "smoky", "dusty", "slow" }, { 16, 19, 20, 34 }, 0),     // UPRIGHT FELT · MELLOW RHODES / OCEAN AIR
            make("NIGHT BUS",    "TRIP HOP", "BUS",   2, { "nocturnal", "warm", "tape" }, { 16, 19, 36, 32 }, 1),  // UPRIGHT FELT · TAPE KEYS / GHOST CHOIR
            make("CRACKED LENS", "TRIP HOP", "LENS",  3, { "broken", "eerie", "dusty" }, { 8, 10, 29, 17 }, 2),    // DARK CURRENT · KALIMBA PLUCK / DARK DRONE
            make("STONE GARDEN", "TRIP HOP", "STONE", 3, { "organic", "moody", "deep" }, { 16, 14, 28, 1 }, 3),    // DUSTY FELT · NYLON PLUCK / VELVET PAD

            // DUB
            make("ECHO CHAMBER", "DUB", "ECHO",    2, { "spacious", "deep", "smoky" }, { 4, 20, 55, 56 }, 0),      // STEPPER ROOT · MELODICA / DUB SKANK
            make("KING STEPPER", "DUB", "STEPPER", 3, { "rootsy", "driving", "warm" }, { 4, 3, 55, 22 }, 1),       // DEEP DUB · MELODICA / DRAWBAR ORGAN
            make("ROOTS RADAR",  "DUB", "RADAR",   2, { "heavy", "hazy", "wide" }, { 4, 20, 22, 56 }, 2),          // STEPPER ROOT · DRAWBAR ORGAN / DUB SKANK
            make("ZION GATE",    "DUB", "GATE",    3, { "uplifting", "rootsy", "wide" }, { 4, 20, 55, 1 }, 3),     // STEPPER ROOT · MELODICA / VELVET PAD
        };
        return library;
    }();
    return presets;
}

} // namespace fable
