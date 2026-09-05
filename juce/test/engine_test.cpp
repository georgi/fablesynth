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
#include <atomic>
#include <thread>

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
// A Hann window with a +/-6-bin mask (the original form of this test) leaks
// about -57 dB into the "alias" bins, so it reported -57 dB for a linear read
// and for a cubic Hermite read alike — it measured the window, not the engine.
// A 4-term Blackman-Harris window (-92 dB sidelobes) with a +/-12-bin mask
// puts the measurement floor below -100 dB, where the reads actually differ.
static double aliasFloorDb(const float* x, int N, double sr, double f0) {
    std::vector<double> re(N), im(N, 0.0);
    static const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    for (int i = 0; i < N; i++) {
        const double t = 2 * M_PI * i / (N - 1);
        const double w = a0 - a1 * std::cos(t) + a2 * std::cos(2 * t) - a3 * std::cos(3 * t);
        re[i] = x[i] * w;
    }
    fft(re.data(), im.data(), N, false);
    auto mag2 = [&](int k) { return re[k] * re[k] + im[k] * im[k]; };

    double binHz = sr / N;
    // The mask must be wider than the window's skirt, or the measurement reads
    // leakage from the harmonics instead of the engine. A fixed +/-12 bins is
    // wide enough only while the harmonics are far apart: at note 36 (89 bins
    // apart, 366 harmonics) it reported -77.5 dB, and widening the mask to
    // +/-40 dropped the same render to -98.3 dB — 21 dB of pure leakage. Wider
    // still would leave no gap to measure in, so cap at 40 and keep at least a
    // few bins clear either side of the midpoint between harmonics.
    const double spacing = f0 / binHz;
    int halfWin = (int)std::min(40.0, std::max(12.0, spacing / 2 - 4));
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
    if (alias <= 0) return -200;
    return 10 * std::log10(alias / harm);
}

// ---- octave-band energy profile, for the invariance checks ----
// Returns per-band energy in dB relative to the total, for bands that are the
// same absolute frequencies at every sample rate and block size.
static std::vector<double> bandProfileDb(const std::vector<float>& x, int N, double sr) {
    std::vector<double> re(N), im(N, 0.0);
    for (int i = 0; i < N; i++) {
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1)); // Hann
        re[i] = (i < (int)x.size() ? x[i] : 0.0f) * w;
    }
    fft(re.data(), im.data(), N, false);
    const double binHz = sr / N;
    static const double EDGES[] = {40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 18000};
    const int nb = (int)(sizeof(EDGES) / sizeof(EDGES[0])) - 1;
    std::vector<double> band((size_t)nb, 0.0);
    double total = 1e-30;
    for (int k = 1; k < N / 2; k++) {
        const double f = k * binHz;
        const double e = re[k] * re[k] + im[k] * im[k];
        for (int b = 0; b < nb; b++)
            if (f >= EDGES[b] && f < EDGES[b + 1]) { band[(size_t)b] += e; break; }
        if (f >= EDGES[0] && f < EDGES[nb]) total += e;
    }
    for (auto& v : band) v = 10 * std::log10(std::max(v, 1e-30) / total);
    return band;
}

// ---- block-rate automation artefact metric (finding J1) ----
// A parameter held for a whole host block and stepped at its boundary amplitude-
// modulates the tone at the block rate, which shows up as sidebands at
// k*f0 +/- m*blockHz around every harmonic. Returns their total energy relative
// to the harmonics, in dB. A per-chunk smoother pushes them into the noise.
static double blockRateSidebandDb(const float* x, int N, double sr, double f0, double blockHz) {
    std::vector<double> re(N), im(N, 0.0);
    static const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    for (int i = 0; i < N; i++) {
        const double t = 2 * M_PI * i / (N - 1);
        re[i] = x[i] * (a0 - a1 * std::cos(t) + a2 * std::cos(2 * t) - a3 * std::cos(3 * t));
    }
    fft(re.data(), im.data(), N, false);
    const double binHz = sr / N;
    auto band = [&](double f) {                  // +/-2 bins around f
        const int b = (int)std::round(f / binHz);
        double e = 0;
        for (int j = b - 2; j <= b + 2; j++)
            if (j > 0 && j < N / 2) e += re[j] * re[j] + im[j] * im[j];
        return e;
    };
    double harm = 0, side = 0;
    for (int k = 1; k * f0 < sr * 0.4; k++) {
        harm += band(k * f0);
        for (int m = 1; m <= 4; m++) {           // stop before the next harmonic
            if (m * blockHz > f0 * 0.45) break;
            side += band(k * f0 - m * blockHz) + band(k * f0 + m * blockHz);
        }
    }
    if (harm <= 0) return 0;
    return 10 * std::log10(std::max(side, 1e-30) / harm);
}

// ---- click detector: a discontinuity is broadband ----
// max |dx| cannot tell a click from a bright filter: switching to HP12 or to a
// square sub legitimately raises the slew. A step, though, injects energy far
// above whatever the signal itself carries. Returns the fraction of an
// N-sample window's energy that sits above fHi.
static double hfFraction(const std::vector<float>& x, int at, int N, double sr, double fHi) {
    std::vector<double> re(N), im(N, 0.0);
    for (int i = 0; i < N; i++) {
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1));   // Hann
        const int j = at + i;
        re[i] = (j >= 0 && j < (int)x.size() ? x[(size_t)j] : 0.0f) * w;
    }
    fft(re.data(), im.data(), N, false);
    const double binHz = sr / N;
    double hi = 0, total = 1e-30;
    for (int k = 1; k < N / 2; k++) {
        const double e = re[k] * re[k] + im[k] * im[k];
        total += e;
        if (k * binHz >= fHi) hi += e;
    }
    return hi / total;
}

