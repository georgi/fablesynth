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
    auto gen = generateTables();

    printf("\n== 1. Wavetable generation ==\n");
    {
        bool ok = gen.size() == 6;
        bool noNan = true, normOk = true;
        for (auto& t : gen) {
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
    // The engine takes shared table pointers (pool swaps share, never copy).
    std::vector<TablePtr> tables;
    for (auto& g : gen) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));

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
                    && !drawn.table->data.empty();
        bool drawFinite = true;
        for (float v : drawn.table->data) if (!std::isfinite(v)) drawFinite = false;
        check(drawOk && drawFinite, "drawn table builds 1 band-limited frame");

        // Fixed-length slice of a longer clip -> several frames, capped.
        std::vector<float> clip(2048 * 5);
        for (int i = 0; i < (int)clip.size(); i++) clip[i] = std::sin(2 * M_PI * i / 2048.0);
        auto sliced = makeUserTable("SLICE", sliceToFrames(clip, 2048.0));
        check(sliced.frames == 5, "fixed-length slice yields 5 frames",
              std::to_string(sliced.frames));

        // wave round-trip (persistence path) preserves frame count + builds.
        auto rebuilt = userTableFromWave("SLICE", sliced.frames, sliced.wave);
        check(rebuilt.frames == 5 && !rebuilt.table->data.empty(), "wave round-trip rebuilds table");

        {
            // framesFromGenerated round-trips a factory table's frame count and a
            // re-built copy is finite + non-silent.
            auto factory = generateTables();
            auto frames = framesFromGenerated(factory[0]);
            check((int)frames.size() == factory[0].frames, "framesFromGenerated frame count");
            check(frames[0].size() == (size_t)SIZE, "framesFromGenerated frame width");
            auto copy = makeUserTable("COPY", frames);
            check(copy.frames == factory[0].frames, "duplicated factory frame count");
            check(finite(copy.table->data) && peak(copy.table->data) > 0.1f, "duplicated factory audible");
        }

        // The engine plays a user table addressed past the procedural slots.
        Engine eng; eng.prepare(sr);
        auto all = tables;                            // 6 procedural
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

    printf("\n== 11. Generic mod destinations (per-param curve rules) ==\n");
    {
        // A NEW per-param destination (F1 RES, dst 17) must modulate. Route VELO
        // (a per-voice constant source) -> F1 RES at a strong amt; two FRESH engines
        // render the identical patch — one with the route, one without — so any diff
        // is purely the new dest taking effect. Use a resonant LP24 so a resonance
        // change is audible, and a low cutoff so the resonant peak dominates.
        auto renderRes = [&](bool on) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto& q = e.params(); q = defaultParams();
            q[FILTER1_BASE + FLT_TYPE]   = 1;      // LP24
            q[FILTER1_BASE + FLT_CUTOFF] = 600.0f; // low cutoff so RES peak is exposed
            q[FILTER1_BASE + FLT_RES]    = 0.2f;   // base resonance
            if (on) {
                q[MAT5_BASE + MAT_SRC] = 4;        // VELO (constant per voice)
                q[MAT5_BASE + MAT_DST] = 17;       // F1 RES (new dest)
                q[MAT5_BASE + MAT_AMT] = 0.7f;     // Lin width-1: res += 0.7 -> ~0.9
            }
            e.noteOn(60, 1.0);                     // vel 1.0 -> x = 1.0*0.7 = 0.7
            std::vector<float> a(8192), b(8192);
            e.render(a.data(), b.data(), 8192);
            return a;
        };
        auto resOff = renderRes(false), resOn = renderRes(true);
        check(finite(resOff) && finite(resOn) && peak(resOn) < 8.0f,
              "mat5 -> F1 RES (new dest) output finite/bounded", "peak=" + std::to_string(peak(resOn)));
        double resDiff = 0;
        for (size_t i = 0; i < resOn.size(); ++i) resDiff += std::abs(resOn[i] - resOff[i]);
        check(resDiff > 1.0, "new dest F1 RES (dst 17) audibly modulates (deterministic diff vs off)",
              "diff=" + std::to_string(resDiff));

        // Data-driven sweep: EVERY new per-param destination must modulate. For each
        // dst, two FRESH engines render an identical patch (route off vs VELO src=4
        // amt 0.7 -> that dst). VELO is a per-voice constant (=1.0) so x = 0.7 exactly;
        // a deterministic finite non-zero diff proves the dst reaches a synthesis read
        // site (guards against a reintroduced p_ read at any site). The base patch is
        // tuned so every knob is audible: both oscs on with unison>1 (detune/spread/
        // pan), a sustaining modEnv + filters on (env/key), serial F1->F2 both on (F2
        // dests), sub + noise on (sub/noise level).
        auto buildSweepPatch = [&]() {
            auto q = defaultParams();
            // both oscillators on, unison>1 so detune/spread/pan are audible
            q[OSCA_BASE + OSC_ON] = 1; q[OSCA_BASE + OSC_UNISON] = 5;
            q[OSCA_BASE + OSC_DETUNE] = 0.3f; q[OSCA_BASE + OSC_SPREAD] = 0.5f;
            q[OSCB_BASE + OSC_ON] = 1; q[OSCB_BASE + OSC_UNISON] = 5;
            q[OSCB_BASE + OSC_DETUNE] = 0.3f; q[OSCB_BASE + OSC_SPREAD] = 0.5f;
            q[OSCB_BASE + OSC_LEVEL] = 0.75f;
            // both filters on, serial route (F1 -> F2), resonant LP24 with low cutoff
            // so res/drive/env/key all move the sound; modEnv sustains so env is live.
            q[FILTER_ROUTE] = 0;                    // SERIAL
            q[FILTER1_BASE + FLT_ON] = 1; q[FILTER1_BASE + FLT_TYPE] = 1; // LP24
            q[FILTER1_BASE + FLT_CUTOFF] = 800.0f; q[FILTER1_BASE + FLT_RES] = 0.3f;
            q[FILTER1_BASE + FLT_DRIVE] = 0.2f;
            q[FILTER2_BASE + FLT_ON] = 1; q[FILTER2_BASE + FLT_TYPE] = 1; // LP24
            q[FILTER2_BASE + FLT_CUTOFF] = 1200.0f; q[FILTER2_BASE + FLT_RES] = 0.3f;
            q[FILTER2_BASE + FLT_DRIVE] = 0.2f;
            q[ENV2_BASE + 2] = 0.8f;                // modEnv sustain > 0 -> env term live
            // sub + noise on so their level dests are audible
            q[SUB_ON] = 1; q[SUB_LEVEL] = 0.5f;
            q[NOISE_ON] = 1; q[NOISE_LEVEL] = 0.3f;
            return q;
        };
        auto renderSweep = [&](int dst, bool on) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto& q = e.params(); q = buildSweepPatch();
            if (on) {
                q[MAT5_BASE + MAT_SRC] = 4;         // VELO (constant per voice = 1.0)
                q[MAT5_BASE + MAT_DST] = (float)dst;
                q[MAT5_BASE + MAT_AMT] = 0.7f;      // x = 1.0 * 0.7 = 0.7
            }
            // note 72 (!= 60) so the filter KEY dests have a nonzero (note-60) term.
            e.noteOn(72, 1.0);
            std::vector<float> a(8192), b(8192);
            e.render(a.data(), b.data(), 8192);
            return a;
        };
        struct DstCase { int dst; const char* name; };
        const DstCase newDests[] = {
            {11, "A DETUNE"}, {14, "B DETUNE"},
            {12, "A SPREAD"}, {15, "B SPREAD"},
            {13, "A PAN"},    {16, "B PAN"},
            {17, "F1 RES"},   {18, "F1 DRIVE"}, {19, "F1 ENV"}, {20, "F1 KEY"},
            {10, "F2 RES"},   {21, "F2 DRIVE"}, {22, "F2 ENV"}, {23, "F2 KEY"},
            {24, "SUB LVL"},  {25, "NOISE LVL"},
        };
        for (const auto& c : newDests) {
            auto dOff = renderSweep(c.dst, false), dOn = renderSweep(c.dst, true);
            double diff = 0;
            for (size_t i = 0; i < dOn.size(); ++i) diff += std::abs(dOn[i] - dOff[i]);
            check(finite(dOn) && finite(dOff) && diff > 1e-6,
                  std::string("new dest ") + c.name + " (dst " + std::to_string(c.dst) +
                  ") modulates (finite + deterministic non-zero diff)",
                  "diff=" + std::to_string(diff));
        }

        // An EXISTING destination (F1 CUT, dst 3) must still obey its documented Log
        // rule: effective cutoff = base * 2^(x*5), D=5, AS PART OF the single legacy
        // exponent base * 2^(env*4*e2 + key*(note-60)/12 + x*5). Probe by equivalence:
        // render a route F1 CUT via VELO at amt a over base Cb, and render WITHOUT a
        // route but with the base cutoff pre-set to Cb*2^(a*5). With ENV/KEY nonzero
        // and a note != 60, the env+key terms are nonzero so this exercises the full
        // single-pow path; multiplying the BASE cutoff by 2^(a*5) is mathematically
        // (and bit-) identical to adding a*5 inside the one std::pow, so the two fresh
        // engines must produce a bit-identical buffer.
        const double Cb = 1000.0, amt = 0.3;        // x = vel(1.0)*0.3 = 0.3
        const double expectedFc = Cb * std::pow(2.0, amt * 5.0); // 1000 * 2^1.5 ~= 2828 Hz
        const int cutNote = 72;                     // != 60 so the key term is nonzero
        auto renderCut = [&](double baseCut, bool route) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto& q = e.params(); q = defaultParams();
            q[FILTER1_BASE + FLT_TYPE]   = 1;       // LP24
            q[FILTER1_BASE + FLT_CUTOFF] = (float)baseCut;
            q[FILTER1_BASE + FLT_ENV]    = 0.6f;    // nonzero -> exercises the env term
            q[FILTER1_BASE + FLT_KEY]    = 0.6f;    // nonzero + note 72 -> key term nonzero
            if (route) {
                q[MAT5_BASE + MAT_SRC] = 4;         // VELO
                q[MAT5_BASE + MAT_DST] = 3;         // F1 CUT (existing dest, Log rule)
                q[MAT5_BASE + MAT_AMT] = (float)amt;
            }
            e.noteOn(cutNote, 1.0);
            std::vector<float> a(8192), b(8192);
            e.render(a.data(), b.data(), 8192);
            return a;
        };
        auto routed   = renderCut(Cb, true);              // base 1000, modulated +1.5 oct
        auto direct   = renderCut(expectedFc, false);     // base pre-set to 1000*2^1.5
        bool cutMatch = routed.size() == direct.size();
        double cutErr = 0;
        for (size_t i = 0; cutMatch && i < routed.size(); ++i) cutErr += std::abs(routed[i] - direct[i]);
        check(cutMatch && cutErr < 1e-6,
              "existing dest F1 CUT obeys base*2^(x*5) inside the single env/key pow: "
              "routed cutoff == direct " + std::to_string((int)expectedFc) + " Hz "
              "(env=key=0.6, note 72)", "err=" + std::to_string(cutErr));

        // Sanity: the modulated cutoff genuinely differs from the un-modulated base
        // (so the equivalence above isn't trivially comparing two identical patches).
        auto baseline = renderCut(Cb, false);             // base 1000, no route
        double cutMove = 0;
        for (size_t i = 0; i < routed.size(); ++i) cutMove += std::abs(routed[i] - baseline[i]);
        check(cutMove > 1.0, "F1 CUT route moves the cutoff away from its base value",
              "move=" + std::to_string(cutMove));
    }

    printf("\n== 12. Unison BLEND & 16-voice ==\n");
    {
        check(defaultParams()[OSCA_BASE + OSC_BLEND] == 1.0f,
              "oscA.blend defaults to 1.0 (preset back-compat)");

        // Render oscA only (bare oscillators, no filter), fast attack + full
        // sustain, and return stereo buffers. Uses the verified harness param
        // API: build a ParamArray, then eng.setParams(p).
        auto renderUni = [&](int uni, float blend, float detune, float spread,
                             int note, std::vector<float>& L, std::vector<float>& R) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto p = defaultParams();
            p[OSCA_BASE + OSC_ON]     = 1;
            p[OSCA_BASE + OSC_UNISON] = (float)uni;
            p[OSCA_BASE + OSC_BLEND]  = blend;
            p[OSCA_BASE + OSC_DETUNE] = detune;
            p[OSCA_BASE + OSC_SPREAD] = spread;
            p[OSCA_BASE + OSC_PAN]    = 0;
            p[OSCA_BASE + OSC_LEVEL]  = 0.8f;
            p[OSCB_BASE + OSC_ON]     = 0;                 // oscA only
            p[FILTER1_BASE + FLT_ON]  = 0;                 // bare oscillators
            p[ENV1_BASE + 0] = 0.001f; p[ENV1_BASE + 2] = 1.0f; // fast attack, full sustain
            e.setParams(p);
            e.noteOn(note, 1.0);
            int n = (int)(0.5 * sr);
            L.assign(n, 0.0f); R.assign(n, 0.0f);
            e.render(L.data(), R.data(), n);
        };
        // Mean stereo power (L^2 + R^2) over the steady-state tail (skip attack).
        auto totalPower = [&](const std::vector<float>& L, const std::vector<float>& R) {
            int start = (int)(0.1 * sr); double s = 0; int cnt = 0;
            for (int i = start; i < (int)L.size(); i++) {
                s += (double)L[i] * L[i] + (double)R[i] * R[i]; cnt++;
            }
            return cnt ? s / cnt : 0.0;
        };

        std::vector<float> L1, R1, L0, R0, Lu1, Ru1;

        // (1) Regression: at uni=1 the single voice sits at sprd=0, so its weight
        // is 1-(1-b)*0 = 1 for ANY blend -> blend is a perfect no-op. Fresh engines
        // share a deterministic RNG seed (Engine.h: s = 0x9e3779b9u), so the two
        // renders are sample-for-sample identical. This proves the new weight/
        // normalization reduces exactly to the legacy single-voice path.
        renderUni(1, 1.0f, 0.3f, 0.5f, 69, L1, R1);
        renderUni(1, 0.0f, 0.3f, 0.5f, 69, L0, R0);
        bool uni1Identical = L1.size() == L0.size();
        for (size_t i = 0; uni1Identical && i < L1.size(); i++)
            if (L1[i] != L0[i] || R1[i] != R0[i]) uni1Identical = false;
        check(uni1Identical,
              "blend is a sample-exact no-op at uni=1 (single voice at sprd=0, weight=1)");

        // (2) blend=0, uni=4 ~= single voice loudness. Equal-power panning
        // (gl^2+gr^2=1) + normalization on sqrt(Sum w^2) make total stereo power
        // independent of uni/blend. Assert within 3 dB.
        renderUni(4, 0.0f, 0.4f, 0.6f, 69, L0, R0);
        renderUni(1, 1.0f, 0.4f, 0.6f, 69, Lu1, Ru1);
        double pBlend0 = totalPower(L0, R0), pUni1 = totalPower(Lu1, Ru1);
        double collapseDb = 10.0 * std::log10(pBlend0 / pUni1);
        check(std::abs(collapseDb) < 3.0,
              "blend=0 uni=4 ~= single voice (uni=1) loudness (within 3 dB)",
              "delta=" + std::to_string(collapseDb) + " dB");

        // (3) Loudness ~constant across the blend sweep at uni=4. Assert within 6 dB.
        renderUni(4, 1.0f, 0.4f, 0.6f, 69, L1, R1);
        double pBlend1 = totalPower(L1, R1);
        double sweepDb = 10.0 * std::log10(pBlend0 / pBlend1);
        check(std::abs(sweepDb) < 6.0,
              "loudness ~constant across blend at uni=4 (total power within 6 dB)",
              "delta=" + std::to_string(sweepDb) + " dB");

        // (4) uni=2, blend=0 degenerate: both voices are endpoints (|sprd|=1 ->
        // weight 0 -> sumW2=0). The divide-by-zero guard must keep output finite.
        // (Near-silent by design; we only assert it never blows up to NaN/Inf.)
        {
            std::vector<float> L, R;
            renderUni(2, 0.0f, 0.4f, 0.6f, 69, L, R);
            check(finite(L) && finite(R), "uni=2 blend=0 stays finite (sumW2=0 guard)");
        }

        // (5) 16-voice render must not alias. aliasFloorDb scores any energy off
        // the exact-harmonic comb as "alias", so detune (which legitimately places
        // partials between harmonics) would false-fail. Use detune=0 to isolate the
        // band-limited mip path for 16 summed voices; reuse the -55 dB threshold.
        {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto p = defaultParams();
            p[OSCA_BASE + OSC_POS]    = 0.66f;            // saw-rich frame
            p[OSCA_BASE + OSC_UNISON] = 16;               // worst-case voice count
            p[OSCA_BASE + OSC_BLEND]  = 1.0f;             // all 16 voices at full level
            p[OSCA_BASE + OSC_DETUNE] = 0.0f;             // isolate aliasing from detune sidebands
            p[OSCA_BASE + OSC_SPREAD] = 0.0f;
            p[OSCA_BASE + OSC_PAN]    = 0;
            p[FILTER1_BASE + FLT_ON]  = 0;
            p[ENV1_BASE + 0] = 0.001f; p[ENV1_BASE + 2] = 1.0f;
            e.setParams(p);
            for (int note : {96, 103, 108}) {             // C7, G7, C8
                double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
                auto buf = renderNote(e, note, 0.4, sr);
                int N = 16384;
                double db = aliasFloorDb(buf.data() + (buf.size() - N), N, sr, f0);
                check(db < -55.0, "16-voice alias floor low @ note " + std::to_string(note),
                      std::to_string(db) + " dB");
                e.panic();
                std::vector<float> flush((size_t)(sr * 0.2), 0);
                e.render(flush.data(), flush.data(), 0);
            }
        }

        // (6) 16 voices at MAX detune (dense-cluster worst case): the alias metric
        // is not meaningful (detune sidebands count as inharmonic), so only assert
        // the render stays finite and bounded.
        {
            std::vector<float> L, R;
            renderUni(16, 1.0f, 1.0f, 1.0f, 103, L, R);   // G7, max detune+spread
            check(finite(L) && finite(R) && peak(L) < 4.0f && peak(R) < 4.0f,
                  "16-voice max-detune render finite/bounded",
                  "peakL=" + std::to_string(peak(L)));
        }
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : (std::to_string(g_fail) + " CHECK(S) FAILED").c_str());
    return g_fail == 0 ? 0 : 1;
}
