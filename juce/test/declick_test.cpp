// Headless declick regression test for WT-1, DR-1 and BL-1 (no JUCE).
//
// Every factory preset of every instrument is rendered in small, irregular
// host blocks (a note landing near the end of a short block once exposed an
// amp-gate step). For each musically relevant event (note on, gate off,
// retrigger during release, voice steal, choke, voice end) the largest
// second difference |x[i] - 2x[i-1] + x[i-2]| in a short window around the
// event is divided by a reference taken from the same render: the 99.9th
// percentile of d2 over the audible part. The ratio is scale free, so a
// bright, distorted preset (high steady-state d2) and a soft preset share one
// threshold. A step in the signal puts a spike in d2 that is many times the
// reference; the declick slews keep the ratio low.
//
// Set DECLICK_REPORT=1 to print every preset's worst ratio per event kind.
// Exits non-zero if any check fails.
#include "../source/bass/dsp/BassParams.h"
#include "../source/bass/dsp/BassEngine.h"
#include "../source/bass/dsp/BassPatches.h"
#include "../source/drum/dsp/DrumEngine.h"
#include "../source/drum/dsp/DrumKits.h"
#include "../source/drum/dsp/DrumParams.h"
#include "../source/drum/dsp/DrumPatches.h"
#include "../source/drum/dsp/DrumTables.h"
#include "../source/drum/dsp/SampledTables.gen.h"
#include "../source/dsp/Engine.h"
#include "../source/dsp/Params.h"
#include "../source/dsp/Presets.h"
#include "../source/dsp/Wavetables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
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
static std::string num(double v, const char* f = "%.3f") { char b[64]; snprintf(b, sizeof b, f, v); return b; }

static const double SR = 48000.0;
static const int BLOCKS[] = {8, 13, 64, 128, 37};   // irregular host block cycle
static const bool REPORT = std::getenv("DECLICK_REPORT") != nullptr;

// Worst allowed window/reference ratio per event kind, set between what the
// current engines reach and what the reverted fixes reach (see ratios printed
// by DECLICK_REPORT=1). The voice-end limits are |last sample| / local peak.
//  BL-1: unconditional gain reset reaches ~220 on note on.
//  WT-1: no amp slew reaches 2.1 on note on, 1.3 on retrigger, 7.5 on a steal.
//  DR-1: no choke slew reaches 27 on retrigger/choke.
// BL-1 voice end: without dcTail the DC-blocker cut reaches 0.75; stopping the
// voice before the amp slew is silent (zero release) left a 26% step.
static const double BL_LIM_N = 60, BL_LIM_G = 30, BL_LIM_Z = 0.01;
static const double WT_LIM_N = 1.6, WT_LIM_F = 4, WT_LIM_R = 0.9, WT_LIM_S = 4, WT_LIM_E = 4, WT_LIM_Z = 0.01;
static const double DR_LIM_H = 4, DR_LIM_R = 18, DR_LIM_C = 18, DR_LIM_Z = 0.01;

// Event kinds. The window covers the block the event lands in.
struct Ev { int a, b; char kind; };

static double d2At(const std::vector<float>& x, int i) {
    return std::fabs((double)x[(size_t)i] - 2.0 * x[(size_t)i - 1] + x[(size_t)i - 2]);
}

// Reference d2 for one event: the larger of
//  - the 99.9th percentile of d2 over the audible samples of the render (what
//    a bright or distorted preset does all the time), and
//  - AMP_FLOOR x the local peak |x| around the event. A clean low sub has
//    almost no d2 of its own, and its legitimate slewed edge (0.3 ms) only
//    reaches a fraction of a percent of the amplitude. An unslewed step of
//    height h gives d2 = h (times the waveform phase at the edge), which is
//    several times larger. The per-kind limits sit between the two.
static const double AMP_FLOOR = 0.002;
static double steadyD2(const std::vector<float>& x) {
    std::vector<float> v;
    for (int i = 2; i < (int)x.size(); i++)
        if (std::fabs(x[(size_t)i]) > 1e-5f || std::fabs(x[(size_t)i - 1]) > 1e-5f)
            v.push_back((float)d2At(x, i));
    if (v.empty()) return 0;
    size_t k = std::min(v.size() - 1, (size_t)((double)v.size() * 0.999));
    std::nth_element(v.begin(), v.begin() + (long)k, v.end());
    return v[k];
}
static double localPeak(const std::vector<float>& x, int a, int b) {
    double m = 0;
    for (int i = std::max(0, a - 2400); i < std::min((int)x.size(), b + 4800); i++) m = std::max(m, (double)std::fabs(x[(size_t)i]));
    return m;
}

