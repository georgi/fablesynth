#pragma once
// Parser for the authored pattern-preset notation — C++ port of
// src/shared/stepNotation.ts. One bar is 16 whitespace-separated tokens:
// "." rest, "-" hold (previous note +1 step), a note letter with optional
// "#"/"b", then optional "," / "'" (octave -1 / +1) and "!" / "~" flags
// (accent / slide). Catalogs are authored, so a malformed bar parses as rests.

#include <array>
#include <sstream>
#include <string>
#include <vector>

namespace fable {

struct NotatedStep {
    bool on = false;
    int note = 0, oct = 0;
    bool acc = false, slide = false;
    int duration = 1;
};

struct NotatedPreset {
    const char* name;
    std::vector<const char*> bars;   // 1..4 bars, played in order
};

inline std::array<NotatedStep, 16> parseNotatedBar(const char* bar) {
    std::array<NotatedStep, 16> steps {};
    std::istringstream in(bar);
    std::string token;
    int i = 0, last = -1;
    while (in >> token && i < 16) {
        NotatedStep& s = steps[(size_t)i];
        if (token == ".") { last = -1; ++i; continue; }
        if (token == "-") { if (last >= 0) ++steps[(size_t)last].duration; ++i; continue; }
        static const int letters[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A..G
        const char letter = token[0];
        if (letter < 'A' || letter > 'G') { last = -1; ++i; continue; }
        int note = letters[letter - 'A'];
        size_t k = 1;
        if (k < token.size() && token[k] == '#') { ++note; ++k; }
        else if (k < token.size() && token[k] == 'b') { --note; ++k; }
        s.on = true;
        s.note = (note + 12) % 12;
        if (k < token.size() && token[k] == ',') { s.oct = -1; ++k; }
        else if (k < token.size() && token[k] == '\'') { s.oct = 1; ++k; }
        for (; k < token.size(); ++k) {
            if (token[k] == '!') s.acc = true;
            if (token[k] == '~') s.slide = true;
        }
        last = i++;
    }
    return steps;
}

} // namespace fable
