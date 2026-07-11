// Headless verification harness for the BL-1 bass DSP core (no JUCE).
// Mirrors test/drum_engine_test.cpp's style; the lockstep reference is the
// web app under src/bass/ (params.ts, worklet-bass.js, seq.ts, patches.ts).
//
// Exits non-zero if any check fails.
#include "../source/bass/dsp/BassParams.h"
#include "../source/bass/dsp/BassEngine.h"
#include "../source/bass/dsp/BassFx.h"
#include "../source/bass/dsp/BassPatches.h"
#include "../source/dsp/Wavetables.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace fable;

static int g_fail = 0;
static void check(bool cond, const std::string& name, const std::string& detail = "") {
    printf("  [%s] %s%s\n", cond ? "PASS" : "FAIL", name.c_str(),
           detail.empty() ? "" : ("  -> " + detail).c_str());
    if (!cond) g_fail++;
}
static std::string num(double v) { char b[64]; snprintf(b, sizeof b, "%.5f", v); return b; }

static bool finite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}
static double rms(const std::vector<float>& v, int a, int b) {
    double s = 0; int n = 0;
    for (int i = a; i < b && i < (int)v.size(); i++) { s += (double)v[i] * v[i]; n++; }
    return n ? std::sqrt(s / n) : 0;
}
static float peak(const std::vector<float>& v) {
    float p = 0; for (float x : v) p = std::max(p, std::abs(x)); return p;
}
// Sign changes in v[a..b) — a cheap pitch proxy for the slide check.
static int crossings(const std::vector<float>& v, int a, int b) {
    int c = 0;
    for (int i = a + 1; i < b && i < (int)v.size(); i++)
        if ((v[i - 1] < 0.0f) != (v[i] < 0.0f)) c++;
    return c;
}

static const double SR = 48000.0;

static std::vector<TablePtr> makeTables() {
    std::vector<TablePtr> out;
    for (auto& g : generateTables())
        out.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    return out;
}

// Render n samples in host-sized blocks, returning L. Records the sample
// index of every currentStep() change into stepAt if given.
static std::vector<float> render(BassEngine& e, int n, int block = 512,
                                 std::vector<std::pair<int, int>>* stepAt = nullptr) {
    std::vector<float> L((size_t)n), R((size_t)n);
    int last = e.currentStep();
    for (int at = 0; at < n; at += block) {
        int run = std::min(block, n - at);
        e.render(L.data() + at, R.data() + at, run);
        if (stepAt && e.currentStep() != last) {
            last = e.currentStep();
            stepAt->push_back({ last, at + run });
        }
    }
    return L;
}

