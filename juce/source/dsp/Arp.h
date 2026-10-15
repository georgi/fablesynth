#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>
#include "CompLibrary.gen.h"

namespace fable {
// Chord track (web src/chords.ts): chords on a 16th grid over the sequence
// length. Quality order and interval tables mirror CHORD_QUALITIES.
struct ArpChord { uint8_t start = 0, length = 0, root = 0, quality = 0, inversion = 0; };
struct ChordQualityDef { const char* key; const char* label; const char* name; int count; std::array<int, 6> iv; };
constexpr int kChordQualities = 28, kMaxArpChords = 64, kMaxArpCycle = 192;
// The longest valid cycle is 144 steps (rate 1/3 or 1/6 over 3 bars); comp
// `lens` are uint8_t, so the cap must stay below 256.
inline const std::array<ChordQualityDef, kChordQualities>& chordQualities() {
    static const std::array<ChordQualityDef, kChordQualities> q {{
        {"maj", "", "MAJOR", 3, {{0, 4, 7}}}, {"min", "m", "MINOR", 3, {{0, 3, 7}}},
        {"dim", "°", "DIMINISHED", 3, {{0, 3, 6}}}, {"aug", "+", "AUGMENTED", 3, {{0, 4, 8}}},
        {"sus2", "sus2", "SUS 2", 3, {{0, 2, 7}}}, {"sus4", "sus4", "SUS 4", 3, {{0, 5, 7}}},
        {"five", "5", "POWER", 3, {{0, 7, 12}}}, {"six", "6", "MAJOR 6", 4, {{0, 4, 7, 9}}},
        {"m6", "m6", "MINOR 6", 4, {{0, 3, 7, 9}}}, {"dom7", "7", "DOMINANT 7", 4, {{0, 4, 7, 10}}},
        {"maj7", "maj7", "MAJOR 7", 4, {{0, 4, 7, 11}}}, {"m7", "m7", "MINOR 7", 4, {{0, 3, 7, 10}}},
        {"mMaj7", "m(maj7)", "MINOR MAJ 7", 4, {{0, 3, 7, 11}}}, {"m7b5", "m7b5", "HALF-DIM 7", 4, {{0, 3, 6, 10}}},
        {"dim7", "°7", "DIMINISHED 7", 4, {{0, 3, 6, 9}}}, {"aug7", "+7", "AUGMENTED 7", 4, {{0, 4, 8, 10}}},
        {"sus7", "7sus4", "7 SUS 4", 4, {{0, 5, 7, 10}}}, {"add9", "add9", "ADD 9", 4, {{0, 4, 7, 14}}},
        {"madd9", "m(add9)", "MINOR ADD 9", 4, {{0, 3, 7, 14}}}, {"dom9", "9", "DOMINANT 9", 5, {{0, 4, 7, 10, 14}}},
        {"maj9", "maj9", "MAJOR 9", 5, {{0, 4, 7, 11, 14}}}, {"m9", "m9", "MINOR 9", 5, {{0, 3, 7, 10, 14}}},
        {"dom7b9", "7b9", "7 FLAT 9", 5, {{0, 4, 7, 10, 13}}}, {"dom7s9", "7#9", "7 SHARP 9", 5, {{0, 4, 7, 10, 15}}},
        {"m11", "m11", "MINOR 11", 6, {{0, 3, 7, 10, 14, 17}}}, {"dom11", "11", "DOMINANT 11", 6, {{0, 4, 7, 10, 14, 17}}},
        {"maj13", "maj13", "MAJOR 13", 6, {{0, 4, 7, 11, 14, 21}}}, {"dom13", "13", "DOMINANT 13", 6, {{0, 4, 7, 10, 14, 21}}},
    }};
    return q;
}
// MIDI notes of a chord voiced from `base` (the C of its octave), root first.
inline int chordNotes(const ArpChord& c, int base, std::array<int, 6>& out) {
    const auto& q = chordQualities()[std::min<int>(c.quality, kChordQualities - 1)];
    const int inv = std::clamp<int>(c.inversion, 0, q.count - 1);
    int n = 0;
    for (int i = 0; i < q.count; ++i) {
        const int k = (i + inv) % q.count;
        const int note = base + c.root + q.iv[(size_t)k] + (i + inv >= q.count ? 12 : 0);
        if (note >= 0 && note <= 127) out[(size_t)n++] = note;
    }
    return n;
}
// Inserts a chord, trimming or splitting the chords it overlaps; keeps start order.
inline void placeChord(std::array<ArpChord, kMaxArpChords>& list, int& count, ArpChord c) {
    std::array<ArpChord, kMaxArpChords> out {}; int n = 0;
    const int end = c.start + c.length;
    for (int i = 0; i < count; ++i) {
        const auto o = list[(size_t)i]; const int oEnd = o.start + o.length;
        if (oEnd <= c.start || o.start >= end) { if (n < kMaxArpChords) out[(size_t)n++] = o; continue; }
        if (o.start < c.start && n < kMaxArpChords) { auto a = o; a.length = (uint8_t)(c.start - o.start); out[(size_t)n++] = a; }
        if (oEnd > end && n < kMaxArpChords) { auto b = o; b.start = (uint8_t)end; b.length = (uint8_t)(oEnd - end); out[(size_t)n++] = b; }
    }
    if (n < kMaxArpChords) out[(size_t)n++] = c;
    std::stable_sort(out.begin(), out.begin() + n, [](const ArpChord& x, const ArpChord& y) { return x.start < y.start; });
    list = out; count = n;
}
// Progression presets (web PROGRESSIONS): {semitones above key, quality, beats}.
struct ProgressionStep { int degree, quality, beats; };
struct Progression { const char* name; const char* mood; int count; std::array<ProgressionStep, 8> steps; };
inline int chordQualityIndex(const char* key) {
    const auto& q = chordQualities();
    for (int i = 0; i < kChordQualities; ++i) if (std::string_view(q[(size_t)i].key) == key) return i;
    return 0;
}
inline const std::vector<Progression>& progressions() {
    static const std::vector<Progression> list = [] {
        auto q = [](const char* k) { return chordQualityIndex(k); };
        struct Raw { const char* name; const char* mood; std::vector<ProgressionStep> steps; };
        const std::vector<Raw> raw {
        {"I–V–vi–IV", "POP", {{0, q("maj"), 4}, {7, q("maj"), 4}, {9, q("min"), 4}, {5, q("maj"), 4}}},
        {"vi–IV–I–V", "POP", {{9, q("min"), 4}, {5, q("maj"), 4}, {0, q("maj"), 4}, {7, q("maj"), 4}}},
        {"I–vi–IV–V", "DOO-WOP", {{0, q("maj"), 4}, {9, q("min"), 4}, {5, q("maj"), 4}, {7, q("maj"), 4}}},
        {"I–IV–vi–V", "POP", {{0, q("maj"), 4}, {5, q("maj"), 4}, {9, q("min"), 4}, {7, q("maj"), 4}}},
        {"I–iii–IV–V", "BALLAD", {{0, q("maj"), 4}, {4, q("min"), 4}, {5, q("maj"), 4}, {7, q("maj"), 4}}},
        {"Canon", "CLASSICAL", {{0, q("maj"), 2}, {7, q("maj"), 2}, {9, q("min"), 2}, {4, q("min"), 2}, {5, q("maj"), 2}, {0, q("maj"), 2}, {5, q("maj"), 2}, {7, q("maj"), 2}}},
        {"IV–V–iii–vi", "ROYAL ROAD", {{5, q("maj7"), 4}, {7, q("dom7"), 4}, {4, q("m7"), 4}, {9, q("m7"), 4}}},
        {"I–♭VII–IV–I", "MIXOLYDIAN", {{0, q("maj"), 4}, {10, q("maj"), 4}, {5, q("maj"), 4}, {0, q("maj"), 4}}},
        {"I–III–IV–iv", "BITTERSWEET", {{0, q("maj"), 4}, {4, q("maj"), 4}, {5, q("maj"), 4}, {5, q("min"), 4}}},
        {"I–IV", "VAMP", {{0, q("maj"), 8}, {5, q("maj"), 8}}},
        {"i–VI–III–VII", "EPIC MINOR", {{0, q("min"), 4}, {8, q("maj"), 4}, {3, q("maj"), 4}, {10, q("maj"), 4}}},
        {"i–VII–VI–V", "ANDALUSIAN", {{0, q("min"), 4}, {10, q("maj"), 4}, {8, q("maj"), 4}, {7, q("maj"), 4}}},
        {"i–iv–v–i", "NATURAL MINOR", {{0, q("min"), 4}, {5, q("min"), 4}, {7, q("min"), 4}, {0, q("min"), 4}}},
        {"i–iv–V–i", "HARMONIC MINOR", {{0, q("min"), 4}, {5, q("min"), 4}, {7, q("dom7"), 4}, {0, q("min"), 4}}},
        {"i–VI–iv–V", "DRAMATIC", {{0, q("min"), 4}, {8, q("maj"), 4}, {5, q("min"), 4}, {7, q("maj"), 4}}},
        {"i–VII–VI–VII", "TRANCE", {{0, q("min"), 4}, {10, q("maj"), 4}, {8, q("maj"), 4}, {10, q("maj"), 4}}},
        {"i–♭II", "PHRYGIAN", {{0, q("min"), 8}, {1, q("maj"), 8}}},
        {"i7–IV7", "DORIAN", {{0, q("m7"), 8}, {5, q("dom7"), 8}}},
        {"i7–iv7", "DUB VAMP", {{0, q("m7"), 8}, {5, q("m7"), 8}}},
        {"i9–VImaj7", "DEEP HOUSE", {{0, q("m9"), 8}, {8, q("maj7"), 8}}},
        {"ii–V–I", "JAZZ", {{2, q("m7"), 4}, {7, q("dom7"), 4}, {0, q("maj7"), 8}}},
        {"I–vi–ii–V", "TURNAROUND", {{0, q("maj7"), 4}, {9, q("m7"), 4}, {2, q("m7"), 4}, {7, q("dom7"), 4}}},
        {"vi–ii–V–I", "CIRCLE", {{9, q("m7"), 4}, {2, q("m7"), 4}, {7, q("dom7"), 4}, {0, q("maj7"), 4}}},
        {"ii9–V13–I9–vi9", "NEO SOUL", {{2, q("m9"), 4}, {7, q("dom13"), 4}, {0, q("maj9"), 4}, {9, q("m9"), 4}}},
        {"I7–IV7–I7–V7", "BLUES", {{0, q("dom7"), 4}, {5, q("dom7"), 4}, {0, q("dom7"), 4}, {7, q("dom7"), 4}}},
        {"iiø–V7♭9–i", "MINOR JAZZ", {{2, q("m7b5"), 4}, {7, q("dom7b9"), 4}, {0, q("m9"), 8}}},
        };
        std::vector<Progression> out;
        for (const auto& r : raw) {
            Progression p {r.name, r.mood, (int)r.steps.size(), {}};
            for (size_t i = 0; i < r.steps.size() && i < 8; ++i) p.steps[i] = r.steps[i];
            out.push_back(p);
        }
        return out;
    }();
    return list;
}
// A progression's chords in `key`; returns the bar count it needs (1–4).
inline int progressionChords(const Progression& p, int key, std::array<ArpChord, kMaxArpChords>& list, int& count) {
    int start = 0; count = 0;
    for (int i = 0; i < p.count; ++i) {
        const auto& s = p.steps[(size_t)i];
        list[(size_t)count++] = {(uint8_t)start, (uint8_t)(s.beats * 4), (uint8_t)((key + s.degree) % 12), (uint8_t)s.quality, 0};
        start += s.beats * 4;
    }
    return std::clamp((start + 15) / 16, 1, 4);
}
// Browser groups for the progression library, by mood (web PROGRESSION_FAMILIES).
struct ProgressionFamily { const char* name; std::array<const char*, 6> moods; };
inline const std::array<ProgressionFamily, 5>& progressionFamilies() {
    static const std::array<ProgressionFamily, 5> f {{
        {"MAJOR", {"POP", "DOO-WOP", "BALLAD", "CLASSICAL", "ROYAL ROAD", "BITTERSWEET"}},
        {"MINOR", {"EPIC MINOR", "ANDALUSIAN", "NATURAL MINOR", "HARMONIC MINOR", "DRAMATIC", nullptr}},
        {"MODAL", {"MIXOLYDIAN", "PHRYGIAN", "DORIAN", nullptr, nullptr, nullptr}},
        {"CLUB", {"VAMP", "TRANCE", "DUB VAMP", "DEEP HOUSE", nullptr, nullptr}},
        {"JAZZ & SOUL", {"JAZZ", "TURNAROUND", "CIRCLE", "NEO SOUL", "BLUES", "MINOR JAZZ"}},
    }};
    return f;
}
inline int progressionFamily(const Progression& p) {
    const auto& f = progressionFamilies();
    for (int i = 0; i < (int)f.size(); ++i)
        for (const char* m : f[(size_t)i].moods) if (m && std::string_view(m) == p.mood) return i;
    return -1;
}
// The library progression a chord track holds in its key, or -1. Inversions do not count.
inline int progressionIndex(const std::array<ArpChord, kMaxArpChords>& chords, int count, int key) {
    const auto& list = progressions();
    for (int i = 0; i < (int)list.size(); ++i) {
        std::array<ArpChord, kMaxArpChords> want {}; int n = 0;
        progressionChords(list[(size_t)i], key, want, n);
        bool same = n == count;
        for (int k = 0; same && k < n; ++k) {
            const auto &a = want[(size_t)k], &b = chords[(size_t)k];
            same = a.start == b.start && a.length == b.length && a.root == b.root && a.quality == b.quality;
        }
        if (same) return i;
    }
    return -1;
}
// Chord comping (web src/comp.ts). Each step strikes a voicing of the current
// chord: LOW is its lowest voice, TOP its highest, MID every voice between,
// BASS the root an octave below. A tied step holds the previous voicing; a
// chord change re-strikes it.
constexpr int kCompBass = 1, kCompLow = 2, kCompMid = 4, kCompTop = 8, kMaxCompNotes = 8;
// Steps: '.' rest, '-' tie, else voices B L M T (C = L+M+T, U = M+T); '>' accents.
struct CompPattern { std::array<uint8_t, 16> masks {}; std::array<bool, 16> ties {}, accents {}; };
inline CompPattern compPattern(const char* steps) {
    CompPattern out; int step = 0;
    for (const char* c = steps; *c && step < 16; ++step) {
        uint8_t m = 0;
        for (; *c && *c != ' '; ++c) switch (*c) {
            case 'B': m |= kCompBass; break; case 'L': m |= kCompLow; break; case 'M': m |= kCompMid; break;
            case 'T': m |= kCompTop; break; case 'C': m |= kCompLow | kCompMid | kCompTop; break;
            case 'U': m |= kCompMid | kCompTop; break; case '-': out.ties[(size_t)step] = true; break;
            case '>': out.accents[(size_t)step] = true; break; default: break;
        }
        out.masks[(size_t)step] = m;
        while (*c == ' ') ++c;
    }
    return out;
}
// The library entry a pattern matches (voices, ties, accents, rate); -1 for a custom pattern.
inline int compLibraryIndex(const std::array<uint8_t, 16>& masks, const std::array<bool, 16>& ties, const std::array<bool, 16>& accents, double rate) {
    const auto& lib = compLibrary();
    for (int i = 0; i < (int)lib.size(); ++i) {
        const auto& e = lib[(size_t)i];
        if (e.rate != rate) continue;
        const auto q = compPattern(e.steps);
        bool same = true;
        for (int s = 0; s < 16 && same; ++s)
            same = q.ties[(size_t)s] == ties[(size_t)s] && (ties[(size_t)s] || (q.masks[(size_t)s] == masks[(size_t)s] && q.accents[(size_t)s] == (accents[(size_t)s] && masks[(size_t)s] != 0)));
        if (same) return i;
    }
    return -1;
}
// Voicing spread (web spreadVoicing): 0 close, 1 drop 2, 2 drop 2+4, 3 open.
inline int spreadVoicing(int* v, int n, int kind) {
    if (n >= 3 && (kind == 1 || kind == 2)) v[n - 2] -= 12;
    if (n >= 4 && kind == 2) v[n - 4] -= 12;
    if (n >= 3 && kind == 3) for (int i = 1; i < n; i += 2) v[i] += 12;
    int w = 0;
    for (int i = 0; i < n; ++i) if (v[i] >= 0 && v[i] <= 127) v[w++] = v[i];
    std::sort(v, v + w);
    return w;
}
// Strum direction (web strumDown): 0 up, 1 down, 2 alternate like a guitarist's
// arm, on 8ths, or on 16ths when the pattern plays odd 16ths.
inline bool strumDown(int dir, int r, bool sixteenths) {
    if (dir != 2) return dir == 1;
    return sixteenths ? r % 2 == 0 : r % 4 < 2;
}
constexpr int kStrumMaxMs = 60;
// Notes a voice mask selects from an ascending voicing; ascending, unique, capped.
inline int compVoices(int mask, const int* voicing, int n, int bass, std::array<uint8_t, kMaxCompNotes>& out) {
    if (n <= 0) return 0;
    std::array<int, 136> picked {}; int count = 0;
    if ((mask & kCompBass) && bass >= 0) picked[(size_t)count++] = bass;
    if (mask & kCompLow) picked[(size_t)count++] = voicing[0];
    if (mask & kCompMid) for (int i = 1; i < n - 1; ++i) picked[(size_t)count++] = voicing[i];
    if (mask & kCompTop) picked[(size_t)count++] = voicing[n - 1];
    std::sort(picked.begin(), picked.begin() + count);
    count = (int)(std::unique(picked.begin(), picked.begin() + count) - picked.begin());
    count = std::min(count, kMaxCompNotes);
    for (int i = 0; i < count; ++i) out[(size_t)i] = (uint8_t)picked[(size_t)i];
    return count;
}
// Value-owned settings and compiled pattern: copying never allocates, including
// clip launches and audio-thread key capture. Notes retain insertion order.
struct ArpSettings {
    bool enabled = false, keys = false, latch = false;
    int order = 0, octaves = 1, count = 4;
    uint32_t seed = 1;
    double rate = .25, gate = .65;
    std::array<int, 128> notes {{50, 53, 57, 60}};
    std::array<bool, 16> hits, accents {}, slides {};
    // Chord-track input: `chords` replaces the stored/played note pool.
    bool chords = false;
    int chordKey = 9, chordOctave = 3, chordCount = 4;
    // SQ-4 clips: chord-track length in bars, and whether the scene's linked
    // clips share the progression (web store linkChordWrites).
    int chordBars = 4;
    bool chordLink = false;
    // Scale mode for written notes (web src/scales.ts, Scales.h): 0 OFF, 1 KEY
    // (one scale on chordKey), 2 CHORDS (a scale per chord), 3 FOLLOW (notes
    // keep their place against followList). scaleType -1 is AUTO, else an
    // index into scaleDefs().
    int scaleMode = 0, scaleType = -1;
    std::array<ArpChord, kMaxArpChords> chordList {{{0, 16, 9, 1, 0}, {16, 16, 5, 0, 0}, {32, 16, 0, 0, 0}, {48, 16, 7, 0, 0}}};
    // FOLLOW: the chords the written notes were written over (web
    // ArpState.followChords); followCount -1 = absent (FOLLOW uses chordList).
    int followCount = -1;
    std::array<ArpChord, kMaxArpChords> followList {};
    // Comp style: `compMask` voices and `ties` replace the single arp note.
    bool comp = false;
    std::array<uint8_t, 16> compMask {};
    std::array<bool, 16> ties {};
    // Comp feel: ms between strummed notes, stroke (0 up, 1 down, 2 alt), voicing spread.
    int strum = 0, strumDir = 0, voicing = 0;
    ArpSettings() { hits.fill(true); for (int i = 0; i < 16; i += 4) accents[i] = true; compMask = compPattern("BC . . . C . . . C . . . C . . .").masks; }
};
// `notes` holds 16 steps, or one chord-track cycle (`length`, a multiple of
// 16). The rhythm lanes repeat every 16 steps.
struct ArpPattern {
    bool enabled = false;
    double rate = .25, gate = .65;
    int length = 16;
    std::array<int, kMaxArpCycle> notes {};
    std::array<bool, 16> hits {}, accents {}, slides {};
    // Comp: each step's voicing and its length in steps (0 = held by a tie).
    // `notes` then holds each voicing's lowest note.
    bool comp = false;
    int strum = 0; // ms between strummed notes; each stack is in strum order
    std::array<std::array<uint8_t, kMaxCompNotes>, kMaxArpCycle> stack {};
    std::array<uint8_t, kMaxArpCycle> stackCount {}, lens {};
};
inline bool validArpSettings(const ArpSettings& a, bool hosted = false) {
    if (a.count < 0 || a.count > 128 || a.order < 0 || a.order > 4 || a.octaves < 1 || a.octaves > 3
        || !std::isfinite(a.gate) || a.gate < .05 || a.gate > .95 || (hosted && (a.keys || a.latch))
        || a.chordBars < 1 || a.chordBars > 4 || a.chordCount < 0 || a.chordCount > kMaxArpChords || a.chordKey < 0 || a.chordKey > 11
        || a.chordOctave < 0 || a.chordOctave > 6 || a.scaleMode < 0 || a.scaleMode > 3 || a.scaleType < -1 || a.scaleType > 18
        || a.followCount < -1 || a.followCount > kMaxArpChords) return false;
    for (auto m : a.compMask) if (m > 15) return false;
    if (a.strum < 0 || a.strum > kStrumMaxMs || a.strumDir < 0 || a.strumDir > 2 || a.voicing < 0 || a.voicing > 3) return false;
    const std::array<double, 8> rates {{.125, .25, .5, 1, 1.0/3, 1.0/6, .375, .75}};
    if (std::find(rates.begin(), rates.end(), a.rate) == rates.end()) return false;
    for (int i = 0; i < a.count; ++i) if (a.notes[i] < 0 || a.notes[i] > 127) return false;
    return true;
}
inline bool arpHasNotes(const ArpPattern& a) {
    for (int i = 0; i < a.length; ++i) if ((a.comp || a.hits[(size_t)(i % 16)]) && a.notes[(size_t)i] >= 0) return true;
    return false;
}
using ArpPool = std::array<int, 768>;
inline int arpPool(const ArpSettings& a, const int* input, int inputCount, ArpPool& pool) {
    int count = 0;
    for (int i = 0; i < std::clamp(inputCount, 0, 128); ++i) {
        const int n = input[i];
        if (n >= 0 && n <= 127 && std::find(pool.begin(), pool.begin() + count, n) == pool.begin() + count)
            pool[(size_t)count++] = n;
    }
    if (a.order != 3) std::sort(pool.begin(), pool.begin() + count);
    const int base = count;
    for (int oct = 1; oct < std::clamp(a.octaves, 1, 3); ++oct)
        for (int i = 0; i < base; ++i) if (pool[(size_t)i] + oct * 12 <= 127) pool[(size_t)count++] = pool[(size_t)i] + oct * 12;
    if (a.order == 1) std::reverse(pool.begin(), pool.begin() + count);
    if (a.order == 2 && count > 2) for (int i = count - 2; i >= 1; --i) pool[(size_t)count++] = pool[(size_t)i];
    return count;
}
// Arp steps until the rate grid and a `bars`-long chord track realign (web arpCycle).
inline int arpCycle(double rate, int bars) {
    const double total = std::clamp(bars, 1, 4) * 16.0, r16 = rate * 4;
    int len = 16;
    while (len < kMaxArpCycle) {
        const double x = len * r16 / total;
        if (std::fabs(x - std::round(x)) <= 1e-6) break;
        len += 16;
    }
    return len;
}
// Sixteenth position of arp step `i` inside a `bars`-long chord track.
inline double arpStepPos(double rate, int bars, int i) { return std::fmod(i * rate * 4, std::clamp(bars, 1, 4) * 16.0); }
// Comp loop (web compSequence): 16 steps, or a full chord-track cycle.
inline void compileComp(const ArpSettings& a, int bars, ArpPattern& p) {
    p.comp = true; p.strum = std::clamp(a.strum, 0, kStrumMaxMs);
    bool sixteenths = false;
    for (int r = 1; r < 16; r += 2) sixteenths = sixteenths || (a.compMask[(size_t)r] && !a.ties[(size_t)r]);
    bars = std::clamp(bars, 1, 4);
    const int total = bars * 16, base = (a.chordOctave + 1) * 12;
    p.length = a.chords ? arpCycle(p.rate, bars) : 16;
    std::array<int, 128> stored {}; int storedCount = 0;
    for (int i = 0; i < std::clamp(a.count, 0, 128); ++i)
        if (const int n = a.notes[(size_t)i]; n >= 0 && n <= 127 && std::find(stored.begin(), stored.begin() + storedCount, n) == stored.begin() + storedCount)
            stored[(size_t)storedCount++] = n;
    std::sort(stored.begin(), stored.begin() + storedCount);
    const int storedBass = storedCount ? stored[0] - 12 : -1;
    storedCount = spreadVoicing(stored.data(), storedCount, a.voicing);
    // -1 rests (a chord gap or no notes); -2 is the stored / played pool.
    std::array<int, kMaxArpCycle> chordAt {};
    for (int i = 0; i < p.length; ++i) {
        if (!a.chords) { chordAt[(size_t)i] = storedCount ? -2 : -1; continue; }
        const double pos = arpStepPos(p.rate, bars, i) + 1e-6; int idx = -1;
        for (int c = 0; c < std::clamp(a.chordCount, 0, kMaxArpChords); ++c) {
            const auto& ch = a.chordList[(size_t)c];
            if (pos >= ch.start && pos < std::min(total, ch.start + ch.length)) { idx = c; break; }
        }
        chordAt[(size_t)i] = idx;
    }
    auto run = [&](int held, bool emit) {
        for (int i = 0; i < p.length; ++i) {
            const int r = i % 16, c = chordAt[(size_t)i];
            const bool changed = c != chordAt[(size_t)((i + p.length - 1) % p.length)];
            if (emit) { p.stackCount[(size_t)i] = 0; p.lens[(size_t)i] = 1; }
            if (c == -1) { held = 0; continue; }
            const int mask = a.ties[(size_t)r] ? held : a.compMask[(size_t)r];
            if (a.ties[(size_t)r] && held && !changed) { if (emit) p.lens[(size_t)i] = 0; continue; }
            held = mask;
            if (!mask || !emit) continue;
            auto& out = p.stack[(size_t)i];
            int n = 0;
            if (c == -2) n = compVoices(mask, stored.data(), storedCount, storedBass, out);
            else {
                const auto& ch = a.chordList[(size_t)c]; std::array<int, 6> v {};
                const int count = spreadVoicing(v.data(), chordNotes(ch, base, v), a.voicing);
                n = compVoices(mask, v.data(), count, base - 12 + ch.root, out);
            }
            if (strumDown(a.strumDir, r, sixteenths)) std::reverse(out.begin(), out.begin() + n);
            p.stackCount[(size_t)i] = (uint8_t)n;
        }
        return held;
    };
    // A tie on the loop's first step holds whatever the loop ends on.
    run(run(0, false), true);
    for (int i = 0; i < p.length; ++i) {
        if (!p.stackCount[(size_t)i]) continue;
        int n = 1;
        while (n < p.length && p.lens[(size_t)((i + n) % p.length)] == 0) ++n;
        p.lens[(size_t)i] = (uint8_t)n;
        const auto& st = p.stack[(size_t)i];
        p.notes[(size_t)i] = *std::min_element(st.begin(), st.begin() + p.stackCount[(size_t)i]);
    }
}
// `bars` is the device sequence length; it only matters for chord-track input.
inline ArpPattern compileArp(const ArpSettings& a, int bars = 1) {
    ArpPattern p; p.enabled = a.enabled;
    p.rate = std::isfinite(a.rate) ? std::clamp(a.rate, .125, 1.0) : .25;
    p.gate = std::isfinite(a.gate) ? std::clamp(a.gate, .05, .95) : .65;
    p.hits = a.hits; p.accents = a.accents; p.slides = a.slides;
    p.notes.fill(-1);
    if (a.comp) { compileComp(a, bars, p); return p; }
    ArpPool pool {};
    uint32_t seed = a.seed;
    if (!a.chords) {
        const int count = arpPool(a, a.notes.data(), a.count, pool);
        for (int i = 0; i < 16; ++i) {
            seed = seed * 1664525u + 1013904223u;
            p.notes[(size_t)i] = count ? pool[(a.order == 4 ? seed >> 8 : (uint32_t)i) % (uint32_t)count] : -1;
        }
        return p;
    }
    // Chord track: the pattern restarts on every chord change; gaps rest.
    bars = std::clamp(bars, 1, 4);
    const int total = bars * 16, base = (a.chordOctave + 1) * 12;
    p.length = arpCycle(p.rate, bars);
    int prev = -2, j = 0, count = 0;
    for (int i = 0; i < p.length; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const double pos = arpStepPos(p.rate, bars, i) + 1e-6;
        int idx = -1;
        for (int c = 0; c < std::clamp(a.chordCount, 0, kMaxArpChords); ++c) {
            const auto& ch = a.chordList[(size_t)c];
            if (pos >= ch.start && pos < std::min(total, ch.start + ch.length)) { idx = c; break; }
        }
        if (idx != prev) {
            j = 0; prev = idx;
            if (idx >= 0) { std::array<int, 6> v {}; const int n = chordNotes(a.chordList[(size_t)idx], base, v); count = arpPool(a, v.data(), n, pool); }
        }
        if (idx < 0) continue;
        p.notes[(size_t)i] = count ? pool[(a.order == 4 ? seed >> 8 : (uint32_t)j) % (uint32_t)count] : -1;
        ++j;
    }
    return p;
}
// Loads a library entry: rhythm, rate and playing feel (web loadCompEntry).
// An SQ-4 clip: a chord track loops one chord cycle; a note list 16 steps.
inline ArpPattern compileClipArp(const ArpSettings& a) { return compileArp(a, a.chords ? a.chordBars : 1); }
inline void applyCompEntry(ArpSettings& a, const CompLibraryEntry& e) {
    const auto q = compPattern(e.steps);
    a.comp = true; a.compMask = q.masks; a.ties = q.ties; a.accents = q.accents;
    a.rate = e.rate; a.gate = e.gate; a.strum = e.strum; a.strumDir = e.strumDir; a.voicing = e.voicing;
}
inline int compLibraryIndex(const ArpSettings& a) { return compLibraryIndex(a.compMask, a.ties, a.accents, a.rate); }
inline ArpSettings bassArpDefaults() {
    ArpSettings a; a.notes = {{36, 39, 43, 46}}; a.chordOctave = 2; return a;
}
// Audio-owned key pool, independent of stored notes. Latch replaces the pool
// on the first key of a new gesture; releasing keys does not erase a latch.
class ArpInput {
public:
    ArpSettings settings;
    void set(const ArpSettings& a) {
        if (a.enabled != settings.enabled) clear();
        else if (a.keys != settings.keys || (settings.latch && !a.latch)) {
            int retained = 0;
            for (int i = 0; i < count; ++i) if (held[notes[i]]) notes[retained++] = notes[i];
            count = retained;
        }
        settings = a;
    }
    void clear() { held.fill(false); count = 0; }
    bool key(int n, bool on) {
        if (!settings.enabled || n < 0 || n > 127) return false;
        if (on) {
            if (settings.keys && settings.latch && std::none_of(held.begin(), held.end(), [](bool b) { return b; })) count = 0;
            held[n] = true;
            if (std::find(notes.begin(), notes.begin() + count, n) == notes.begin() + count && count < 128) notes[count++] = n;
        } else {
            held[n] = false;
            if (!settings.keys || !settings.latch) {
                auto end = std::remove(notes.begin(), notes.begin() + count, n);
                count = (int)(end - notes.begin());
            }
        }
        return settings.keys;
    }
    ArpPattern pattern(int bars = 1) const {
        return compileArp(snapshot(), bars);
    }
    ArpSettings snapshot() const { auto a = settings; if (a.keys) { a.notes = notes; a.count = count; } return a; }
private:
    std::array<bool, 128> held {};
    std::array<int, 128> notes {};
    int count = 0;
};
// Single message-thread writer, bounded audio reader. Every payload word is
// atomic, so a concurrent edit is skipped safely until the following block.
class ArpMailbox {
public:
    void publish(const ArpSettings& a) {
        version.fetch_add(1, std::memory_order_seq_cst);
        int i = 0;
        put(i, a.enabled); put(i, a.keys); put(i, a.latch); put(i, a.order);
        put(i, a.octaves); put(i, a.count); put(i, a.seed);
        for (auto n : a.notes) put(i, n);
        for (auto b : a.hits) put(i, b);
        for (auto b : a.accents) put(i, b);
        for (auto b : a.slides) put(i, b);
        put(i, a.chords); put(i, (uint32_t)a.chordKey); put(i, (uint32_t)a.chordOctave); put(i, (uint32_t)a.chordCount);
        for (const auto& c : a.chordList)
            put(i, (uint32_t)c.start | (uint32_t)c.length << 8 | (uint32_t)c.root << 16 | (uint32_t)c.quality << 20 | (uint32_t)c.inversion << 26);
        put(i, a.comp);
        for (int s = 0; s < 16; ++s) put(i, (uint32_t)a.compMask[(size_t)s] | (uint32_t)a.ties[(size_t)s] << 4);
        put(i, (uint32_t)a.strum | (uint32_t)a.strumDir << 8 | (uint32_t)a.voicing << 10);
        rate.store(a.rate); gate.store(a.gate);
        version.fetch_add(1, std::memory_order_seq_cst);
    }
    bool consume(ArpSettings& a, uint32_t& seen) const {
        const auto v = version.load(); if ((v & 1) || v == seen) return false;
        ArpSettings next; int i = 0;
        next.enabled = get(i); next.keys = get(i); next.latch = get(i); next.order = (int)get(i);
        next.octaves = (int)get(i); next.count = (int)get(i); next.seed = get(i);
        for (auto& n : next.notes) n = (int)get(i);
        for (auto& b : next.hits) b = get(i);
        for (auto& b : next.accents) b = get(i);
        for (auto& b : next.slides) b = get(i);
        next.chords = get(i); next.chordKey = (int)get(i); next.chordOctave = (int)get(i); next.chordCount = (int)get(i);
        for (auto& c : next.chordList) {
            const uint32_t w = get(i);
            c = {(uint8_t)(w & 255), (uint8_t)(w >> 8 & 255), (uint8_t)(w >> 16 & 15), (uint8_t)(w >> 20 & 63), (uint8_t)(w >> 26 & 7)};
        }
        next.comp = get(i);
        for (int s = 0; s < 16; ++s) { const uint32_t w = get(i); next.compMask[(size_t)s] = (uint8_t)(w & 15); next.ties[(size_t)s] = (w >> 4) & 1; }
        { const uint32_t w = get(i); next.strum = (int)(w & 255); next.strumDir = (int)(w >> 8 & 3); next.voicing = (int)(w >> 10 & 3); }
        next.rate = rate.load(); next.gate = gate.load();
        if (version.load() != v) return false;
        a = next; seen = v; return true;
    }
private:
    void put(int& i, uint32_t n) { words[(size_t)i++].store(n); }
    uint32_t get(int& i) const { return words[(size_t)i++].load(); }
    std::array<std::atomic<uint32_t>, 183 + 4 + kMaxArpChords + 18> words {};
    std::atomic<double> rate {.25}, gate {.65};
    std::atomic<uint32_t> version {0};
};
}
