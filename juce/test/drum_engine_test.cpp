// Headless verification harness for the DR-1 drum DSP core (no JUCE).
// Mirrors test/engine_test.cpp's style; the lockstep reference is the web
// app under src/drum/ (params.ts, worklet-drum.js, drumtables.ts, kits.ts).
//
// Exits non-zero if any check fails.
#include "../source/drum/dsp/DrumParams.h"
#include "../source/drum/dsp/DrumTables.h"
#include "../source/drum/dsp/DrumEngine.h"
#include "../source/drum/dsp/DrumFx.h"
#include "../source/drum/dsp/DrumKits.h"

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

// Windowed projection onto a single frequency — magnitude in dB. Used for
// relative harmonic measurements (both tones share the same window).
static double toneDb(const float* x, int N, double sr, double f) {
    double re = 0, im = 0;
    for (int i = 0; i < N; i++) {
        double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1)); // Hann
        double ph = 2 * M_PI * f * i / sr;
        re += x[i] * w * std::cos(ph);
        im -= x[i] * w * std::sin(ph);
    }
    return 20 * std::log10(std::sqrt(re * re + im * im) + 1e-30);
}

static std::vector<float> slice(const std::vector<float>& v, int a, int b) {
    return std::vector<float>(v.begin() + a, v.begin() + b);
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

    printf("\n== 4b. Host transport lock ==\n");
    {
        // Host-locked mode: setHostTransport(ppq, bpm, playing) per block drives
        // the sequencer from song position — the internal play() is not used.
        DrumEngine he; he.prepare(48000); he.setTables(allTables());
        std::vector<uint8_t> pats(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
        auto pidx = [](int pat, int padI, int step) {
            return pat * DR_NPADS * DR_STEPS + padI * DR_STEPS + step;
        };
        // Render in host mode: feed ppq block-by-block like a DAW would.
        // Returns MAIN L. blockSize deliberately not a divisor of step lengths.
        auto renderHost = [](DrumEngine& e, double startPpq, double bpm, int total,
                             bool playing = true, int block = 480) {
            std::vector<float> acc; acc.reserve((size_t)total);
            double ppq = startPpq;
            const double ppqPerSample = bpm / 60.0 / 48000.0;
            int done = 0;
            while (done < total) {
                int n = std::min(block, total - done);
                e.setHostTransport(ppq, bpm, playing);
                std::vector<float> bufs[DR_NBUSES][2];
                float* outs[DR_NBUSES][2];
                for (int b = 0; b < DR_NBUSES; b++)
                    for (int c = 0; c < 2; c++) {
                        bufs[b][c].assign((size_t)n, 0.0f);
                        outs[b][c] = bufs[b][c].data();
                    }
                e.render(outs, n);
                acc.insert(acc.end(), bufs[0][0].begin(), bufs[0][0].end());
                ppq += n * ppqPerSample;
                done += n;
            }
            return acc;
        };

        pats[pidx(0, 0, 0)] = 2;
        pats[pidx(0, 0, 4)] = 1;
        he.setPatterns(pats.data(), (int)pats.size());
        he.setParam(DG_MASTER_SWING, 0.0f);
        he.setParam(dpid(0, DP_AENV_DEC), 0.02f);
        he.setParam(dpid(1, DP_AENV_DEC), 0.02f);

        // 1. host playing drives steps without play(); bpm 120 -> 6000-sample steps
        auto hout = renderHost(he, 0.0, 120.0, 48000);
        auto hon = onsets(hout);
        check(hon.size() == 2 && near(hon[0], 0, 64) && near(hon[1], 24000, 64),
              "host-locked: steps 0 and 4 from ppq", onsetsStr(hon));
        check(he.isPlaying(), "isPlaying true while host plays");

        // 2. host stop: sequencer stops, step resets
        auto hstop = renderHost(he, 2.0, 120.0, 24000, /*playing=*/false);
        check(onsets(hstop).empty(), "no steps while host stopped");
        check(!he.isPlaying() && he.currentStep() == -1, "host stop resets step");

        // 3. position lock: starting at ppq 0.9 puts step 4 (ppq 1.0) at sample 2400
        auto hmid = renderHost(he, 0.9, 120.0, 12000);
        auto hmon = onsets(hmid);
        check(hmon.size() == 1 && near(hmon[0], 2400, 64),
              "host-locked: mid-bar start lands step 4 at ppq 1.0", onsetsStr(hmon));
        renderHost(he, 3.9, 120.0, 4800, false);   // stop + let tails die

        // 4. swing in host mode: odd step 1 at ppq 0.25 + 1*0.667*0.25 = 0.41675
        std::fill(pats.begin(), pats.end(), 0);
        pats[pidx(0, 0, 1)] = 1;
        he.setPatterns(pats.data(), (int)pats.size());
        he.setParam(DG_MASTER_SWING, 1.0f);
        auto hsw = renderHost(he, 0.0, 120.0, 18000);
        auto hswon = onsets(hsw);
        check(hswon.size() == 1 && near(hswon[0], 10002, 64),
              "host-locked swing delays odd step", onsetsStr(hswon));
        he.setParam(DG_MASTER_SWING, 0.0f);
        renderHost(he, 5.0, 120.0, 4800, false);

        // 5. chain position derives from the bar index: chain {0,1}, pattern B
        //    has pad1 at step 0 -> starting at bar 1 (ppq 4.0) fires it at 0
        std::fill(pats.begin(), pats.end(), 0);
        pats[pidx(1, 1, 0)] = 2;
        he.setPatterns(pats.data(), (int)pats.size());
        const int chAB[2] = { 0, 1 };
        he.setChain(chAB, 2);
        auto hbar = renderHost(he, 4.0, 120.0, 12000);
        auto hbon = onsets(hbar);
        check(hbon.size() == 1 && near(hbon[0], 0, 64),
              "chain pattern from host bar index", onsetsStr(hbon));
        check(he.currentPattern() == 1, "currentPattern follows host bar");
        renderHost(he, 8.0, 120.0, 4800, false);

        // 6. loop wrap: jumping back to ppq 4.0 re-fires the step, exactly once
        auto hloop1 = renderHost(he, 4.0, 120.0, 6000);
        auto hloop2 = renderHost(he, 4.0, 120.0, 6000);   // host looped back
        check(onsets(hloop1).size() == 1 && onsets(hloop2).size() == 1,
              "loop jump resyncs without double-fire");
        renderHost(he, 8.0, 120.0, 4800, false);

        // 7. host playing suppresses the internal transport: play() then host
        //    start + stop leaves the sequencer stopped (host owns the clock)
        const int chA[1] = { 0 };
        he.setChain(chA, 1);
        std::fill(pats.begin(), pats.end(), 0);
        pats[pidx(0, 0, 0)] = 2;
        he.setPatterns(pats.data(), (int)pats.size());
        he.play();
        renderHost(he, 0.0, 120.0, 12000);                 // host takes over
        renderHost(he, 0.25 * 12000 / 24000.0 + 0.5, 120.0, 4800, false);
        check(!he.isPlaying(), "host stop leaves sequencer stopped despite prior play()");
        check(onsets(renderHost(he, 1.0, 120.0, 24000, false)).empty(),
              "internal clock does not free-run after host stop");

        // play() pressed WHILE the host is rolling is ignored too — the host
        // owns the transport for as long as it reports playing
        renderHost(he, 0.0, 120.0, 4800);
        he.play();
        renderHost(he, 0.1 + 0.0, 120.0, 100, false);   // host stops
        check(!he.isPlaying(),
              "play() during host roll does not arm the internal clock");

        // 8. pre-roll: negative ppq fires nothing until the song start
        auto hpre = renderHost(he, -0.5, 120.0, 24000);
        auto hpon = onsets(hpre);
        check(hpon.size() == 1 && near(hpon[0], 12000, 64),
              "negative ppq waits for song start", onsetsStr(hpon));
    }

    printf("\n== 5. DrumFx ==\n");
    const double fsr = 48000.0;
    // Every DrumFx test starts from all-stages-off (comp + reverb default ON).
    auto fxOff = [] {
        auto p = defaultDrumParams();
        p[DG_FXDRIVE_ON] = 0; p[DG_FXCOMP_ON] = 0; p[DG_FXCHORUS_ON] = 0;
        p[DG_FXDELAY_ON] = 0; p[DG_FXREVERB_ON] = 0;
        return p;
    };
    {
        // passthrough-ish: stages off, volume 0.78 -> gain = 0.78^2*1.6 times the
        // WebAudio-spec limiter makeup ((1/c(1))^0.6 at thr -8 dB ratio 14),
        // matching Fx.cpp / the web DynamicsCompressor limiter.
        DrumFx fx; fx.prepare(fsr);
        fx.setParams(fxOff());
        int n = (int)fsr;
        std::vector<float> L(n), R(n);
        for (int i = 0; i < n; i++)
            L[i] = R[i] = 0.1f * (float)std::sin(2 * M_PI * 1000.0 * i / fsr);
        double inRms = rms(L, n / 2);
        fx.process(L.data(), R.data(), n);
        double c1 = std::pow(1.0 / 0.398, 1.0 / 14.0 - 1.0);
        double expect = 0.78 * 0.78 * 1.6 * std::pow(1.0 / c1, 0.6);
        double got = rms(L, n / 2) / inRms;
        check(finite(L) && std::abs(got - expect) < expect * 0.03,
              "stages off: gain = vol^2 * 1.6 * limiter makeup",
              std::to_string(got) + " vs " + std::to_string(expect));
    }
    {
        // comp reduces crest factor: slow AM sine (0.04..0.34), thr -30 dB
        auto renderAm = [&](bool compOn) {
            DrumFx fx; fx.prepare(fsr);
            auto p = fxOff();
            if (compOn) {
                p[DG_FXCOMP_ON] = 1;
                p[DG_FXCOMP_THR] = -30.0f;
                p[DG_FXCOMP_GAIN] = 0.0f;
            }
            fx.setParams(p);
            int n = (int)(4 * fsr);
            std::vector<float> L(n), R(n);
            for (int i = 0; i < n; i++) {
                double t = i / fsr;
                double a = 0.19 + 0.15 * std::sin(2 * M_PI * 2.0 * t);
                L[i] = R[i] = (float)(a * std::sin(2 * M_PI * 500.0 * t));
            }
            fx.process(L.data(), R.data(), n);
            return slice(L, (int)fsr, n);   // skip smoother settle
        };
        auto amOff = renderAm(false), amOn = renderAm(true);
        double crOff = peak(amOff) / rms(amOff), crOn = peak(amOn) / rms(amOn);
        check(finite(amOn) && crOn < crOff * 0.9, "comp reduces crest factor",
              std::to_string(crOn) + " vs " + std::to_string(crOff));
    }
    {
        // drive on amt 1 mix 1: sine in -> 3rd harmonic rises above -40 dB rel f0
        double f0 = 200.0 * fsr / 32768.0;   // FFT-bin aligned, ~293 Hz
        auto thd3 = [&](bool driveOn) {
            DrumFx fx; fx.prepare(fsr);
            auto p = fxOff();
            if (driveOn) {
                p[DG_FXDRIVE_ON] = 1;
                p[DG_FXDRIVE_AMT] = 1.0f;
                p[DG_FXDRIVE_MIX] = 1.0f;
            }
            fx.setParams(p);
            int n = 2 * (int)fsr;
            std::vector<float> L(n), R(n);
            for (int i = 0; i < n; i++)
                L[i] = R[i] = 0.3f * (float)std::sin(2 * M_PI * f0 * i / fsr);
            fx.process(L.data(), R.data(), n);
            const float* tail = L.data() + (n - 32768);
            return toneDb(tail, 32768, fsr, 3 * f0) - toneDb(tail, 32768, fsr, f0);
        };
        double h3Clean = thd3(false), h3Drive = thd3(true);
        check(h3Clean < -60.0, "clean sine has no 3rd harmonic", std::to_string(h3Clean) + " dB");
        check(h3Drive > -40.0, "drive amt 1 adds 3rd harmonic > -40 dB", std::to_string(h3Drive) + " dB");
    }
    {
        // delay on: burst in -> echo lands at fx.delay.time (default 0.36 s)
        DrumFx fx; fx.prepare(fsr);
        auto p = fxOff();
        p[DG_FXDELAY_ON] = 1;   // time 0.36, fb 0.35, mix 0.15 defaults
        fx.setParams(p);
        std::vector<float> z((int)fsr, 0.0f), z2 = z;
        fx.process(z.data(), z2.data(), (int)z.size());   // settle time smoother
        int n = 24000;
        std::vector<float> L(n, 0.0f), R(n, 0.0f);
        for (int i = 0; i < 128; i++)
            L[i] = R[i] = 0.5f * (float)std::sin(2 * M_PI * 1000.0 * i / fsr);
        fx.process(L.data(), R.data(), n);
        auto quiet = slice(L, 10000, 16000);
        auto echo = slice(L, 16900, 17700);   // 0.36 s * 48k = 17280
        check(finite(L) && rms(echo) > 1e-5 && rms(echo) > 10 * rms(quiet),
              "delay echo at 0.36 s",
              "echo=" + std::to_string(rms(echo)) + " pre=" + std::to_string(rms(quiet)));
    }
    {
        // reverb on: burst in -> audible tail at 1 s, decayed away by 6 s
        DrumFx fx; fx.prepare(fsr);
        auto p = fxOff();
        p[DG_FXREVERB_ON] = 1;
        p[DG_FXREVERB_SIZE] = 0.8f;
        p[DG_FXREVERB_MIX] = 0.5f;
        fx.setParams(p);
        std::vector<float> z(24000, 0.0f), z2 = z;
        fx.process(z.data(), z2.data(), (int)z.size());
        int n = (int)(6.5 * fsr);
        std::vector<float> L(n, 0.0f), R(n, 0.0f);
        for (int i = 0; i < 240; i++)
            L[i] = R[i] = 0.8f * (float)std::sin(2 * M_PI * 1000.0 * i / fsr);
        fx.process(L.data(), R.data(), n);
        double tail1 = rms(slice(L, (int)fsr, (int)(1.2 * fsr)));
        double tail6 = rms(slice(L, (int)(6.0 * fsr), (int)(6.3 * fsr)));
        check(finite(L) && tail1 > 1e-6, "reverb tail alive at 1 s", std::to_string(tail1));
        check(tail6 < tail1 * 0.02, "reverb tail decayed by 6 s",
              std::to_string(tail6) + " vs " + std::to_string(tail1));
    }
    {
        // stress: every stage on, extreme settings, 2 s noise -> finite + bounded
        DrumFx fx; fx.prepare(fsr);
        auto p = defaultDrumParams();
        p[DG_MASTER_VOLUME] = 1.0f;
        p[DG_FXDRIVE_ON] = 1;  p[DG_FXDRIVE_AMT] = 1.0f;  p[DG_FXDRIVE_MIX] = 1.0f;
        p[DG_FXCOMP_ON] = 1;   p[DG_FXCOMP_THR] = -40.0f; p[DG_FXCOMP_GAIN] = 12.0f;
        p[DG_FXCHORUS_ON] = 1; p[DG_FXCHORUS_RATE] = 8.0f;
        p[DG_FXCHORUS_DEPTH] = 1.0f; p[DG_FXCHORUS_MIX] = 1.0f;
        p[DG_FXDELAY_ON] = 1;  p[DG_FXDELAY_TIME] = 0.02f;
        p[DG_FXDELAY_FB] = 0.92f; p[DG_FXDELAY_MIX] = 1.0f;
        p[DG_FXREVERB_ON] = 1; p[DG_FXREVERB_SIZE] = 1.0f; p[DG_FXREVERB_MIX] = 1.0f;
        fx.setParams(p);
        int n = 2 * (int)fsr;
        std::vector<float> L(n), R(n);
        uint32_t seed = 0x12345678u;
        for (int i = 0; i < n; i++) {
            seed = seed * 1664525u + 1013904223u;
            float w = (float)((double)seed / 4294967296.0 * 2.0 - 1.0);
            L[i] = 0.8f * w;
            seed = seed * 1664525u + 1013904223u;
            R[i] = 0.8f * (float)((double)seed / 4294967296.0 * 2.0 - 1.0);
        }
        fx.process(L.data(), R.data(), n);
        check(finite(L) && finite(R) && peak(L) < 4.0f && peak(R) < 4.0f,
              "all stages on: 2 s noise finite + bounded",
              "peak=" + std::to_string(std::max(peak(L), peak(R))));
    }

    printf("\n== 6. DrumKits ==\n");
    {
        const auto& kits = factoryKits();
        check(kits.size() == 3, "3 factory kits");
        check(kits[0].name == "TR-VOID" && kits[1].name == "ROOM ONE" && kits[2].name == "BITCRUSH",
              "kit names/order");

        // Every override pid resolves and its value is within [min,max].
        bool allResolve = true, allInRange = true;
        std::string badPid;
        for (const auto& kit : kits)
            for (const auto& [pid, v] : kit.params) {
                int id = drumIdFromString(pid);
                if (id < 0) { allResolve = false; badPid = kit.name + ":" + pid; continue; }
                const auto& pi = drumParamInfo()[(size_t)id];
                if (v < pi.min || v > pi.max) {
                    allInRange = false;
                    badPid = kit.name + ":" + pid + "=" + std::to_string(v);
                }
            }
        check(allResolve, "all override pids resolve", badPid);
        check(allInRange, "all override values within [min,max]", badPid);

        // Structural invariants shared by all kits.
        for (const auto& kit : kits) {
            check((int)kit.patterns.size() == DR_NPATTERNS * DR_NPADS * DR_STEPS,
                  kit.name + " patterns 4*16*16");
            check(kit.chain == std::vector<int>{0}, kit.name + " chain {0}");
            check(kit.padNames[0] == "KICK" && kit.padNames[15] == "GLITCH",
                  kit.name + " pad names");
        }

        // Kit values land on the right flat ids through applyKit.
        auto findOverride = [](const DrumKit& k, const std::string& pid, float fallback) {
            float out = fallback;
            for (const auto& [p, v] : k.params) if (p == pid) out = v;
            return out;
        };
        auto tv = applyKit(kits[0]);
        check(tv[DG_SEQ_BPM] == 126.0f, "TR-VOID bpm 126");
        check(tv[dpid(0, DP_OSCA_TUNE)] == -14.0f, "TR-VOID pad0 oscA.tune -14");
        check(tv[dpid(5, DP_CHOKE)] == 1.0f && tv[dpid(6, DP_CHOKE)] == 1.0f &&
              tv[dpid(5, DP_FLT_ON)] == 1.0f && tv[dpid(6, DP_FLT_ON)] == 1.0f,
              "TR-VOID hats choke 1 + flt.on");
        // Non-overridden params keep defaults.
        check(tv[dpid(1, DP_PAN)] == defaultDrumParams()[dpid(1, DP_PAN)],
              "applyKit keeps defaults elsewhere");

        // Patterns: pad 0 hits on steps 0/4/8/12, accent (2) on step 0 only.
        auto pidx = [](int pat, int padI, int step) {
            return pat * DR_NPADS * DR_STEPS + padI * DR_STEPS + step;
        };
        const auto& pats = kits[0].patterns;
        bool kickOk = pats[pidx(0, 0, 0)] == 2 && pats[pidx(0, 0, 4)] == 1 &&
                      pats[pidx(0, 0, 8)] == 1 && pats[pidx(0, 0, 12)] == 1;
        for (int s = 0; s < DR_STEPS; s++)
            if (s % 4 != 0 && pats[pidx(0, 0, s)] != 0) kickOk = false;
        check(kickOk, "TR-VOID kick on 0/4/8/12, accent on 0");

        auto ro = applyKit(kits[1]);
        check(ro[DG_SEQ_BPM] == 116.0f, "ROOM ONE bpm 116");
        check(findOverride(kits[1], "pad0.penv.amt", -999.0f) == 12.0f,
              "ROOM ONE penv.amt = round(22 * 0.55)");

        auto bc = applyKit(kits[2]);
        bool allGrit = true;
        for (int i = 0; i < DR_NPADS; i++)
            if (bc[dpid(i, DP_OSCA_TABLE)] != 3.0f) allGrit = false;
        check(allGrit && bc[DG_SEQ_BPM] == 140.0f, "BITCRUSH oscA.table 3 on all pads, bpm 140");

        // End to end: TR-VOID into a DrumEngine, one bar of audio on MAIN.
        DrumEngine ke; ke.prepare(48000); ke.setTables(allTables());
        ke.setParams(tv);
        ke.setPatterns(kits[0].patterns.data(), (int)kits[0].patterns.size());
        ke.setChain(kits[0].chain.data(), (int)kits[0].chain.size());
        ke.play();
        auto bar = renderMain(ke, 96000);   // 1 bar @ 126 bpm ~ 91429 samples
        check(finite(bar) && rms(bar) > 1e-4, "TR-VOID plays a bar on MAIN",
              "rms=" + std::to_string(rms(bar)));
        ke.stop();
    }

    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