// Largest sample-to-sample step in [from, to) — the click detector.
static double maxDelta(const std::vector<float>& x, int from, int to) {
    double m = 0;
    from = std::max(1, from);
    to = std::min(to, (int)x.size());
    for (int i = from; i < to; i++) m = std::max(m, (double)std::abs(x[i] - x[i - 1]));
    return m;
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
        // 65536-point transform: at note 36 (65.4 Hz) the harmonics are 89 bins
        // apart, so a +/-12-bin mask still leaves real gaps to measure in.
        const int N = 65536;
        for (int note : {36, 48, 60, 72, 84, 96}) {
            double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            auto buf = renderNote(eng, note, (double)(N + 4096) / sr, sr);
            double db = aliasFloorDb(buf.data() + (buf.size() - N), N, sr, f0);
            // -85 dB at every note, with no per-note exception: the mask in
            // aliasFloorDb widens where the harmonics crowd together, so the
            // measurement floor stays below the bound everywhere. Measured with
            // the Hermite read: -98.3 / -92.8 / -103.8 / -100.1 / -102.5 /
            // -100.3 dB. Reverting rdH to a linear read gives -86.7 / -72.9 /
            // -81.8, so notes 48 and 60 fail — the check has teeth.
            const double limit = -85.0;
            check(db < limit, "alias floor low @ note " + std::to_string(note),
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

    printf("\n== 4b. LP24 resonance mapping (finding B3) ==\n");
    {
        // B3 moved LP24's resonance into stage 1 alone (stage 2 fixed at k = 2)
        // and retapered res, keeping the magnitude at fc where it was. Nothing
        // tested the taper before, which is exactly why the four ports drifted.
        //
        // |H(f)| is measured through the real engine with no curve fitting:
        // render white noise with the filter ON and with it OFF. The RNG is
        // deterministic and the noise generator is its only consumer here, so
        // both renders see the SAME noise sequence and the per-bin ratio of the
        // two spectra is |H(f)| exactly.
        auto noisePatch = [&](float res, bool filterOn) {
            auto p = defaultParams();
            p[OSCA_BASE + OSC_ON] = 0; p[OSCB_BASE + OSC_ON] = 0;   // noise only
            p[NOISE_ON] = 1; p[NOISE_TYPE] = 0; p[NOISE_LEVEL] = 1.0f;
            p[FILTER1_BASE + FLT_ON] = filterOn ? 1.0f : 0.0f;
            p[FILTER1_BASE + FLT_TYPE] = 1;                          // LP24
            p[FILTER1_BASE + FLT_CUTOFF] = 1000.0f;
            p[FILTER1_BASE + FLT_RES] = res;
            p[FILTER1_BASE + FLT_DRIVE] = 0;                         // linear path
            p[ENV1_BASE + 0] = 0.001f; p[ENV1_BASE + 2] = 1.0f;      // open and hold
            return p;
        };
        const int settle = (int)(0.5 * sr), N = 32768;
        auto renderNoise = [&](float res, bool filterOn) {
            Engine e; e.prepare(sr); e.setTables(tables);
            e.setParams(noisePatch(res, filterOn));
            std::vector<float> L((size_t)(settle + N), 0.0f), R(L.size(), 0.0f);
            e.noteOn(60, 1.0);
            e.render(L.data(), R.data(), (int)L.size());
            return std::vector<float>(L.end() - N, L.end());
        };
        auto spectrum = [&](const std::vector<float>& x) {
            std::vector<double> re(N), im(N, 0.0);
            for (int i = 0; i < N; i++) re[(size_t)i] = x[(size_t)i];
            fft(re.data(), im.data(), N, false);
            std::vector<double> m((size_t)(N / 2));
            for (int k = 0; k < N / 2; k++)
                m[(size_t)k] = std::sqrt(re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]);
            return m;
        };
        const auto flat = spectrum(renderNoise(0.0f, false));
        const double binHz = sr / N;
        // Analytic values for the landed mapping (resT = res + 0.0035*res^4,
        // k1 = max(0.002, 0.5*(2-1.93*resT)^2), k2 = 2), and for the OLD
        // cascade, at fc = 1 kHz. The old column is what presets were voiced
        // against, so |H(fc)| is the fidelity check; max_f |H(f)| is the
        // "does the knob resonate" check and reads 0 dB when there is no peak.
        struct Case { float res; double atFc; double peak; double oldAtFc; double oldPeak; };
        const Case cases[] = {
            {0.00f, -12.04,  -0.01, -12.04,  -0.01},
            {0.18f,  -8.73,  -0.00,  -8.73,  -0.00},
            {0.50f,  -0.59,  +0.76,  -0.60,  +2.11},
            {0.90f, +23.50, +23.50, +23.20, +23.35},
        };
        // The per-bin ratio is |H| only up to the truncation error of a finite
        // segment, and a max over 13600 bins would report the worst positive
        // outlier of that error (about +2 dB). Averaging the power ratio over
        // 9 bins (13 Hz) removes it and is far narrower than the resonance
        // being measured (fc/Q = 33 Hz even at res 0.9).
        const int SM = 4;                       // +/- SM bins
        for (const auto& c : cases) {
            const auto got = spectrum(renderNoise(c.res, true));
            auto ratioDb = [&](int k) {
                double num = 0, den = 0;
                for (int j = k - SM; j <= k + SM; j++) {
                    if (j < 1 || j >= N / 2) continue;
                    num += got[(size_t)j] * got[(size_t)j];
                    den += flat[(size_t)j] * flat[(size_t)j];
                }
                return den > 0 ? 10 * std::log10(num / den) : -300.0;
            };
            double atFc = 0, peak = -300;
            for (int k = 1; k < N / 2; k++) {
                const double f = k * binHz;
                if (f < 20 || f > 20000) continue;
                const double db = ratioDb(k);
                peak = std::max(peak, db);
                if (std::abs(f - 1000.0) < binHz * 0.5) atFc = db;
            }
            check(std::abs(atFc - c.atFc) < 0.6,
                  "LP24 |H(fc)| at res " + std::to_string(c.res).substr(0, 4)
                      + " matches the mapping",
                  std::to_string(atFc) + " dB, expected " + std::to_string(c.atFc)
                      + " (old cascade " + std::to_string(c.oldAtFc) + ")");
            check(std::abs(peak - c.peak) < 0.6,
                  "LP24 max|H| at res " + std::to_string(c.res).substr(0, 4)
                      + " matches the mapping",
                  std::to_string(peak) + " dB, expected " + std::to_string(c.peak)
                      + " (old cascade " + std::to_string(c.oldPeak) + ")");
        }

        // The other half of B3: the top of the knob must actually ring. The old
        // cascade bottomed out at Q ~= 14 (45 ms to -60 dB); one resonant stage
        // at k1 = 0.002 is Q ~= 470 (about a second). Cut the noise and time the
        // tail — the amp envelope is held open, so what decays is the filter.
        {
            Engine e; e.prepare(sr); e.setTables(tables);
            e.setParams(noisePatch(0.999f, true));
            std::vector<float> L((size_t)(0.5 * sr)), R(L.size());
            e.noteOn(60, 1.0);
            e.render(L.data(), R.data(), (int)L.size());
            e.params()[NOISE_LEVEL] = 0.0f;                 // silence the source
            const int tailN = (int)(2.0 * sr);
            std::vector<float> T((size_t)tailN, 0.0f), TR((size_t)tailN, 0.0f);
            e.render(T.data(), TR.data(), tailN);
            double pk = 0;
            for (int i = 0; i < (int)(0.02 * sr); i++) pk = std::max(pk, (double)std::abs(T[(size_t)i]));
            int last = 0;
            for (int i = 0; i < tailN; i++)
                if (std::abs(T[(size_t)i]) > pk * 1e-3) last = i;   // -60 dB
            const double ring = (double)last / sr;
            check(finite(T) && ring > 0.3,
                  "LP24 at res 1 rings near self-oscillation (was 45 ms)",
                  std::to_string(ring) + " s to -60 dB");
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

    printf("\n== 5b. Tone EQ shapes spectrum ==\n");
    {
        // Drive a pure sine through Fx with every dynamics/time stage bypassed so
        // only the EQ (and the transparent DC block / limiter, kept linear by the
        // small 0.1 amplitude) touches the signal. Measure RMS vs a flat run.
        auto eqRun = [&](double freqHz, float onOff, float lowDb, float midDb,
                         float mFreq, float highDb) {
            Fx fx; fx.prepare(sr);
            auto p = defaultParams();
            p[FXCOMP_ON] = 0; p[FXDRIVE_ON] = 0; p[FXCHORUS_ON] = 0;
            p[FXDELAY_ON] = 0; p[FXREVERB_ON] = 0;
            p[FXEQ_ON] = onOff; p[FXEQ_LOW] = lowDb; p[FXEQ_MID] = midDb;
            p[FXEQ_MFREQ] = mFreq; p[FXEQ_HIGH] = highDb;
            fx.setParams(p);
            int n = (int)(0.5 * sr);
            std::vector<float> L(n), R(n);
            for (int i = 0; i < n; i++)
                L[i] = R[i] = 0.1f * (float)std::sin(2 * M_PI * freqHz * i / sr);
            for (int off = 0; off < n; off += 128) {
                int bs = std::min(128, n - off);
                fx.process(L.data() + off, R.data() + off, bs);
            }
            // Skip the first 0.1 s so smoothing/limiter settling doesn't skew RMS.
            return rms(L, (int)(0.1 * sr));
        };
        // The FX chain applies a fixed makeup/master gain, so compare every band
        // against a per-frequency *flat* run (EQ on, all 0 dB); the chain gain
        // cancels in the ratio and only the EQ's effect remains. +12 dB ≈ 3.98x.
        double flat50 = eqRun(50, 1, 0, 0, 900, 0);
        double flat900 = eqRun(900, 1, 0, 0, 900, 0);
        double flat1k = eqRun(1000, 1, 0, 0, 900, 0);
        double flat10k = eqRun(10000, 1, 0, 0, 900, 0);

        // Off with dialed gains equals the flat run — bypass forces 0 dB.
        double off1k = eqRun(1000, 0, 12, 6, 900, 12);
        check(std::abs(off1k - flat1k) / flat1k < 0.01, "EQ off ignores dialed gains (flat bypass)",
              "off=" + std::to_string(off1k) + " flat=" + std::to_string(flat1k));

        // Low shelf +12 dB boosts a 50 Hz tone (well below the 120 Hz corner).
        double lowBoost = eqRun(50, 1, 12, 0, 900, 0) / flat50;
        check(lowBoost > 3.3 && lowBoost < 4.3, "low shelf +12 dB boosts sub-bass ~4x",
              "gain=" + std::to_string(lowBoost));

        // High shelf +12 dB boosts a 10 kHz tone but leaves 50 Hz alone.
        double hiBoost = eqRun(10000, 1, 0, 0, 900, 12) / flat10k;
        check(hiBoost > 3.2 && hiBoost < 4.3, "high shelf +12 dB boosts treble ~4x",
              "gain=" + std::to_string(hiBoost));
        double hiLeavesLow = eqRun(50, 1, 0, 0, 900, 12) / flat50;
        check(std::abs(hiLeavesLow - 1.0) < 0.05, "high shelf leaves sub-bass alone",
              "gain=" + std::to_string(hiLeavesLow));

        // Mid bell +12 dB at its centre frequency boosts that band.
        double midBoost = eqRun(900, 1, 0, 12, 900, 0) / flat900;
        check(midBoost > 2.5, "mid bell +12 dB boosts its centre",
              "gain=" + std::to_string(midBoost));

        // A cut works too: low shelf -12 dB attenuates sub-bass.
        double lowCut = eqRun(50, 1, -12, 0, 900, 0) / flat50;
        check(lowCut < 0.4, "low shelf -12 dB cuts sub-bass",
              "gain=" + std::to_string(lowCut));
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

        // A malformed/heavily-stacked DETUNE modulation must stay within the
        // parameter's 0..1 range before it reaches the oscillator table reader.
        // The intentionally extreme route depth would previously let phase jump
        // beyond a wavetable in one sample.
        Engine detune; detune.prepare(sr); detune.setTables(tables);
        auto& detuneParams = detune.params(); detuneParams = defaultParams();
        detuneParams[OSCA_BASE + OSC_UNISON] = 16;
        for (int slot = 1; slot <= MOD_MATRIX_SIZE; ++slot) {
            int base = matBase(slot);
            detuneParams[base + MAT_SRC] = 4;   // velocity: a stable +1 source
            detuneParams[base + MAT_DST] = 11;  // A DETUNE
            detuneParams[base + MAT_AMT] = 100;
        }
        detune.noteOn(127, 1.0);
        std::vector<float> detuneL(4096), detuneR(4096);
        detune.render(detuneL.data(), detuneR.data(), (int)detuneL.size());
        check(finite(detuneL) && finite(detuneR) && peak(detuneL) < 4.0f,
              "heavy A DETUNE modulation stays finite and bounded",
              "peak=" + std::to_string(peak(detuneL)));
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

    printf("\n== 11b. Live-mod viz feed (vizMod / vizModAny) ==\n");
    {
        check((int)MOD_DESTS.size() == NUM_MOD_DESTS,
              "NUM_MOD_DESTS matches MOD_DESTS.size()");

        // A routed sounding voice publishes moving per-destination route sums;
        // after release the feed goes idle (vizModAny false) so the UI can hide
        // its indicators rather than freeze them. LFO1 -> F1 CUT (dst 3) at a
        // slow rate: consecutive blocks must show different sums.
        Engine e; e.prepare(sr); e.setTables(tables);
        auto& q = e.params(); q = defaultParams();
        q[LFO1_BASE + LFO_SHAPE] = 2;      // SAW — monotonic within a cycle
        q[LFO1_BASE + LFO_RATE]  = 2.0f;
        q[LFO1_BASE + LFO_RETRIG] = 1;
        q[MAT5_BASE + MAT_SRC] = 1;        // LFO 1
        q[MAT5_BASE + MAT_DST] = 3;        // F1 CUT
        q[MAT5_BASE + MAT_AMT] = 0.8f;
        std::vector<float> a(2048), b(2048);
        check(!e.vizModAny, "viz feed idle before any note");
        e.noteOn(60, 1.0);
        e.render(a.data(), b.data(), (int)a.size());
        const double x1 = e.vizMod[3];
        check(e.vizModAny, "viz feed active while a routed voice sounds");
        check(std::isfinite(x1) && std::abs(x1) <= 0.8 + 1e-9,
              "F1 CUT route sum bounded by amt", "x=" + std::to_string(x1));
        e.render(a.data(), b.data(), (int)a.size());
        const double x2 = e.vizMod[3];
        check(std::abs(x2 - x1) > 1e-4, "route sum moves with the LFO across blocks",
              "x1=" + std::to_string(x1) + " x2=" + std::to_string(x2));
        check(e.vizMod[1] == 0.0 && e.vizMod[17] == 0.0,
              "unrouted destinations stay zero");
        e.noteOff(60);
        for (int i = 0; i < 64 && e.vizActive > 0; ++i)   // ride out the release tail
            e.render(a.data(), b.data(), (int)a.size());
        check(!e.vizModAny, "viz feed idle after release (no stale frozen values)");

        // Global-only routes (PITCH) have no owning knob: the feed stays inactive.
        Engine g2; g2.prepare(sr); g2.setTables(tables);
        auto& qg = g2.params(); qg = defaultParams();
        qg[MAT5_BASE + MAT_SRC] = 1; qg[MAT5_BASE + MAT_DST] = 4; qg[MAT5_BASE + MAT_AMT] = 0.5f; // LFO1 -> PITCH
        g2.noteOn(60, 1.0);
        g2.render(a.data(), b.data(), (int)a.size());
        check(!g2.vizModAny, "global-only route (PITCH) does not activate the per-knob feed");
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
        // band-limited mip path for 16 summed voices. Now that the mask scales
        // with the harmonic spacing the measurement is clean here too, so this
        // holds the same -85 dB bound as the single-voice check rather than the
        // -55 dB the leakage-limited metric used to need. Measured -103 to
        // -106 dB, so the margin is ~18 dB.
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
                check(db < -85.0, "16-voice alias floor low @ note " + std::to_string(note),
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

    printf("\n== 13. Note sequencer (worklet.js seqFire/seqGateOff parity) ==\n");
    {
        // -- constants + packed-layout parity with noteseq.ts --
        check(SEQ_ACCENT_VEL == 1.0f && SEQ_PLAIN_VEL == 0.72f,
              "accent/plain velocities 1.0 / 0.72");
        check(std::abs(SEQ_SWING_MAX - 0.667) < 1e-9, "SWING_MAX = 0.667");
        check(SEQ_PATTERN_BYTES == 4 * 16 * 3, "4 patterns x 16 steps x 3 bytes");
        {
            auto pats = makeEmptySeqPatterns();
            bool neutral = true;
            for (int p = 0; p < SEQ_NPATTERNS; p++)
                for (int s = 0; s < SEQ_STEPS; s++) {
                    auto st = getNoteSeqStep(pats.data(), p, s);
                    if (st.on || st.oct != 0 || st.duration != 1) neutral = false;
                }
            check(neutral, "empty patterns read back as neutral rests (duration 1, oct 0)");
            NoteSeqStep w; w.on = true; w.note = 7; w.oct = -1; w.acc = true; w.duration = 5;
            setNoteSeqStep(pats.data(), 2, 11, w);
            auto r = getNoteSeqStep(pats.data(), 2, 11);
            check(r.on && r.acc && r.duration == 5 && r.note == 7 && r.oct == -1, "step round-trips");
            auto er = Engine::readSeqStep(pats.data(), 2, 11);
            check(er.on && er.acc && er.duration == 5 && er.semi == 7 - 12,
                  "engine unpack folds note+oct to semi", std::to_string(er.semi));
        }

        // Base sequencer params: fast attack, full sustain, short release so
        // gate-off edges are observable; seq defaults (120 BPM -> 6000-sample
        // steps at 48 kHz).
        auto seqParams = [&] {
            auto p = defaultParams();
            p[ENV1_BASE + 0] = 0.001f;  // ATK
            p[ENV1_BASE + 2] = 1.0f;    // SUS
            p[ENV1_BASE + 3] = 0.005f;  // REL
            return p;
        };
        const double stepDur = (60.0 / 120.0 / 4.0) * sr;   // 6000
        auto onset = [](const std::vector<float>& v, int from) {
            for (int i = from; i < (int)v.size(); i++)
                if (std::abs(v[i]) > 1e-4f) return i;
            return -1;
        };
        auto rmsw = [](const std::vector<float>& v, int a, int b) {
            double s = 0; int n = 0;
            for (int i = a; i < b && i < (int)v.size(); i++) { s += (double)v[i] * v[i]; n++; }
            return n ? std::sqrt(s / n) : 0.0;
        };
        auto renderSeq = [&](const std::vector<uint8_t>& pats, const ParamArray& p,
                             int nSamples) {
            Engine e; e.prepare(sr); e.setTables(tables);
            e.setParams(p);
            e.setSeqPatterns(pats.data(), (int)pats.size());
            e.seqPlay();
            std::vector<float> L((size_t)nSamples, 0.0f), R((size_t)nSamples, 0.0f);
            e.render(L.data(), R.data(), nSamples);
            return L;
        };

        // -- (a) steps fire at the right sample positions for a given BPM --
        {
            auto pats = makeEmptySeqPatterns();
            NoteSeqStep s0; s0.on = true; s0.note = 0;
            setNoteSeqStep(pats.data(), 0, 0, s0);
            setNoteSeqStep(pats.data(), 0, 4, s0);
            auto buf = renderSeq(pats, seqParams(), 30000);
            const int o0 = onset(buf, 0);
            const int o4 = onset(buf, 20000);   // past step 0's gated tail
            check(o0 >= 0 && o0 < 96, "step 1 fires at sample 0", std::to_string(o0));
            check(o4 >= 24000 && o4 < 24000 + 96,
                  "step 5 fires at 4 * 6000 samples @ 120 BPM", std::to_string(o4));
            check(rmsw(buf, 12000, 23000) < 1e-4,
                  "rest steps stay silent between the notes");
        }

        // -- (b) accent velocity 1.0 vs 0.72 (velGain = 0.25 + 0.75 v^2) --
        {
            auto pats = makeEmptySeqPatterns();
            NoteSeqStep s0; s0.on = true; s0.note = 0;
            setNoteSeqStep(pats.data(), 0, 0, s0);
            auto plain = renderSeq(pats, seqParams(), 3200);
            s0.acc = true;
            setNoteSeqStep(pats.data(), 0, 0, s0);
            auto accented = renderSeq(pats, seqParams(), 3200);
            const double rp = rmsw(plain, 500, 3000), ra = rmsw(accented, 500, 3000);
            const double expect = 1.0 / (0.25 + 0.75 * 0.72 * 0.72); // 1.5655
            check(rp > 1e-4 && std::abs(ra / rp - expect) < 0.15 * expect,
                  "accent/plain gain ratio matches velocities 1.0/0.72",
                  "ratio=" + std::to_string(ra / rp) + " expect=" + std::to_string(expect));
        }

        // -- (c) duration: a note sustains for N steps without retrigger --
        {
            auto pats = makeEmptySeqPatterns();
            NoteSeqStep s0; s0.on = true; s0.note = 0;
            s0.duration = 2;                       // two-step note (gate off @ 12000)
            setNoteSeqStep(pats.data(), 0, 0, s0);
            auto held = renderSeq(pats, seqParams(), 15000);
            s0.duration = 1;                       // one-step note (gate off @ 6000)
            setNoteSeqStep(pats.data(), 0, 0, s0);
            auto cut = renderSeq(pats, seqParams(), 15000);

            // a two-step note keeps sounding across the second step where the
            // one-step note has already released
            const double rHeld = rmsw(held, 7000, 11000), rCut = rmsw(cut, 7000, 11000);
            check(rHeld > 10 * std::max(rCut, 1e-6),
                  "duration holds the gate across steps",
                  std::to_string(rCut) + " -> " + std::to_string(rHeld));

            // no envelope retrigger at the step-1 boundary (6000): a single
            // sustained note stays continuous across it
            double wMin = 1e9, wMax = 0;
            for (int a = 5000; a + 256 <= 7200; a += 128) {
                double w = rmsw(held, a, a + 256);
                wMin = std::min(wMin, w); wMax = std::max(wMax, w);
            }
            check(wMax > 1e-3 && wMin > 0.3 * wMax,
                  "duration does not retrigger the amp envelope (no level dip)",
                  "min/max=" + std::to_string(wMin / wMax));
        }

        // -- (d) a one-step note gates off at its step boundary --
        {
            auto pats = makeEmptySeqPatterns();
            NoteSeqStep s0; s0.on = true; s0.note = 0;   // duration 1 (gate off @ 6000)
            setNoteSeqStep(pats.data(), 0, 0, s0);
            auto buf = renderSeq(pats, seqParams(), 9000);
            check(rmsw(buf, 1000, 2900) > 1e-3, "one-step note sounding within its step");
            // release (0.005s = 240 samples) -> silent shortly after 6000
            check(rmsw(buf, 6500, 8500) < 0.05 * rmsw(buf, 1000, 2900),
                  "one-step note released after its step boundary",
                  std::to_string(rmsw(buf, 6500, 8500)));
        }

        // -- (e) overlapping notes keep independent duration timers --
        {
            auto pats = makeEmptySeqPatterns();
            NoteSeqStep c; c.on = true; c.note = 0; c.duration = 3;
            NoteSeqStep e4; e4.on = true; e4.note = 4; e4.duration = 1;
            setNoteSeqStep(pats.data(), 0, 0, c);
            setNoteSeqStep(pats.data(), 0, 1, e4);

            Engine e; e.prepare(sr); e.setTables(tables);
            e.setParams(seqParams());
            e.setSeqPatterns(pats.data(), (int)pats.size());
            e.seqPlay();
            float L[128], R[128];
            auto run = [&](int samples) {
                while (samples > 0) {
                    const int n = std::min(samples, 128);
                    e.render(L, R, n);
                    samples -= n;
                }
            };

            run(6100); // C starts at 0; E starts at 6000, while C is held.
            check(e.seqPendingOffCount() == 2,
                  "overlapping sequencer notes keep both pending note-offs");
            run(6000); // E's one-step duration has elapsed; C still has one step.
            check(e.seqPendingOffCount() == 1,
                  "short overlapping note releases without cutting the long note");
            run(6000);
            check(e.seqPendingOffCount() == 0,
                  "long overlapping note releases at its own duration boundary");
        }

        // -- (f) swing delays odd 16ths by swing * 0.667 * step --
        {
            auto pats = makeEmptySeqPatterns();
            NoteSeqStep s0; s0.on = true; s0.note = 0;
            setNoteSeqStep(pats.data(), 0, 0, s0);
            setNoteSeqStep(pats.data(), 0, 1, s0);
            // percussive pluck (SUS 0, short DEC) so each step is a distinct
            // onset — seq.gate is retired, so discrete transients replace it
            auto pluck = seqParams();
            pluck[ENV1_BASE + 1] = 0.01f;  // DEC
            pluck[ENV1_BASE + 2] = 0.0f;   // SUS
            auto straight = renderSeq(pats, pluck, 14000);
            auto pSwung = pluck; pSwung[SEQ_SWING] = 1.0f;
            auto swung = renderSeq(pats, pSwung, 14000);
            const int oS = onset(straight, 3000), oW = onset(swung, 3000);
            check(oS >= 5900 && oS < 6100, "swing 0: step 2 fires at 6000", std::to_string(oS));
            const double delta = oW - oS, expect = SEQ_SWING_MAX * stepDur; // 4002
            check(std::abs(delta - expect) < 130,
                  "full swing delays step 2 by 0.667 * dur",
                  std::to_string(delta) + " vs " + std::to_string(expect));
        }

        // -- (f) chain advances to the next pattern at the bar wrap --
        {
            auto pats = makeEmptySeqPatterns();
            NoteSeqStep s0; s0.on = true; s0.note = 0;
            setNoteSeqStep(pats.data(), 0, 0, s0);
            NoteSeqStep sB; sB.on = true; sB.note = 2;
            setNoteSeqStep(pats.data(), 1, 0, sB);
            Engine e; e.prepare(sr); e.setTables(tables);
            e.setParams(seqParams());
            e.setSeqPatterns(pats.data(), (int)pats.size());
            const int chain[2] = { 0, 1 };
            e.setSeqChain(chain, 2);
            e.seqPlay();
            std::vector<float> L((size_t)(17.5 * stepDur), 0.0f), R(L.size(), 0.0f);
            e.render(L.data(), R.data(), (int)L.size());   // 1.5 bars
            check(e.seqCurrentPattern() == 1, "chain A->B follows the bar wrap",
                  std::to_string(e.seqCurrentPattern()));
            check(e.seqCurrentStep() == 1, "step counter wrapped into bar 2",
                  std::to_string(e.seqCurrentStep()));
            e.seqStop();
            check(e.seqCurrentStep() == -1 && !e.seqIsPlaying(), "stop resets the step");
        }

        // -- (g) host transport lock: steps derive from the playhead ppq --
        {
            auto pats = makeEmptySeqPatterns();
            NoteSeqStep s0; s0.on = true; s0.note = 0;
            setNoteSeqStep(pats.data(), 0, 0, s0);
            setNoteSeqStep(pats.data(), 0, 4, s0);
            Engine e; e.prepare(sr); e.setTables(tables);
            e.setParams(seqParams());
            e.setSeqPatterns(pats.data(), (int)pats.size());
            // host rolling at 150 BPM from ppq 0 — no internal play() at all
            const double bpm = 150.0, spb = 60.0 / bpm * sr; // samples per beat
            std::vector<float> L((size_t)(2.0 * spb), 0.0f), R(L.size(), 0.0f);
            double ppq = 0;
            const int block = 480;
            for (int off = 0; off + block <= (int)L.size(); off += block) {
                e.setBpmOverride(bpm);
                e.setSeqHostTransport(ppq, bpm, true);
                e.render(L.data() + off, R.data() + off, block);
                ppq += block / spb;
            }
            check(e.seqIsPlaying(), "host transport reports playing");
            const int o0 = onset(L, 0);
            const int o4 = onset(L, (int)(0.8 * spb));
            const int expect4 = (int)(1.0 * spb);            // step 4 = beat 1
            check(o0 >= 0 && o0 < 96, "host-locked step 1 fires at ppq 0", std::to_string(o0));
            check(o4 >= expect4 - 96 && o4 < expect4 + 96,
                  "host-locked step 5 fires on beat 2 at 150 BPM",
                  std::to_string(o4) + " vs " + std::to_string(expect4));
            e.setSeqHostTransport(ppq, bpm, false);          // host stop
            check(!e.seqIsPlaying() && e.seqCurrentStep() == -1, "host stop stops the sequencer");
        }
    }


    // A patch that exercises the whole voice: two oscs, unison, sub, LP24 with
    // resonance and drive, and an envelope that is fully open by the time the
    // measurement window starts. Used by the invariance + click checks.
    auto invariancePatch = [] {
        auto p = defaultParams();
        p[OSCA_BASE + OSC_POS] = 0.66f;
        p[OSCA_BASE + OSC_UNISON] = 3;
        p[OSCA_BASE + OSC_DETUNE] = 0.3f;
        p[SUB_LEVEL] = 0.4f;
        p[FILTER1_BASE + FLT_ON] = 1;
        p[FILTER1_BASE + FLT_TYPE] = 1;              // LP24
        p[FILTER1_BASE + FLT_CUTOFF] = 2400;
        p[FILTER1_BASE + FLT_RES] = 0.5f;
        p[FILTER1_BASE + FLT_DRIVE] = 0.4f;
        p[ENV1_BASE + 0] = 0.002f;                   // fast attack
        p[ENV1_BASE + 2] = 1.0f;                     // full sustain
        p[ENV1_BASE + 3] = 0.05f;                    // short release
        return p;
    };

    printf("\n== 14. Sample-rate invariance ==\n");
    {
        // Same patch, same note, three device rates. The engine's smoothers,
        // envelopes and DC/noise poles are all derived from sr, so the octave
        // band profile must be the same at every rate.
        const double rates[3] = {44100.0, 48000.0, 96000.0};
        std::vector<std::vector<double>> prof;
        for (double r : rates) {
            Engine e; e.prepare(r); e.setTables(tables);
            auto p = invariancePatch();
            // Drive off for this one: at 96 kHz the mip guard admits an octave
            // more harmonics, and a saturator folds those extra partials into
            // the mid band. That is correct behaviour, not a smoother bug, and
            // it is not what this test is about.
            p[FILTER1_BASE + FLT_DRIVE] = 0;
            // Detune off too: detuned unison voices beat at a fixed rate in
            // Hz, and a fixed 32768-SAMPLE window covers a different span of
            // seconds at each rate, so the beat lands at a different phase.
            // That is the measurement moving, not the engine.
            p[OSCA_BASE + OSC_DETUNE] = 0;
            e.setParams(p);
            const int n = (int)(1.5 * r);
            std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
            e.noteOn(57, 1.0);                        // A3
            e.render(L.data(), R.data(), n);
            std::vector<float> tail(L.end() - 32768, L.end());
            prof.push_back(bandProfileDb(tail, 32768, r));
        }
        // Only bands the patch actually fills are comparable: a band 35 dB
        // below the total holds nothing but the DC-blocker residue and the
        // float32 floor, where a large relative difference means nothing.
        for (int i = 0; i < 3; i += 2) {              // 44.1 and 96 vs the 48 k reference
            double worst = 0; int worstBand = -1;
            for (size_t b = 0; b < prof[1].size(); b++) {
                if (prof[1][b] < -35.0 || prof[(size_t)i][b] < -35.0) continue;
                const double d = std::abs(prof[(size_t)i][b] - prof[1][b]);
                if (d > worst) { worst = d; worstBand = (int)b; }
            }
            check(worst < 0.5,
                  "band profile matches 48 kHz @ " + std::to_string((int)rates[i]) + " Hz",
                  "worst " + std::to_string(worst) + " dB in band " + std::to_string(worstBand));
        }
    }

    printf("\n== 15. Host block-size invariance ==\n");
    {
        // The engine chunks internally to 128 samples, but a host block that is
        // not a multiple of 128 splits those chunks differently, which changes
        // every smoother's cadence. The spectrum must not notice.
        auto renderBlocks = [&](int block) {
            Engine e; e.prepare(sr); e.setTables(tables);
            e.setParams(invariancePatch());
            const int n = 1 << 16;
            std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
            e.noteOn(57, 1.0);
            for (int off = 0; off < n; off += block) {
                const int m = std::min(block, n - off);
                e.render(L.data() + off, R.data() + off, m);
            }
            std::vector<float> tail(L.end() - 32768, L.end());
            return bandProfileDb(tail, 32768, sr);
        };
        const auto ref = renderBlocks(128);
        for (int block : {480, 512, 4096}) {
            const auto got = renderBlocks(block);
            double worst = 0;
            for (size_t b = 0; b < ref.size(); b++) {
                if (ref[b] < -35.0 || got[b] < -35.0) continue;
                worst = std::max(worst, std::abs(got[b] - ref[b]));
            }
            check(worst < 1.0, "band profile matches 128-sample blocks @ " + std::to_string(block),
                  "worst " + std::to_string(worst) + " dB");
        }
    }

    printf("\n== 16. Click detector (release / voice steal / filter switch) ==\n");
    {
        // A click is a sample-to-sample step far larger than the signal's own.
        // Measure the waveform's natural max |dx| first, then check that each
        // discontinuity event does not exceed a small multiple of it.
        Engine e; e.prepare(sr); e.setTables(tables);
        e.setParams(invariancePatch());
        const int seg = 4096;
        std::vector<float> L((size_t)seg * 6, 0.0f), R(L.size(), 0.0f);
        e.noteOn(57, 1.0);
        e.render(L.data(), R.data(), seg * 2);
        const double natural = maxDelta(L, seg, seg * 2);
        check(natural > 1e-4, "steady-state slew measurable", std::to_string(natural));

        e.noteOff(57);
        e.render(L.data() + seg * 2, R.data() + seg * 2, seg);
        const double relDelta = maxDelta(L, seg * 2, seg * 3);
        check(relDelta < natural * 1.5, "no click on note release",
              std::to_string(relDelta) + " vs " + std::to_string(natural));

        // Voice steal: fill every voice, then take one more note. The stolen
        // voice fades over STEAL_FADE_SEC instead of cutting.
        Engine st; st.prepare(sr); st.setTables(tables);
        st.setParams(invariancePatch());
        std::vector<float> SL((size_t)seg * 3, 0.0f), SR(SL.size(), 0.0f);
        for (int i = 0; i < NVOICES; i++) st.noteOn(48 + i, 1.0);
        st.render(SL.data(), SR.data(), seg * 2);
        const double stNatural = maxDelta(SL, seg, seg * 2);
        st.noteOn(72, 1.0);                            // one voice too many -> steal
        st.render(SL.data() + seg * 2, SR.data() + seg * 2, seg);
        const double stealDelta = maxDelta(SL, seg * 2, seg * 3);
        check(stealDelta < stNatural * 2.0, "no click on voice steal",
              std::to_string(stealDelta) + " vs " + std::to_string(stNatural));

    }

    printf("\n== 16b. Discrete switches are crossfaded (finding J2) ==\n");
    {
        // Every discrete switch used to land in one sample: the filter type and
        // route re-plumb the section (a route change even swaps which scratch
        // buffer is the output), a TABLE change swaps the wavetable under a
        // running phase, and SUB SHAPE jumps between sine and square. Each is
        // now rendered twice for 3 ms and equal-power crossfaded, so the step
        // at the switch must stay inside the signal's own slew.
        const int seg = 4096;
        // The reference is the signal's own slew on BOTH sides of the switch: a
        // switch to HP12 or to a square sub legitimately raises the slew, and
        // that is the new tone, not a click. A real click is a step far larger
        // than either steady state produces.
        // A window straddling the switch must not carry more high-frequency
        // energy than the steady state on either side of it. Both slew and
        // brightness change legitimately at a switch; a step does not.
        const int W = 1024;
        const double fHi = 9000.0;
        // The switch instant is SWEPT across one period of the note. At a fixed
        // instant the two waveforms are often near-continuous by luck, and the
        // test then measures nothing: with the crossfade disabled, a fixed
        // switch point reported table and sub-shape changes as clean. Sixteen
        // phases over one period of A3 (218 samples) and the WORST case is what
        // gets asserted.
        const double period = sr / (440.0 * std::pow(2.0, (57 - 69) / 12.0));  // A3, 218.2
        const int kPhases = 16;
        auto expect = [&](const std::string& what, const ParamArray& patch, int pid, float to) {
            double worstHf = 0, refAtWorst = 0, worstDx = 0, slewAtWorst = 1e-30;
            bool allFinite = true;
            int worstPhase = 0;
            for (int ph = 0; ph < kPhases; ph++) {
                Engine e; e.prepare(sr); e.setTables(tables);
                e.setParams(patch);
                const int at = seg * 2 + (int)(period * ph / kPhases);
                std::vector<float> L((size_t)seg * 5, 0.0f), R(L.size(), 0.0f);
                e.noteOn(57, 1.0);
                e.render(L.data(), R.data(), at);
                e.params()[(size_t)pid] = to;
                e.render(L.data() + at, R.data() + at, seg * 5 - at);
                if (!finite(L)) allFinite = false;
                const double hAt   = hfFraction(L, at - W / 8, W, sr, fHi);
                const double ref   = std::max(hfFraction(L, at - W - W / 2, W, sr, fHi),
                                              hfFraction(L, at + seg, W, sr, fHi));
                const double sw    = maxDelta(L, at, at + W);
                const double slew  = std::max(maxDelta(L, at - seg, at),
                                              maxDelta(L, at + seg, at + 2 * seg));
                // Rank phases by how far each metric exceeds its own bound, so
                // the reported case is the worst one and not just the loudest.
                if (hAt / std::max(ref, 1e-12) > worstHf / std::max(refAtWorst, 1e-12)) {
                    worstHf = hAt; refAtWorst = ref; worstPhase = ph;
                }
                if (sw / slew > worstDx / slewAtWorst) { worstDx = sw; slewAtWorst = slew; }
            }
            // A fade to silence has no post-switch steady state, so the ratio
            // reference collapses to the pre-switch window (or to zero). The
            // absolute floor keeps that case meaningful: a 3 ms fade-out has a
            // little bandwidth of its own, while the uncrossfaded step measures
            // 1899e-6 here -- 1900x the floor.
            check(allFinite && worstHf < refAtWorst * 3.0 + 1e-6 && worstDx < slewAtWorst * 1.3,
                  "no click on " + what,
                  "worst of " + std::to_string(kPhases) + " phases (#" + std::to_string(worstPhase)
                  + "): hf " + std::to_string(worstHf * 1e6) + "e-6 vs "
                  + std::to_string(refAtWorst * 1e6) + "e-6, |dx| " + std::to_string(worstDx)
                  + " vs " + std::to_string(slewAtWorst));
        };

        auto base = invariancePatch();
        base[SUB_ON] = 1;                              // sub audible for the SHAPE case
        base[FILTER1_BASE + FLT_RES] = 0.85f;          // a loaded filter: switching
        base[FILTER1_BASE + FLT_CUTOFF] = 700;         // types with the state hot is
        base[FILTER1_BASE + FLT_DRIVE] = 0;            // the worst case for a click
        for (int ft : {0, 2, 3, 4, 5, 6})
            expect("filter type switch to " + std::to_string(ft), base,
                   FILTER1_BASE + FLT_TYPE, (float)ft);

        // Route: both filters on so all three topologies actually differ.
        auto twoFilters = base;
        twoFilters[FILTER2_BASE + FLT_ON] = 1;
        twoFilters[FILTER2_BASE + FLT_TYPE] = 0;       // LP12
        twoFilters[FILTER2_BASE + FLT_CUTOFF] = 1400;
        twoFilters[OSCB_BASE + OSC_ON] = 1;            // SPLIT needs oscB
        twoFilters[OSCB_BASE + OSC_LEVEL] = 0.7f;
        for (int rt : {1, 2})
            expect("filter route switch to " + std::to_string(rt), twoFilters, FILTER_ROUTE, (float)rt);

        // Oscillator-side switches are measured with the filter OFF: a click
        // born in the oscillator would otherwise be low-passed away before it
        // reaches the output, and the test would be measuring the filter.
        auto bare = base;
        bare[FILTER1_BASE + FLT_ON] = 0;
        bare[OSCA_BASE + OSC_UNISON] = 1;
        bare[OSCA_BASE + OSC_DETUNE] = 0;
        bare[OSCA_BASE + OSC_POS] = 0.0f;   // the smooth end of each table:
                                            // a step there is not hidden by the
                                            // waveform's own edges
        bare[SUB_ON] = 0;
        for (int ti = 1; ti < 6; ti++)
            expect("table switch to slot " + std::to_string(ti), bare, OSCA_BASE + OSC_TABLE, (float)ti);
        expect("oscillator ON -> OFF", bare, OSCA_BASE + OSC_ON, 0.0f);

        // Sub shape: sine -> square, on a bare patch where the sub dominates.
        auto subPatch = bare;
        subPatch[SUB_ON] = 1;
        subPatch[SUB_LEVEL] = 0.9f;
        subPatch[OSCA_BASE + OSC_LEVEL] = 0.15f;
        expect("sub shape switch (sine -> square)", subPatch, SUB_SHAPE, 1.0f);

        // LFO shape, routed to POS so the value change reaches the audio.
        auto lfoPatch = bare;
        lfoPatch[LFO1_BASE + LFO_SHAPE] = 0;           // SINE
        lfoPatch[LFO1_BASE + LFO_RATE] = 3.0f;
        lfoPatch[LFO1_BASE + LFO_RETRIG] = 1;
        lfoPatch[MAT1_BASE + MAT_SRC] = 1; lfoPatch[MAT1_BASE + MAT_DST] = 1;
        lfoPatch[MAT1_BASE + MAT_AMT] = 0.9f;
        expect("LFO shape switch (sine -> square)", lfoPatch, LFO1_BASE + LFO_SHAPE, 3.0f);
    }

    printf("\n== 17. User-table import is band-limited (finding J8) ==\n");
    {
        // A single sine at a NON-integer period. The old linear stretch-to-2048
        // import baked its sinc^2 interpolation images into the table as real
        // harmonics; exact spectral resampling leaves only the fundamental.
        const double period = 183.37;                 // not a whole number of samples
        std::vector<float> clip((size_t)(period * 8));
        for (size_t i = 0; i < clip.size(); i++)
            clip[i] = (float)std::sin(2 * M_PI * (double)i / period);
        auto frames = sliceToFrames(clip, period);
        check(frames.size() >= 4 && frames[0].size() == (size_t)SIZE,
              "sliceToFrames yields SIZE-sample frames", std::to_string(frames.size()));

        // One frame is a single cycle: its spectrum must be one line at k = 1.
        std::vector<double> re(SIZE), im(SIZE, 0.0);
        for (int i = 0; i < SIZE; i++) re[(size_t)i] = frames[1][(size_t)i];
        fft(re.data(), im.data(), SIZE, false);
        auto mag = [&](int k) { return std::sqrt(re[(size_t)k] * re[(size_t)k]
                                               + im[(size_t)k] * im[(size_t)k]); };
        const double fund = mag(1);
        double worst = 0; int worstK = 0;
        for (int k = 2; k < SIZE / 2; k++)
            if (mag(k) > worst) { worst = mag(k); worstK = k; }
        const double db = 20 * std::log10(std::max(worst, 1e-30) / std::max(fund, 1e-30));
        check(fund > 0.1, "imported cycle keeps its fundamental", std::to_string(fund));
        check(db < -60.0, "no resampling images in an imported cycle",
              std::to_string(db) + " dB @ harmonic " + std::to_string(worstK));

        // detectCycleLength resolves a fractional period to sub-sample accuracy.
        std::vector<float> tone((size_t)(48000 * 0.4));
        for (size_t i = 0; i < tone.size(); i++)
            tone[i] = (float)std::sin(2 * M_PI * (double)i / period);
        const double det = detectCycleLength(tone, sr);
        check(std::abs(det - period) < 0.25, "detectCycleLength is sub-sample accurate",
              std::to_string(det) + " vs " + std::to_string(period));
    }

    // Deleting the selected table must retain the outgoing samples across
    // render calls, then reclaim them on the message thread after the fade.
    {
        Engine e; e.prepare(sr);
        auto user = std::make_shared<const GeneratedTable>(*tables[0]);
        std::weak_ptr<const GeneratedTable> retired = user;
        e.setTables({tables[0], user});
        user.reset();
        e.setParam(OSCA_BASE + OSC_TABLE, 1);
        e.setParam(ENV1_BASE + 2, 1.0f);
        e.noteOn(60, 1);
        float l[1024], r[1024];
        e.render(l, r, 1024);
        e.setTables({tables[0]});
        e.setParam(OSCA_BASE + OSC_TABLE, 0);
        check(!retired.expired(), "deleted table remains alive between renders");
        e.render(l, r, 1);
        e.collectRetiredTables();
        check(!retired.expired(), "deleted table remains alive during crossfade");
        for (int n = 0; n < 16; ++n) e.render(l, r, 128);
        check(!retired.expired(), "audio leaves table destruction to collector");
        e.collectRetiredTables();
        check(retired.expired(), "deleted table reclaimed after crossfade");
        bool clean = true;
        for (float x : l) clean = clean && std::isfinite(x);
        check(clean, "table deletion produces finite audio");
    }

    printf("\n== 17b. Table publication is lock-free and frees off the audio thread (J3) ==\n");
    {
        // render() used to take a shared_ptr snapshot with the free-function
        // std::atomic_load, which is a hashed spinlock in both libstdc++ and
        // libc++ — the audio thread could block behind a UI table swap — and
        // the audio thread could drop the last reference at the end of a block,
        // i.e. free inside the render callback. Now the message thread owns
        // every set and reclaims it only after the render that could still hold
        // it has returned.
        //
        // Each published set carries a sentinel table whose deleter records the
        // thread that freed it. A real second thread renders continuously while
        // this one republishes, so the check exercises the actual race.
        static std::atomic<int> freeCount{0}, audioFrees{0};
        static std::atomic<bool> audioRunning{false};
        static std::thread::id audioThread;
        auto sentinel = [&](int seed) {
            auto* g = new GeneratedTable();
            g->name = "SENTINEL"; g->frames = 1; g->mips = 1; g->size = SIZE;
            g->data.resize((size_t)SIZE);
            for (int i = 0; i < SIZE; i++)
                g->data[(size_t)i] = (float)std::sin(2 * M_PI * (seed + 1) * i / SIZE);
            return TablePtr(g, [](const GeneratedTable* q) {
                freeCount.fetch_add(1, std::memory_order_relaxed);
                if (audioRunning.load(std::memory_order_acquire)
                    && std::this_thread::get_id() == audioThread)
                    audioFrees.fetch_add(1, std::memory_order_relaxed);
                delete q;
            });
        };

        Engine e; e.prepare(sr);
        e.setTables({sentinel(0)});
        auto p = defaultParams();
        p[OSCA_BASE + OSC_TABLE] = 0;
        p[ENV1_BASE + 2] = 1.0f;
        e.setParams(p);
        e.noteOn(60, 1.0);

        const int blocks = 3000, bs = 256;
        std::atomic<bool> stop{false};
        std::atomic<bool> clean{true};
        std::thread audio([&] {
            audioThread = std::this_thread::get_id();
            audioRunning.store(true, std::memory_order_release);
            std::vector<float> L((size_t)bs), R((size_t)bs);
            for (int b = 0; b < blocks && !stop.load(std::memory_order_relaxed); b++) {
                e.render(L.data(), R.data(), bs);
                for (int i = 0; i < bs; i++)
                    if (!std::isfinite(L[i]) || !std::isfinite(R[i]) || std::abs(L[i]) > 8.0f)
                        clean.store(false, std::memory_order_relaxed);
            }
            audioRunning.store(false, std::memory_order_release);
        });
        // Republish from this (message) thread while that renders.
        int publishes = 0;
        for (int k = 0; k < 400; k++) {
            e.setTables({sentinel(k % 7)});
            publishes++;
            std::this_thread::yield();
        }
        stop.store(true);
        audio.join();

        // Finish the final replacement fade before collecting its old table.
        float settleL[1024], settleR[1024];
        e.render(settleL, settleR, 1024);
        // Everything still pending is reclaimed here, on the message thread.
        e.collectRetiredTables();
        check(clean.load(), "rendered output stays finite and bounded across table swaps");
        check(audioFrees.load() == 0, "no table freed on the audio thread",
              std::to_string(audioFrees.load()) + " audio-thread frees");
        check(freeCount.load() >= publishes - 2,
              "retired sets are reclaimed on the message thread",
              std::to_string(freeCount.load()) + " of " + std::to_string(publishes + 1) + " freed");
        check(e.retiredTableSetCount() <= 1, "retire list drains",
              std::to_string(e.retiredTableSetCount()) + " pending");
    }

    printf("\n== 18. Host automation is not block-rate (finding J1) ==\n");
    {
        // Sweep the cutoff over one second while feeding the engine parameter
        // updates ONCE PER HOST BLOCK, the way a DAW does. At a 1024-sample
        // block that is one step every 21.3 ms; without a smoother the steps
        // modulate the tone at 46.875 Hz and put sidebands on every harmonic.
        // paramTargets() takes the smoothed path (what the plugin uses),
        // params() the old snapped one — so the two runs below are the A/B for
        // the fix itself, no rebuild needed.
        const int hostBlock = 1024;
        const double blockHz = sr / hostBlock;         // 46.875 Hz
        auto sweep = [&](bool smoothed, bool automate = true) {
            Engine e; e.prepare(sr); e.setTables(tables);
            auto p = defaultParams();
            p[OSCA_BASE + OSC_UNISON] = 1;
            p[OSCA_BASE + OSC_POS] = 0.66f;            // harmonically rich
            p[OSCA_BASE + OSC_PAN] = 0;
            p[FILTER1_BASE + FLT_ON] = 1;
            p[FILTER1_BASE + FLT_TYPE] = 1;            // LP24
            p[FILTER1_BASE + FLT_RES] = 0.5f;
            p[ENV1_BASE + 0] = 0.002f; p[ENV1_BASE + 2] = 1.0f;
            e.setParams(p);
            e.noteOn(60, 1.0);                          // 261.63 Hz
            const int n = (int)(1.4 * sr);
            std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
            for (int off = 0; off < n; off += hostBlock) {
                const int m = std::min(hostBlock, n - off);
                const double t = (double)off / n;
                const float cut = automate ? (float)(400.0 * std::pow(6000.0 / 400.0, t))
                                           : (float)(400.0 * std::pow(6000.0 / 400.0, 0.75));
                (smoothed ? e.paramTargets() : e.params())[FILTER1_BASE + FLT_CUTOFF] = cut;
                e.render(L.data() + off, R.data() + off, m);
            }
            return L;
        };
        const int N = 32768;
        const double f0 = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
        auto sm = sweep(true), raw = sweep(false), flat = sweep(true, false);
        std::vector<float> flatTail(flat.end() - N, flat.end());
        std::vector<float> smTail(sm.end() - N, sm.end()), rawTail(raw.end() - N, raw.end());
        const double dbRaw = blockRateSidebandDb(rawTail.data(), N, sr, f0, blockHz);
        const double dbSm  = blockRateSidebandDb(smTail.data(), N, sr, f0, blockHz);
        const double dbFlat = blockRateSidebandDb(flatTail.data(), N, sr, f0, blockHz);
        printf("    block-rate sidebands: snapped %.1f dB, smoothed %.1f dB, static cutoff %.1f dB\n", dbRaw, dbSm, dbFlat);
        check(finite(sm), "smoothed automation output finite");
        // Measured: -45.0 dB snapped, -101.5 dB smoothed, and -111.7 dB with a
        // static cutoff (the window/float floor for this patch). The smoothed
        // run also matches a 128-sample-block render of the same sweep to
        // 0.3 dB, i.e. the block boundary leaves no trace at all.
        check(dbSm < -90.0, "no block-rate line under smoothed automation",
              std::to_string(dbSm) + " dB");
        check(dbSm < dbRaw - 40.0, "smoothing removes >= 40 dB of block-rate sidebands",
              std::to_string(dbRaw) + " -> " + std::to_string(dbSm) + " dB");
        check(dbFlat < -100.0, "static-cutoff reference sits at the measurement floor",
              std::to_string(dbFlat) + " dB");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : (std::to_string(g_fail) + " CHECK(S) FAILED").c_str());
    return g_fail == 0 ? 0 : 1;
}
