#pragma once
#include "../dsp/AnalysisTap.h"

#include <juce_core/juce_core.h>

#include <map>
#include <string>
#include <vector>

// Analysis sources a device exposes to the native auto-mix controls
// (see AutoMix.h). Plain data: taps are owned by the processors.
namespace fui {

// Where an EQ can listen. A null tap shows `note` and disables LEARN.
struct AutoEqSource {
    const fable::ProgramTap* tap = nullptr;
    // True when the tap sits before this EQ, so its colouring is not removed.
    bool preEq = false;
    std::string reference = "mix";
    juce::String note = "NO SIGNAL TAP";
};

// The compressor's detector input (max |L|,|R| per sample, or the key while keyed).
struct AutoCompSource {
    const fable::DetectorTrace* trace = nullptr;
    std::string style = "glue";
    juce::String note = "NO SIGNAL TAP";
};

// COMP side-chain menu: `sources` are the param's options, `labels` the
// display names (SQ-4 track names, DR-1 pad names). External sources need
// the SQ-4 key bus, so a standalone device lists them disabled.
struct SideChainInfo {
    std::vector<std::string> sources;
    bool hosted = false;
    std::map<std::string, juce::String> labels;
    juce::String label(int index) const;
    bool external(int index) const;
};

// SQ-4 display names for the track sources: TRACK n -> "n NAME".
std::map<std::string, juce::String> trackKeyLabels(const std::vector<std::string>& names);

} // namespace fui
