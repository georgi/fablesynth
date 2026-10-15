#include "AutoEq.h"
#include "../Wavetables.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace fable::automix {

static constexpr double PI = 3.14159265358979323846;

const Bands BAND_HZ = {31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000};

// Band-power curves (third octaves), so pink noise is flat. Mix curves follow
// the usual -3 to -5 dB/octave tilt of finished electronic music; the part
// curves describe where a single instrument should sit before mixing.
const std::array<EqReference, 9> EQ_REFERENCES = {{
    {"mix", "MIX \xc2\xb7 MODERN", "Balanced club/electronic master tilt", {-3.8, 100, 0, 40, 3, 10000, 3}},
    {"warm", "MIX \xc2\xb7 WARM", "Darker top, fuller low end", {-4.5, 100, 1.5, 40, 3, 10000, 3}},
    {"bright", "MIX \xc2\xb7 BRIGHT", "Forward presence and air", {-3, 100, -1, 40, 3, 12000, 3}},
    {"dub", "MIX \xc2\xb7 DUB", "Deep sub, soft and dark top", {-5, 90, 2, 35, 6, 6000, 4}},
    {"pink", "FLAT \xc2\xb7 PINK", "Equal energy per octave", {0, 20, 0, 0, 0, 0, 0}},
    {"drums", "PART \xc2\xb7 DRUMS", "Kick weight, tight mids, open top", {-3.2, 70, 2, 35, 9, 14000, 3}},
    {"bass", "PART \xc2\xb7 BASS", "Solid low end, little top", {-7, 120, 0, 35, 12, 3000, 6}},
    {"lead", "PART \xc2\xb7 LEAD", "Mid focus, clear of the bass", {-3, 250, 0, 200, 12, 9000, 4}},
    {"pad", "PART \xc2\xb7 PAD", "Soft and wide, low end thinned", {-4, 200, 0, 120, 9, 8000, 4}},
}};

double powerDb(double p) { return 10 * std::log10(std::max(p, 1e-20)); }

double jsRound(double v) {
    const double r = std::floor(v);
    return v - r >= 0.5 ? r + 1 : r;
}

std::string toFixed(double v, int digits) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v < 0 ? "-Infinity" : "Infinity";
    const bool neg = v < 0;
    char buf[512];
    std::snprintf(buf, sizeof buf, "%.*f", digits + 25, std::fabs(v));
    std::string s(buf);
    const auto dot = s.find('.');
    const auto d = (size_t)digits;
    std::string keep = s.substr(0, dot) + s.substr(dot + 1, d);
    // Ties pick the larger magnitude, so the first dropped digit decides.
    if (s[dot + 1 + d] >= '5') {
        size_t i = keep.size();
        for (; i > 0 && keep[i - 1] == '9'; --i) keep[i - 1] = '0';
        if (i == 0) keep.insert(keep.begin(), '1'); else ++keep[i - 1];
    }
    if (digits > 0) keep.insert(keep.end() - digits, '.');
    return (neg ? "-" : "") + keep;
}

const EqReference& referenceById(const std::string& id) {
    for (const auto& r : EQ_REFERENCES) if (id == r.id) return r;
    return EQ_REFERENCES[0];
}

double referenceDb(const EqReference& ref, double f) {
    const auto& s = ref.shape;
    double v = s.slope * std::log2(std::max(f, s.bassHz) / 1000);
    if (f < s.bassHz) v += s.bass * std::min(1.0, std::log2(s.bassHz / f));
    if (truthy(s.lowCutHz) && f < s.lowCutHz) v -= s.lowCut * std::log2(s.lowCutHz / f);
    if (truthy(s.rollHz) && f > s.rollHz) v -= s.roll * std::log2(f / s.rollHz);
    return v;
}

