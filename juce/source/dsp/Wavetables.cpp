#include "Wavetables.h"
#include <algorithm>
#include <cmath>

namespace fable {

static constexpr double PI = 3.14159265358979323846;

// ---------- FFT (iterative radix-2, complex, in-place) ----------
void fft(double* re, double* im, int n, bool inverse) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = ((inverse ? 2.0 : -2.0) * PI) / len;
        double wr = std::cos(ang), wi = std::sin(ang);
        int half = len >> 1;
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < half; k++) {
                double ur = re[i + k], ui = im[i + k];
                double xr = re[i + k + half], xi = im[i + k + half];
                double vr = xr * cr - xi * ci;
                double vi = xr * ci + xi * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + half] = ur - vr; im[i + k + half] = ui - vi;
                double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
    if (inverse) {
        for (int i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
    }
}

// Add harmonic k with amplitude a, phase ph (cosine convention) to a spectrum.
static inline void setHarm(double* re, double* im, int k, double a, double ph) {
    re[k] += a * std::cos(ph);
    im[k] += a * std::sin(ph);
    re[SIZE - k] += a * std::cos(ph);
    im[SIZE - k] -= a * std::sin(ph);
}

static const double SINE_PH = -PI / 2; // sin(x) == cos(x - pi/2)

static inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

