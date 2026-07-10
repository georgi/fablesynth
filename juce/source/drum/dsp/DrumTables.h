// DR-1 procedural drum tables. C++ port of src/drum/engine/drumtables.ts.
// Each spec renders FRAMES time-domain single cycles which buildUserTable
// runs through the shared FFT band-limit + mip pipeline — so drum tables
// anti-alias exactly like every other table.
//
// The full DR-1 table list is generateDrumTables() followed by
// generateTables() (drum tables first), matching DRUM_TABLE_NAMES.
#pragma once

#include "../../dsp/Wavetables.h"

#include <vector>

namespace fable {

// Exactly THUD, CRACK, TINE, GRIT in that order.
std::vector<GeneratedTable> generateDrumTables();

} // namespace fable
