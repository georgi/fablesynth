// Headless verification harness for the DR-1 drum DSP core (no JUCE).
// Mirrors test/engine_test.cpp's style; the lockstep reference is the web
// app under src/drum/ (params.ts, worklet-drum.js, drumtables.ts, kits.ts).
//
// Exits non-zero if any check fails.
#include "../source/drum/dsp/DrumParams.h"
#include "../source/drum/dsp/DrumTables.h"
#include "../source/drum/dsp/DrumEngine.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include <string>

using namespace fable;

static int g_fail = 0;
static void check(bool cond, const std::string& name, const std::string& detail = "") {
    printf("  [%s] %s%s\n", cond ? "PASS" : "FAIL", name.c_str(),
           detail.empty() ? "" : ("  -> " + detail).c_str());
    if (!cond) g_fail++;
}

static bool finite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}
static double rms(const std::vector<float>& v, int start = 0) {
    double s = 0; int n = 0;
    for (int i = start; i < (int)v.size(); i++) { s += (double)v[i] * v[i]; n++; }
    return n ? std::sqrt(s / n) : 0;
}
static float peak(const std::vector<float>& v) {
    float p = 0; for (float x : v) p = std::max(p, std::abs(x)); return p;
}
// Sign changes in v[a..b) — a cheap pitch proxy for the pitch-env check.
static int crossings(const std::vector<float>& v, int a, int b) {
    int c = 0;
    for (int i = a + 1; i < b && i < (int)v.size(); i++)
        if ((v[i - 1] < 0.0f) != (v[i] < 0.0f)) c++;
    return c;
}
// Attack starts: samples where |x| exceeds thr after >= quiet quiet samples.
// The buffer start counts as quiet, so a hit at sample 0 is an onset.
static std::vector<int> onsets(const std::vector<float>& v, float thr = 1e-4f, int quiet = 1000) {
    std::vector<int> out;
    int q = quiet;
    for (int i = 0; i < (int)v.size(); i++) {
        if (std::fabs(v[i]) > thr) {
            if (q >= quiet) out.push_back(i);
            q = 0;
        } else {
            q++;
        }
    }
    return out;
}
static std::string onsetsStr(const std::vector<int>& o) {
    std::string s = "onsets:";
    for (int x : o) s += " " + std::to_string(x);
    return s;
}
static bool near(int a, int b, int tol) { return std::abs(a - b) <= tol; }

// ---- anti-aliasing measurement (copied from engine_test.cpp): ratio of ----
// ---- non-harmonic to harmonic energy over a Hann-windowed FFT          ----
static double aliasFloorDb(const float* x, int N, double sr, double f0) {
    std::vector<double> re(N), im(N, 0.0);
    for (int i = 0; i < N; i++) {
        double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1)); // Hann
        re[i] = x[i] * w;
    }
    fft(re.data(), im.data(), N, false);
    auto mag2 = [&](int k) { return re[k] * re[k] + im[k] * im[k]; };

    double binHz = sr / N;
    int halfWin = 6;                 // bins around each harmonic counted as "signal"
    int loBin = (int)(40 / binHz);   // ignore DC / sub-bass leakage
    std::vector<char> isHarm(N / 2, 0);
    for (int k = 1; k * f0 < sr * 0.5; k++) {
        int b = (int)std::round(k * f0 / binHz);
        for (int j = b - halfWin; j <= b + halfWin; j++)
            if (j >= 0 && j < N / 2) isHarm[j] = 1;
    }
    double harm = 0, alias = 0;
    for (int k = loBin; k < N / 2; k++) (isHarm[k] ? harm : alias) += mag2(k);
    if (harm <= 0) return 0;
    return 10 * std::log10(alias / harm);
}

// Full DR-1 table list: 4 drum tables followed by the 6 WT-1 procedural
// tables (matches DRUM_TABLE_NAMES). Built once — the pyramids are shared.
static const std::vector<TablePtr>& allTables() {
    static const std::vector<TablePtr> tabs = [] {
        std::vector<TablePtr> out;
        for (auto& t : generateDrumTables())
            out.push_back(std::make_shared<const GeneratedTable>(std::move(t)));
        for (auto& t : generateTables())
            out.push_back(std::make_shared<const GeneratedTable>(std::move(t)));
        return out;
    }();
    return tabs;
}

