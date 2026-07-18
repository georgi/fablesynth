// Transcription of src/bass/seq.ts (packed step helpers) and
// src/bass/patches.ts (the acid A/B lines + the factory patch bank).
#include "BassPatches.h"

#include <algorithm>

namespace fable {

// ---- seq.ts helpers ----

std::vector<uint8_t> makeEmptyBassPatterns() {
    std::vector<uint8_t> p(BL_PATTERN_BYTES, 0);
    for (size_t i = 0; i < p.size(); i += BL_STEP_STRIDE) { p[i] = 1 << 2; p[i + 2] = 1; }
    return p;
}

BassSeqStep getBassStep(const uint8_t* pats, int pat, int step) {
    const int o = (pat * BL_STEPS + step) * BL_STEP_STRIDE;
    const uint8_t flags = pats[o];
    BassSeqStep s;
    s.on    = (flags & 1) != 0;
    s.acc   = (flags & 2) != 0;
    s.slide = (pats[o + 1] & 0x80) != 0;
    s.note  = std::min(BL_NOTE_LANES - 1, (int)(pats[o + 1] & 0x7f));
    s.oct   = std::min(BL_OCT_MAX, std::max(BL_OCT_MIN, (int)pats[o + 2] - 1));
    s.duration = std::max(1, std::min(63, (int)(flags >> 2)));
    return s;
}

void setBassStep(uint8_t* pats, int pat, int step, const BassSeqStep& s) {
    const int o = (pat * BL_STEPS + step) * BL_STEP_STRIDE;
    pats[o]     = (uint8_t)((s.on ? 1 : 0) | (s.acc ? 2 : 0) | (std::max(1, std::min(63, s.duration)) << 2));
    pats[o + 1] = (uint8_t)(std::min(BL_NOTE_LANES - 1, std::max(0, s.note)) | (s.slide ? 0x80 : 0));
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
        out.push_back({ "DEEP DUB", Overrides{
            { "osc.table", 0 }, { "osc.pos", 0.18f }, { "osc.level", 0.62f },
            { "sub.shape", 0 }, { "sub.oct", -2 }, { "sub.level", 0.88f },
            { "flt.type", 1 }, { "flt.cut", 145 }, { "flt.res", 0.28f }, { "flt.drive", 0.22f },
            { "flt.env", 0.32f }, { "fenv.dec", 0.55f },
            { "aenv.dec", 0.7f }, { "aenv.sus", 0.82f }, { "aenv.rel", 0.22f },
            { "acc.amt", 0.38f }, { "slide.time", 0.13f }, { "lfo.depth", 0.04f },
            { "fx.drive.amt", 0.16f }, { "fx.delay.on", 1 }, { "fx.delay.time", 0.5f },
            { "fx.delay.fb", 0.5f }, { "fx.delay.mix", 0.14f },
            { "fx.reverb.size", 0.58f }, { "fx.reverb.mix", 0.16f },
            { "seq.bpm", 112 }, { "master.swing", 0.4f },
        }, acid, { 0 } });
        out.push_back({ "WAREHOUSE", Overrides{
            { "osc.table", 3 }, { "osc.pos", 0.72f }, { "osc.unison", 2 }, { "osc.detune", 0.18f },
            { "osc.spread", 0.18f }, { "sub.level", 0.38f },
            { "flt.type", 1 }, { "flt.cut", 430 }, { "flt.res", 0.68f }, { "flt.drive", 0.72f },
            { "flt.env", 0.76f }, { "fenv.dec", 0.14f }, { "aenv.dec", 0.22f }, { "aenv.sus", 0.42f },
            { "acc.amt", 0.9f }, { "slide.time", 0.055f },
            { "fx.drive.amt", 0.7f }, { "fx.drive.mix", 0.2f },
            { "fx.reverb.mix", 0.05f }, { "seq.bpm", 136 }, { "master.swing", 0.16f },
        }, acid, { 0, 1 } });
        out.push_back({ "ROUNDHOUSE", Overrides{
            { "osc.table", 1 }, { "osc.pos", 0.28f }, { "osc.unison", 1 }, { "osc.level", 0.68f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 0.68f },
            { "flt.type", 0 }, { "flt.cut", 680 }, { "flt.res", 0.22f }, { "flt.drive", 0.34f },
            { "flt.env", 0.48f }, { "flt.track", 0.45f }, { "fenv.dec", 0.28f },
            { "aenv.dec", 0.36f }, { "aenv.sus", 0.62f }, { "aenv.rel", 0.12f },
            { "acc.amt", 0.52f }, { "lfo.depth", 0.08f },
            { "fx.drive.amt", 0.28f }, { "fx.chorus.on", 1 }, { "fx.chorus.rate", 0.42f },
            { "fx.chorus.depth", 0.22f }, { "fx.chorus.mix", 0.1f },
            { "seq.bpm", 124 }, { "master.swing", 0.32f },
        }, acid, { 0 } });
        out.push_back({ "METAL PULSE", Overrides{
            // A bandpass on a bass throws away most of its energy; the level
            // here is set so the patch can still reach the mix target at a
            // sane track fader.
            { "osc.table", 4 }, { "osc.pos", 0.86f }, { "osc.fine", 9 },
            { "osc.unison", 3 }, { "osc.detune", 0.12f }, { "osc.spread", 0.32f }, { "osc.level", 1 },
            { "sub.shape", 1 }, { "sub.level", 0.9f },
            { "flt.type", 2 }, { "flt.cut", 620 }, { "flt.res", 0.58f }, { "flt.drive", 0.38f },
            { "flt.env", 0.62f }, { "fenv.dec", 0.19f },
            { "aenv.dec", 0.4f }, { "aenv.sus", 0.75f }, { "acc.amt", 0.72f },
            { "lfo.rate", 8 }, { "lfo.shape", 2 }, { "lfo.depth", 0.24f },
            { "fx.chorus.on", 1 }, { "fx.chorus.rate", 1.4f }, { "fx.chorus.depth", 0.5f },
            { "fx.chorus.mix", 0.22f }, { "fx.delay.on", 1 }, { "fx.delay.time", 0.22f },
            { "fx.delay.mix", 0.12f }, { "seq.bpm", 130 }, { "master.swing", 0.2f },
        }, acid, { 0, 1 } });
        out.push_back({ "TAPE BASS", Overrides{
            { "osc.table", 0 }, { "osc.pos", 0.38f }, { "osc.fine", -7 }, { "osc.unison", 2 },
            { "osc.detune", 0.08f }, { "osc.spread", 0.12f }, { "osc.level", 0.66f },
            { "sub.shape", 0 }, { "sub.level", 0.62f },
            { "flt.type", 0 }, { "flt.cut", 510 }, { "flt.res", 0.18f }, { "flt.drive", 0.3f },
            { "flt.env", 0.38f }, { "fenv.att", 0.006f }, { "fenv.dec", 0.4f },
            { "aenv.att", 0.008f }, { "aenv.dec", 0.5f }, { "aenv.sus", 0.7f }, { "aenv.rel", 0.2f },
            { "acc.amt", 0.44f }, { "slide.time", 0.1f }, { "lfo.rate", 3 }, { "lfo.depth", 0.08f },
            { "fx.drive.amt", 0.22f }, { "fx.drive.mix", 0.2f },
            { "fx.chorus.on", 1 }, { "fx.chorus.rate", 0.18f }, { "fx.chorus.depth", 0.2f },
            { "fx.chorus.mix", 0.12f }, { "fx.reverb.mix", 0.08f },
            { "seq.bpm", 104 }, { "master.swing", 0.5f },
        }, acid, { 0 } });
        out.push_back({ "REESE MONO", Overrides{
            { "osc.table", 0 }, { "osc.pos", 0.64f }, { "osc.unison", 7 }, { "osc.detune", 0.46f },
            { "osc.spread", 0.18f }, { "osc.level", 0.72f }, { "sub.shape", 0 }, { "sub.level", 0.54f },
            { "flt.type", 1 }, { "flt.cut", 360 }, { "flt.res", 0.3f }, { "flt.drive", 0.54f },
            { "flt.env", 0.3f }, { "flt.track", 0.22f }, { "fenv.dec", 0.65f },
            { "aenv.dec", 0.4f }, { "aenv.sus", 0.88f }, { "aenv.rel", 0.16f },
            { "acc.amt", 0.5f }, { "slide.time", 0.085f }, { "lfo.rate", 2 }, { "lfo.depth", 0.1f },
            { "fx.drive.amt", 0.48f }, { "fx.drive.mix", 0.2f },
            { "fx.chorus.on", 1 }, { "fx.chorus.rate", 0.32f }, { "fx.chorus.depth", 0.26f },
            { "fx.chorus.mix", 0.16f }, { "fx.reverb.mix", 0.05f },
            { "seq.bpm", 128 }, { "master.swing", 0.24f },
        }, acid, { 0, 1 } });
        out.push_back({ "PLUCKED WIRE", Overrides{
            { "osc.table", 2 }, { "osc.pos", 0.62f }, { "osc.tune", 0 }, { "osc.unison", 2 },
            { "osc.detune", 0.1f }, { "osc.spread", 0.28f }, { "osc.level", 1 },
            { "sub.level", 0.18f }, { "flt.type", 1 }, { "flt.cut", 1500 }, { "flt.res", 0.42f },
            { "flt.drive", 0.26f }, { "flt.env", 0.88f }, { "flt.track", 0.62f },
            { "fenv.dec", 0.075f }, { "aenv.dec", 0.11f }, { "aenv.sus", 0.5f }, { "aenv.rel", 0.05f },
            { "acc.amt", 0.8f }, { "slide.time", 0.035f }, { "lfo.depth", 0 },
            { "fx.drive.amt", 0.24f }, { "fx.delay.on", 1 }, { "fx.delay.time", 0.31f },
            { "fx.delay.fb", 0.34f }, { "fx.delay.mix", 0.18f },
            { "fx.reverb.size", 0.42f }, { "fx.reverb.mix", 0.14f },
            { "seq.bpm", 132 }, { "master.swing", 0.18f },
        }, acid, { 1, 0 } });
        out.push_back({ "DARK CURRENT", Overrides{
            { "osc.table", 5 }, { "osc.pos", 0.24f }, { "osc.unison", 4 },
            { "osc.detune", 0.32f }, { "osc.spread", 0.36f }, { "osc.level", 0.68f },
            { "sub.shape", 1 }, { "sub.oct", -2 }, { "sub.level", 0.62f },
            { "flt.type", 1 }, { "flt.cut", 260 }, { "flt.res", 0.46f }, { "flt.drive", 0.62f },
            { "flt.env", -0.38f }, { "flt.track", 0.18f }, { "fenv.att", 0.035f }, { "fenv.dec", 0.8f },
            { "aenv.att", 0.012f }, { "aenv.dec", 0.55f }, { "aenv.sus", 0.78f }, { "aenv.rel", 0.28f },
            { "acc.amt", 0.58f }, { "slide.time", 0.16f }, { "lfo.rate", 4 }, { "lfo.shape", 1 },
            { "lfo.depth", 0.34f }, { "fx.drive.amt", 0.56f }, { "fx.drive.mix", 0.2f },
            { "fx.delay.on", 1 }, { "fx.delay.time", 0.6f }, { "fx.delay.fb", 0.58f },
            { "fx.delay.mix", 0.16f }, { "fx.reverb.size", 0.68f }, { "fx.reverb.mix", 0.18f },
            { "seq.bpm", 118 }, { "master.swing", 0.36f },
        }, acid, { 0, 1 } });
        out.push_back({ "CLEAN SUB", Overrides{
            { "osc.table", 1 }, { "osc.pos", 0 }, { "osc.unison", 1 }, { "osc.level", 0.36f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 0.92f },
            { "flt.type", 0 }, { "flt.cut", 780 }, { "flt.res", 0.08f }, { "flt.drive", 0.08f },
            { "flt.env", 0.12f }, { "flt.track", 0.5f }, { "fenv.dec", 0.45f },
            { "aenv.att", 0.006f }, { "aenv.dec", 0.5f }, { "aenv.sus", 0.9f }, { "aenv.rel", 0.12f },
            { "acc.amt", 0.32f }, { "slide.time", 0.07f }, { "lfo.depth", 0 },
            { "fx.drive.on", 0 }, { "fx.chorus.on", 0 }, { "fx.delay.on", 0 }, { "fx.reverb.on", 0 },
            { "seq.bpm", 120 }, { "master.swing", 0.26f },
        }, acid, { 0 } });
        // ---- genre bank: one purpose-built voice per SQ-4 song family, so no
        // family has to borrow a bass that fights its groove. Levelled to land
        // in the same -7..-13 dB pre-fader window as the originals (measured by
        // test/measure_track_levels.cpp in real song context).
        out.push_back({ "SOFT HORIZON", Overrides{
            // AMBIENT: no attack, no bite — a bass that behaves like a low pad.
            { "osc.table", 1 }, { "osc.pos", 0.34f }, { "osc.unison", 3 }, { "osc.detune", 0.14f },
            { "osc.spread", 0.3f }, { "osc.level", 0.62f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 0.78f },
            { "flt.type", 1 }, { "flt.cut", 320 }, { "flt.res", 0.14f }, { "flt.drive", 0.12f },
            { "flt.env", 0.18f }, { "flt.track", 0.4f },
            { "fenv.att", 0.06f }, { "fenv.dec", 1.2f },
            { "aenv.att", 0.05f }, { "aenv.dec", 0.9f }, { "aenv.sus", 0.9f }, { "aenv.rel", 0.45f },
            { "acc.amt", 0.22f }, { "slide.time", 0.2f }, { "lfo.rate", 1 }, { "lfo.depth", 0.12f },
            { "fx.drive.amt", 0.1f }, { "fx.chorus.on", 1 }, { "fx.chorus.rate", 0.22f },
            { "fx.chorus.depth", 0.34f }, { "fx.chorus.mix", 0.18f },
            { "fx.reverb.size", 0.7f }, { "fx.reverb.mix", 0.2f },
            { "seq.bpm", 96 }, { "master.swing", 0.2f },
        }, acid, { 0 } });
        out.push_back({ "HOUSE ORGAN", Overrides{
            // HOUSE: the classic organ-ish bump — short, round, under a 4/4 kick.
            { "osc.table", 2 }, { "osc.pos", 0.42f }, { "osc.unison", 2 }, { "osc.detune", 0.12f },
            { "osc.spread", 0.2f }, { "osc.level", 0.72f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 0.7f },
            { "flt.type", 1 }, { "flt.cut", 420 }, { "flt.res", 0.3f }, { "flt.drive", 0.4f },
            { "flt.env", 0.5f }, { "flt.track", 0.35f },
            { "fenv.dec", 0.16f }, { "aenv.dec", 0.24f }, { "aenv.sus", 0.55f }, { "aenv.rel", 0.09f },
            { "acc.amt", 0.62f }, { "slide.time", 0.05f }, { "lfo.depth", 0.06f },
            { "fx.drive.amt", 0.34f }, { "fx.chorus.on", 1 }, { "fx.chorus.rate", 0.7f },
            { "fx.chorus.depth", 0.24f }, { "fx.chorus.mix", 0.14f },
            { "fx.reverb.mix", 0.06f }, { "seq.bpm", 124 }, { "master.swing", 0.1f },
        }, acid, { 0 } });
        out.push_back({ "DUSTY FELT", Overrides{
            // LO-FI: dark, slightly flat, felt-muted — the tape hiss's companion.
            { "osc.table", 0 }, { "osc.pos", 0.22f }, { "osc.fine", -5 }, { "osc.unison", 2 },
            { "osc.detune", 0.1f }, { "osc.spread", 0.14f }, { "osc.level", 0.6f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 0.74f },
            { "flt.type", 1 }, { "flt.cut", 280 }, { "flt.res", 0.16f }, { "flt.drive", 0.26f },
            { "flt.env", 0.3f }, { "flt.track", 0.3f },
            { "fenv.att", 0.012f }, { "fenv.dec", 0.34f },
            { "aenv.att", 0.014f }, { "aenv.dec", 0.42f }, { "aenv.sus", 0.66f }, { "aenv.rel", 0.18f },
            { "acc.amt", 0.36f }, { "slide.time", 0.12f }, { "lfo.rate", 2 }, { "lfo.depth", 0.07f },
            { "fx.drive.amt", 0.24f }, { "fx.chorus.on", 1 }, { "fx.chorus.rate", 0.14f },
            { "fx.chorus.depth", 0.26f }, { "fx.chorus.mix", 0.14f },
            { "fx.reverb.mix", 0.08f }, { "seq.bpm", 88 }, { "master.swing", 0.52f },
        }, acid, { 0 } });
        out.push_back({ "CINEMA SUB", Overrides{
            // CINEMATIC: almost all sub, two octaves down, swelled not played.
            { "osc.table", 0 }, { "osc.pos", 0.1f }, { "osc.unison", 3 }, { "osc.detune", 0.2f },
            { "osc.spread", 0.24f }, { "osc.level", 0.44f },
            { "sub.shape", 0 }, { "sub.oct", -2 }, { "sub.level", 0.95f },
            { "flt.type", 1 }, { "flt.cut", 190 }, { "flt.res", 0.2f }, { "flt.drive", 0.3f },
            { "flt.env", 0.24f }, { "flt.track", 0.2f },
            { "fenv.att", 0.09f }, { "fenv.dec", 1.6f },
            { "aenv.att", 0.03f }, { "aenv.dec", 1.1f }, { "aenv.sus", 0.92f }, { "aenv.rel", 0.5f },
            { "acc.amt", 0.42f }, { "slide.time", 0.22f }, { "lfo.rate", 0 }, { "lfo.depth", 0.1f },
            { "fx.drive.amt", 0.4f }, { "fx.reverb.size", 0.8f }, { "fx.reverb.mix", 0.18f },
            { "seq.bpm", 96 }, { "master.swing", 0 },
        }, acid, { 0 } });
        out.push_back({ "SUB STAB", Overrides{
            // MINIMAL: a sub stab. The oscillator is only there to give the
            // note an edge to speak with — the weight is all sub, and a 260 Hz
            // low-pass keeps any of it from reading as bright. Dry, and gone
            // before the next step.
            { "osc.table", 0 }, { "osc.pos", 0.1f }, { "osc.unison", 1 }, { "osc.level", 0.34f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 1 },
            { "flt.type", 1 }, { "flt.cut", 260 }, { "flt.res", 0.16f }, { "flt.drive", 0.3f },
            { "flt.env", 0.3f }, { "flt.track", 0.25f },
            { "fenv.dec", 0.1f }, { "aenv.dec", 0.3f }, { "aenv.sus", 0.6f }, { "aenv.rel", 0.09f },
            { "acc.amt", 0.45f }, { "slide.time", 0.04f }, { "lfo.depth", 0 },
            { "fx.drive.on", 0 }, { "fx.chorus.on", 0 }, { "fx.delay.on", 0 }, { "fx.reverb.on", 0 },
            { "seq.bpm", 132 }, { "master.swing", 0 },
        }, acid, { 0 } });
        out.push_back({ "808 GLIDE", Overrides{
            // FUTURE BASS: the 808 — pure sub, long tail, glides between roots.
            { "osc.table", 0 }, { "osc.pos", 0.06f }, { "osc.unison", 1 }, { "osc.level", 0.3f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 1 },
            { "flt.type", 1 }, { "flt.cut", 240 }, { "flt.res", 0.1f }, { "flt.drive", 0.35f },
            { "flt.env", 0.15f }, { "flt.track", 0.3f },
            { "fenv.dec", 0.8f }, { "aenv.att", 0.003f }, { "aenv.dec", 1.4f }, { "aenv.sus", 0.85f },
            { "aenv.rel", 0.35f }, { "acc.amt", 0.5f }, { "slide.time", 0.14f }, { "lfo.depth", 0 },
            { "fx.drive.amt", 0.45f }, { "fx.reverb.mix", 0.05f },
            { "seq.bpm", 150 }, { "master.swing", 0 },
        }, acid, { 0 } });
        out.push_back({ "GROWL WIDE", Overrides{
            // FUTURE BASS: the other half — wide detuned growl, 1/8 filter motion.
            { "osc.table", 5 }, { "osc.pos", 0.48f }, { "osc.unison", 5 }, { "osc.detune", 0.38f },
            { "osc.spread", 0.42f }, { "osc.level", 0.66f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 0.62f },
            { "flt.type", 1 }, { "flt.cut", 300 }, { "flt.res", 0.4f }, { "flt.drive", 0.6f },
            { "flt.env", 0.5f }, { "flt.track", 0.25f },
            { "fenv.dec", 0.3f }, { "aenv.dec", 0.5f }, { "aenv.sus", 0.8f }, { "aenv.rel", 0.18f },
            { "acc.amt", 0.6f }, { "slide.time", 0.08f },
            { "lfo.rate", 5 }, { "lfo.shape", 1 }, { "lfo.depth", 0.4f },
            { "fx.drive.amt", 0.55f }, { "fx.chorus.on", 1 }, { "fx.chorus.rate", 0.5f },
            { "fx.chorus.depth", 0.4f }, { "fx.chorus.mix", 0.2f },
            { "fx.reverb.mix", 0.06f }, { "seq.bpm", 150 }, { "master.swing", 0 },
        }, acid, { 0, 1 } });
        out.push_back({ "UPRIGHT FELT", Overrides{
            // TRIP HOP: woody and finger-soft, a hair behind the beat.
            { "osc.table", 1 }, { "osc.pos", 0.16f }, { "osc.unison", 1 }, { "osc.level", 0.7f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 0.8f },
            { "flt.type", 1 }, { "flt.cut", 250 }, { "flt.res", 0.22f }, { "flt.drive", 0.28f },
            { "flt.env", 0.42f }, { "flt.track", 0.42f },
            { "fenv.att", 0.008f }, { "fenv.dec", 0.22f },
            { "aenv.att", 0.01f }, { "aenv.dec", 0.55f }, { "aenv.sus", 0.6f }, { "aenv.rel", 0.22f },
            { "acc.amt", 0.48f }, { "slide.time", 0.14f }, { "lfo.depth", 0.04f },
            { "fx.drive.amt", 0.2f }, { "fx.reverb.size", 0.5f }, { "fx.reverb.mix", 0.12f },
            { "seq.bpm", 86 }, { "master.swing", 0.42f },
        }, acid, { 0 } });
        out.push_back({ "STEPPER ROOT", Overrides{
            // DUB: the steppers root — enormous, round, slow to speak, long to leave.
            { "osc.table", 0 }, { "osc.pos", 0.14f }, { "osc.unison", 1 }, { "osc.level", 0.4f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 0.98f },
            { "flt.type", 1 }, { "flt.cut", 165 }, { "flt.res", 0.18f }, { "flt.drive", 0.3f },
            { "flt.env", 0.28f }, { "flt.track", 0.25f },
            { "fenv.att", 0.02f }, { "fenv.dec", 0.5f },
            { "aenv.att", 0.014f }, { "aenv.dec", 0.6f }, { "aenv.sus", 0.88f }, { "aenv.rel", 0.26f },
            { "acc.amt", 0.4f }, { "slide.time", 0.16f }, { "lfo.depth", 0.03f },
            { "fx.drive.amt", 0.22f }, { "fx.reverb.size", 0.6f }, { "fx.reverb.mix", 0.14f },
            { "seq.bpm", 74 }, { "master.swing", 0.14f },
        }, acid, { 0 } });
        out.push_back({ "TECHNO SUB", Overrides{
            // MINIMAL: the held counterpart to SUB STAB — a pure sine sub that
            // sits under the whole bar. No oscillator content above the
            // low-pass at all, so it reads as weight rather than as a part.
            { "osc.table", 0 }, { "osc.pos", 0.04f }, { "osc.unison", 1 }, { "osc.level", 0.22f },
            { "sub.shape", 0 }, { "sub.oct", -1 }, { "sub.level", 1 },
            { "flt.type", 1 }, { "flt.cut", 210 }, { "flt.res", 0.1f }, { "flt.drive", 0.24f },
            { "flt.env", 0.16f }, { "flt.track", 0.22f },
            { "fenv.dec", 0.4f }, { "aenv.att", 0.004f }, { "aenv.dec", 0.7f }, { "aenv.sus", 0.88f },
            { "aenv.rel", 0.16f }, { "acc.amt", 0.35f }, { "slide.time", 0.08f }, { "lfo.depth", 0 },
            { "fx.drive.amt", 0.2f }, { "fx.chorus.on", 0 }, { "fx.delay.on", 0 }, { "fx.reverb.on", 0 },
            { "seq.bpm", 130 }, { "master.swing", 0 },
        }, acid, { 0 } });
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
    // Keep the factory bank dry, matching the web factory patch transform;
    // drive, where a patch enables it, blends at a common 65% mix.
    p[(size_t)BL_FXDELAY_ON] = 0.0f;
    p[(size_t)BL_FXREVERB_ON] = 0.0f;
    if (p[(size_t)BL_FXDRIVE_ON] >= 0.5f) p[(size_t)BL_FXDRIVE_MIX] = 0.65f;
    return p;
}

} // namespace fable
