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

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : (std::to_string(g_fail) + " CHECK(S) FAILED").c_str());
    return g_fail == 0 ? 0 : 1;
}
