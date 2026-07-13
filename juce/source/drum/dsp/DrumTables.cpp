// Function-for-function port of src/drum/engine/drumtables.ts. Formulas are
// pure math and transcribed exactly; buildUserTable normalizes peaks, which
// matches the web pipeline.
#include "DrumTables.h"

#include <cmath>
#include <string>

namespace fable {
namespace {

constexpr double kTau = 6.283185307179586476925286766559; // 2*pi

void thud(double t, std::vector<float>& out) {
    for (int i = 0; i < SIZE; i++) {
        const double x = (double)i / SIZE;
        double s = std::sin(kTau * x);
        s += t * 0.6 * std::sin(kTau * 2 * x + 0.6);
        s += t * t * 0.45 * std::sin(kTau * 3 * x + 1.2);
        out[static_cast<size_t>(i)] = (float)std::tanh(s * (1 + t * 1.8));
    }
}

void crack(double t, std::vector<float>& out) {
    for (int i = 0; i < SIZE; i++) {
        const double x = (double)i / SIZE;
        double s = 0;
        for (int k = 1; k <= 31; k += 2) {
            const double comb = 0.5 + 0.5 * std::cos(k * 0.9 + t * 5.2);
            const double roll = std::exp(-std::pow((k - 5 - t * 10) / 7.0, 2.0));
            s += comb * roll * std::sin(kTau * k * x + std::sin(k * 7.31) * 1.4);
        }
        out[static_cast<size_t>(i)] = (float)s;
    }
}

constexpr int TINE_K[] = { 1, 4, 7, 11, 16, 22 };

void tine(double t, std::vector<float>& out) {
    for (int i = 0; i < SIZE; i++) {
        const double x = (double)i / SIZE;
        double s = 0;
        for (int j = 0; j < 6; j++) {
            const double a = std::pow(std::max(t, 0.001), 0.3 * j) / (j + 1);
            s += a * std::sin(kTau * TINE_K[j] * x + j * j * 1.7);
        }
        out[static_cast<size_t>(i)] = (float)s;
    }
}

void grit(double t, std::vector<float>& out) {
    const int hold = 2 + (int)std::lround(30 * t);
    double held = 0;
    for (int i = 0; i < SIZE; i++) {
        if (i % hold == 0) {
            const double r = std::sin((i + 1) * 127.1) * 43758.5453;
            held = (r - std::floor(r)) * 2 - 1;
        }
        // blend toward a sine at t=0 so frame 0 is tonal, not static
        const double x = (double)i / SIZE;
        out[static_cast<size_t>(i)] = (float)(held * t + std::sin(kTau * x) * (1 - t));
    }
}

using FrameFn = void (*)(double, std::vector<float>&);

struct Spec { const char* name; FrameFn fn; };

constexpr Spec SPECS[] = {
    { "THUD",  thud  },
    { "CRACK", crack },
    { "TINE",  tine  },
    { "GRIT",  grit  },
};

} // namespace

std::vector<GeneratedTable> generateDrumTables() {
    std::vector<GeneratedTable> tables;
    tables.reserve(4);
    for (const auto& spec : SPECS) {
        std::vector<std::vector<float>> frames(FRAMES, std::vector<float>(SIZE));
        for (int f = 0; f < FRAMES; f++)
            spec.fn((double)f / (FRAMES - 1), frames[static_cast<size_t>(f)]);
        tables.push_back(buildUserTable(spec.name, frames));
    }
    return tables;
}

} // namespace fable
