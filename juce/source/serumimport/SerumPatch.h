#pragma once
// Serum 1 (.fxp) -> WT-1 patch conversion. Pure data code: no plugin hosting.
// SerumHost supplies the parameter dump and the wavetable capture from the
// user's own Serum install; this file maps them onto WT-1 parameters.
//
// Format facts (measured against Serum 1.x factory presets):
//   .fxp = 'CcnK' / 'FPCh' / fxID 'XfsX', chunk at byte 60 (size at 56, BE).
//   chunk = zlib(main struct, 20704 bytes) + zlib(embedded table keyframes) + 4 bytes.
//   main struct: 16 mod slots of 40 bytes, marker [?? ?? slot ff] at 32 + 40 * slot;
//   amount float at marker-28, source int at marker-12 (low 16 = source, high
//   16 = aux), destination VST parameter index (u16) at marker-6.
//   Strings: osc A table 15368, osc B table 15880, noise sample 16392.
//   Voice mode floats: 14344 MONO, 14348 LEGATO, 14372 voice count (0 = one).
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

namespace serumimport {

constexpr int FRAME = 2048;           // WT-1 / Serum single-cycle length
constexpr int MAIN_STRUCT_SIZE = 20704;

struct SerumParam {
    std::string name;   // Serum's VST parameter name
    float v = 0;        // normalized value
    std::string text;   // Serum's display text (trimmed)
};

struct ModSlot {
    int slot = 0;       // 1..16
    int src = 0, aux = 0;
    float amt = 0;
    int dest = 0;       // Serum VST parameter index
};

struct FxpData {
    juce::String programName;
    juce::MemoryBlock chunk;          // the opaque chunk, as Serum's state
    juce::MemoryBlock main;           // first zlib stream, decompressed
    std::vector<ModSlot> slots() const;
    std::string cstr(int offset) const;
    float floatAt(int offset) const;
};

// Parses an .fxp file. Returns false (with a reason) for anything that is not
// a Serum 1 program chunk.
bool parseFxp(const juce::MemoryBlock& file, FxpData& out, juce::String& error);

struct Table {
    std::string name;
    int frames = 0;
    std::vector<float> wave;          // frames * FRAME, peak-normalized
};

struct ImportedPreset {
    std::string name, category;
    std::vector<std::pair<std::string, float>> params; // WT-1 parameter id -> value
    std::vector<Table> tables;        // pool for oscX.table >= 6
    std::vector<std::string> losses;  // what could not be reproduced
};

// Captured single cycles across WTPos, per oscillator (empty when off).
struct Capture {
    std::vector<float> oscA, oscB;    // K frames * FRAME
};

ImportedPreset convert(const std::vector<SerumParam>& dump, const FxpData& fxp, const Capture& capture,
                       const std::string& name, const std::string& category);

// Static capture -> one frame; peak-normalize. Exposed for tests.
Table tableFromCapture(const std::vector<float>& frames, const std::string& name);
Table subTable(const std::string& shape);

// WT-1 preset JSON (web Preset shape plus category/losses/source).
juce::var toJson(const ImportedPreset&);
bool fromJson(const juce::var&, ImportedPreset&);

} // namespace serumimport
