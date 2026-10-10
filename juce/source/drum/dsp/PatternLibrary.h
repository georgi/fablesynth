#pragma once
#include <juce_core/juce_core.h>
#include "PatternLibrary.gen.h"

#include <cstdint>
#include <vector>

// DR-1 pattern library — the native side of src/drum/patternLibrary.ts. The
// generated header holds each entry's used bars packed at 2 bits per step;
// patternLibrary() decodes them once into full 4-bar DR-1 sequences
// ((bar * 16 + pad) * 16 + step, value 0 / 1 / 2) with chain 0..bars-1.
namespace fable {

struct PatternLibraryEntry {
    juce::String name, genre, role, family, note;
    int bpm = 120;
    float swing = 0;              // the swing the groove was written for, 0..1
    std::vector<uint8_t> steps;   // 1024 bytes; slots past the chain stay empty
    std::vector<int> chain;
};

namespace patternlib {
inline std::vector<uint8_t> decodeBase64(const char* s) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        return c == '+' ? 62 : c == '/' ? 63 : -1;
    };
    std::vector<uint8_t> out;
    unsigned acc = 0;
    int bits = 0;
    for (; *s && *s != '='; ++s) {
        const int v = value(*s);
        if (v < 0) continue;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((acc >> bits) & 0xff)); }
    }
    return out;
}
} // namespace patternlib

inline const std::vector<PatternLibraryEntry>& patternLibrary() {
    static const std::vector<PatternLibraryEntry> library = [] {
        std::vector<PatternLibraryEntry> out;
        out.reserve((size_t)patternlib::kCount);
        for (const auto& raw : patternlib::kEntries) {
            PatternLibraryEntry e;
            e.name = juce::String::fromUTF8(raw.name);
            e.genre = juce::String::fromUTF8(raw.genre);
            e.role = raw.role;
            e.family = raw.family;
            e.note = juce::String::fromUTF8(patternlib::kNotes[raw.note]);
            e.bpm = raw.bpm;
            e.swing = raw.swing;
            e.steps.assign(1024, 0);
            const auto packed = patternlib::decodeBase64(raw.packed);
            for (size_t k = 0; k < packed.size() * 4 && k < e.steps.size(); ++k)
                e.steps[k] = (uint8_t)((packed[k >> 2] >> ((k & 3) * 2)) & 3);
            for (int bar = 0; bar < raw.bars; ++bar) e.chain.push_back(bar);
            out.push_back(std::move(e));
        }
        return out;
    }();
    return library;
}

} // namespace fable
