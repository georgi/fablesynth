#include "UserTables.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fable {

// ---------- construction ----------
UserTable makeUserTable(const std::string& name, const std::vector<std::vector<float>>& frames) {
    int avail = (int)frames.size();
    int nf = std::max(1, std::min(MAX_FRAMES, avail)); // always at least 1 frame
    UserTable u;
    u.name = name;
    u.frames = nf;
    u.wave.assign((size_t)nf * SIZE, 0.0f);
    // Take the available frames (zero-padding if the caller passed none), never
    // reading past the input — frames.begin() + nf is UB when frames is empty.
    std::vector<std::vector<float>> use;
    use.reserve(static_cast<size_t>(nf));
    for (int f = 0; f < nf; ++f)
        use.push_back(f < avail ? frames[(size_t)f] : std::vector<float>(SIZE, 0.0f));
    for (int f = 0; f < nf; ++f)
        for (int i = 0; i < SIZE && i < (int)use[(size_t)f].size(); ++i)
            u.wave[static_cast<size_t>(f) * static_cast<size_t>(SIZE) + static_cast<size_t>(i)]
                = use[static_cast<size_t>(f)][static_cast<size_t>(i)];
    u.table = std::make_shared<const GeneratedTable>(buildUserTable(name, use));
    return u;
}

UserTable userTableFromWave(const std::string& name, int frames, const std::vector<float>& wave) {
    int nf = std::max(1, std::min(MAX_FRAMES, frames));
    std::vector<std::vector<float>> fr;
    fr.reserve(static_cast<size_t>(nf));
    for (int f = 0; f < nf; ++f) {
        std::vector<float> frame(SIZE, 0.0f);
        for (int i = 0; i < SIZE; ++i) {
            const size_t idx = static_cast<size_t>(f) * static_cast<size_t>(SIZE)
                             + static_cast<size_t>(i);
            frame[static_cast<size_t>(i)] = idx < wave.size() ? wave[idx] : 0.0f;
        }
        fr.push_back(std::move(frame));
    }
    return makeUserTable(name, fr);
}

std::vector<std::vector<float>> framesFromGenerated(const GeneratedTable& t) {
    std::vector<std::vector<float>> frames;
    frames.reserve((size_t)t.frames);
    for (int f = 0; f < t.frames; ++f) {
        const int off = (f * t.mips + 0) * t.size;
        frames.emplace_back(t.data.begin() + off, t.data.begin() + off + t.size);
    }
    return frames;
}

// ---------- audio analysis ----------
std::vector<float> mixToMono(const float* const* channels, int numChannels, int n) {
    std::vector<float> out(static_cast<size_t>(std::max(0, n)), 0.0f);
    if (numChannels <= 0) return out;
    for (int ch = 0; ch < numChannels; ++ch) {
        const float* d = channels[ch];
        if (!d) continue;
        for (int i = 0; i < n; ++i) out[static_cast<size_t>(i)] += d[i];
    }
    float g = 1.0f / (float)std::max(1, numChannels);
    for (int i = 0; i < n; ++i) out[static_cast<size_t>(i)] *= g;
    return out;
}

