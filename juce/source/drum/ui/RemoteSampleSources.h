#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace fui {

// Provenance-only bookmarks shown by DR-1's remote import flow. These are
// deliberately source pages rather than bundled files: the user chooses a
// sound, confirms its license, and initiates a direct file import themselves.
// That avoids silently redistributing a "royalty-free for songs" sample pack.
struct Cc0SampleSource {
    const char* id;
    const char* name;
    const char* detail;
    const char* url;
};

inline const std::array<Cc0SampleSource, 2>& dr1Cc0SampleSources() {
    static const std::array<Cc0SampleSource, 2> sources {{
        { "selekt-cc0", "SELEKT CC0 DRUM KIT",
          "CC0/public-domain source packs. Confirm the selected file is marked CC0 before importing.",
          "https://selektaudio.com/free-samples" },
        { "signature-cc0", "SIGNATURE SOUNDS CC0",
          "CC0 drum hits, percussion, textures and source recordings.",
          "https://signaturesounds.org/the-signature-soundbank" },
    }};
    return sources;
}

} // namespace fui
