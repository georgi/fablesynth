// Scale mode for the note sequencers (web src/scales.ts). Written notes snap
// to a scale at playback: KEY uses one static scale on the chord-track key;
// CHORDS follows the chord track and gives each chord the scale that fits it
// best in the progression's key. The compiled map holds one remap table (a
// semitone offset per pitch class) per sixteenth; engines add it at note-on.
// Header-only, JUCE-free.
#pragma once

#include "Arp.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace fable {

struct ScaleDef { const char* id; const char* name; int count; std::array<int, 8> iv; };
constexpr int kScaleCount = 19;
// Order mirrors web SCALE_IDS.
inline const std::array<ScaleDef, kScaleCount>& scaleDefs() {
    static const std::array<ScaleDef, kScaleCount> s {{
        {"major", "MAJOR", 7, {{0, 2, 4, 5, 7, 9, 11}}},
        {"minor", "MINOR", 7, {{0, 2, 3, 5, 7, 8, 10}}},
        {"dorian", "DORIAN", 7, {{0, 2, 3, 5, 7, 9, 10}}},
        {"mixolydian", "MIXOLYDIAN", 7, {{0, 2, 4, 5, 7, 9, 10}}},
        {"phrygian", "PHRYGIAN", 7, {{0, 1, 3, 5, 7, 8, 10}}},
        {"lydian", "LYDIAN", 7, {{0, 2, 4, 6, 7, 9, 11}}},
        {"locrian", "LOCRIAN", 7, {{0, 1, 3, 5, 6, 8, 10}}},
        {"harmMinor", "HARMONIC MINOR", 7, {{0, 2, 3, 5, 7, 8, 11}}},
        {"melMinor", "MELODIC MINOR", 7, {{0, 2, 3, 5, 7, 9, 11}}},
        {"phrygDom", "PHRYGIAN DOM", 7, {{0, 1, 4, 5, 7, 8, 10}}},
        {"lydDom", "LYDIAN DOM", 7, {{0, 2, 4, 6, 7, 9, 10}}},
        {"locrian2", "LOCRIAN \xe2\x99\xae" "2", 7, {{0, 2, 3, 5, 6, 8, 10}}},
        {"altered", "ALTERED", 7, {{0, 1, 3, 4, 6, 8, 10}}},
        {"wholeTone", "WHOLE TONE", 6, {{0, 2, 4, 6, 8, 10}}},
        {"dimHW", "DIM HALF-WHOLE", 8, {{0, 1, 3, 4, 6, 7, 9, 10}}},
        {"dimWH", "DIM WHOLE-HALF", 8, {{0, 2, 3, 5, 6, 8, 9, 11}}},
        {"majPent", "MAJOR PENT", 5, {{0, 2, 4, 7, 9}}},
        {"minPent", "MINOR PENT", 5, {{0, 3, 5, 7, 10}}},
        {"blues", "BLUES", 6, {{0, 3, 5, 6, 7, 10}}},
    }};
    return s;
}
enum ScaleIdx { kScMajor, kScMinor, kScDorian, kScMixolydian, kScPhrygian, kScLydian, kScLocrian, kScHarmMinor,
    kScMelMinor, kScPhrygDom, kScLydDom, kScLocrian2, kScAltered, kScWholeTone, kScDimHW, kScDimWH };
inline int scaleIndex(const char* id) {
    for (int i = 0; i < kScaleCount; ++i) if (std::string_view(scaleDefs()[(size_t)i].id) == id) return i;
    return -1;
}

inline int scaleMask(int root, int id) {
    const auto& d = scaleDefs()[(size_t)std::clamp(id, 0, kScaleCount - 1)];
    int m = 0;
    for (int i = 0; i < d.count; ++i) m |= 1 << ((root + d.iv[(size_t)i]) % 12);
    return m;
}
inline int chordMask(const ArpChord& c) {
    const auto& q = chordQualities()[(size_t)std::min<int>(c.quality, kChordQualities - 1)];
    int m = 0;
    for (int i = 0; i < q.count; ++i) m |= 1 << ((c.root + q.iv[(size_t)i]) % 12);
    return m;
}
inline int scaleBits(int m) { int n = 0; for (int b = m & 0xfff; b; b &= b - 1) ++n; return n; }