static double windowMax(const std::vector<float>& x, int a, int b) {
    double m = 0;
    for (int i = std::max(2, a - 16); i < std::min((int)x.size(), b + 24); i++) m = std::max(m, d2At(x, i));
    return m;
}

// Per instrument: worst ratio per kind, with the preset that produced it.
struct Worst { double ratio = 0; std::string preset; int at = 0; int count = 0; };
using WorstMap = std::map<char, Worst>;

// Voice end: where the output drops into silence (an idle voice renders
// nothing, a DC-blocker tail decays below SILENT) the last sample before it must already have faded out. A voice cut
// while its amp slew or DC-blocker tail is still ringing leaves a step of
// that size. Reported as |last sample| / local peak.
static const float SILENT = 1e-6f;
static void settleEvents(const std::vector<float>& x, std::vector<std::pair<int, double>>& out) {
    const int n = (int)x.size();
    for (int i = 1; i + 16 < n; i++) {
        if (std::fabs(x[(size_t)i]) >= SILENT || std::fabs(x[(size_t)i - 1]) < SILENT) continue;
        int z = 0;
        while (i + z < n && std::fabs(x[(size_t)(i + z)]) < SILENT && z < 16) z++;
        if (z < 16) continue;
        double pk = 0;
        for (int k = std::max(0, i - 4800); k < i; k++) pk = std::max(pk, (double)std::fabs(x[(size_t)k]));
        if (pk > 1e-3) out.push_back({i, std::fabs(x[(size_t)i - 1]) / pk});
    }
}

static void scoreRender(const std::string& preset, const std::vector<float>& x,
                        const std::vector<Ev>& evs, WorstMap& w) {
    const double steady = steadyD2(x);
    std::map<char, double> local;
    std::vector<std::pair<int, double>> settle;
    settleEvents(x, settle);
    for (auto& [at, r] : settle) {
        if (REPORT && r > 0.1) {
            printf("    voice end %s @%d: ", preset.c_str(), at);
            for (int i = std::max(0, at - 24); i < std::min((int)x.size(), at + 6); i++) printf("%d ", (int)(x[(size_t)i] * 32767));
            printf("\n");
        }
        auto& ww = w['Z'];
        ww.count++;
        if (r > ww.ratio) { ww.ratio = r; ww.preset = preset; ww.at = at; }
        local['Z'] = std::max(local['Z'], r);
    }
    for (const auto& e : evs) {
        double ref = std::max({steady, AMP_FLOOR * localPeak(x, e.a, e.b), 1e-6});
        double r = windowMax(x, e.a, e.b) / ref;
        auto& ww = w[e.kind];
        ww.count++;
        if (r > ww.ratio) { ww.ratio = r; ww.preset = preset; ww.at = e.a; }
        local[e.kind] = std::max(local[e.kind], r);
    }
    if (REPORT) {
        printf("    %-26s steady d2 %.5f", preset.c_str(), steady);
        for (auto& [k, r] : local) printf("  %c %.2f", k, r);
        printf("\n");
    }
}

static void verdict(const char* inst, const WorstMap& w, const std::map<char, double>& limit,
                    const std::map<char, std::string>& label, int presets) {
    int events = 0;
    for (auto& [k, ww] : w) events += ww.count;
    printf("  %s: %d presets, %d events\n", inst, presets, events);
    for (auto& [k, lim] : limit) {
        auto it = w.find(k);
        if (it == w.end() || it->second.count == 0) {
            check(false, std::string(inst) + " " + label.at(k) + ": events were exercised", "none seen");
            continue;
        }
        const auto& ww = it->second;
        check(ww.ratio < lim, std::string(inst) + " " + label.at(k) + " has no click",
              "worst " + num(ww.ratio, "%.2f") + "x ref (limit " + num(lim, "%.1f") + ") in " + ww.preset +
              " @" + std::to_string(ww.at) + ", " + std::to_string(ww.count) + " events");
    }
}

