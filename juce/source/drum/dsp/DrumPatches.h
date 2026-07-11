// DR-1 factory pad patches — C++ port of src/drum/patches.ts. A patch is one
// pad's sound (kick, snare, hat…), independent of kits. Factory entries hold
// only authored overrides on the pad defaults; applyPatchToPad() resolves
// defaults ∪ overrides for every included field so a patch fully resets the
// pad. `out` and `choke` are kit-level routing and never included. JUCE-free.
#pragma once

#include "DrumParams.h"

#include <string>
#include <utility>
#include <vector>

namespace fable {

struct PadPatch {
    std::string name;                                    // displayed uppercase, e.g. "BD DEEP"
    std::vector<std::pair<std::string, float>> params;   // PAD-RELATIVE ids -> overrides
};

// SAME names/values/order as FACTORY_PATCHES in src/drum/patches.ts.
const std::vector<PadPatch>& factoryPatches();

// Absolute (flat pid, value) pairs applying `patch` to pad `padI`: defaults ∪
// overrides for every included field. `pad<I>.out` / `pad<I>.choke` are never
// returned, so their current values survive.
std::vector<std::pair<int, float>> applyPatchToPad(int padI, const PadPatch& patch);

} // namespace fable