// The key scale on `key` that holds the most chord-track sixteenths' worth of
// chord tones; ties keep the earlier candidate (web KEY_CANDIDATES order).
inline int keyScale(const ArpChord* chords, int count, int key) {
    static const int cand[] {kScMinor, kScMajor, kScDorian, kScMixolydian, kScPhrygian, kScLydian, kScHarmMinor};
    int best = cand[0]; long bestMiss = -1;
    for (int id : cand) {
        const int k = scaleMask(key, id);
        long miss = 0;
        for (int i = 0; i < count; ++i) miss += (long)chords[i].length * scaleBits(chordMask(chords[i]) & ~k);
        if (bestMiss < 0 || miss < bestMiss) { best = id; bestMiss = miss; }
    }
    return best;
}
// The scale on the chord's root that holds all its tones, stays closest to
// the key scale, and has the fewest avoid notes (a semitone above a chord tone).
inline int chordScale(const ArpChord& c, int keyMask) {
    static const int cand[] {kScMajor, kScMinor, kScDorian, kScMixolydian, kScPhrygian, kScLydian, kScLocrian,
        kScHarmMinor, kScMelMinor, kScPhrygDom, kScLydDom, kScLocrian2, kScAltered, kScWholeTone, kScDimHW, kScDimWH};
    const int t = chordMask(c);
    const int above = ((t << 1) | (t >> 11)) & 0xfff & ~t;
    int best = cand[0], bestScore = 0; bool first = true;
    for (int id : cand) {
        const int s = scaleMask(c.root, id);
        const int score = scaleBits(s & t) * 100 + 4 * (scaleBits(s & keyMask) - scaleBits(s & ~keyMask & 0xfff)) - 3 * scaleBits(s & above);
        if (first || score > bestScore) { best = id; bestScore = score; first = false; }
    }
    return best;
}
// Offsets that move each pitch class to the nearest one in `mask`. A tie
// prefers a pitch class in `prefer` (the chord tones), else the lower one.
inline std::array<int8_t, 12> scaleRemap(int mask, int prefer = 0) {
    auto has = [](int pc, int m) { return ((m >> (((pc % 12) + 12) % 12)) & 1) == 1; };
    std::array<int8_t, 12> out {};
    for (int pc = 0; pc < 12; ++pc) {
        if (has(pc, mask)) continue;
        for (int d = 1; d < 12; ++d) {
            const bool down = has(pc - d, mask), up = has(pc + d, mask);
            if (down && up) { out[(size_t)pc] = (int8_t)(has(pc + d, prefer) && !has(pc - d, prefer) ? d : -d); break; }
            if (down) { out[(size_t)pc] = (int8_t)-d; break; }
            if (up) { out[(size_t)pc] = (int8_t)d; break; }
        }
    }
    return out;
}

// A run of sixteenths that shares one scale; chord < 0 = a gap (the KEY scale).
struct ScaleSegment { int start = 0, length = 0, root = 0, scale = 0, chord = -1; };
struct ScaleSegments { int count = 0; std::array<ScaleSegment, 2 * kMaxArpChords + 1> seg {}; };

