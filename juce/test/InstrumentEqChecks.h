#pragma once
#include <cmath>
#include <vector>

// Exercise the actual instrument FX chain, not just the response drawing.
// configure(fx, sr, enabled, gain) must bypass all other FX and select a 1 kHz bell.
template <class Fx, class Configure>
bool instrumentEqChecks(Configure configure) {
    for (double sr : {44100., 48000., 96000.}) {
        auto render = [&](bool on, float gain) {
            Fx fx;
            configure(fx, sr, on, gain);
            const int n = (int)sr;
            std::vector<float> l((size_t)n), r((size_t)n);
            for (int i = 0; i < n; ++i) l[(size_t)i] = .01f * (float)std::sin(2 * 3.141592653589793 * 1000 * i / sr);
            // Vary block boundaries so coefficient ramps cannot depend on the host buffer size.
            for (int i = 0; i < n; i += 127) fx.process(l.data() + i, r.data() + i, std::min(127, n - i));
            double sum = 0;
            for (int i = n / 2; i < n; ++i) {
                if (!std::isfinite(l[(size_t)i]) || std::abs(r[(size_t)i]) > 1e-8f) return -1.;
                sum += l[(size_t)i] * l[(size_t)i];
            }
            return sum;
        };
        const double bypass = render(false, 0), neutral = render(true, 0);
        const double boost = render(true, 6), cut = render(true, -6), bypassBoost = render(false, 6);
        if (bypass <= 0 || neutral <= 0 || boost <= 0 || cut <= 0 || bypassBoost != bypass) return false;
        if (std::abs(10 * std::log10(neutral / bypass)) > .01 ||
            std::abs(10 * std::log10(boost / bypass) - 6) > .05 ||
            std::abs(10 * std::log10(cut / bypass) + 6) > .05) return false;
    }
    return true;
}