// Renders `total` samples in irregular blocks. `before(at, blockLen)` runs
// ahead of each block, `after(at, blockLen)` right behind it.
template <class Before, class Render, class After>
static void blocked(int total, Before before, Render render, After after) {
    int ci = 0;
    for (int at = 0; at < total;) {
        int len = std::min(BLOCKS[ci++ % 5], total - at);
        before(at, len);
        render(at, len);
        after(at, len);
        at += len;
    }
}

// ------------------------------------------------------------------ BL-1
static void testBass() {
    printf("\n== BL-1: factory patches (internal sequencer) ==\n");
    std::vector<TablePtr> tables;
    for (auto& g : generateTables()) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    WorstMap w;
    int presets = 0;
    // Pass 1 plays each patch as authored; pass 2 forces zero amp attack and release.
    for (int pass = 0; pass < 2; pass++)
    for (const auto& p : bassFactoryPatches()) {
        BassEngine e; e.prepare(SR); e.setTables(tables);
        auto prm = applyBassPatch(p);
        if (pass) { prm[BL_AENV_ATT] = 0.0f; prm[BL_AENV_REL] = 0.0f; }
        e.setParams(prm); e.snapParams();
        e.setPatterns(p.patterns.data(), (int)p.patterns.size());
        e.setChain(p.chain.data(), (int)p.chain.size());
        e.play();
        int n = (int)(SR * 6);
        std::vector<float> L((size_t)n), R((size_t)n);
        std::vector<Ev> evs;
        bool gate = false; bool sounding = false;
        blocked(n, [](int, int) {},
                [&](int at, int len) { e.render(&L[(size_t)at], &R[(size_t)at], len); },
                [&](int at, int len) {
                    if (e.vizGate != gate) { evs.push_back({at, at + len, e.vizGate ? 'N' : 'G'}); gate = e.vizGate; }
                    bool s = e.vizSemi != -100;
                    if (sounding && !s) evs.push_back({at, at + len, 'E'});
                    sounding = s;
                });
        presets++;
        if (pass) evs.clear();                           // zero att/rel only probes the voice end
        scoreRender(p.name + (pass ? " (zero att/rel)" : ""), L, evs, w);
    }
    verdict("BL-1", w, {{'N', BL_LIM_N}, {'G', BL_LIM_G}, {'Z', BL_LIM_Z}},
            {{'N', "note on"}, {'G', "gate off"}, {'Z', "voice end"}}, presets);
}

// ------------------------------------------------------------------ WT-1
static void testWavetable() {
    printf("\n== WT-1: factory presets (note on/off, retrigger, overlap, voice steal) ==\n");
    std::vector<TablePtr> tables;
    for (auto& g : generateTables()) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    WorstMap w;
    int presets = 0;
    // Pass 1 plays each preset as authored. Pass 2 forces the amp envelope to
    // zero attack and release, where only the declick slew rounds the edges.
    for (int pass = 0; pass < 2; pass++)
    for (const auto& pr : factoryPresets()) {
        Engine e; e.prepare(SR); e.setTables(tables);
        auto prm = applyPreset(pr);
        if (pass) { prm[ENV1_BASE + 0] = 0.0f; prm[ENV1_BASE + 3] = 0.0f; }
        // This scenario is polyphonic by design (overlapping notes, a voice-
        // steal burst). Mono presets (master.mono, honored by the engine since
        // the WT-1 mono port) would fold the burst into one gliding voice; that
        // drops the render's d2 reference ~5x and inflates every ratio although
        // the audio around each event is unchanged. Score the poly behavior.
        prm[MASTER_MONO] = 0.0f;
        e.setParams(prm);
        const int n = (int)(SR * 3.0);
        std::vector<float> L((size_t)n), R((size_t)n);
        struct Act { int time; int note; bool on; char kind; };
        std::vector<Act> acts = {
            {2400, 57, true, 'N'}, {19200, 57, false, 'F'},
            {21600, 57, true, 'R'},                          // same note again during its release
            {28800, 64, true, 'N'},                          // second, overlapping note
        };
        for (int k = 0; k < NVOICES + 4; k++) acts.push_back({43200 + k * 150, 40 + 3 * k, true, 'S'});
        acts.push_back({72000, 57, false, 'F'}); acts.push_back({72000, 64, false, 'F'});
        for (int k = 0; k < NVOICES + 4; k++) acts.push_back({72000, 40 + 3 * k, false, 'F'});
        size_t next = 0;
        std::vector<Ev> evs;
        blocked(n,
                [&](int at, int len) {
                    while (next < acts.size() && acts[next].time <= at) {
                        const auto& a = acts[next++];
                        if (a.on) e.noteOn(a.note, 1.0); else e.noteOff(a.note);
                        if (a.kind == 'S' && !evs.empty() && evs.back().kind == 'S') evs.back().b = at + len;
                        else evs.push_back({at, at + len, a.kind});
                    }
                },
                [&](int at, int len) { e.render(&L[(size_t)at], &R[(size_t)at], len); },
                [](int, int) {});
        evs.push_back({72000 + 2000, n, 'E'});               // release tails until the voices go idle
        presets++;
        scoreRender(pr.name + (pass ? " (zero att/rel)" : ""), L, evs, w);
    }
    verdict("WT-1", w, {{'N', WT_LIM_N}, {'F', WT_LIM_F}, {'R', WT_LIM_R}, {'S', WT_LIM_S}, {'E', WT_LIM_E}, {'Z', WT_LIM_Z}},
            {{'N', "note on"}, {'F', "note off"}, {'R', "retrigger in release"}, {'S', "voice steal"},
             {'E', "release tail"}, {'Z', "voice end"}}, presets);
}