// The KEY scale: the chosen type, or AUTO's best fit to the chord track.
inline int resolvedKeyScale(const ArpSettings& a) {
    if (a.scaleType >= 0 && a.scaleType < kScaleCount) return a.scaleType;
    return keyScale(a.chordList.data(), std::clamp(a.chordCount, 0, kMaxArpChords), a.chordKey);
}
// The scale of each region of a `bars`-long timeline; empty when OFF. CHORDS
// gives each chord its own scale and gaps the KEY scale.
inline ScaleSegments scaleSegments(const ArpSettings& a, int bars) {
    ScaleSegments out;
    const int total = std::clamp(bars, 1, 4) * 16;
    if (a.scaleMode <= 0) return out;
    const ScaleSegment key {0, total, a.chordKey, resolvedKeyScale(a), -1};
    if (a.scaleMode == 1) { out.seg[0] = key; out.count = 1; return out; }
    const int keyMask = scaleMask(key.root, key.scale);
    const int n = std::clamp(a.chordCount, 0, kMaxArpChords);
    std::array<int, kMaxArpChords> order {};
    for (int i = 0; i < n; ++i) order[(size_t)i] = i;
    std::stable_sort(order.begin(), order.begin() + n, [&](int x, int y) { return a.chordList[(size_t)x].start < a.chordList[(size_t)y].start; });
    int at = 0;
    auto gap = [&](int end) { if (end > at) { auto g = key; g.start = at; g.length = end - at; out.seg[(size_t)out.count++] = g; } };
    for (int k = 0; k < n; ++k) {
        const auto& c = a.chordList[(size_t)order[(size_t)k]];
        if (c.start >= total || c.start < at) continue;
        gap(c.start);
        const int length = std::min(total, c.start + c.length) - c.start;
        out.seg[(size_t)out.count++] = {c.start, length, c.root, chordScale(c, keyMask), order[(size_t)k]};
        at = c.start + length;
    }
    gap(total);
    return out;
}

// Engine scale map: one remap table per sixteenth, looping; length 0 = OFF.
struct ScaleMap {
    int length = 0;
    std::array<std::array<int8_t, 12>, 64> table {};
};
// The FOLLOW transposition between two roots: the nearest move, -6 ... +5 semitones.
inline int followShift(int from, int to) { return (((to - from) % 12) + 18) % 12 - 6; }
// FOLLOW at each sixteenth: the semitone move from the written chord to the
// current one, and the scale segment it snaps to; seg -1 where the chord is unchanged.
struct FollowStep { int d = 0, seg = -1; };
struct FollowSteps { int length = 0; std::array<FollowStep, 64> step {}; };
inline FollowSteps followSteps(const ArpSettings& a, const ScaleSegments& segs, int bars) {
    FollowSteps out;
    const bool hasRef = a.followCount >= 0;
    const auto& ref = hasRef ? a.followList : a.chordList;
    const int n = std::clamp(a.chordCount, 0, kMaxArpChords), rn = hasRef ? std::min(a.followCount, kMaxArpChords) : n;
    const int total = std::clamp(bars, 1, 4) * 16;
    int end = 0;
    for (int i = 0; i < rn; ++i) end = std::max(end, ref[(size_t)i].start + ref[(size_t)i].length);
    const int refTotal = std::clamp((end + 15) / 16, 1, 4) * 16;
    // The loop must realign both chord tracks; 3 against 4 bars is too long, so it takes the current length.
    const int loop = total * refTotal / std::gcd(total, refTotal), len = loop <= 64 ? loop : total;
    // The first chord in list order that holds `pos` of its loop (web chordAt).
    auto chordAt = [](const std::array<ArpChord, kMaxArpChords>& list, int count, int span, int pos) -> const ArpChord* {
        const int p = pos % span;
        for (int i = 0; i < count; ++i) {
            const auto& c = list[(size_t)i];
            if (p >= c.start && p < c.start + c.length && c.start < span) return &c;
        }
        return nullptr;
    };
    out.length = len;
    for (int pos = 0; pos < len; ++pos) {
        const auto* now = chordAt(a.chordList, n, total, pos);
        const auto* was = chordAt(ref, rn, refTotal, pos);
        if ((!now && !was) || (now && was && now->root == was->root && now->quality == was->quality)) continue;
        const int p = pos % total;
        int seg = 0;
        for (int k = 0; k < segs.count; ++k)
            if (p >= segs.seg[(size_t)k].start && p < segs.seg[(size_t)k].start + segs.seg[(size_t)k].length) { seg = k; break; }
        out.step[(size_t)pos] = {followShift(was ? was->root : a.chordKey, now ? now->root : a.chordKey), seg};
    }
    return out;
}

