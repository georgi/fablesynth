#pragma once
// Scale mode parity checks (web src/scales.test.ts, src/engine/worklet.mono.test.ts).
#include "ArpEngineChecks.h"
#include "../source/dsp/Scales.h"
#include <cstring>

namespace scale_checks {
using namespace fable;
inline ArpChord chord(int root, const char* q, int start = 0, int length = 16) {
    return {(uint8_t)start, (uint8_t)length, (uint8_t)root, (uint8_t)chordQualityIndex(q), 0};
}
inline ArpSettings withProgression(const char* name, int key) {
    ArpSettings a; a.chordKey = key;
    for (const auto& p : progressions())
        if (std::strcmp(p.name, name) == 0) a.chordBars = progressionChords(p, key, a.chordList, a.chordCount);
    return a;
}
inline int keyOf(const char* name, int key) {
    const auto a = withProgression(name, key);
    return keyScale(a.chordList.data(), a.chordCount, key);
}

template<class Engine, class Check>
void playback(const std::vector<TablePtr>& tables, Check check) {
    using Rig = arp_checks::Rig<Engine>;
    constexpr bool bass = Rig::bass;
    const char* tag = bass ? "BL-1 " : "WT-1 ";
    auto name = [&](const char* what) { static std::string s; s = std::string(tag) + what; return s.c_str(); };
    const int root = bass ? BL_ROOT_MIDI : 48;
    ScaleMap cMajor; cMajor.length = 1; cMajor.table[0] = scaleRemap(scaleMask(0, kScMajor));
    ScaleMap twoKeys = cMajor; twoKeys.length = 2; twoKeys.table[1] = scaleRemap(scaleMask(7, kScMajor));
    // Hosted clip: two F♯ sixteenths.
    auto bytes = sqEmptyClip(bass ? Machine::BL1 : Machine::WT1, 1);
    for (int s = 0; s < 2; ++s) {
        const int o = bass ? s * BL_STEP_STRIDE : sqWtNoteIdx(0, s, 0);
        bytes[(size_t)o] = 5; bytes[(size_t)o + 1] = 6; bytes[(size_t)o + 2] = 1;
    }
    auto hosted = [&](const ScaleMap& map, int& first, int& second) {
        Rig r(tables); r.engine->setHostClipMode(true, 512);
        r.engine->hostTempo(120, 0, 512);
        r.engine->hostClip(bytes.data(), (int)bytes.size(), 1, 512, 0, {}, nullptr, {}, map);
        r.render(513); first = r.gated() ? r.pitch() : -1;
        r.render(6000); second = r.gated() ? r.pitch() : -1;
    };
    int a = 0, b = 0;
    hosted({}, a, b); check(a == root + 6 && b == root + 6, name("hosted clip without a map plays written notes"));
    hosted(cMajor, a, b); check(a == root + 5 && b == root + 5, name("hosted clip snaps F# to F in C major"));
    hosted(twoKeys, a, b); check(a == root + 5 && b == root + 6, name("hosted map follows the anchor-grid sixteenth"));
    // Standalone sequencer: chain position * 16 + step.
    {
        Rig r(tables);
        if constexpr (bass) {
            std::array<uint8_t, BL_PATTERN_BYTES> p {}; p[0] = 5; p[1] = 6; p[2] = 1;
            r.engine->setPatterns(p.data(), (int)p.size());
        } else {
            std::array<uint8_t, SEQ_PATTERN_BYTES> p {}; p[0] = 5; p[1] = 6; p[2] = 1;
            r.engine->setSeqPatterns(p.data(), (int)p.size());
        }
        r.engine->setSeqScale(cMajor); r.play(); r.render(1);
        check(r.gated() && r.pitch() == root + 5, name("standalone sequencer snaps F# to F in C major"));
    }
}

template<class Check> void runScaleChecks(Check check) {
    check(keyOf("i–VI–III–VII", 9) == kScMinor, "key scale: epic minor is minor");
    check(keyOf("I–V–vi–IV", 0) == kScMajor, "key scale: pop is major");
    check(keyOf("i–iv–V–i", 9) == kScHarmMinor, "key scale: i-iv-V-i is harmonic minor");
    check(keyOf("I–♭VII–IV–I", 7) == kScMixolydian, "key scale: I-bVII-IV-I is mixolydian");
    check(keyOf("i7–IV7", 2) == kScDorian, "key scale: i7-IV7 is dorian");
    const int cMaj = scaleMask(0, kScMajor);
    check(chordScale(chord(0, "maj7"), cMaj) == kScMajor && chordScale(chord(2, "m7"), cMaj) == kScDorian
          && chordScale(chord(4, "m7"), cMaj) == kScPhrygian && chordScale(chord(5, "maj7"), cMaj) == kScLydian
          && chordScale(chord(7, "dom7"), cMaj) == kScMixolydian && chordScale(chord(9, "m7"), cMaj) == kScMinor
          && chordScale(chord(11, "m7b5"), cMaj) == kScLocrian, "chord scale: diatonic chords get their mode");
    check(chordScale(chord(4, "dom7"), scaleMask(9, kScMinor)) == kScPhrygDom
          && chordScale(chord(10, "dom7"), cMaj) == kScLydDom && chordScale(chord(0, "dim7"), cMaj) == kScDimWH
          && chordScale(chord(0, "aug"), cMaj) == kScWholeTone && chordScale(chord(4, "dom7s9"), cMaj) == kScDimHW,
          "chord scale: borrowed chords get the closest altered scale");
    const auto t = scaleRemap(cMaj);
    check(t[1] == -1 && t[6] == -1 && t[4] == 0 && scaleRemap(cMaj, 1 << 2)[1] == 1
          && scaleRemap(scaleMask(0, scaleIndex("minPent")))[1] == -1, "remap: nearest, tie to chord tone, else down");
    {
        auto a = withProgression("i–VI–III–VII", 9);
        check(scaleMap(a, 4).length == 0, "scale map is empty when OFF");
        a.scaleMode = 1; const auto key = scaleMap(a, 4);
        check(key.length == 1 && scaleNote(key, 37, 61) == 60, "KEY map is one table: C# -> C in A minor");
    }
    {
        ArpSettings b; b.chordKey = 0; b.scaleMode = 2; b.scaleType = kScMajor; b.chordCount = 2;
        b.chordList[0] = chord(0, "maj"); b.chordList[1] = chord(10, "dom7", 32, 16);
        const auto segs = scaleSegments(b, 4);
        const bool shape = segs.count == 4 && segs.seg[0].start == 0 && segs.seg[0].scale == kScMajor
            && segs.seg[1].start == 16 && segs.seg[1].chord < 0 && segs.seg[2].start == 32 && segs.seg[2].root == 10
            && segs.seg[2].scale == kScLydDom && segs.seg[3].start == 48 && segs.seg[3].length == 16;
        check(shape, "CHORDS segments: a scale per chord, the key scale in gaps");
        const auto map = scaleMap(b, 4);
        check(map.length == 64 && scaleNote(map, 2, 63) == 64 && scaleNote(map, 34, 63) == 62 && scaleNote(map, 34, 64) == 64,
              "CHORDS map snaps per sixteenth toward chord tones");
        check(compileClipScale(true, b).length == 64 && compileClipScale(false, b).length == 0, "clip scale: SEQ-mode clips only");
        auto on = b; on.enabled = true;
        check(compileClipScale(true, on).length == 0, "clip scale: an enabled arp plays no written notes");
    }
    {   // FOLLOW (web followMap): Dm9 written, Gm7 now in bars 3-4 -> d = +5 plus the snap.
        ArpSettings f; f.chordKey = 2; f.scaleMode = 3; f.chordCount = 2;
        f.chordList[0] = chord(2, "m9", 0, 32); f.chordList[1] = chord(7, "m7", 32, 32);
        f.followCount = 1; f.followList[0] = chord(2, "m9", 0, 64);
        const auto map = scaleMap(f, 4);
        bool zero = true; for (int pc = 0; pc < 12; ++pc) zero = zero && map.table[0][(size_t)pc] == 0;
        const std::array<int, 12> want {{5, 4, 5, 4, 5, 5, 4, 5, 6, 5, 4, 5}};
        bool moved = true; for (int pc = 0; pc < 12; ++pc) moved = moved && map.table[32][(size_t)pc] == want[(size_t)pc];
        check(map.length == 64 && zero && moved, "FOLLOW map: unchanged chords stay, Dm9 -> Gm7 moves +5 and snaps");
        std::array<int, 64> masks {};
        check(scaleStepMasks(f, 4, 64, masks) && masks[0] == 0xfff && masks[32] == 0xab5, "FOLLOW step masks: all where unchanged, new scale moved back");
        auto same = f; same.followCount = 2; same.followList = f.chordList;
        const auto still = scaleMap(same, 4);
        bool flat = still.length == 64;
        for (int i = 0; i < still.length; ++i) for (int pc = 0; pc < 12; ++pc) flat = flat && still.table[(size_t)i][(size_t)pc] == 0;
        auto absent = same; absent.followCount = -1;
        const auto self = scaleMap(absent, 4);
        for (int i = 0; i < self.length; ++i) for (int pc = 0; pc < 12; ++pc) flat = flat && self.table[(size_t)i][(size_t)pc] == 0;
        check(flat, "FOLLOW map: an unchanged progression gives zero offsets");
        ArpSettings loop; loop.chordKey = 0; loop.scaleMode = 3; loop.chordCount = 1; loop.chordList[0] = chord(0, "maj");
        loop.followCount = 2; loop.followList[0] = chord(0, "maj"); loop.followList[1] = chord(5, "maj", 16, 16);
        const auto l = scaleMap(loop, 1);
        check(l.length == 32 && l.table[0][0] == 0 && l.table[16][0] == -5, "FOLLOW map loops over both chord tracks (1 bar against 2)");
        check(followShift(2, 7) == 5 && followShift(7, 2) == -5 && followShift(0, 6) == -6, "followShift takes the nearest move");
    }
#if FABLE_SQ4_TEST_JUCE
    {
        ArpSettings f; f.scaleMode = 3; f.followCount = 2; f.followList[0] = chord(2, "m9", 0, 32); f.followList[1] = chord(7, "m7", 32, 32);
        ArpSettings back;
        const auto json = juce::JSON::toString(arpToVar(f));
        check(json.contains("\"follow\"") && arpFromVar(juce::JSON::parse(json), back, true) && back.scaleMode == 3 && back.followCount == 2
              && back.followList[1].root == 7 && back.followList[1].quality == chordQualityIndex("m7") && juce::JSON::toString(arpToVar(back)) == json,
              "arp JSON round-trips FOLLOW and followChords");
        ArpSettings none; check(!juce::JSON::toString(arpToVar(none)).contains("followChords"), "arp JSON omits absent followChords");
    }
    {
        ArpSettings a; a.scaleMode = 2; a.scaleType = kScDorian; ArpSettings back;
        check(arpFromVar(juce::JSON::parse(juce::JSON::toString(arpToVar(a))), back, true) && back.scaleMode == 2 && back.scaleType == kScDorian,
              "arp JSON round-trips scale mode and type");
        ArpSettings plain; const auto s = juce::JSON::toString(arpToVar(plain));
        check(!s.contains("\"scale"), "arp JSON omits OFF / AUTO scale fields");
    }
#endif
    std::vector<TablePtr> tables;
    for (auto& g : generateTables()) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    playback<Engine>(tables, check);
    playback<BassEngine>(tables, check);
}
} // namespace scale_checks