EqCoefficients eqCoefficients(const EqBand& band, double sampleRate) {
    const double A = std::pow(10.0, (band.on ? band.gain : 0) / 40);
    const double w = 2 * PI * std::min(band.freq, sampleRate * 0.49) / sampleRate;
    const double c = std::cos(w), alpha = std::sin(w) / (2 * band.q);
    if (band.type == 1) {
        const double a0 = 1 + alpha / A;
        return {(1 + alpha * A) / a0, -2 * c / a0, (1 - alpha * A) / a0, -2 * c / a0, (1 - alpha / A) / a0};
    }
    const double t = 2 * std::sqrt(A) * alpha;
    if (band.type == 0) {
        const double a0 = A + 1 + (A - 1) * c + t;
        return {A * (A + 1 - (A - 1) * c + t) / a0, 2 * A * (A - 1 - (A + 1) * c) / a0,
                A * (A + 1 - (A - 1) * c - t) / a0, -2 * (A - 1 + (A + 1) * c) / a0,
                (A + 1 + (A - 1) * c - t) / a0};
    }
    const double a0 = A + 1 - (A - 1) * c + t;
    return {A * (A + 1 + (A - 1) * c + t) / a0, -2 * A * (A - 1 + (A + 1) * c) / a0,
            A * (A + 1 + (A - 1) * c - t) / a0, 2 * (A - 1 - (A + 1) * c) / a0,
            (A + 1 - (A - 1) * c - t) / a0};
}

double eqResponseDb(const EqCoefficients& c, double freq, double sampleRate) {
    const double w = 2 * PI * freq / sampleRate;
    const double c1 = std::cos(w), c2 = std::cos(2 * w), s1 = std::sin(w), s2 = std::sin(2 * w);
    const double nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = c.b1 * s1 + c.b2 * s2;
    const double dr = 1 + c.a1 * c1 + c.a2 * c2, di = c.a1 * s1 + c.a2 * s2;
    return 10 * std::log10(std::max(1e-20, (nr * nr + ni * ni) / (dr * dr + di * di)));
}

double eqCurveDb(const std::vector<EqBand>& bands, double f, double sampleRate) {
    double a = 0;
    for (const auto& b : bands) a += b.on ? eqResponseDb(eqCoefficients(b, sampleRate), f, sampleRate) : 0;
    return a;
}

void analyserSpectrumDb(const float* time, int n, float* outDb) {
    std::vector<double> re((size_t)n), im((size_t)n, 0.0);
    for (size_t i = 0; i < re.size(); ++i) {
        const double x = (double)i / n;
        re[i] = time[i] * (0.42 - 0.5 * std::cos(2 * PI * x) + 0.08 * std::cos(4 * PI * x));
    }
    fable::fft(re.data(), im.data(), n, false);
    for (size_t k = 0; k < re.size() / 2; ++k)
        outDb[k] = (float)(20 * std::log10(std::sqrt(re[k] * re[k] + im[k] * im[k]) / n));
}

void addBandPower(Bands& bands, const float* spectrumDb, int bins, double sampleRate, int fftSize) {
    const double binHz = sampleRate / fftSize;
    for (size_t b = 0; b < NUM_BANDS; ++b) {
        const double lo = BAND_HZ[b] / std::pow(2.0, 1.0 / 6), hi = BAND_HZ[b] * std::pow(2.0, 1.0 / 6);
        double sum = 0;
        for (int k = std::max(1, (int)std::ceil(lo / binHz)); k < std::min((double)bins, hi / binHz); ++k)
            sum += std::pow(10.0, spectrumDb[k] / 10.0);
        bands[b] += sum;
    }
}

double rmsDb(const float* x, int n) {
    double s = 0;
    for (int i = 0; i < n; ++i) s += (double)x[i] * x[i];
    return powerDb(s / std::max(1, n));
}

bool SpectrumCapture::add(const float* spectrumDb, const float* time, int timeSize) {
    if (rmsDb(time, timeSize) < SILENCE_DB) return false;
    ++frames;
    addBandPower(bandPower, spectrumDb, fftSize / 2, sampleRate, fftSize);
    return true;
}

