#include "KitMix.h"
#include "../Wavetables.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace fable::automix {

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double MAX_CUT = 12, MAX_BOOST = 6;
/** Penalty per dB² of move, against dB² of band error summed over ~25 bands. */
constexpr double MOVE_PENALTY = 0.3;

/** Power per bin (0..n/2) of a Hann-windowed frame; n must be a power of two. */
std::vector<double> powerSpectrum(const float* x, int n) {
    std::vector<double> re((size_t)n), im((size_t)n, 0.0);
    for (int i = 0; i < n; ++i) re[(size_t)i] = x[i] * (0.5 - 0.5 * std::cos(2 * PI * i / n));
    fable::fft(re.data(), im.data(), n, false);
    std::vector<double> out((size_t)n / 2);
    for (size_t k = 0; k < out.size(); ++k) out[k] = re[k] * re[k] + im[k] * im[k];
    return out;
}

/** Solves A·x = b in place by Gaussian elimination with partial pivoting. */
std::vector<double> solve(std::vector<std::vector<double>>& A, std::vector<double>& b) {
    const size_t n = b.size();
    for (size_t c = 0; c < n; ++c) {
        size_t p = c;
        for (size_t r = c + 1; r < n; ++r) if (std::abs(A[r][c]) > std::abs(A[p][c])) p = r;
        std::swap(A[c], A[p]); std::swap(b[c], b[p]);
        const double d = truthy(A[c][c]) ? A[c][c] : 1e-12;
        for (size_t r = c + 1; r < n; ++r) {
            const double f = A[r][c] / d;
            for (size_t k = c; k < n; ++k) A[r][k] -= f * A[c][k];
            b[r] -= f * b[c];
        }
    }
    std::vector<double> x(n, 0.0);
    for (size_t r = n; r-- > 0;) {
        double s = b[r];
        for (size_t k = r + 1; k < n; ++k) s -= A[r][k] * x[k];
        x[r] = s / (truthy(A[r][r]) ? A[r][r] : 1e-12);
    }
    return x;
}

std::vector<double> kitLevels(const std::vector<Bands>& pads, const std::vector<double>& w) {
    std::vector<double> out;
    for (size_t b = 0; b < NUM_BANDS; ++b) {
        double s = 0;
        for (size_t i = 0; i < pads.size(); ++i) s += w[i] * pads[i][(size_t)b];
        out.push_back(powerDb(s));
    }
    return out;
}

struct Aligned { std::vector<double> out; double err; };
Aligned aligned(const std::vector<double>& levels, const EqReference& ref, const std::vector<bool>& present) {
    const double offset = alignToReference(levels, ref).offset;
    Aligned a{{}, 0};
    double sum = 0; int count = 0;
    for (size_t b = 0; b < levels.size(); ++b) {
        a.out.push_back(levels[b] - offset);
        if (present[b]) { sum += std::pow(a.out[b] - referenceDb(ref, BAND_HZ[b]), 2); ++count; }
    }
    a.err = std::sqrt(sum / std::max(1, count));
    return a;
}
}

void KitCapture::add(int pad, const float* data, int size, int frames_) {
    frames = std::max(frames, frames_);
    if (pad < 0 || pad >= (int)bands.size()) return;
    auto& b = bands[(size_t)pad];
    const auto power = powerSpectrum(data, size);
    const double binHz = sampleRate / size;
    // Parseval over the one-sided spectrum, corrected for the Hann window's
    // 3/8 power gain: the band sums are the block's energy in each band.
    const double norm = 2 / (size * 0.375);
    for (size_t k = 0; k < NUM_BANDS; ++k) {
        const double lo = BAND_HZ[k] / std::pow(2.0, 1.0 / 6), hi = BAND_HZ[k] * std::pow(2.0, 1.0 / 6);
        double sum = 0;
        for (int j = std::max(1, (int)std::ceil(lo / binHz)); j < std::min((double)power.size(), hi / binHz); ++j)
            sum += power[(size_t)j];
        b[(size_t)k] += sum * norm;
    }
}

std::vector<Bands> KitCapture::meanBands() const {
    const double f = std::max(1, frames);
    auto out = bands;
    for (auto& b : out) for (auto& v : b) v /= f;
    return out;
}

