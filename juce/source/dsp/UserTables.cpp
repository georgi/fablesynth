#include "UserTables.h"
#include <algorithm>
#include <cmath>

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

    int bestLag = minLag;
    double bestScore = -1e300;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double corr = 0;
        for (int i = 0; i < win - lag; ++i)
            corr += static_cast<double>(x[static_cast<size_t>(i)])
                  * x[static_cast<size_t>(i + lag)];
        // Bias slightly toward longer periods to avoid octave-too-high errors.
        double score = (corr / energy) * (1.0 + (double)lag / maxLag * 0.02);
        if (score > bestScore) { bestScore = score; bestLag = lag; }
    }
    return bestLag;
}

std::vector<std::vector<float>> sliceToFrames(const std::vector<float>& x, double cycleLen) {
    double len = std::max(1.0, cycleLen);
    int total = (int)x.size();
    int nf = std::max(1, std::min(MAX_FRAMES, (int)std::floor(total / len)));
    std::vector<std::vector<float>> frames;
    frames.reserve(static_cast<size_t>(nf));
    for (int f = 0; f < nf; ++f) {
        double start = f * len;
        std::vector<float> frame(SIZE, 0.0f);
        for (int i = 0; i < SIZE; ++i) {
            double src = start + ((double)i / SIZE) * len;
            int i0 = (int)std::floor(src);
            double frac = src - i0;
            float a = (i0 >= 0 && i0 < total) ? x[static_cast<size_t>(i0)] : 0.0f;
            float b = (i0 + 1 >= 0 && i0 + 1 < total)
                        ? x[static_cast<size_t>(i0 + 1)] : a;
            frame[static_cast<size_t>(i)] = a + (float)frac * (b - a);
        }
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
