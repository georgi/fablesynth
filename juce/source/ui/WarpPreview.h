#pragma once

#include "../dsp/Params.h"

#include <cmath>

// Display-side copy of the oscillator warp (Engine::setupOsc / renderOscWarp),
// for the wavetable view. C++ port of src/engine/warp.ts; keep them in step.
namespace fui {

// The mode's map parameter for an amount in 0..1 (the engine's `w`).
inline double warpParam(int mode, double amount) {
    const double amt = amount < 0 ? 0 : amount > 1 ? 1 : amount;
    switch (mode) {
        case fable::WARP_SYNC: case fable::WARP_SYNCWIN: return std::pow(2.0, amt * 4);
        case fable::WARP_BEND: case fable::WARP_ASYM: return std::pow(2.0, (amt - 0.5) * 6);
        case fable::WARP_PWM: return 1 - 0.9 * amt;
        case fable::WARP_MIRROR: return 0.5 + 0.4 * amt;
        case fable::WARP_QUANT: return std::pow(2.0, 1 + 7 * (1 - amt));
        case fable::WARP_FM: return amt * amt * 2;
        default: return amt;
    }
}

// One warped sample at oscillator phase `pn` (cycles, 0..1). `read` returns
// the unwarped waveform at a phase in cycles; `mod` is the other oscillator's
// waveform at the same time, for FM, AM and RM.
template <typename Read>
inline double warpSample(int mode, double amount, double pn, Read&& read, double mod = 0) {
    const double w = warpParam(mode, amount);
    switch (mode) {
        case fable::WARP_SYNC: return read(pn * w);
        case fable::WARP_SYNCWIN: return read(pn * w) * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * pn));
        case fable::WARP_BEND: {
            if (pn < 0.5) { const double x = 2 * pn; return read((0.5 * x) / (x + w * (1 - x))); }
            const double x = 2 - 2 * pn;
            return read(1 - (0.5 * x) / (x + w * (1 - x)));
        }
        case fable::WARP_ASYM: return read(pn / (pn + w * (1 - pn)));
        case fable::WARP_PWM: return read(pn < w ? pn / w : 1.0);
        case fable::WARP_MIRROR: return read(pn < w ? pn / w : (1 - pn) / (1 - w));
        case fable::WARP_QUANT: return read(std::floor(pn * w) / w);
        case fable::WARP_FM: return read(pn + w * mod);
        case fable::WARP_AM: return read(pn) * (1 - 0.5 * w + 0.5 * w * mod);
        case fable::WARP_RM: return read(pn) * (1 - w + w * mod);
        default: return read(pn);
    }
}

} // namespace fui
