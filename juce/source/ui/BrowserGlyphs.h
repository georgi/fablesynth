#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/Wavetables.h"

// Four-cell sound fingerprints for the preset browser — ports of
// src/components/presetBrowser/PresetGlyph.tsx and
// src/drum/components/browser/DrumGlyph.tsx. Thin strokes in one colour; the
// detail strip passes fill = true for an area tint and cell dividers.
namespace fui::glyph {

// WT-1 / BL-1: oscillator A frame, filter response, amp envelope, LFO depth.
struct WaveData {
    int table = 0;
    float pos = 0;
    int filterType = -1;         // -1: filter off; else the shared filter type index
    float cutoff = 1000, res = 0;
    float env[4] = { 0, 0, 1, 0 };   // a, d, s, r
    float lfoDepth = 0;              // 0..1
};
void drawWave(juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool fill,
              const WaveData&, const fable::GeneratedTable* table);

struct AmpEnv {
    float att = 0, hold = 0, dec = 0.1f, curve = 0;
};

// DR-1 pad patch: pitch envelope, amp envelope, filter, osc/sample/noise/ring bars.
struct PadData {
    float penvAmt = 0, penvDec = 0.05f;
    AmpEnv amp;
    bool filterOn = false;
    int filterType = 0;
    float cutoff = 1000, res = 0;
    float mix[4] = { 0, 0, 0, 0 };
};
void drawPad(juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool fill, const PadData&);

// DR-1 kit: kick, snare and hat decays, then the three tom tunings.
struct KitData {
    AmpEnv voices[3];
    float toms[3] = { 0, 0, 0 };   // semitones
};
void drawKit(juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool fill, const KitData&);

} // namespace fui::glyph
