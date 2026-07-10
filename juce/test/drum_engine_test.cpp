// Headless verification harness for the DR-1 drum DSP core (no JUCE).
// Mirrors test/engine_test.cpp's style; the lockstep reference is the web
// app under src/drum/ (params.ts, worklet-drum.js, drumtables.ts, kits.ts).
//
// Exits non-zero if any check fails.
#include "../source/drum/dsp/DrumParams.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <string>

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

    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