std::vector<double> SpectrumCapture::levels() const {
    double total = 0;
    for (double p : bandPower) total += p;
    if (!truthy(total)) total = 1e-20;
    std::vector<double> out;
    for (double p : bandPower) out.push_back(powerDb(p / total));
    return out;
}

Alignment alignToReference(const std::vector<double>& levels, const EqReference& ref) {
    double peak = -std::numeric_limits<double>::infinity();
    for (double l : levels) peak = std::max(peak, l);
    Alignment a;
    std::vector<double> judged;
    for (size_t i = 0; i < levels.size(); ++i) {
        a.present.push_back(levels[i] > peak - 45);
        if (i < (size_t)NUM_BANDS && a.present[i] && BAND_HZ[i] >= 100 && BAND_HZ[i] <= 8000)
            judged.push_back(levels[i] - referenceDb(ref, BAND_HZ[i]));
    }
    std::sort(judged.begin(), judged.end());
    a.offset = judged.empty() ? 0 : judged[judged.size() / 2];
    return a;
}

EqFit fitEq(const std::vector<double>& levels, const EqReference& ref, double amount, double sampleRate) {
    const auto al = alignToReference(levels, ref);
    EqFit fit;
    fit.present = al.present;
    for (size_t i = 0; i < NUM_BANDS; ++i) {
        const double raw = levels[i] - referenceDb(ref, BAND_HZ[i]);
        fit.deviation.push_back(al.present[i] ? raw - al.offset : 0);
        fit.measured.push_back(levels[i] - al.offset);
    }
    const auto& dev = fit.deviation;
    auto correction = [&](double d, double boostCap, double cutCap) {
        const double g = std::clamp(-d * amount, -cutCap, boostCap);
        return std::abs(g) < 0.5 ? 0.0 : jsRound(g * 2) / 2;
    };
    auto mean = [&](double lo, double hi) {
        double s = 0; int n = 0;
        for (size_t i = 0; i < NUM_BANDS; ++i) if (BAND_HZ[i] >= lo && BAND_HZ[i] <= hi) { s += dev[i]; ++n; }
        return s / std::max(1, n);
    };
    struct Worst { double f, d; };
    auto worst = [&](double lo, double hi) {
        Worst best{std::sqrt(lo * hi), 0};
        for (size_t i = 1; i < NUM_BANDS - 1; ++i) {
            if (BAND_HZ[i] < lo || BAND_HZ[i] > hi) continue;
            const double d = (dev[i - 1] + 2 * dev[i] + dev[i + 1]) / 4;
            if (std::abs(d) > std::abs(best.d)) best = {BAND_HZ[i], d};
        }
        return best;
    };
    const auto mid = worst(160, 1250), mid2 = worst(1600, 6300);
    const double sqrtHalf = 0.7071067811865476;
    fit.bands = {
        {110, correction(mean(40, 100), 3, 6), sqrtHalf, 0, true},
        {jsRound(mid.f), correction(mid.d, 2, 4), 1.1, 1, true},
        {jsRound(mid2.f), correction(mid2.d, 2, 4), 1.1, 1, true},
        {9000, correction(mean(8000, 16000), 3, 6), sqrtHalf, 2, true},
    };
    for (size_t i = 0; i < NUM_BANDS; ++i)
        fit.residual.push_back(dev[i] + (al.present[i] ? eqCurveDb(fit.bands, BAND_HZ[i], sampleRate) : 0));
    for (const auto& b : fit.bands) fit.change += std::abs(b.gain);
    return fit;
}

std::vector<double> removeEq(const std::vector<double>& levels, const std::vector<EqBand>& bands, double sampleRate) {
    std::vector<double> out;
    for (size_t i = 0; i < levels.size(); ++i) out.push_back(levels[i] - eqCurveDb(bands, BAND_HZ[i], sampleRate));
    return out;
}

}