std::optional<KitMixFit> fitKitMix(const std::vector<Bands>& pads, const std::vector<double>& lvl,
                                   const EqReference& ref, double amount) {
    const size_t P = pads.size();
    std::vector<double> energy;
    double loudest = -INFINITY;
    for (const auto& p : pads) {
        double s = 0;
        for (double v : p) s += v;
        energy.push_back(s);
        loudest = std::max(loudest, s);
    }
    KitMixFit fit;
    std::vector<size_t> idx;
    for (size_t i = 0; i < P; ++i) {
        fit.heard.push_back(energy[i] > 0 && energy[i] > loudest * 1e-4 && lvl[i] > 0);
        if (fit.heard[i]) idx.push_back(i);
    }
    if (idx.size() < 2) return std::nullopt;

    const auto kit0 = kitLevels(pads, std::vector<double>(P, 1.0));
    const double peak = *std::max_element(kit0.begin(), kit0.end());
    std::vector<size_t> bands;
    for (size_t b = 0; b < NUM_BANDS; ++b) {
        fit.present.push_back(kit0[(size_t)b] > peak - 45 && BAND_HZ[b] >= 40);
        if (fit.present.back()) bands.push_back((size_t)b);
        fit.target.push_back(referenceDb(ref, BAND_HZ[b]));
    }

    // Unknowns: one dB move per heard pad, plus a free level offset c.
    const size_t m = idx.size();
    std::vector<double> x(m + 1, 0.0), w(m), parts(m), J(m + 1);
    for (int iter = 0; iter < 40; ++iter) {
        for (size_t k = 0; k < m; ++k) w[k] = std::pow(10.0, x[k] / 10);
        std::vector<std::vector<double>> A(m + 1, std::vector<double>(m + 1, 0.0));
        std::vector<double> g(m + 1, 0.0);
        for (auto b : bands) {
            double sum = 0;
            for (size_t k = 0; k < m; ++k) { parts[k] = w[k] * pads[idx[k]][b]; sum += parts[k]; }
            if (!truthy(sum)) sum = 1e-30;
            for (size_t k = 0; k < m; ++k) J[k] = parts[k] / sum;
            J[m] = -1;
            const double r = powerDb(sum) - fit.target[b] - x[m];
            for (size_t i = 0; i <= m; ++i) {
                g[i] += J[i] * r;
                for (size_t j = 0; j <= m; ++j) A[i][j] += J[i] * J[j];
            }
        }
        for (size_t k = 0; k < m; ++k) { A[k][k] += MOVE_PENALTY; g[k] += MOVE_PENALTY * x[k]; }
        for (auto& v : g) v = -v;
        const auto step = solve(A, g);
        double change = 0;
        for (size_t k = 0; k <= m; ++k) {
            const double next = k < m ? std::clamp(x[k] + step[k], -MAX_CUT, MAX_BOOST) : x[k] + step[k];
            change = std::max(change, std::abs(next - x[k]));
            x[k] = next;
        }
        if (change < 0.01) break;
    }

    // Scale by amount, keep the kit's total level, then respect the level
    // ceiling: a pad that cannot rise far enough lowers the others instead.
    std::vector<double> moves(m);
    double total = 0, kept = 0;
    for (size_t k = 0; k < m; ++k) { moves[k] = x[k] * amount; total += energy[idx[k]]; }
    for (size_t k = 0; k < m; ++k) kept += energy[idx[k]] * std::pow(10.0, moves[k] / 10);
    const double keep = -powerDb(kept / total);
    double over = 0;
    for (size_t k = 0; k < m; ++k) {
        moves[k] += keep;
        over = std::max(over, moves[k] - 40 * std::log10(1 / lvl[idx[k]]));
    }
    for (auto& d : moves) d -= over;

    fit.moves.assign(P, 0.0);
    fit.levels.assign(P, std::nullopt);
    for (size_t k = 0; k < m; ++k) {
        const size_t p = idx[k];
        const double d = std::abs(moves[k]) < 0.5 ? 0 : jsRound(moves[k] * 2) / 2;
        if (!truthy(d)) continue;
        const double to = std::clamp(lvl[p] * std::pow(10.0, d / 40), 0.0, 1.0);
        fit.levels[p] = jsRound(to * 1000) / 1000;
        fit.moves[p] = 40 * std::log10(*fit.levels[p] / lvl[p]);
    }
    std::vector<double> gains;
    for (double d : fit.moves) gains.push_back(std::pow(10.0, d / 10));
    const auto before = aligned(kit0, ref, fit.present);
    const auto after = aligned(kitLevels(pads, gains), ref, fit.present);
    fit.before = before.out; fit.after = after.out;
    fit.errorBefore = before.err; fit.errorAfter = after.err;
    double heardTotal = 0;
    for (size_t i = 0; i < P; ++i) heardTotal += fit.heard[i] ? energy[i] : 0;
    for (size_t i = 0; i < P; ++i) fit.share.push_back(fit.heard[i] ? energy[i] / heardTotal : 0);
    return fit;
}

}