// ---------- table specs ----------
static void specPrime(double t, double* re, double* im) {
    double seg = std::min(2.9999, t * 3);
    int s = (int)seg; double f = seg - s;
    for (int k = 1; k <= 1024; k++) {
        double sine = (k == 1) ? 1.0 : 0.0;
        double tri = (k % 2 == 1) ? ((((k - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (double)(k * k)) : 0.0;
        double saw = 1.0 / k;
        double sqr = (k % 2 == 1) ? 1.27 / k : 0.0;
        double shapes[4] = {sine, tri, saw, sqr};
        double a = lerp(shapes[s], shapes[s + 1], f);
        if (a != 0) setHarm(re, im, k, a, SINE_PH);
    }
}

static void specBloom(double t, double* re, double* im) {
    int n = 1 + (int)std::floor(t * t * 220);
    for (int k = 1; k <= 1024; k++) {
        double roll = std::exp(-std::pow((double)k / n, 4));
        double a = roll / std::pow((double)k, 1.25);
        if (a < 1e-5) break;
        double ph = SINE_PH + std::sin(k * 12.9898) * 0.7 * t;
        setHarm(re, im, k, a, ph);
    }
}

static void specPulse(double t, double* re, double* im) {
    double d = 0.5 - 0.44 * t;
    for (int k = 1; k <= 1024; k++) {
        double a = (2.0 / (k * PI)) * std::sin(PI * k * d);
        setHarm(re, im, k, a, SINE_PH - PI * k * d);
    }
}

static const double VOX_F[5][3] = {
    {730, 1090, 2440}, {530, 1840, 2480}, {390, 1990, 2550}, {570, 840, 2410}, {440, 1020, 2240}};
static const double VOX_AMPS[3] = {1, 0.55, 0.32};
static const double VOX_BW[3]   = {95, 120, 160};
static void specVox(double t, double* re, double* im) {
    double pos = t * 4;
    int v = std::min(3, (int)pos); double f = pos - v;
    double F[3];
    for (int j = 0; j < 3; j++) F[j] = lerp(VOX_F[v][j], VOX_F[v + 1][j], f);
    for (int k = 1; k <= 256; k++) {
        double freq = k * 110.0;
        double g = 0.04;
        for (int j = 0; j < 3; j++) {
            double d = (freq - F[j]) / VOX_BW[j];
            g += VOX_AMPS[j] * std::exp(-0.5 * d * d);
        }
        setHarm(re, im, k, g / std::pow((double)k, 0.75), SINE_PH);
    }
}

static const int PARTIALS[10] = {1, 2, 3, 5, 7, 9, 13, 16, 19, 24};
static void specChime(double t, double* re, double* im) {
    for (int j = 0; j < 10; j++) {
        int k = PARTIALS[j];
        double base = 1.0 / std::pow((double)(j + 1), 0.8);
        double a = base * std::pow(std::max(t, 0.001), j * 0.45);
        double ph = SINE_PH + std::fmod(j * j * 1.7, PI * 2);
        setHarm(re, im, k, a, ph);
        if (j > 0 && t > 0.15) setHarm(re, im, k + 1, a * 0.28 * t, ph + 1.1);
    }
}

static void waveGlitch(double t, double* out) {
    double levels = lerp(40, 2.4, t);
    int hold = 1 + (int)std::round(t * 40);
    double held = 0;
    for (int n = 0; n < SIZE; n++) {
        if (n % hold == 0) {
            double x = std::sin((2 * PI * n) / SIZE)
                     + 0.45 * t * std::sin((2 * PI * 5 * n) / SIZE + 1.3)
                     + 0.2 * t * std::sin((2 * PI * 9 * n) / SIZE + 2.6);
            held = std::round(x * levels) / levels;
        }
        out[n] = held;
    }
}

// ---------- band-limiting ----------
// Turn one frame's full-band spectrum into every mip level. Mip m keeps
// harmonics 1..(1024>>m); each level is inverse-FFT'd. The mip-0 peak sets one
// per-frame normalization (0.92 headroom) shared by all levels.
static void bandlimitFrame(const double* re, const double* im,
                           std::vector<float>& data, std::vector<float>& viz, int f,
                           double* wre, double* wim) {
    double scale = 1;
    for (int m = 0; m < MIPS; m++) {
        int maxHarm = 1024 >> m;
        for (int i = 0; i < SIZE; i++) { wre[i] = re[i]; wim[i] = im[i]; }
        for (int k = maxHarm + 1; k <= SIZE - maxHarm - 1; k++) { wre[k] = 0; wim[k] = 0; }
        fft(wre, wim, SIZE, true);

        if (m == 0) {
            double peak = 1e-9;
            for (int i = 0; i < SIZE; i++) peak = std::max(peak, std::abs(wre[i]));
            scale = 0.92 / peak;
        }
        int off = (f * MIPS + m) * SIZE;
        for (int i = 0; i < SIZE; i++) data[off + i] = (float)(wre[i] * scale);
        if (m == 0) {
            int step = SIZE / VIZ_N;
            for (int i = 0; i < VIZ_N; i++) viz[f * VIZ_N + i] = (float)(wre[(i * step)] * scale);
        }
    }
}

// ---------- generation ----------
struct TableSpec {
    std::string name;
    void (*spectrum)(double, double*, double*);
    void (*wave)(double, double*);
};

std::vector<GeneratedTable> generateTables() {
    static const TableSpec SPECS[6] = {
        {"PRIME", specPrime, nullptr},
        {"BLOOM", specBloom, nullptr},
        {"PULSE", specPulse, nullptr},
        {"VOX",   specVox,   nullptr},
        {"CHIME", specChime, nullptr},
        {"GLITCH", nullptr,  waveGlitch},
    };

    std::vector<double> re(SIZE), im(SIZE), wre(SIZE), wim(SIZE), tmp(SIZE);
    std::vector<GeneratedTable> out;

    for (const auto& spec : SPECS) {
        GeneratedTable g;
        g.name = spec.name; g.frames = FRAMES; g.mips = MIPS; g.size = SIZE;
        g.data.assign((size_t)FRAMES * MIPS * SIZE, 0.0f);
        g.viz.assign((size_t)FRAMES * VIZ_N, 0.0f);

        for (int f = 0; f < FRAMES; f++) {
            double t = (double)f / (FRAMES - 1);
            std::fill(re.begin(), re.end(), 0.0);
            std::fill(im.begin(), im.end(), 0.0);
            if (spec.spectrum) {
                spec.spectrum(t, re.data(), im.data());
            } else {
                spec.wave(t, tmp.data());
                for (int i = 0; i < SIZE; i++) { re[i] = tmp[i]; im[i] = 0; }
                fft(re.data(), im.data(), SIZE, false);
            }
            re[0] = 0; im[0] = 0;            // kill DC
            re[SIZE / 2] = 0; im[SIZE / 2] = 0;
            bandlimitFrame(re.data(), im.data(), g.data, g.viz, f, wre.data(), wim.data());
        }
        out.push_back(std::move(g));
    }
    return out;
}

GeneratedTable buildUserTable(const std::string& name, const std::vector<std::vector<float>>& frames) {
    int nf = std::max(1, (int)frames.size());
    GeneratedTable g;
    g.name = name; g.frames = nf; g.mips = MIPS; g.size = SIZE;
    g.data.assign((size_t)nf * MIPS * SIZE, 0.0f);
    g.viz.assign((size_t)nf * VIZ_N, 0.0f);

    std::vector<double> re(SIZE), im(SIZE), wre(SIZE), wim(SIZE);
    for (int f = 0; f < nf; f++) {
        std::fill(re.begin(), re.end(), 0.0);
        std::fill(im.begin(), im.end(), 0.0);
        if (f < (int)frames.size() && !frames[f].empty())
            for (int i = 0; i < SIZE && i < (int)frames[f].size(); i++) re[i] = frames[f][i];
        fft(re.data(), im.data(), SIZE, false);
        re[0] = 0; im[0] = 0;
        re[SIZE / 2] = 0; im[SIZE / 2] = 0;
        bandlimitFrame(re.data(), im.data(), g.data, g.viz, f, wre.data(), wim.data());
    }
    return g;
}

} // namespace fable
