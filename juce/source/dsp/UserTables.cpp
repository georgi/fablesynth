#include "UserTables.h"
#include <algorithm>
#include <cmath>

namespace fable {

// ---------- construction ----------
UserTable makeUserTable(const std::string& name, const std::vector<std::vector<float>>& frames) {
    int nf = std::max(1, std::min(MAX_FRAMES, (int)frames.size()));
    UserTable u;
    u.name = name;
    u.frames = nf;
    u.wave.assign((size_t)nf * SIZE, 0.0f);
    std::vector<std::vector<float>> use(frames.begin(), frames.begin() + nf);
    for (int f = 0; f < nf; ++f)
        for (int i = 0; i < SIZE && i < (int)use[f].size(); ++i)
            u.wave[(size_t)f * SIZE + i] = use[f][i];
    u.table = buildUserTable(name, use);
    return u;
}

UserTable userTableFromWave(const std::string& name, int frames, const std::vector<float>& wave) {
    int nf = std::max(1, std::min(MAX_FRAMES, frames));
    std::vector<std::vector<float>> fr;
    fr.reserve(nf);
    for (int f = 0; f < nf; ++f) {
        std::vector<float> frame(SIZE, 0.0f);
        for (int i = 0; i < SIZE; ++i) {
            size_t idx = (size_t)f * SIZE + i;
            frame[i] = idx < wave.size() ? wave[idx] : 0.0f;
        }
        fr.push_back(std::move(frame));
    }
    return makeUserTable(name, fr);
}

// ---------- audio analysis ----------
std::vector<float> mixToMono(const float* const* channels, int numChannels, int n) {
    std::vector<float> out(std::max(0, n), 0.0f);
    if (numChannels <= 0) return out;
    for (int ch = 0; ch < numChannels; ++ch) {
        const float* d = channels[ch];
        if (!d) continue;
        for (int i = 0; i < n; ++i) out[i] += d[i];
    }
    float g = 1.0f / (float)std::max(1, numChannels);
    for (int i = 0; i < n; ++i) out[i] *= g;
    return out;
}

double detectCycleLength(const std::vector<float>& x, double sampleRate) {
    int len = (int)x.size();
    int win = std::min(len, 16384);
    int minLag = std::max(2, (int)std::floor(sampleRate / 1000.0));
    int maxLag = std::min((int)std::floor(sampleRate / 40.0), (win >> 1) - 1);
    if (maxLag <= minLag) return std::max(2, std::min(SIZE, len));

    double energy = 1e-9;
    for (int i = 0; i < win; ++i) energy += (double)x[i] * x[i];

    int bestLag = minLag;
    double bestScore = -1e300;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double corr = 0;
        for (int i = 0; i < win - lag; ++i) corr += (double)x[i] * x[i + lag];
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
    frames.reserve(nf);
    for (int f = 0; f < nf; ++f) {
        double start = f * len;
        std::vector<float> frame(SIZE, 0.0f);
        for (int i = 0; i < SIZE; ++i) {
            double src = start + ((double)i / SIZE) * len;
            int i0 = (int)std::floor(src);
            double frac = src - i0;
            float a = (i0 >= 0 && i0 < total) ? x[i0] : 0.0f;
            float b = (i0 + 1 >= 0 && i0 + 1 < total) ? x[i0 + 1] : a;
            frame[i] = a + (float)frac * (b - a);
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
        float a = points[i0 % n];
        float b = points[(i0 + 1) % n];
        frame[i] = a + (float)frac * (b - a);
    }
    return frame;
}

} // namespace fable
