#include "AutoComp.h"
#include "AutoEq.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace fable::automix {

const std::array<CompStyle, 5> COMP_STYLES = {{
    {"glue", "GLUE", "Gentle bus cohesion, transients pass", 2, 0.01, 0.5, 2, 0.7},
    {"punch", "PUNCH", "Slow attack lets the hit through, fast recovery", 4, 0.025, 0.25, 4, 0.75},
    {"level", "LEVEL", "Even out a part that jumps between loud and quiet", 3, 0.005, 1, 4, 0.5},
    {"peaks", "PEAKS", "Only catch the loudest hits", 6, 0.0005, 0.125, 3, 0.95},
    {"squash", "SQUASH", "Dense, obvious pumping", 8, 0.001, 0.125, 8, 0.5},
}};

const CompStyle& compStyleById(const std::string& id) {
    for (const auto& s : COMP_STYLES) if (id == s.id) return s;
    return COMP_STYLES[0];
}

double compGainDb(double levelDb, double threshold, double ratio) {
    const double over = levelDb - threshold, slope = 1 / ratio - 1;
    return over <= 0 ? 0 : over < 9 ? slope * over * over / 18 : slope * (over - 4.5);
}

static double percentile(const std::vector<double>& sorted, double p) {
    const double n = (double)sorted.size() - 1;
    return sorted[(size_t)std::clamp(jsRound(n * p), 0.0, n)];
}

// Math.max semantics: NaN propagates (std::max would drop it).
static double jsMax(double a, double b) { return std::isnan(a) || std::isnan(b) ? NAN : std::max(a, b); }

std::optional<double> estimateBpm(const std::vector<double>& blocks, double rate) {
    const size_t n = blocks.size();
    std::vector<double> raw(n), x(n);
    for (size_t i = 0; i < n; ++i) raw[i] = i ? jsMax(0, blocks[i] - blocks[i - 1]) : 0;
    // Widen single-block onsets so fractional beat periods still overlap.
    double mean = 0;
    for (size_t i = 0; i < n; ++i) {
        x[i] = 0.25 * (i ? raw[i - 1] : 0) + 0.5 * raw[i] + 0.25 * (i + 1 < n ? raw[i + 1] : 0);
        mean += x[i];
    }
    mean /= std::max<double>(1, (double)n);
    double energy = 0;
    for (auto& v : x) { v -= mean; energy += v * v; }
    if (energy <= 0 || (double)n < rate * 3) return std::nullopt;
    // Autocorrelation at a fractional lag (linear interpolation), normalised.
    auto ac = [&](double lag) {
        const auto k = (size_t)std::floor(lag);
        const double f = lag - (double)k;
        double r = 0;
        for (size_t i = k + 1; i < n; ++i) r += x[i] * (x[i - k] * (1 - f) + x[i - k - 1] * f);
        return r / energy;
    };
    // Comb score with a tempo prior around 120 BPM, as on the web.
    double bestLag = 0, bestR = 0, bestScore = -std::numeric_limits<double>::infinity();
    for (double lag = rate * 60 / 180; lag <= rate * 60 / 70; lag += 0.05) {
        const double r = ac(lag), comb = r + 0.5 * ac(2 * lag) + 0.25 * ac(4 * lag) + 0.5 * jsMax(0, ac(lag / 2));
        const double octaves = std::log2(60 * rate / lag / 120), score = comb * std::exp(-2 * octaves * octaves);
        if (score > bestScore) { bestLag = lag; bestR = r; bestScore = score; }
    }
    if (bestR < 0.1) return std::nullopt;
    return jsRound(60 * rate / bestLag);
}

std::vector<DetectorBlock> detectorDb(const std::vector<Trace>& traces, double attack, double release, double sampleRate) {
    const double ca = 1 - std::exp(-1 / (std::clamp(attack, 0.0001, 0.1) * sampleRate));
    const double cr = 1 - std::exp(-1 / (std::clamp(release, 0.01, 2.0) * sampleRate));
    double env = 0;
    std::vector<DetectorBlock> out(traces.size());
    for (size_t b = 0; b < traces.size(); ++b) {
        const auto& t = traces[b];
        if (!t.data) continue;
        out[b].present = true;
        out[b].db.resize((size_t)t.size);
        for (int i = 0; i < t.size; ++i) {
            const double pk = t.data[i];
            env += (pk - env) * (pk > env ? ca : cr);
            out[b].db[(size_t)i] = (float)(20 * std::log10(std::max(1e-9, env)));
        }
    }
    return out;
}

std::optional<CompFit> fitComp(const std::vector<double>& blocks, const CompStyle& style, double depth,
                               const FitOptions& options) {
    std::vector<size_t> audible;
    for (size_t i = 0; i < blocks.size(); ++i)
        if (std::isfinite(blocks[i]) && blocks[i] > SILENCE_DB) audible.push_back(i);
    if ((double)audible.size() < BLOCK_RATE) return std::nullopt;
    std::vector<double> sorted;
    for (auto i : audible) sorted.push_back(blocks[i]);
    std::sort(sorted.begin(), sorted.end());
    const double cut = percentile(sorted, style.loudPct);
    std::vector<size_t> loud;
    for (auto i : audible) if (blocks[i] >= cut) loud.push_back(i);
    const double target = style.targetGr * depth;
    const auto tempo = options.bpm ? options.bpm : estimateBpm(blocks);
    const double release = jsRound(std::clamp(style.releaseBeats * 60 / tempo.value_or(120), 0.01, 2.0) * 1000) / 1000;

    // Per-block detector levels: exact from the trace, else RMS plus the offset.
    const auto& traces = options.traces;
    bool exact = truthy(options.sampleRate);
    for (auto i : loud) exact = exact && i < traces.size() && traces[i].data && traces[i].size > 0;
    std::vector<DetectorBlock> det;
    if (exact) det = detectorDb(traces, style.attack, release, options.sampleRate);
    auto detAt = [&](size_t i) -> const DetectorBlock* { return i < det.size() && det[i].present ? &det[i] : nullptr; };
    auto blockGr = [&](size_t i, double thr) {
        const auto* d = detAt(i);
        if (!d) return -compGainDb(blocks[i] + DETECTOR_OFFSET_DB, thr, style.ratio);
        double s = 0;
        for (float v : d->db) s += compGainDb(v, thr, style.ratio);
        return -s / (double)d->db.size();
    };
    auto gr = [&](const std::vector<size_t>& set, double thr) {
        double a = 0;
        for (auto i : set) a += blockGr(i, thr);
        return a / (double)set.size();
    };
    // Reduction falls monotonically as the threshold rises: bisect on [-40, 0].
    double lo = -40, hi = 0;
    for (int n = 0; n < 16; ++n) {
        const double mid = (lo + hi) / 2;
        if (gr(loud, mid) > target) lo = mid; else hi = mid;
    }
    CompFit fit;
    fit.threshold = jsRound((lo + hi) / 2 * 2) / 2;
    fit.ratio = style.ratio;
    fit.attack = style.attack;
    fit.release = release;
    fit.loudGr = gr(loud, fit.threshold);
    fit.meanGr = gr(audible, fit.threshold);
    fit.rangeDb = percentile(sorted, 0.9) - percentile(sorted, 0.1);
    fit.bpm = tempo;
    for (auto i : audible) {
        const auto* d = detAt(i);
        if (!d) { fit.levels.push_back(blocks[i] + DETECTOR_OFFSET_DB); continue; }
        double s = 0;
        for (float v : d->db) s += v;
        fit.levels.push_back(s / (double)d->db.size());
    }
    fit.exact = exact;
    return fit;
}

}