int main() {
    printf("\n== BL-1 params (BassParams vs src/bass/params.ts) ==\n");
    {
        const auto& info = bassParamInfo();
        check((int)info.size() == BL_NUM_PARAMS, "descriptor count == BL_NUM_PARAMS (45)",
              num((double)info.size()));
        bool ordered = true;
        for (int i = 0; i < (int)info.size(); i++) if (info[(size_t)i].id != i) ordered = false;
        check(ordered, "descriptors ordered by flat id");
        auto d = defaultBassParams();
        check(d[BL_OSC_TUNE] == -12.0f, "osc.tune default -12 ST", num(d[BL_OSC_TUNE]));
        check(d[BL_FLT_CUT] == 340.0f, "flt.cut default 340 Hz", num(d[BL_FLT_CUT]));
        check(d[BL_SEQ_BPM] == 138.0f, "seq.bpm default 138", num(d[BL_SEQ_BPM]));
        check(d[BL_SUB_OCT] == -1.0f, "sub.oct default -1", num(d[BL_SUB_OCT]));
        check(d[BL_FXDRIVE_ON] == 1.0f && d[BL_FXREVERB_ON] == 1.0f && d[BL_FXDELAY_ON] == 0.0f,
              "fx defaults: drive+reverb on, delay off");
        check(bassIdFromString("flt.cut") == BL_FLT_CUT, "id lookup flt.cut");
        check(bassIdFromString("nope") == -1, "unknown id -> -1");
    }

    printf("\n== packed patterns (seq.ts helpers) ==\n");
    {
        auto p = makeEmptyBassPatterns();
        check((int)p.size() == BL_PATTERN_BYTES, "pattern buffer is 4*16*3 bytes");
        check(getBassStep(p.data(), 2, 9).oct == 0, "empty pattern reads oct 0 (byte = 1)");
        BassSeqStep s; s.on = true; s.note = 7; s.oct = -1; s.acc = true; s.slide = true;
        setBassStep(p.data(), 1, 5, s);
        auto r = getBassStep(p.data(), 1, 5);
        check(r.on && r.acc && r.slide && r.note == 7 && r.oct == -1, "step round-trips");
        // Engine unpack combines note + oct into a semi offset.
        auto es = BassEngine::readStep(p.data(), 1, 5);
        check(es.on && es.acc && es.slide && es.semi == 7 - 12,
              "engine readStep semi = note + 12*oct", num(es.semi));
        s.note = 99; s.oct = 9;
        setBassStep(p.data(), 1, 6, s);
        r = getBassStep(p.data(), 1, 6);
        check(r.note == 11 && r.oct == 1, "note/oct clamp on write");
    }

    printf("\n== factory patches (patches.ts) ==\n");
    {
        const auto& bank = bassFactoryPatches();
        check((int)bank.size() == 3, "3 factory patches");
        check(bank[0].name == "ACID LINE" && bank[1].name == "RUBBER SUB"
              && bank[2].name == "NEON SQUELCH", "patch names + order");
        const auto& acid = bank[0];
        auto s0 = getBassStep(acid.patterns.data(), 0, 0);
        check(s0.on && s0.acc && s0.note == 0 && s0.oct == 0, "acid A step 1: accented root");
        auto s3 = getBassStep(acid.patterns.data(), 0, 3);
        check(s3.on && s3.slide && s3.note == 3, "acid A step 4: slide into +3");
        auto s13 = getBassStep(acid.patterns.data(), 0, 13);
        check(s13.on && s13.acc && s13.oct == 1, "acid A step 14: accented +1 oct throw");
        check(bank[2].chain == std::vector<int>({ 0, 1 }), "NEON SQUELCH chains A->B");
        auto pv = applyBassPatch(bank[1]);
        check(pv[BL_FLT_CUT] == 210.0f, "RUBBER SUB overrides flt.cut=210", num(pv[BL_FLT_CUT]));
        check(pv[BL_OSC_TUNE] == -12.0f, "unlisted params keep defaults", num(pv[BL_OSC_TUNE]));
    }

    auto tables = makeTables();

    printf("\n== audition voice (keyOn/keyOff, worklet noteOn/release) ==\n");
    {
        BassEngine e;
        e.prepare(SR);
        e.setTables(tables);
        e.keyOn(12, 0.9f);
        auto held = render(e, 9600);
        check(finite(held), "output finite");
        check(rms(held, 2400, 9600) > 1e-3, "keyOn produces audio",
              num(rms(held, 2400, 9600)));
        check(e.vizGate && e.vizSemi == 12, "viz reports gate + semi 12");
        e.keyOff(12);
        auto tail = render(e, 48000);
        check(rms(tail, 40000, 48000) < 1e-4, "keyOff releases (0.08 s tail dies)",
              num(rms(tail, 40000, 48000)));
        check(!e.vizGate, "viz gate clears");
    }

    printf("\n== legato slide (last-note priority) ==\n");
    {
        BassEngine e;
        e.prepare(SR);
        e.setTables(tables);
        auto& p = e.params();
        p[BL_SLIDE_TIME] = 0.06f;
        p[BL_SUB_LEVEL] = 0;                 // clean single-osc pitch proxy
        p[BL_OSC_UNISON] = 1;
        e.keyOn(0, 0.9f);
        auto a = render(e, 24000);
        const int c0 = crossings(a, 12000, 24000);
        e.keyOn(12, 0.9f);                   // overlapping press = legato glide
        auto b = render(e, 24000);
        const int c1 = crossings(b, 12000, 24000);
        check(c1 > c0 * 3 / 2, "legato press glides pitch up an octave",
              num(c0) + " -> " + num(c1));
        // amp env never restarted: no silent gap across the transition
        check(rms(b, 0, 2400) > 1e-3, "no re-attack dip on the slide", num(rms(b, 0, 2400)));
        e.keyOff(12);
        auto back = render(e, 24000);
        const int c2 = crossings(back, 12000, 24000);
        check(c2 < c1, "release of the top key slides back to the held note",
              num(c1) + " -> " + num(c2));
        e.keyOff(0);
    }

    printf("\n== sequencer: step timing, gate frac, swing ==\n");
    {
        BassEngine e;
        e.prepare(SR);
        e.setTables(tables);
        auto pats = makeEmptyBassPatterns();
        BassSeqStep s; s.on = true; s.note = 0;
        setBassStep(pats.data(), 0, 0, s);
        e.setPatterns(pats.data(), (int)pats.size());
        int chain[] = { 0 };
        e.setChain(chain, 1);
        e.params()[BL_MASTER_SWING] = 0;

        const double dur = (60.0 / 138.0 / 4.0) * SR;   // ~5217 samples per 16th
        e.play();
        auto bar = render(e, (int)(dur * 16) + 64, 64);
        check(finite(bar), "bar output finite");
        check(rms(bar, 0, (int)(dur * BL_GATE_FRAC)) > 1e-3, "step 1 sounds");
        // gate closes at 0.55 * dur (classic 303 staccato) + 0.08 s release
        const int relEnd = (int)(dur * BL_GATE_FRAC + 0.08 * 4.5 * SR);
        check(rms(bar, relEnd, relEnd + 2000) < rms(bar, 0, 2000) * 0.1,
              "gate frac releases the step",
              num(rms(bar, relEnd, relEnd + 2000)) + " vs " + num(rms(bar, 0, 2000)));
        e.stop();
        check(e.currentStep() == -1, "stop resets step");

        // swing pushes odd steps late by swing * 0.667 * dur
        auto measureStep1 = [&](float swing) {
            BassEngine e2;
            e2.prepare(SR);
            e2.setTables(tables);
            e2.setPatterns(pats.data(), (int)pats.size());
            e2.setChain(chain, 1);
            e2.params()[BL_MASTER_SWING] = swing;
            e2.play();
            std::vector<std::pair<int, int>> stepAt;
            render(e2, (int)(dur * 3), 16, &stepAt);
            for (auto& [st, at] : stepAt) if (st == 1) return at;
            return -1;
        };
        const int plain = measureStep1(0.0f);
        const int swung = measureStep1(1.0f);
        check(plain > 0 && swung > 0, "step 2 fires in both runs");
        const double delta = swung - plain;
        check(std::abs(delta - BL_SWING_MAX * dur) < 64,
              "full swing delays step 2 by 0.667 * dur", num(delta));
    }

    printf("\n== accent: louder + brighter (worklet accent path) ==\n");
    {
        auto renderStep = [&](bool acc) {
            BassEngine e;
            e.prepare(SR);
            e.setTables(tables);
            auto pats = makeEmptyBassPatterns();
            BassSeqStep s; s.on = true; s.note = 0; s.acc = acc;
            setBassStep(pats.data(), 0, 0, s);
            e.setPatterns(pats.data(), (int)pats.size());
            int chain[] = { 0 };
            e.setChain(chain, 1);
            e.play();
            return render(e, 4800);
        };
        auto plain = renderStep(false);
        auto accented = renderStep(true);
        // vel 1.0 vs 0.72 and the acc.amt * 0.7 gain boost
        check(peak(accented) > peak(plain) * 1.2f, "accented step peaks louder",
              num(peak(plain)) + " -> " + num(peak(accented)));
        // accent raises the filter env peak -> more HF energy (crossings proxy)
        const int cp = crossings(plain, 0, 4800), ca = crossings(accented, 0, 4800);
        check(ca >= cp, "accented step at least as bright", num(cp) + " -> " + num(ca));
    }

    printf("\n== slide tie holds the gate through the step ==\n");
    {
        auto renderTwo = [&](bool tie) {
            BassEngine e;
            e.prepare(SR);
            e.setTables(tables);
            auto pats = makeEmptyBassPatterns();
            BassSeqStep s0; s0.on = true; s0.note = 0;
            setBassStep(pats.data(), 0, 0, s0);
            BassSeqStep s1; s1.on = tie; s1.note = 7; s1.slide = tie;
            setBassStep(pats.data(), 0, 1, s1);
            e.setPatterns(pats.data(), (int)pats.size());
            int chain[] = { 0 };
            e.setChain(chain, 1);
            e.params()[BL_MASTER_SWING] = 0;
            e.play();
            return render(e, 6400);
        };
        const double dur = (60.0 / 138.0 / 4.0) * SR;
        auto cut = renderTwo(false);
        auto tied = renderTwo(true);
        // just before the step-2 boundary the tied run must still be sounding
        const int a = (int)(dur * 0.9), b = (int)(dur * 0.99);
        check(rms(tied, a, b) > rms(cut, a, b) * 2,
              "tied step keeps sounding across the gate-frac point",
              num(rms(cut, a, b)) + " -> " + num(rms(tied, a, b)));
    }

    printf("\n== host transport lock ==\n");
    {
        BassEngine e;
        e.prepare(SR);
        e.setTables(tables);
        const auto& acid = bassFactoryPatches()[0];
        e.setPatterns(acid.patterns.data(), (int)acid.patterns.size());
        int chain[] = { 0 };
        e.setChain(chain, 1);
        // roll 2 s at 120 bpm from ppq 0 — 8 steps/s
        const int block = 512;
        const double ppqInc = block / SR * 2.0;
        double ppq = 0;
        int stepChanges = 0, last = -1;
        std::vector<float> L(block), R(block);
        for (int i = 0; i < (int)(2.0 * SR / block); i++) {
            e.setBpmOverride(120.0);
            e.setHostTransport(ppq, 120.0, true);
            e.render(L.data(), R.data(), block);
            ppq += ppqInc;
            if (e.currentStep() != last) { last = e.currentStep(); stepChanges++; }
        }
        check(e.isPlaying(), "host transport counts as playing");
        check(stepChanges >= 13 && stepChanges <= 19,
              "steps follow host position (~16 in 2 s @ 120)", num(stepChanges));
        e.setHostTransport(ppq, 120.0, false);      // host stop
        e.render(L.data(), R.data(), block);
        check(!e.isPlaying() && e.currentStep() == -1, "host stop stops the sequencer");
    }

    printf("\n== BassFx (bass-synth.ts graph) ==\n");
    {
        BassFx fx;
        fx.prepare(SR);
        auto p = defaultBassParams();
        fx.setParams(p);
        // impulse burst through the default chain (drive + reverb on)
        const int n = 48000;
        std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
        for (int i = 0; i < 400; i++) { L[(size_t)i] = 0.5f; R[(size_t)i] = 0.5f; }
        fx.process(L.data(), R.data(), n);
        check(finite(L) && finite(R), "fx output finite");
        check(rms(L, 0, 1200) > 1e-3, "burst passes through", num(rms(L, 0, 1200)));
        check(rms(L, 12000, 24000) > 1e-6, "reverb leaves a tail",
              num(rms(L, 12000, 24000)));
        check(peak(L) < 1.5f, "limiter bounds the output", num(peak(L)));

        // master volume 0 mutes (after the smoother settles)
        p[BL_MASTER_VOLUME] = 0;
        fx.setParams(p);
        std::vector<float> L2((size_t)n, 0.25f), R2((size_t)n, 0.25f);
        fx.process(L2.data(), R2.data(), n);
        check(rms(L2, 24000, 48000) < 1e-4, "volume 0 mutes", num(rms(L2, 24000, 48000)));
    }

    printf("%s\n", g_fail == 0 ? "BASS ENGINE CHECKS PASSED" : "BASS ENGINE CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
