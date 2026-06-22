// Headless verification harness for the FableSynth DSP core (no JUCE).
// Exercises the audio engine the way the web app's Node smoke test + CDP audio
// check did: wavetable correctness, a live note producing audio, the Serum-style
// anti-aliasing floor, every filter type, the FX chain, and all factory presets.
//
// Exits non-zero if any check fails.
#include "../source/dsp/Engine.h"
#include "../source/dsp/Fx.h"
#include "../source/dsp/Wavetables.h"
#include "../source/dsp/Presets.h"
#include "../source/dsp/Params.h"
#include "../source/dsp/UserTables.h"
#include "../source/dsp/FrameOps.h"

#include <cmath>
#include <cstdio>
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

// Render a mono note through the bare engine (no FX) and return the buffer.
static std::vector<float> renderNote(Engine& eng, int note, double seconds, double sr) {
    int n = (int)(seconds * sr);
    std::vector<float> L(n, 0.0f), R(n, 0.0f);
    eng.noteOn(note, 1.0);
    eng.render(L.data(), R.data(), n);
    return L;
}

// ---- anti-aliasing measurement: ratio of non-harmonic to harmonic energy ----
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

int main() {
    const double sr = 48000;
    auto tables = generateTables();

    printf("\n== 1. Wavetable generation ==\n");
    {
        bool ok = tables.size() == 6;
        bool noNan = true, normOk = true;
        for (auto& t : tables) {
            for (float v : t.data) if (!std::isfinite(v)) noNan = false;
            // mip-0 frame peak should be ~0.92 (normalization headroom)
            float pk = 0;
            for (int i = 0; i < SIZE; i++) pk = std::max(pk, std::abs(t.data[i]));
            if (pk < 0.8f || pk > 0.95f) normOk = false;
        }
        check(ok, "6 procedural tables generated");
        check(noNan, "no NaN/Inf in table data");
        check(normOk, "mip-0 frames peak-normalized to ~0.92");
    }

    printf("\n== 2. Live note produces audio ==\n");
    {
        Engine eng; eng.prepare(sr); eng.setTables(tables);
        eng.setParams(defaultParams());           // INIT: oscA PRIME, LP24 filter
        auto buf = renderNote(eng, 69, 0.5, sr);   // A4
        check(finite(buf), "output finite");
        double r = rms(buf, (int)(0.05 * sr));
        check(r > 1e-3, "RMS > 0 (audio present)", "rms=" + std::to_string(r));
        check(peak(buf) < 4.0f, "output bounded", "peak=" + std::to_string(peak(buf)));
    }

    printf("\n== 3. Anti-aliasing (band-limited mips) ==\n");
    {
        // Bright PRIME table (saw/square region) at a high note, filter off.
        Engine eng; eng.prepare(sr); eng.setTables(tables);
        auto p = defaultParams();
        p[OSCA_BASE + OSC_POS] = 0.66f;            // saw-rich frame
        p[OSCA_BASE + OSC_UNISON] = 1;
        p[OSCA_BASE + OSC_PAN] = 0;
        p[FILTER1_BASE + FLT_ON] = 0;
        p[ENV1_BASE + 0] = 0.001f; p[ENV1_BASE + 2] = 1.0f; // fast attack, full sustain
        eng.setParams(p);
        for (int note : {96, 103, 108}) {           // C7, G7, C8
            double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            auto buf = renderNote(eng, note, 0.4, sr);
            int N = 16384;
            double db = aliasFloorDb(buf.data() + (buf.size() - N), N, sr, f0);
            check(db < -55.0, "alias floor low @ note " + std::to_string(note),
                  std::to_string(db) + " dB");
            eng.panic();
            std::vector<float> flush(sr * 0.2, 0); eng.render(flush.data(), flush.data(), 0);
        }
    }

    printf("\n== 4. All filter types stable ==\n");
    {
        const char* names[] = {"LP12", "LP24", "BP12", "HP12", "NOTCH", "COMB", "VOWEL"};
        for (int ft = 0; ft < 7; ft++) {
            Engine eng; eng.prepare(sr); eng.setTables(tables);
            auto p = defaultParams();
            p[FILTER1_BASE + FLT_TYPE] = (float)ft;
            p[FILTER1_BASE + FLT_CUTOFF] = 1200;
            p[FILTER1_BASE + FLT_RES] = 0.5f;
            p[FILTER1_BASE + FLT_DRIVE] = 0.4f;     // exercise the ADAA saturator too
            eng.setParams(p);
            auto buf = renderNote(eng, 60, 0.4, sr);
            check(finite(buf) && peak(buf) < 8.0f, std::string("filter ") + names[ft] + " stable",
                  "peak=" + std::to_string(peak(buf)));
        }
    }

    printf("\n== 5. FX chain stable ==\n");
    {
        Engine eng; eng.prepare(sr); eng.setTables(tables);
        Fx fx; fx.prepare(sr);
        auto p = defaultParams();
        p[FXDRIVE_ON] = 1; p[FXDRIVE_AMT] = 0.6f;
        p[FXCHORUS_ON] = 1; p[FXDELAY_ON] = 1; p[FXDELAY_FB] = 0.6f;
        p[FXREVERB_ON] = 1; p[FXREVERB_SIZE] = 0.8f; p[FXREVERB_MIX] = 0.5f;
        eng.setParams(p); fx.setParams(p);
        int n = (int)(1.5 * sr);
        std::vector<float> L(n, 0), R(n, 0);
        eng.noteOn(60, 1.0); eng.noteOn(64, 1.0); eng.noteOn(67, 1.0);
        // process in 128-blocks (host-like)
        for (int off = 0; off < n; off += 128) {
            int bs = std::min(128, n - off);
            eng.render(L.data() + off, R.data() + off, bs);
            fx.process(L.data() + off, R.data() + off, bs);
        }
        check(finite(L) && finite(R), "FX output finite");
        check(peak(L) < 1.2f && peak(R) < 1.2f, "limiter holds output near unity",
              "peakL=" + std::to_string(peak(L)));
        check(rms(L) > 1e-3, "FX path passes audio", "rms=" + std::to_string(rms(L)));
    }

    printf("\n== 6. All %zu factory presets render cleanly ==\n", factoryPresets().size());
    {
        int bad = 0;
        for (const auto& preset : factoryPresets()) {
            Engine eng; eng.prepare(sr); eng.setTables(tables);
            Fx fx; fx.prepare(sr);
            auto p = applyPreset(preset);
            eng.setParams(p); fx.setParams(p);
            int n = (int)(0.6 * sr);
            std::vector<float> L(n, 0), R(n, 0);
            eng.noteOn(48, 1.0); eng.noteOn(60, 0.9);
            for (int off = 0; off < n; off += 128) {
                int bs = std::min(128, n - off);
                eng.render(L.data() + off, R.data() + off, bs);
                fx.process(L.data() + off, R.data() + off, bs);
            }
            bool ok = finite(L) && finite(R) && peak(L) < 1.5f && rms(L) > 1e-5;
            if (!ok) { bad++; printf("    FAIL preset: %s (peak=%.3f rms=%.5f)\n",
                                     preset.name.c_str(), peak(L), rms(L)); }
        }
        check(bad == 0, "every preset finite / bounded / audible",
              std::to_string(factoryPresets().size() - bad) + " ok");
    }

    printf("\n== 7. User wavetables (import / draw) ==\n");
    {
        // Drawn single cycle: a sine over DRAW_N points -> 1 band-limited frame.
        std::vector<float> pts(256);
        for (int i = 0; i < 256; i++) pts[i] = std::sin(2 * M_PI * i / 256.0);
        auto frame = frameFromDrawing(pts);
        auto drawn = makeUserTable("DRAW", {frame});
        bool drawOk = drawn.frames == 1 && (int)drawn.wave.size() == SIZE
                    && !drawn.table.data.empty();
        bool drawFinite = true;
        for (float v : drawn.table.data) if (!std::isfinite(v)) drawFinite = false;
        check(drawOk && drawFinite, "drawn table builds 1 band-limited frame");

        // Fixed-length slice of a longer clip -> several frames, capped.
        std::vector<float> clip(2048 * 5);
        for (int i = 0; i < (int)clip.size(); i++) clip[i] = std::sin(2 * M_PI * i / 2048.0);
        auto sliced = makeUserTable("SLICE", sliceToFrames(clip, 2048.0));
        check(sliced.frames == 5, "fixed-length slice yields 5 frames",
              std::to_string(sliced.frames));

        // wave round-trip (persistence path) preserves frame count + builds.
        auto rebuilt = userTableFromWave("SLICE", sliced.frames, sliced.wave);
        check(rebuilt.frames == 5 && !rebuilt.table.data.empty(), "wave round-trip rebuilds table");

        {
            // framesFromGenerated round-trips a factory table's frame count and a
            // re-built copy is finite + non-silent.
            auto factory = generateTables();
            auto frames = framesFromGenerated(factory[0]);
            check((int)frames.size() == factory[0].frames, "framesFromGenerated frame count");
            check(frames[0].size() == (size_t)SIZE, "framesFromGenerated frame width");
            auto copy = makeUserTable("COPY", frames);
            check(copy.frames == factory[0].frames, "duplicated factory frame count");
            check(finite(copy.table.data) && peak(copy.table.data) > 0.1f, "duplicated factory audible");
        }

        // The engine plays a user table addressed past the procedural slots.
        Engine eng; eng.prepare(sr);
        std::vector<GeneratedTable> all = tables;     // 6 procedural
        all.push_back(drawn.table);                   // user slot -> index 6
        eng.setTables(all);
        auto p = defaultParams();
        p[OSCA_BASE + OSC_TABLE] = (float)tables.size(); // select the user table
        eng.setParams(p);
        auto buf = renderNote(eng, 60, 0.4, sr);
        check(finite(buf) && rms(buf, (int)(0.05 * sr)) > 1e-3,
              "engine renders a user table", "rms=" + std::to_string(rms(buf, (int)(0.05 * sr))));
    }

    printf("\n== 8. FrameOps (frame-list editing) ==\n");
    {
        // FrameOps — duplicate/delete/move on a frame list, and pad-downsample.
        using Frame = std::vector<float>;
        std::vector<Frame> fs{ Frame(SIZE, 0.1f), Frame(SIZE, 0.2f), Frame(SIZE, 0.3f) };
        auto dup = fable::duplicateFrame(fs, 1);
        check(dup.size() == 4 && dup[2][0] == 0.2f, "duplicateFrame inserts copy after i");
        auto del = fable::deleteFrame(fs, 0);
        check(del.size() == 2 && del[0][0] == 0.2f, "deleteFrame removes i");
        auto one = std::vector<Frame>{ Frame(SIZE, 0.5f) };
        check(fable::deleteFrame(one, 0).size() == 1, "deleteFrame refuses last frame");
        auto mv = fable::moveFrame(fs, 0, 2);
        check(mv.size() == 3 && mv[2][0] == 0.1f, "moveFrame relocates frame");
        auto pad = fable::framePoints(Frame(SIZE, 0.7f), 256);
        check(pad.size() == 256 && std::abs(pad[10] - 0.7f) < 1e-6f, "framePoints samples DRAW_N points");
    }

    printf("\n== 9. LFO controls (sync / rise / phase / retrig) ==\n");
    {
        check(std::abs(lfoDivFactor(2) - 1.0) < 1e-9, "lfoDivFactor 1/4 = 1.0");
        check(std::abs(lfoDivFactor(5) - 2.0) < 1e-9, "lfoDivFactor 1/8 = 2.0");
        check(std::abs(lfoDivFactor(0) - 0.25) < 1e-9, "lfoDivFactor 1/1 = 0.25");

        auto dp = defaultParams();
        check(dp[LFO1_BASE + LFO_RETRIG] == 1.0f, "lfo retrig defaults on (legacy behaviour)");
        check(dp[LFO1_BASE + LFO_SYNC] == 0.0f, "lfo sync defaults off");
        check((int)dp[LFO1_BASE + LFO_SYNCRATE] == 2, "lfo syncrate defaults 1/4");

        Rng rng; Lfo lf; lf.rng = &rng; lf.reset();
        check(lf.riseGain(1.0, 48000) == 0.0, "rise gain 0 at note-on");
        lf.advance(2.0, 24000, 48000);
        check(std::abs(lf.riseGain(1.0, 48000) - 0.5) < 1e-6, "rise gain ~0.5 mid-ramp");
        check(lf.riseGain(0.0, 48000) == 1.0, "rise gain 1 when rise=0");

        // Engine renders finite/bounded audio with synced + free-running LFO routed to A POS.
        Engine eng; eng.prepare(sr);
        eng.setTables(tables);
        auto& p = eng.params();
        p = defaultParams();
        p[LFO1_BASE + LFO_SYNC] = 1; p[LFO1_BASE + LFO_SYNCRATE] = 5; p[LFO1_BASE + LFO_RETRIG] = 0;
        p[LFO1_BASE + LFO_RISE] = 0.2f;
        p[MAT1_BASE + MAT_SRC] = 1; p[MAT1_BASE + MAT_DST] = 1; p[MAT1_BASE + MAT_AMT] = 1.0f; // LFO1 -> A POS
        eng.setBpm(128);
        eng.noteOn(60, 1.0);
        std::vector<float> bl(2048), br(2048);
        eng.render(bl.data(), br.data(), 2048);
        check(finite(bl) && peak(bl) < 4.0f, "engine finite/bounded with synced free-run LFO",
              "peak=" + std::to_string(peak(bl)));

        // Transport phase-lock: a synced free-run LFO derives its phase from the
        // host position, so two transport spots a whole number of cycles apart
        // give identical modulation (downbeat alignment, independent of elapsed
        // time). SAW shape keeps it deterministic; route LFO1 -> A POS.
        auto firstPos = [&](double ppq) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto& q = e.params(); q = defaultParams();
            q[LFO1_BASE + LFO_SHAPE] = 2;   // SAW
            q[LFO1_BASE + LFO_SYNC] = 1; q[LFO1_BASE + LFO_SYNCRATE] = 2; q[LFO1_BASE + LFO_RETRIG] = 0; // 1/4, free-run
            q[MAT1_BASE + MAT_SRC] = 1; q[MAT1_BASE + MAT_DST] = 1; q[MAT1_BASE + MAT_AMT] = 1.0f;       // LFO1 -> A POS
            e.setBpm(120); e.setTransport(ppq, true);
            e.noteOn(60, 1.0);
            std::vector<float> a(128), b(128);
            e.render(a.data(), b.data(), 128);
            return e.vizA;
        };
        // 1/4 = 1 cycle per beat, so ppq 0 and ppq 4 are 4 cycles apart -> same phase.
        check(std::abs(firstPos(0.0) - firstPos(4.0)) < 1e-6, "synced LFO phase locks to transport (downbeat aligned)");
        check(std::abs(firstPos(0.0) - firstPos(0.5)) > 1e-3, "synced LFO phase varies within the bar");

        eng.setTransport(2.0, true);
        eng.render(bl.data(), br.data(), 2048);
        check(finite(bl) && peak(bl) < 4.0f, "engine finite/bounded with transport-locked LFO");

        // LFO shape audibly changes the modulation: route LFO1 -> PITCH and
        // confirm SINE / SAW / SQR produce materially different output.
        auto renderShape = [&](int shape) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto& q = e.params(); q = defaultParams();
            q[LFO1_BASE + LFO_SHAPE] = (float)shape;
            q[LFO1_BASE + LFO_RATE] = 6.0f;
            q[MAT1_BASE + MAT_SRC] = 1; q[MAT1_BASE + MAT_DST] = 4; q[MAT1_BASE + MAT_AMT] = 0.5f; // LFO1 -> PITCH
            e.noteOn(60, 1.0);
            std::vector<float> a(8192), b(8192);
            e.render(a.data(), b.data(), 8192);
            return a;
        };
        auto shSine = renderShape(0), shSaw = renderShape(2), shSqr = renderShape(3);
        double dSaw = 0, dSqr = 0;
        for (size_t i = 0; i < shSine.size(); ++i) { dSaw += std::abs(shSine[i] - shSaw[i]); dSqr += std::abs(shSine[i] - shSqr[i]); }
        check(dSaw > 1.0 && dSqr > 1.0, "LFO shape changes modulated output (sine vs saw/sqr differ)",
              "dSaw=" + std::to_string(dSaw) + " dSqr=" + std::to_string(dSqr));
    }

    printf("\n== 10. Mod matrix beyond slot 4 (16-slot pool) ==\n");
    {
        // A slot past the legacy 4 (mat5) must modulate its dest. Two FRESH engines
        // render the identical patch — one with mat5 -> PITCH at a high amt, one with
        // mat5 off — so any diff is purely the slot taking effect. The engine is
        // deterministic (seeded RNG + deterministic LFO), so a fresh-vs-fresh diff is
        // reproducible run to run.
        auto renderMat5 = [&](bool on) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto& q = e.params(); q = defaultParams();
            q[LFO1_BASE + LFO_SHAPE] = 2;       // SAW — deterministic, audibly sweeps pitch
            q[LFO1_BASE + LFO_RATE]  = 6.0f;
            q[LFO1_BASE + LFO_RETRIG] = 1;      // per-voice phase, reset at note-on (deterministic)
            if (on) {
                q[MAT5_BASE + MAT_SRC] = 1;     // LFO 1
                q[MAT5_BASE + MAT_DST] = 4;     // PITCH
                q[MAT5_BASE + MAT_AMT] = 0.9f;  // high depth
            }
            e.noteOn(60, 1.0);
            std::vector<float> a(8192), b(8192);
            e.render(a.data(), b.data(), 8192);
            return a;
        };
        auto off = renderMat5(false), on = renderMat5(true);
        check(finite(off) && finite(on) && peak(on) < 4.0f, "mat5 -> PITCH output finite/bounded",
              "peak=" + std::to_string(peak(on)));
        double diff = 0;
        for (size_t i = 0; i < on.size(); ++i) diff += std::abs(on[i] - off[i]);
        check(diff > 1.0, "mat5 (slot > 4) audibly modulates PITCH (deterministic diff vs off)",
              "diff=" + std::to_string(diff));

        // A fresh engine rendering the same on-patch must reproduce the exact buffer
        // (no time-dependent state) — the diff above is deterministic.
        auto on2 = renderMat5(true);
        bool identical = on.size() == on2.size();
        for (size_t i = 0; identical && i < on.size(); ++i) if (on[i] != on2[i]) identical = false;
        check(identical, "mat5 modulation is reproducible across fresh engines");

        // Multiple routes to one dest accumulate (engine sums per-dest), and the
        // result stays finite/bounded even when three high-amt slots stack on PITCH.
        auto renderRoutes = [&](int count) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto& q = e.params(); q = defaultParams();
            q[LFO1_BASE + LFO_SHAPE] = 2; q[LFO1_BASE + LFO_RATE] = 6.0f; q[LFO1_BASE + LFO_RETRIG] = 1;
            q[LFO2_BASE + LFO_SHAPE] = 0; q[LFO2_BASE + LFO_RATE] = 4.0f; q[LFO2_BASE + LFO_RETRIG] = 1;
            // Three distinct slots, mixed sources, all targeting PITCH (dst 4).
            const int bases[3] = { MAT6_BASE, MAT9_BASE, MAT14_BASE };
            const int srcsIdx[3] = { 1, 2, 4 };  // LFO 1, LFO 2, VELO
            for (int i = 0; i < count; ++i) {
                q[bases[i] + MAT_SRC] = (float)srcsIdx[i];
                q[bases[i] + MAT_DST] = 4;        // PITCH
                q[bases[i] + MAT_AMT] = 0.8f;
            }
            e.noteOn(60, 1.0);
            std::vector<float> a(8192), b(8192);
            e.render(a.data(), b.data(), 8192);
            return a;
        };
        auto r1 = renderRoutes(1), r3 = renderRoutes(3);
        check(finite(r3) && peak(r3) < 4.0f, "3 routes -> PITCH accumulate finite/bounded",
              "peak=" + std::to_string(peak(r3)));
        double dAccum = 0;
        for (size_t i = 0; i < r1.size(); ++i) dAccum += std::abs(r3[i] - r1[i]);
        check(dAccum > 1.0, "stacking routes on one dest changes output (accumulation, not last-wins)",
              "dAccum=" + std::to_string(dAccum));
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : (std::to_string(g_fail) + " CHECK(S) FAILED").c_str());
    return g_fail == 0 ? 0 : 1;
}