double detectCycleLength(const std::vector<float>& x, double sampleRate) {
    int len = (int)x.size();
    int win = std::min(len, 16384);
    int minLag = std::max(2, (int)std::floor(sampleRate / 1000.0));
    int maxLag = std::min((int)std::floor(sampleRate / 40.0), (win >> 1) - 1);
    if (maxLag <= minLag) return std::max(2, std::min(SIZE, len));

    double energy = 1e-9;
    for (int i = 0; i < win; ++i) {
        const auto index = static_cast<size_t>(i);
        energy += static_cast<double>(x[index]) * x[index];
    }

    // Keep every score so the peak can be refined to sub-sample precision
    // below — real periods are almost never a whole number of samples.
    std::vector<double> score(static_cast<size_t>(maxLag - minLag + 1), 0.0);
    int bestLag = minLag;
    double bestScore = -1e300;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double corr = 0;
        for (int i = 0; i < win - lag; ++i)
            corr += static_cast<double>(x[static_cast<size_t>(i)])
                  * x[static_cast<size_t>(i + lag)];
        // Bias slightly toward longer periods to avoid octave-too-high errors.
        const double sc = (corr / energy) * (1.0 + (double)lag / maxLag * 0.02);
        score[static_cast<size_t>(lag - minLag)] = sc;
        if (sc > bestScore) { bestScore = sc; bestLag = lag; }
    }

    // Finding J8: parabolic interpolation through the three samples around the
    // autocorrelation peak. Returning the integer lag quantised every imported
    // cycle to a whole sample, which sliceToFrames then had to paper over.
    if (bestLag > minLag && bestLag < maxLag) {
        const double sm = score[static_cast<size_t>(bestLag - minLag - 1)];
        const double s0 = score[static_cast<size_t>(bestLag - minLag)];
        const double sp = score[static_cast<size_t>(bestLag - minLag + 1)];
        const double denom = sm - 2.0 * s0 + sp;
        if (denom < -1e-18) {          // a real maximum, not a flat/inverted run
            double delta = 0.5 * (sm - sp) / denom;
            if (delta > 0.5) delta = 0.5;
            if (delta < -0.5) delta = -0.5;
            return (double)bestLag + delta;
        }
    }
    return bestLag;
}

// Finding J8: exact band-limited resampling of each detected cycle to SIZE.
//
// The old path stretched a 100-500 sample cycle to 2048 with LINEAR
// interpolation and then band-limited the result. Linear upsampling by 4-20x
// leaves its sinc^2 interpolation images (-20 to -40 dB) inside the kept
// 1024-harmonic band, so they survive as permanent "harmonics" of the imported
// table, and the genuine partials get the sinc^2 droop.
//
// Instead: transform the cycle at its own (rounded) length N, copy harmonics
// 1..N/2 into the SIZE-point spectrum, zero everything above, and transform
// back. That is exact band-limited resampling — no images, no droop.

static constexpr double kPi = 3.14159265358979323846;

static inline double sincf(double t) {
    if (t > -1e-12 && t < 1e-12) return 1.0;
    const double a = kPi * t;
    return std::sin(a) / a;
}

// Band-limited read of x at a fractional position. `cutoff` (<= 1) narrows the
// passband when the segment is decimated, so nothing folds back. Samples
// outside x read as zero; the kernel is normalised so DC gain stays 1.
static double sincRead(const std::vector<float>& x, double pos, double cutoff) {
    const int total = (int)x.size();
    const double halfWidth = 16.0 / cutoff;
    const int lo = (int)std::ceil(pos - halfWidth);
    const int hi = (int)std::floor(pos + halfWidth);
    double acc = 0, wsum = 0;
    for (int i = lo; i <= hi; ++i) {
        const double d = pos - (double)i;
        const double u = (d / halfWidth + 1.0) * 0.5;              // 0..1
        const double w = 0.42 - 0.5 * std::cos(2 * kPi * u) + 0.08 * std::cos(4 * kPi * u);
        const double h = w * cutoff * sincf(cutoff * d);
        wsum += h;
        if (i >= 0 && i < total) acc += h * (double)x[static_cast<size_t>(i)];
    }
    return wsum > 1e-12 ? acc / wsum : 0.0;
}

