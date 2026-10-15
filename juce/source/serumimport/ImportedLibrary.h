#pragma once
// The user's imported presets on disk (written by FableSerumImport):
//   <folder>/<category>/<name>.json  one preset with its wavetables
//   <folder>/index.json              name, category, file and parameters of
//                                    every preset, so a browser can list and
//                                    draw them without parsing the tables.
#include "SerumPatch.h"

namespace serumimport {

struct ImportedEntry {
    juce::String name, category;
    juce::File file;
    std::vector<std::pair<std::string, float>> params;
};

// ~/Library/Application Support/FableSynth/Imported (or $FABLE_IMPORTED_DIR).
juce::File importedFolder();
juce::File indexFile(const juce::File& folder);

std::vector<ImportedEntry> loadIndex(const juce::File& folder);
// Adds or replaces entries (matched by file) and drops entries whose file is gone.
bool updateIndex(const juce::File& folder, const std::vector<ImportedEntry>& changed);
bool loadPreset(const ImportedEntry&, ImportedPreset& out);

// WT-1 browser role for a Serum category folder (bass, lead, pad, keys, pluck, fx).
juce::String roleForCategory(const juce::String& category);
// The category as a browser tag when it says more than the role ("hard" for
// "Bass (Hard)", "seq" for "Seq"); empty when it only repeats the role.
juce::String tagForCategory(const juce::String& category);

} // namespace serumimport
