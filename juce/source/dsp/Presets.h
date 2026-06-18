// Factory presets — C++ port of src/presets.ts. Each preset is a list of
// (string id, value) overrides applied on top of the parameter defaults.
#pragma once

#include "Params.h"
#include <string>
#include <utility>
#include <vector>

namespace fable {

struct Preset {
    std::string name;
    std::vector<std::pair<std::string, float>> params; // overrides
};

const std::vector<Preset>& factoryPresets();

// Apply a preset onto a defaults-initialized ParamArray.
ParamArray applyPreset(const Preset& preset);

} // namespace fable