std::vector<std::vector<float>> sliceToFrames(const std::vector<float>& x, double cycleLen) {
    const double len = std::max(1.0, cycleLen);
    const int total = (int)x.size();
    const int nf = std::max(1, std::min(MAX_FRAMES, (int)std::floor(total / len)));
    std::vector<std::vector<float>> frames;
    frames.reserve(static_cast<size_t>(nf));

    // Native cycle length, capped at SIZE (a cycle longer than the table is
    // decimated, which the sinc read's narrowed passband handles).
    const int N = std::max(2, std::min(SIZE, (int)std::llround(len)));
    const double ratio = len / (double)N;               // ~1 unless len > SIZE
    const double cutoff = ratio > 1.0 ? 1.0 / ratio : 1.0;
    const int keep = std::min((N - 1) / 2, SIZE / 2 - 1); // harmonics 1..keep
    const double scale = (double)SIZE / (double)N;        // iFFT is 1/SIZE-normalised

    // One twiddle table for the odd-N direct transform (indexed by (k*i) % N).
    const bool pow2 = (N & (N - 1)) == 0;
    std::vector<double> twc, tws;
    if (!pow2) {
        twc.resize(static_cast<size_t>(N));
        tws.resize(static_cast<size_t>(N));
        for (int m = 0; m < N; ++m) {
            const double a = -2.0 * kPi * (double)m / (double)N;
            twc[static_cast<size_t>(m)] = std::cos(a);
            tws[static_cast<size_t>(m)] = std::sin(a);
        }
    }

    std::vector<double> sre(static_cast<size_t>(N)), sim(static_cast<size_t>(N));
    std::vector<double> re(static_cast<size_t>(SIZE)), im(static_cast<size_t>(SIZE));

    for (int f = 0; f < nf; ++f) {
        const double start = (double)f * len;
        for (int i = 0; i < N; ++i) {
            sre[static_cast<size_t>(i)] = sincRead(x, start + (double)i * ratio, cutoff);
            sim[static_cast<size_t>(i)] = 0.0;
        }
        std::fill(re.begin(), re.end(), 0.0);
        std::fill(im.begin(), im.end(), 0.0);

        if (pow2) {
            fft(sre.data(), sim.data(), N, false);
            for (int k = 1; k <= keep; ++k) {
                re[static_cast<size_t>(k)] = sre[static_cast<size_t>(k)] * scale;
                im[static_cast<size_t>(k)] = sim[static_cast<size_t>(k)] * scale;
            }
        } else {
            for (int k = 1; k <= keep; ++k) {
                double ar = 0, ai = 0;
                for (int i = 0; i < N; ++i) {
                    const size_t m = static_cast<size_t>((long long)k * i % N);
                    const double v = sre[static_cast<size_t>(i)];
                    ar += v * twc[m];
                    ai += v * tws[m];
                }
                re[static_cast<size_t>(k)] = ar * scale;
                im[static_cast<size_t>(k)] = ai * scale;
            }
        }
        // Hermitian mirror (DC and everything above `keep` stay zero — DC is
        // killed by buildUserTable anyway), then back to SIZE time samples.
        for (int k = 1; k <= keep; ++k) {
            re[static_cast<size_t>(SIZE - k)] =  re[static_cast<size_t>(k)];
            im[static_cast<size_t>(SIZE - k)] = -im[static_cast<size_t>(k)];
        }
        fft(re.data(), im.data(), SIZE, true);

        std::vector<float> frame(static_cast<size_t>(SIZE), 0.0f);
        for (int i = 0; i < SIZE; ++i)
            frame[static_cast<size_t>(i)] = (float)re[static_cast<size_t>(i)];
        frames.push_back(std::move(frame));
    }
    return frames;
}

std::vector<std::vector<float>> singleCycleFrame(const std::vector<float>& x) {
    return sliceToFrames(x, (double)std::max<size_t>(1, x.size()));
}

std::vector<float> frameFromDrawing(const std::vector<float>& points) {
    int n = (int)points.size();
    std::vector<float> frame(SIZE, 0.0f);
    if (n == 0) return frame;
    for (int i = 0; i < SIZE; ++i) {
        double src = ((double)i / SIZE) * n;
        int i0 = (int)std::floor(src);
        double frac = src - i0;
        float a = points[static_cast<size_t>(i0 % n)];
        float b = points[static_cast<size_t>((i0 + 1) % n)];
        frame[(size_t)i] = a + (float)frac * (b - a);
    }
    return frame;
}

} // namespace fable