// --------------------------------------------------------------- WT-1 mono
// Factory presets with master.mono = 1, played as one voice: a retrigger of a
// still-sounding note (legato off), a legato glide (legato on), the fall back
// to a held note when the newer key lifts, and a note-on in release. The
// reference is single-voice: the 99.9th percentile d2 of the preset's own held
// note rendered alone: its largest d2 after the onset (plus the AMP_FLOOR x
// local peak floor), never the polyphonic burst of the scenario above.
static const double WTM_LIM_T = 1.2, WTM_LIM_G = 1.2, WTM_LIM_B = 1.2, WTM_LIM_L = 1.2;
static const double WTM_STEP_REF = 2.0; // injected one-sample step, in limit x reference units

static std::vector<float> renderMono(Engine& e, int n, const std::vector<std::pair<int, std::pair<int, bool>>>& acts,
                                     std::vector<Ev>* evs, const std::vector<char>& kinds) {
    std::vector<float> L((size_t)n), R((size_t)n);
    size_t next = 0;
    blocked(n,
            [&](int at, int len) {
                while (next < acts.size() && acts[next].first <= at) {
                    const auto& a = acts[next];
                    if (a.second.second) e.noteOn(a.second.first, 1.0); else e.noteOff(a.second.first);
                    if (evs && kinds[next] != '-') evs->push_back({at, at + len, kinds[next]});
                    ++next;
                }
            },
            [&](int at, int len) { e.render(&L[(size_t)at], &R[(size_t)at], len); },
            [](int, int) {});
    return L;
}