// Render n samples into all 5 stereo buses; returns 10 buffers indexed
// [bus*2 + ch]. render() zero-fills, so fresh vectors arrive untouched.
static std::vector<std::vector<float>> renderBuses(DrumEngine& e, int n) {
    std::vector<std::vector<float>> bufs(DR_NBUSES * 2);
    float* outs[DR_NBUSES][2];
    for (int b = 0; b < DR_NBUSES; b++)
        for (int c = 0; c < 2; c++) {
            bufs[b * 2 + c].assign(n, 0.0f);
            outs[b][c] = bufs[b * 2 + c].data();
        }
    e.render(outs, n);
    return bufs;
}
static std::vector<float> renderMain(DrumEngine& e, int n) {
    return renderBuses(e, n)[0];   // MAIN L
}

int main() {
    using namespace fable;
    // Silence unused-helper warnings until later tasks use them.
    (void)finite; (void)rms; (void)peak;

    printf("\n== 1. DrumParams ==\n");
    check(DPAD_NFIELDS == 48, "48 per-pad fields");
    check(DR_NUM_PARAMS == 788, "788 total params");
    const auto& info = drumParamInfo();
    check((int)info.size() == DR_NUM_PARAMS, "info covers all params");
    check(info[dpid(0, DP_OSCA_TABLE)].pid == "pad0.oscA.table", "pad0 table pid");
    check(info[dpid(3, DP_FLT_CUT)].pid == "pad3.flt.cut", "pad3 cut pid");
    check(info[DG_SEQ_BPM].pid == "seq.bpm", "bpm pid");
    check(info[DG_FXREVERB_MIX].pid == "fx.reverb.mix", "last pid");
    auto d = defaultDrumParams();
    check(d[DG_SEQ_BPM] == 126.0f, "bpm default 126");
    check(d[dpid(5, DP_OSCA_LEVEL)] == 0.75f && d[dpid(5, DP_OSCB_LEVEL)] == 0.0f, "osc level defaults");
    check(d[dpid(0, DP_AENV_DEC)] == 0.24f && d[dpid(0, DP_LVL)] == 0.8f, "env/lvl defaults");
    check(d[DG_FXCOMP_ON] == 1.0f && d[DG_FXREVERB_ON] == 1.0f, "comp+reverb default on");
    check(drumIdFromString("pad15.out") == dpid(15, DP_OUT), "idFromString");
    check(drumIdFromString("nope") == -1, "unknown id -> -1");
    // log-curve mapping identical to web: flt.cut min 20 max 20000, norm 0.5 -> sqrt(20*20000)
    const auto& cut = info[dpid(0, DP_FLT_CUT)];
    check(std::abs(normToValue(cut, 0.5f) - std::sqrt(20.0f * 20000.0f)) < 1.0f, "log curve midpoint");

    printf("\n== 2. Drum tables ==\n");
    auto dt = generateDrumTables();
    check(dt.size() == 4, "4 drum tables");
    check(dt[0].name == "THUD" && dt[1].name == "CRACK" && dt[2].name == "TINE" && dt[3].name == "GRIT", "names/order");
    // Web-reference overall peaks (vitest run of src/drum/engine/drumtables.ts):
    // buildUserTable normalizes each frame's mip 0 to 0.92; coarser mips share
    // that frame scale, so band-truncation overshoot can exceed 0.92.
    const float kWebPeaks[] = { 1.073356f, 0.922846f, 0.941088f, 1.245046f };
    for (int ti = 0; ti < (int)dt.size(); ti++) {
        auto& t = dt[ti];
        check(t.frames == FRAMES && t.size == SIZE && t.mips == MIPS, t.name + " geometry");
        check(finite(t.data) && (int)t.data.size() == t.frames * t.mips * t.size, t.name + " data valid");
        float pk0 = 0;  // peak over mip-0 slices only — the normalization target
        for (int f = 0; f < t.frames; f++)
            for (int i = 0; i < t.size; i++)
                pk0 = std::max(pk0, std::abs(t.data[(f * t.mips + 0) * t.size + i]));
        check(std::abs(pk0 - 0.92f) < 1e-4f, t.name + " mip0 normalized to 0.92", std::to_string(pk0));
        float pk = peak(t.data);
        check(std::abs(pk - kWebPeaks[ti]) < 2e-3f, t.name + " overall peak matches web", std::to_string(pk));
    }

    printf("\n== 3. DrumEngine voice path ==\n");
    {
        DrumEngine eng; eng.prepare(48000);
        eng.setTables(allTables());

        // silence without a trigger
        check(rms(renderMain(eng, 24000)) < 1e-6, "silent before trigger");

        // trigger produces audio; hit flags + viz publish
        eng.selectPad(0);
        eng.trigger(0, 1.0f);
        check(eng.consumeHits() == 1u, "hit flag set for pad 0");
        check(eng.consumeHits() == 0u, "hit flags consumed");
        auto head = renderMain(eng, 4800);
        check(finite(head) && rms(head) > 1e-4, "pad 0 trigger produces audio",
              "rms=" + std::to_string(rms(head)));
        check(eng.vizEnv > 0.0f && eng.vizA >= 0.0f, "viz publishes active pad",
              "env=" + std::to_string(eng.vizEnv) + " a=" + std::to_string(eng.vizA));

        // accent louder than plain (v2l default 0.6)
        eng.panic(); eng.trigger(1, DR_PLAIN_VEL);  double rp = rms(renderMain(eng, 12000));
        eng.panic(); eng.trigger(1, DR_ACCENT_VEL); double ra = rms(renderMain(eng, 12000));
        check(ra > rp * 1.05, "accent > plain", std::to_string(ra) + " vs " + std::to_string(rp));

        // amp env: one-shot AHD decays to silence (default dec 0.24 s)
        eng.panic(); eng.trigger(0, 1.0f); renderMain(eng, 48000);
        check(rms(renderMain(eng, 48000)) < 1e-5, "voice ends after AHD");

        // pitch env: +24 st -> higher zero-crossing rate early vs late
        eng.setParam(dpid(2, DP_PENV_AMT), 24.0f);
        eng.setParam(dpid(2, DP_PENV_DEC), 0.2f);
        eng.setParam(dpid(2, DP_AENV_DEC), 2.0f);
        eng.panic(); eng.trigger(2, 1.0f);
        auto pev = renderMain(eng, 16000);
        int early = crossings(pev, 0, 2400), late = crossings(pev, 12000, 14400);
        check(early * 2 > late * 3, "pitch env raises early pitch",
              std::to_string(early) + " vs " + std::to_string(late));

        // filter: LP at 100 Hz kills a +24 st osc
        eng.setParam(dpid(3, DP_OSCA_TUNE), 24.0f);
        eng.panic(); eng.trigger(3, 1.0f); double rOff = rms(renderMain(eng, 12000));
        eng.setParam(dpid(3, DP_FLT_ON), 1.0f);
        eng.setParam(dpid(3, DP_FLT_CUT), 100.0f);
        eng.panic(); eng.trigger(3, 1.0f); double rOn = rms(renderMain(eng, 12000));
        check(rOn < rOff * 0.5 && rOff > 1e-4, "LP 100 Hz attenuates",
              std::to_string(rOn) + " vs " + std::to_string(rOff));

        // mod matrix: MOD ENV -> CUTOFF (amt -1 = -5 octaves) changes the output
        eng.setParam(dpid(6, DP_FLT_ON), 1.0f);
        eng.setParam(dpid(6, DP_FLT_CUT), 2000.0f);
        eng.setParam(dpid(6, DP_MOD1_SRC), 1.0f);   // MOD ENV
        eng.setParam(dpid(6, DP_MOD1_DST), 4.0f);   // CUTOFF
        eng.panic(); eng.trigger(6, 1.0f); auto m0 = renderMain(eng, 12000);
        eng.setParam(dpid(6, DP_MOD1_AMT), -1.0f);
        eng.panic(); eng.trigger(6, 1.0f); auto m1 = renderMain(eng, 12000);
        double dsum = 0;
        for (int i = 0; i < 12000; i++) { double d = (double)m0[i] - m1[i]; dsum += d * d; }
        check(std::sqrt(dsum / 12000) > 1e-4, "mod env -> cutoff moves filter",
              std::to_string(std::sqrt(dsum / 12000)));

        // choke: pads 4+5 in group 1; triggering 5 silences 4's long tail
        eng.panic();
        eng.setParam(dpid(4, DP_CHOKE), 1.0f); eng.setParam(dpid(5, DP_CHOKE), 1.0f);
        eng.setParam(dpid(4, DP_AENV_DEC), 2.0f);
        eng.trigger(4, 1.0f); renderMain(eng, 4800);
        eng.trigger(5, 1.0f);
        eng.setParam(dpid(5, DP_OSCA_LEVEL), 0.0f);   // mute 5 so we only hear 4's tail
        auto tail = renderMain(eng, 4800);
        check(rms(tail, 2400) < 1e-4, "choke silences group peer",
              std::to_string(rms(tail, 2400)));
    }
    {
        // determinism: two identical engines produce identical output (seeded Rng)
        DrumEngine e1, e2; e1.prepare(48000); e2.prepare(48000);
        e1.setTables(allTables()); e2.setTables(allTables());
        e1.trigger(0, 1); e2.trigger(0, 1);
        auto o1 = renderMain(e1, 12000), o2 = renderMain(e2, 12000);
        bool same = true;
        for (int i = 0; i < 12000; i++) if (o1[i] != o2[i]) { same = false; break; }
        check(same, "deterministic render (seeded RNG)");
    }
    {
        // NaN/denormal scan: all 16 pads at once, 2 s, every bus finite + bounded
        DrumEngine ne; ne.prepare(48000); ne.setTables(allTables());
        for (int i = 0; i < DR_NPADS; i++) ne.trigger(i, 1.0f);
        check(ne.consumeHits() == 0xFFFFu, "all 16 hit flags set");
        auto bufs = renderBuses(ne, 96000);
        bool ok = true; float pk = 0;
        for (auto& b : bufs) { ok = ok && finite(b); pk = std::max(pk, peak(b)); }
        check(ok && pk < 4.0f, "16-pad render finite + bounded", "peak=" + std::to_string(pk));
    }
    {
        // anti-aliasing: GRIT (worst case) at +24 st, no filter/noise -> mips wired
        DrumEngine ae; ae.prepare(48000); ae.setTables(allTables());
        ae.setParam(dpid(0, DP_OSCA_TABLE), 3.0f);    // GRIT
        ae.setParam(dpid(0, DP_OSCA_TUNE), 24.0f);
        ae.setParam(dpid(0, DP_OSCA_DETUNE), 0.0f);
        ae.setParam(dpid(0, DP_AENV_DEC), 4.0f);      // long tail for a clean FFT
        ae.setParam(dpid(0, DP_AENV_CURVE), 0.0f);
        ae.trigger(0, 1.0f);
        auto ab = renderMain(ae, 48000);
        double f0 = 440.0 * std::pow(2.0, (60 + 24 - 69) / 12.0);
        double fdb = aliasFloorDb(ab.data() + 4800, 32768, 48000, f0);
        check(fdb < -55.0, "GRIT +24st alias floor < -55 dB", std::to_string(fdb) + " dB");
    }

    printf("\n== 4. Sequencer ==\n");
    {
        // timing: bpm 120 @ 48k -> 6000-sample steps; pad0 on steps 0 (accent) and 4
        DrumEngine se; se.prepare(48000); se.setTables(allTables());
        std::vector<uint8_t> pats(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
        auto pidx = [](int pat, int padI, int step) {
            return pat * DR_NPADS * DR_STEPS + padI * DR_STEPS + step;
        };
        pats[pidx(0, 0, 0)] = 2;
        pats[pidx(0, 0, 4)] = 1;
        se.setPatterns(pats.data(), (int)pats.size());
        se.setParam(DG_SEQ_BPM, 120.0f);
        se.setParam(DG_MASTER_SWING, 0.0f);
        se.setParam(dpid(0, DP_AENV_DEC), 0.02f);   // short blips -> measurable onsets
        se.setParam(dpid(1, DP_AENV_DEC), 0.02f);
        check(!se.isPlaying() && se.currentStep() == -1, "stopped by default");
        se.play();
        check(se.isPlaying(), "isPlaying after play()");
        auto sout = renderMain(se, 48000);
        auto on = onsets(sout);
        check(on.size() == 2 && near(on[0], 0, 64) && near(on[1], 24000, 64),
              "bpm 120: onsets at steps 0 and 4", onsetsStr(on));
        check(se.currentStep() == 7, "currentStep after 8 steps",
              std::to_string(se.currentStep()));

        // swing 1 delays odd steps by 0.667 * 6000: pad on step 1 fires at 6000+4002
        std::fill(pats.begin(), pats.end(), 0);
        pats[pidx(0, 0, 1)] = 1;
        se.setPatterns(pats.data(), (int)pats.size());
        se.setParam(DG_MASTER_SWING, 1.0f);
        se.play();
        sout = renderMain(se, 18000);
        on = onsets(sout);
        check(on.size() == 1 && near(on[0], 10002, 64),
              "swing 1 delays odd step by 0.667*dur", onsetsStr(on));

        // chain [0,1]: bar 1 = empty pattern A, bar 2 = pattern B (pad1 step 0)
        std::fill(pats.begin(), pats.end(), 0);
        pats[pidx(1, 1, 0)] = 2;
        se.setPatterns(pats.data(), (int)pats.size());
        se.setParam(DG_MASTER_SWING, 0.0f);
        const int chainAB[2] = { 0, 1 };
        se.setChain(chainAB, 2);
        se.play();
        check(se.currentPattern() == 0, "chain starts at pattern A");
        sout = renderMain(se, 16 * 6000 + 12000);
        on = onsets(sout);
        check(on.size() == 1 && near(on[0], 96000, 64),
              "chain advances to pattern B in bar 2", onsetsStr(on));
        check(se.currentPattern() == 1, "currentPattern reports chain[1]");

        // stop(): no further steps fire; step query resets
        se.stop();
        check(!se.isPlaying() && se.currentStep() == -1, "stop resets step to -1");
        check(rms(renderMain(se, 24000)) < 1e-6, "silent after stop");

        // bpm override 240 (beyond the 60..200 param clamp) -> 3000-sample steps
        std::fill(pats.begin(), pats.end(), 0);
        pats[pidx(0, 0, 0)] = 2;
        pats[pidx(0, 0, 4)] = 1;
        se.setPatterns(pats.data(), (int)pats.size());
        const int chainA[1] = { 0 };
        se.setChain(chainA, 1);
        se.setBpmOverride(240.0);
        se.play();
        sout = renderMain(se, 24000);
        on = onsets(sout);
        check(on.size() == 2 && near(on[0], 0, 64) && near(on[1], 12000, 64),
              "bpm override 240 halves the step", onsetsStr(on));

        // clearing the override (<= 0) restores DG_SEQ_BPM timing
        se.setBpmOverride(0.0);
        se.play();
        sout = renderMain(se, 48000);
        on = onsets(sout);
        check(on.size() == 2 && near(on[1], 24000, 64),
              "override <= 0 restores param bpm", onsetsStr(on));
        se.stop();
    }
    {
        // multi-out: pad 0 routed to AUX 2 lands on bus 2 only; MAIN stays silent
        DrumEngine me; me.prepare(48000); me.setTables(allTables());
        me.setParam(dpid(0, DP_OUT), 2.0f);
        me.trigger(0, 1.0f);
        auto bufs = renderBuses(me, 12000);
        check(rms(bufs[2 * 2 + 0]) > 1e-4 && rms(bufs[2 * 2 + 1]) > 1e-4,
              "pad routed to AUX 2", "rmsL=" + std::to_string(rms(bufs[2 * 2 + 0])));
        double leak = 0;
        for (int b = 0; b < DR_NBUSES; b++) {
            if (b == 2) continue;
            leak = std::max(leak, rms(bufs[b * 2 + 0]));
            leak = std::max(leak, rms(bufs[b * 2 + 1]));
        }
        check(leak < 1e-6, "other buses silent (incl. MAIN)", std::to_string(leak));
    }

    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
