// The factory session — C++ port of src/seq/factory.ts. Builds "NEON TALE",
// the SQ-4 mock's six scenes as real, playable clips. JUCE-free.
#pragma once

#include "SeqModel.h"

#include <array>
#include <string>
#include <vector>

namespace fable {

SessionData factorySession();

// One top-level SQ-4 library item. Unlike a device patch or a clip-library
// entry, this is a complete playable session: arrangement, clip bytes, all
// four device patches, tempo, swing, quantize, and mixer state.
struct SessionPreset {
    std::string name;
    std::string family;
    std::string variation;
    int energy = 3; // 1..5, used by the future library browser/filter UI.
    std::vector<std::string> tags;
    SessionData session;
};

const std::vector<SessionPreset>& factorySessionLibrary();

// Which library preset SQ-4 boots into. factorySession() above is the NEON
// TALE base every preset is built from and is unaffected by this; mirrors
// sessionPresets.ts's DEFAULT_SESSION_NAME / defaultSession().
inline constexpr const char* kDefaultSessionName = "GLASS BLOOM";
SessionData defaultSession();

} // namespace fable