static void testWavetableMono() {
    printf("\n== WT-1 mono: retrigger, legato glide, fall back, note-on in release ==\n");
    std::vector<TablePtr> tables;
    for (auto& g : generateTables()) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    WorstMap w;
    int presets = 0;
    bool stepCaught = true;
    std::string stepMiss;
    for (const auto& pr : factoryPresets()) {
        const auto base = applyPreset(pr);
        if (base[MASTER_MONO] < 0.5f) continue;
        for (int legato = 0; legato < 2; ++legato) {
            auto prm = base;
            prm[MASTER_LEGATO] = (float)legato;
            // Single-voice reference per pitch: the preset's own note, held alone
            // (d2 grows with frequency squared, so each event is measured against
            // a held note at the pitch it lands on).
            std::map<int, double> ref0;
            for (int pitch : { 57, 60, 64 }) {
                Engine solo; solo.prepare(SR); solo.setTables(tables); solo.setParams(prm);
                const auto held = renderMono(solo, (int)(SR * 0.8), { {2400, {pitch, true}}, {(int)(SR * 0.6), {pitch, false}} },
                                             nullptr, { '-', '-' });
                // Largest d2 from note-on to note-off: the preset's own attack and
                // waveform at full level. A mono event must not be harsher than a
                // fresh note of the same preset and pitch (the poly scenario above
                // checks that note-on against the render).
                double m = 0;
                for (int i = 2400; i < (int)(SR * 0.6); i++) m = std::max(m, d2At(held, i));
                ref0[pitch] = m;
            }
            // Scenario. Kinds: T retrigger (legato off) / G glide (legato on),
            // B fall back to the held key, L note-on in release.
            const char move = legato ? 'G' : 'T';
            std::vector<std::pair<int, std::pair<int, bool>>> acts = {
                {2400, {57, true}}, {7200, {60, true}},      // second key while the first sounds
                {12000, {60, false}},                        // newer key lifts: back to 57
                {19200, {57, false}},                        // release
                {21600, {64, true}}, {28800, {64, false}},   // note-on in release
            };
            const std::vector<char> kinds = { '-', move, 'B', '-', 'L', '-' };
            Engine e; e.prepare(SR); e.setTables(tables); e.setParams(prm);
            std::vector<Ev> evs;
            const int n = (int)(SR * 1.2);
            auto x = renderMono(e, n, acts, &evs, kinds);
            const std::string name = pr.name + (legato ? " (legato)" : " (retrigger)");
            std::map<char, double> local;
            for (const auto& ev : evs) {
                const int pitch = ev.kind == 'B' ? 57 : ev.kind == 'L' ? 64 : 60;
                const double ref = std::max({ ref0[pitch], AMP_FLOOR * localPeak(x, ev.a, ev.b), 1e-6 });
                const double r = windowMax(x, ev.a, ev.b) / ref;
                auto& ww = w[ev.kind]; ww.count++;
                if (r > ww.ratio) { ww.ratio = r; ww.preset = name; ww.at = ev.a; }
                local[ev.kind] = std::max(local[ev.kind], r);
                // Sensitivity: the same render with a deliberate one-sample step
                // at the event must exceed the limit for its kind.
                const double lim = ev.kind == 'T' ? WTM_LIM_T : ev.kind == 'G' ? WTM_LIM_G : ev.kind == 'B' ? WTM_LIM_B : WTM_LIM_L;
                auto y = x;
                const double h = WTM_STEP_REF * lim * ref;
                for (int i = ev.a + 8; i < (int)y.size(); i++) y[(size_t)i] += (float)h;
                if (windowMax(y, ev.a, ev.b) / ref <= lim) {
                    stepCaught = false;
                    stepMiss = name + " " + std::string(1, ev.kind) + " @" + std::to_string(ev.a);
                    if (REPORT) printf("    step missed: %s (ratio %.2f)\n", stepMiss.c_str(), windowMax(y, ev.a, ev.b) / ref);
                }
            }
            if (REPORT) {
                printf("    %-30s held d2 %.5f/%.5f/%.5f", name.c_str(), ref0[57], ref0[60], ref0[64]);
                for (auto& [k, r] : local) printf("  %c %.2f", k, r);
                printf("\n");
            }
        }
        presets++;
    }
    check(presets > 0, "WT-1 mono: factory presets with master.mono exist", std::to_string(presets));
    check(stepCaught, "WT-1 mono: an injected one-sample step of 2x the limit is caught at every event", stepMiss.empty() ? "all events" : stepMiss);
    verdict("WT-1 mono", w, {{'T', WTM_LIM_T}, {'G', WTM_LIM_G}, {'B', WTM_LIM_B}, {'L', WTM_LIM_L}},
            {{'T', "retrigger of a sounding note"}, {'G', "legato glide"}, {'B', "fall back to the held key"},
             {'L', "note-on in release"}}, presets);
}

// ------------------------------------------------------------------ DR-1
static std::vector<TablePtr> drumTables() {
    std::vector<TablePtr> t;
    for (auto& g : generateDrumTables()) t.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    for (auto& g : generateTables()) t.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    for (auto& g : generateSampledDrumTables()) t.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    return t;
}

