#pragma once
#include <juce_core/juce_core.h>
#include "../dsp/Params.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Helpers shared by the per-machine preset browser adapters.
namespace fui::browser {

// One VARY rule: a parameter id and how far it may move. Log params move by up
// to +-amount octaves; linear params by up to +-amount of their range.
struct VaryRule { const char* id; float amount; };

// `r` is in [-1, 1]. Enums and bools stay put.
inline float nudgeValue(const fable::ParamInfo& d, float v, float amount, float r) {
    if (d.kind != fable::Kind::Float) return v;
    const float next = d.curve == fable::Curve::Log ? v * std::pow(2.0f, r * amount) : v + r * amount * (d.max - d.min);
    float out = juce::jlimit(d.min, d.max, next);
    if (d.curve == fable::Curve::Int) out = juce::jlimit(d.min, d.max, std::round(out));
    return out;
}

// FNV-1a over float bit patterns: a cheap "did the glyph inputs change" key.
inline std::uint64_t hashFloats(std::initializer_list<float> values, std::uint64_t seed = 1469598103934665603ull) {
    std::uint64_t h = seed;
    for (float v : values) {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        h = (h ^ bits) * 1099511628211ull;
    }
    return h;
}

inline juce::StringArray splitWords(const char* s) {
    juce::StringArray out;
    out.addTokens(juce::String(s), " ", "");
    out.removeEmptyStrings();
    return out;
}

inline const juce::String kDot = juce::String::fromUTF8(" \xc2\xb7 ");

} // namespace fui::browser
