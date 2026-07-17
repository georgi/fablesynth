// DR-1 factory kits — C++ port of src/drum/kits.ts. The 14 derived kits
// (TR-VOID … 808+UZU HYBRID) plus four authored kits (NEON GRID, ACID CAVE,
// BOOM BAP, PIRATE RADIO) that ship multi-pattern A A A B chains. Factory
// entries contain only authored overrides on top of the canonical drum
// defaults; applyKit() fills the rest, mirroring kitToState. JUCE-free.
#pragma once

#include "DrumParams.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fable {

struct DrumKit {
    std::string name;
    std::vector<std::pair<std::string, float>> params;   // overrides on defaults, string pids
    std::array<std::string, DR_NPADS> padNames;
    std::vector<uint8_t> patterns;                        // 4*16*16
    std::vector<int> chain;                               // e.g. {0}
};

// All 18 kits in the same order as FACTORY_KITS in kits.ts.
const std::vector<DrumKit>& factoryKits();

// defaultDrumParams() + kit overrides (resolved via drumIdFromString).
DrumParamArray applyKit(const DrumKit& kit);

} // namespace fable
