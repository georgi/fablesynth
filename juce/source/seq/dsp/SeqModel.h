// SQ-4 session model — C++ port of src/seq/protocol.ts (session document +
// validation) and src/seq/model.ts:20-51 (mute/solo gates). Header-only,
// JUCE-free. Base64 encode/decode lives in the JUCE layer (Task 14); this
// model carries decoded clip bytes only.
#pragma once

#include "SeqProtocol.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace fable {

struct ClipData {
    std::string name;
    int bars = 1;
    std::vector<uint8_t> bytes;
};

// v1 sessions use factory patches; params are only meaningful when !factory.
struct PatchRef {
    bool factory = true;
    int index = 0;
    std::map<std::string, float> params;
};

struct TrackData {
    Machine machine;
    std::string name;
    uint32_t color;
    float gain;
    PatchRef patch;
};

// clips[t] is meaningful only where hasClip[t] — models the web's
// (ClipDoc | null)[]. pass holds the indices of tracks whose empty cell is
// pass-through on scene launch (the previous clip rides through instead of
// stopping); tracks not listed here default to Ableton-style stop buttons
// on their empty cells (protocol.ts:66-72, docs/sq4-clips.md §4).
struct SceneData {
    std::string name;
    std::vector<ClipData> clips;
    std::vector<bool> hasClip;
    std::vector<int> pass;
};

struct SessionData {
    std::string name;
    double bpm = 122;
    double swing = 0;
    Quant quant = Quant::Bar;
    std::vector<TrackData> tracks;
    std::vector<SceneData> scenes;
};

// Returns "" when valid, else a human-readable reason. Port of
// src/seq/protocol.ts:139-160 (the v/loadSession JSON-shape checks are the
// JUCE layer's concern; this only checks the in-memory document).
inline std::string validateSession(const SessionData& doc) {
    if (!(doc.bpm >= 60.0 && doc.bpm <= 200.0)) return "bpm out of range";
    if (doc.tracks.empty()) return "no tracks";
    for (size_t s = 0; s < doc.scenes.size(); s++) {
        const auto& sc = doc.scenes[s];
        if (sc.clips.size() != doc.tracks.size() || sc.hasClip.size() != doc.tracks.size())
            return "scene " + std::to_string(s) + ": clip count != track count";
        for (size_t t = 0; t < sc.clips.size(); t++) {
            if (!sc.hasClip[t]) continue;
            const auto& c = sc.clips[t];
            if (!(c.bars >= 1 && c.bars <= SQ_MAX_BARS))
                return "scene " + std::to_string(s) + " track " + std::to_string(t) + ": bars out of range";
            const int want = c.bars * sqBytesPerBar(doc.tracks[t].machine);
            if ((int)c.bytes.size() != want)
                return "scene " + std::to_string(s) + " track " + std::to_string(t) + ": pattern is "
                     + std::to_string(c.bytes.size()) + " bytes, expected " + std::to_string(want);
        }
    }
    return "";
}

// Port of src/seq/model.ts:20-33 — muted by its own mute button, by another
// track's solo, or by a scene-mute on the scene that owns it.
inline bool isTrackAudible(int t, const std::unordered_map<int, int>& owner,
                            const std::vector<bool>& trackMute, const std::vector<bool>& sceneMute,
                            const std::vector<bool>& solo) {
    auto it = owner.find(t);
    if (it == owner.end()) return false;
    if (t < (int)trackMute.size() && trackMute[(size_t)t]) return false;
    bool anySolo = false;
    for (bool b : solo) if (b) { anySolo = true; break; }
    if (anySolo && !(t < (int)solo.size() && solo[(size_t)t])) return false;
    int o = it->second;
    return !(o < (int)sceneMute.size() && sceneMute[(size_t)o]);
}

// Port of src/seq/model.ts:35-48 — mute/solo gate independent of clip
// ownership; open even between clips (drives the track GainNode).
inline bool isTrackOpen(int t, const std::unordered_map<int, int>& owner,
                         const std::vector<bool>& trackMute, const std::vector<bool>& sceneMute,
                         const std::vector<bool>& solo) {
    if (t < (int)trackMute.size() && trackMute[(size_t)t]) return false;
    bool anySolo = false;
    for (bool b : solo) if (b) { anySolo = true; break; }
    if (anySolo && !(t < (int)solo.size() && solo[(size_t)t])) return false;
    auto it = owner.find(t);
    if (it == owner.end()) return true;
    int o = it->second;
    return !(o < (int)sceneMute.size() && sceneMute[(size_t)o]);
}

// 16-step cell preview derived from the clip's first bar of real pattern
// data — port of src/seq/model.ts:61-80 (StepBar.h is a bar height in px of a
// 20px lane; `bytes` must have at least one bar's worth for `machine`).
struct StepBar {
    int h = 3;
    bool on = false;
};

inline std::array<StepBar, SQ_STEPS_PER_BAR> sqPreviewSteps(Machine machine, const uint8_t* bytes) {
    std::array<StepBar, SQ_STEPS_PER_BAR> out {};
    for (int s = 0; s < SQ_STEPS_PER_BAR; s++) {
        if (machine == Machine::DR1) {
            int count = 0;
            bool acc = false;
            for (int pad = 0; pad < SQ_DR1_PADS; pad++) {
                const uint8_t v = bytes[sqDr1Idx(0, pad, s)];
                if (v) { count++; if (v == 2) acc = true; }
            }
            out[(size_t)s] = { count ? std::min(19, 5 + count * 3 + (acc ? 2 : 0)) : 3, count > 0 };
        } else {
            bool on = false;
            int semi = -12;
            const int lanes = machine == Machine::WT1 ? SQ_WT_POLY_LANES : 1;
            for (int lane = 0; lane < lanes; ++lane) {
                const int o = machine == Machine::WT1 ? sqWtNoteIdx(0, s, lane) : sqNoteIdx(0, s);
                if ((bytes[o] & 1) != 0) {
                    on = true;
                    semi = std::max(semi, std::min(11, (int)bytes[o + 1]) + 12 * ((int)bytes[o + 2] - 1));
                }
            }
            out[(size_t)s] = { on ? (int)std::lround(5.0 + ((semi + 12) / 35.0) * 14.0) : 3, on };
        }
    }
    return out;
}

} // namespace fable