struct DrumOut {
    std::vector<std::vector<float>> bufs{(size_t)DR_NBUSES * 2, std::vector<float>(128)};
    float* outs[DR_NBUSES][2];
    DrumOut() { for (int b = 0; b < DR_NBUSES; b++) for (int c = 0; c < 2; c++) outs[b][c] = bufs[(size_t)b * 2 + c].data(); }
    void render(DrumEngine& e, float* dst, int len) {
        e.render(outs, len);
        for (int i = 0; i < len; i++) {
            float s = 0;
            for (int b = 0; b < DR_NBUSES; b++) s += bufs[(size_t)b * 2][(size_t)i];
            dst[i] = s;
        }
    }
};

static void testDrums() {
    printf("\n== DR-1: factory kits (internal sequencer) and pad patches (retrigger, choke) ==\n");
    auto tables = drumTables();
    WorstMap w;
    int presets = 0;
    for (const auto& kit : factoryKits()) {
        DrumEngine e; e.prepare(SR); e.setTables(tables);
        e.setParams(applyKit(kit));
        e.setPatterns(kit.patterns.data(), (int)kit.patterns.size());
        e.setChain(kit.chain.data(), (int)kit.chain.size());
        e.play();
        const int n = (int)(SR * 4);
        std::vector<float> x((size_t)n); DrumOut out;
        std::vector<Ev> evs; int lastStep = -1;
        blocked(n, [](int, int) {},
                [&](int at, int len) { out.render(e, &x[(size_t)at], len); },
                [&](int at, int len) {
                    int s = e.currentStep();
                    if (s == lastStep) return;
                    lastStep = s;
                    if (s < 0) return;
                    size_t base = (size_t)(e.currentPattern() * DR_NPADS * DR_STEPS);
                    bool hit = false;
                    for (int p = 0; p < DR_NPADS; p++) hit |= kit.patterns[base + (size_t)(p * DR_STEPS + s)] != 0;
                    if (hit) evs.push_back({at, at + len, 'H'});
                });
        presets++;
        scoreRender("kit " + kit.name, x, evs, w);
    }
    // Pad patches on pad 0 / 1 (one choke group). Scenario A uses the patch's
    // own envelope with quick retriggers; B forces a long ring so the
    // retrigger and choke always cut a sounding voice.
    for (int scen = 0; scen < 2; scen++) {
        for (const auto& patch : factoryPatches()) {
            DrumEngine e; e.prepare(SR); e.setTables(tables);
            auto p = defaultDrumParams();
            for (int pad : {0, 1}) {
                for (auto& [pid, v] : applyPatchToPad(pad, patch)) p[(size_t)pid] = v;
                p[(size_t)dpid(pad, DP_CHOKE)] = 1;
                if (scen == 1) { p[(size_t)dpid(pad, DP_AENV_DEC)] = 1.5f; p[(size_t)dpid(pad, DP_AENV_HOLD)] = 0.5f; }
            }
            e.setParams(p);
            const int gap = scen == 0 ? 960 : 12000;               // 20 ms vs 250 ms
            struct Act { int time; int pad; char kind; };
            std::vector<Act> acts = {{2400, 0, 'H'}, {2400 + gap, 0, 'R'}, {2400 + 2 * gap, 0, 'R'},
                                     {2400 + 3 * gap, 1, 'C'}, {2400 + 4 * gap, 1, 'R'}};
            const int n = 2400 + 4 * gap + (int)(SR * 0.4);
            std::vector<float> x((size_t)n); DrumOut out;
            size_t next = 0; std::vector<Ev> evs;
            blocked(n,
                    [&](int at, int len) {
                        while (next < acts.size() && acts[next].time <= at) {
                            e.trigger(acts[next].pad, 1.0f);
                            evs.push_back({at, at + len, acts[next].kind});
                            next++;
                        }
                    },
                    [&](int at, int len) { out.render(e, &x[(size_t)at], len); }, [](int, int) {});
            evs.erase(evs.begin());                          // the first hit starts from silence
            presets++;
            scoreRender("patch " + patch.name + (scen ? " (long ring)" : ""), x, evs, w);
        }
    }
    verdict("DR-1", w, {{'H', DR_LIM_H}, {'R', DR_LIM_R}, {'C', DR_LIM_C}, {'Z', DR_LIM_Z}},
            {{'H', "sequenced hit"}, {'R', "retrigger"}, {'C', "choke"}, {'Z', "voice end"}}, presets);
}

int main() {
    testBass();
    testWavetable();
    testWavetableMono();
    testDrums();
    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
