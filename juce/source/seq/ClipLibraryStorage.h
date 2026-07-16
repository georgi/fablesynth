#pragma once

#include "dsp/ClipLibrary.h"
#include <juce_core/juce_core.h>

namespace fable {

enum class ClipSource { factory, user, imported };

struct SourcedClip {
    ClipLibraryEntry entry;
    ClipSource source = ClipSource::factory;
};

// Native counterpart of the web library store. USER and IMPORTED documents
// live outside plugin state so they are shared by every SQ-4 instance.
class ClipLibraryStorage {
public:
    explicit ClipLibraryStorage(juce::File root = defaultRoot());

    static juce::File defaultRoot();
    static juce::String sourceName(ClipSource);
    static juce::String encodeSqclip(const std::vector<ClipLibraryEntry>&);
    static bool decodeSqclip(const juce::String&, std::vector<ClipLibraryEntry>&,
                             juce::String& error);

    bool reload(juce::String* error = nullptr);
    std::vector<SourcedClip> all() const;
    const std::vector<ClipLibraryEntry>& users() const { return users_; }
    const std::vector<ClipLibraryEntry>& imported() const { return imported_; }

    bool addUser(ClipLibraryEntry, juce::String* error = nullptr);
    bool importSqclip(const juce::String&, juce::String* error = nullptr);
    bool exportSqclip(const ClipLibraryEntry&, const juce::File&, juce::String* error = nullptr) const;
    juce::File root() const { return root_; }

private:
    bool persist(ClipSource, juce::String*);
    bool uniqueAgainstAll(const ClipLibraryEntry&, juce::String*) const;

    juce::File root_;
    std::vector<ClipLibraryEntry> users_, imported_;
};

} // namespace fable