inline ScaleMap scaleMap(const ArpSettings& a, int bars) {
    ScaleMap m;
    const auto segs = scaleSegments(a, bars);
    if (!segs.count) return m;
    auto tableOf = [&](const ScaleSegment& s) {
        return scaleRemap(scaleMask(s.root, s.scale), s.chord >= 0 ? chordMask(a.chordList[(size_t)s.chord]) : 1 << s.root);
    };
    if (a.scaleMode == 3) {
        const auto f = followSteps(a, segs, bars);
        m.length = f.length;
        for (int pos = 0; pos < f.length; ++pos) {
            const auto& g = f.step[(size_t)pos];
            if (g.seg < 0) continue; // zero table: the chord is unchanged
            const auto snap = tableOf(segs.seg[(size_t)g.seg]);
            for (int pc = 0; pc < 12; ++pc) m.table[(size_t)pos][(size_t)pc] = (int8_t)(g.d + snap[(size_t)((pc + g.d + 12) % 12)]);
        }
        return m;
    }
    if (segs.count == 1) { m.length = 1; m.table[0] = tableOf(segs.seg[0]); return m; }
    for (int i = 0; i < segs.count; ++i) {
        const auto t = tableOf(segs.seg[(size_t)i]);
        for (int k = 0; k < segs.seg[(size_t)i].length && m.length < 64; ++k) m.table[(size_t)m.length++] = t;
    }
    return m;
}
// The scale map for an SQ-4 clip's written notes: only SEQ mode plays them
// (web compileClipScale).
inline ScaleMap compileClipScale(bool hasArp, const ArpSettings& a) {
    return hasArp && !a.enabled ? scaleMap(a, a.chordBars) : ScaleMap {};
}
// `note` moved into the scale that `map` holds at sixteenth `pos`.
inline int scaleNote(const ScaleMap& map, long pos, int note) {
    if (map.length <= 0) return note;
    const auto& t = map.table[(size_t)(((pos % map.length) + map.length) % map.length)];
    return std::clamp(note + t[(size_t)(((note % 12) + 12) % 12)], 0, 127);
}
// The pitch classes each of `steps` grid sixteenths allows (UI), or false when OFF.
inline bool scaleStepMasks(const ArpSettings& a, int bars, int steps, std::array<int, 64>& out) {
    const auto segs = scaleSegments(a, bars);
    if (!segs.count) return false;
    // FOLLOW: written notes sit against the written chords; a moved step allows its new scale moved back.
    if (a.scaleMode == 3) {
        const auto f = followSteps(a, segs, bars);
        for (int i = 0; i < std::min(steps, 64); ++i) {
            const auto& g = f.step[(size_t)(i % f.length)];
            if (g.seg < 0) { out[(size_t)i] = 0xfff; continue; }
            const auto& s = segs.seg[(size_t)g.seg];
            const int mk = scaleMask(s.root, s.scale), r = ((-g.d % 12) + 12) % 12;
            out[(size_t)i] = ((mk << r) | (mk >> (12 - r))) & 0xfff;
        }
        return true;
    }
    const auto& last = segs.seg[(size_t)segs.count - 1];
    const int total = last.start + last.length;
    for (int i = 0; i < std::min(steps, 64); ++i) {
        const int p = i % total;
        for (int k = 0; k < segs.count; ++k) {
            const auto& s = segs.seg[(size_t)k];
            if (p >= s.start && p < s.start + s.length) { out[(size_t)i] = scaleMask(s.root, s.scale); break; }
        }
    }
    return true;
}

} // namespace fable
