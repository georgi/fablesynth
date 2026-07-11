// Transcription of src/bass/seq.ts (packed step helpers) and
// src/bass/patches.ts (the acid A/B lines + the three factory patches).
#include "BassPatches.h"

#include <algorithm>

namespace fable {

// ---- seq.ts helpers ----

std::vector<uint8_t> makeEmptyBassPatterns() {
    std::vector<uint8_t> p(BL_PATTERN_BYTES, 0);
    for (size_t i = 2; i < p.size(); i += BL_STEP_STRIDE) p[i] = 1;
    return p;
}

BassSeqStep getBassStep(const uint8_t* pats, int pat, int step) {
    const int o = (pat * BL_STEPS + step) * BL_STEP_STRIDE;
    const uint8_t flags = pats[o];
    BassSeqStep s;
    s.on    = (flags & 1) != 0;
    s.acc   = (flags & 2) != 0;
    s.slide = (flags & 4) != 0;
    s.note  = std::min(BL_NOTE_LANES - 1, (int)pats[o + 1]);
    s.oct   = std::min(BL_OCT_MAX, std::max(BL_OCT_MIN, (int)pats[o + 2] - 1));
    return s;
}

void setBassStep(uint8_t* pats, int pat, int step, const BassSeqStep& s) {
    const int o = (pat * BL_STEPS + step) * BL_STEP_STRIDE;
    pats[o]     = (uint8_t)((s.on ? 1 : 0) | (s.acc ? 2 : 0) | (s.slide ? 4 : 0));
    pats[o + 1] = (uint8_t)std::min(BL_NOTE_LANES - 1, std::max(0, s.note));
    pats[o + 2] = (uint8_t)(std::min(BL_OCT_MAX, std::max(BL_OCT_MIN, s.oct)) + 1);
}

// ---- patches.ts ----

namespace {

struct S {   // patches.ts S(note, opts) shorthand
    int note; bool acc = false, slide = false; int oct = 0;
};

void writePattern(std::vector<uint8_t>& p, int pat,
                  const std::vector<std::pair<bool, S>>& steps) {
    for (int i = 0; i < (int)steps.size() && i < BL_STEPS; ++i) {
        BassSeqStep st;
        st.on = steps[(size_t)i].first;
        if (st.on) {
            const S& s = steps[(size_t)i].second;
            st.note = s.note; st.oct = s.oct; st.acc = s.acc; st.slide = s.slide;
        }
        setBassStep(p.data(), pat, i, st);
    }
}

// The design mock's A/B acid lines, verbatim (patches.ts acidPatterns).
std::vector<uint8_t> acidPatterns() {
    auto on   = [](S s) { return std::make_pair(true, s); };
    auto rest = []      { return std::make_pair(false, S{0}); };
    std::vector<uint8_t> p = makeEmptyBassPatterns();
    writePattern(p, 0, {
        on({0, true}), on({0, false, false, -1}), rest(), on({3, false, true}),
        on({0}), rest(), on({7, true}), on({6, false, true}),
        on({0}), rest(), on({10, false, false, -1}), on({0, false, true}),
        rest(), on({3, true, false, 1}), on({0, false, true}), rest(),
    });
    writePattern(p, 1, {
        on({0, true}), rest(), on({0}), on({5, false, true}),
        rest(), on({0, false, false, -1}), on({7, false, true}), rest(),
        on({0, true}), on({3}), rest(), on({10, false, true, -1}),
        on({0}), rest(), on({0, true, true, 1}), rest(),
    });
    return p;
}

using Overrides = std::vector<std::pair<std::string, float>>;

} // namespace

const std::vector<BassPatch>& bassFactoryPatches() {
    static const std::vector<BassPatch> patches = [] {
        const std::vector<uint8_t> acid = acidPatterns();
        std::vector<BassPatch> out;
        // The design defaults ARE the acid line — overrides only where the
        // mock differs.
        out.push_back({ "ACID LINE", {}, acid, { 0 } });
        out.push_back({ "RUBBER SUB", Overrides{
            { "osc.table", 2 },       // PULSE
            { "osc.pos", 0.12f },
            { "osc.level", 0.6f },
            { "sub.shape", 1 },
            { "sub.oct", -1 },
            { "sub.level", 0.8f },
            { "flt.cut", 210 },
            { "flt.res", 0.35f },
            { "flt.drive", 0.25f },
            { "flt.env", 0.45f },
            { "fenv.dec", 0.32f },
            { "aenv.sus", 0.72f },
            { "acc.amt", 0.5f },
            { "lfo.depth", 0.05f },
            { "fx.drive.amt", 0.2f },
            { "fx.reverb.mix", 0.06f },
            { "seq.bpm", 122 },
            { "master.swing", 0.42f },
        }, acid, { 0 } });
        out.push_back({ "NEON SQUELCH", Overrides{
            { "osc.pos", 0.55f },
            { "osc.unison", 3 },
            { "osc.detune", 0.3f },
            { "osc.spread", 0.4f },
            { "sub.level", 0.3f },
            { "flt.cut", 520 },
            { "flt.res", 0.78f },
            { "flt.env", 0.85f },
            { "fenv.dec", 0.12f },
            { "acc.amt", 0.85f },
            { "slide.time", 0.09f },
            { "lfo.depth", 0.3f },
            { "lfo.rate", 8 },        // 1/16
            { "fx.chorus.on", 1 },
            { "fx.delay.on", 1 },
            { "fx.delay.mix", 0.24f },
            { "seq.bpm", 142 },
        }, acid, { 0, 1 } });
        return out;
    }();
    return patches;
}

BassParamArray applyBassPatch(const BassPatch& patch) {
    BassParamArray p = defaultBassParams();
    for (const auto& [pid, v] : patch.params) {
        int id = bassIdFromString(pid);
        if (id >= 0) p[(size_t)id] = v;
    }
    return p;
}

} // namespace fable
