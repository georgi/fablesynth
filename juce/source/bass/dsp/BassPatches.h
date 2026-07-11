// BL-1 factory patches — C++ port of src/bass/patches.ts (+ the packed
// pattern helpers from src/bass/seq.ts the patches are built with). A patch is
// the whole machine state — sound params + the four pitch patterns + chain
// (same "kit" model as DR-1, since a 303 line is inseparable from its
// pattern). Factory entries contain only authored overrides; applyBassPatch()
// fills the rest from the canonical defaults, mirroring patchToState. JUCE-free.
#pragma once

#include "BassParams.h"
#include "BassEngine.h"     // BassStep

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fable {

// ---- packed pattern helpers (seq.ts) ----
// byte 0: bit0 on, bit1 acc, bit2 slide · byte 1: note 0..11 · byte 2: oct+1
constexpr int BL_NOTE_LANES = 12;
constexpr int BL_OCT_MIN = -1, BL_OCT_MAX = 1;

// A pattern step for editing (seq.ts Step: note 0..11 + oct, unlike the
// engine-facing BassStep whose semi is already combined).
struct BassSeqStep {
    bool on = false;
    int  note = 0;      // 0..11
    int  oct = 0;       // -1 | 0 | 1
    bool acc = false, slide = false;
};

// oct byte defaults to 1 (= oct 0) so untouched rests read back neutral.
std::vector<uint8_t> makeEmptyBassPatterns();

BassSeqStep getBassStep(const uint8_t* pats, int pat, int step);
void setBassStep(uint8_t* pats, int pat, int step, const BassSeqStep& s);

// ---- factory patches (patches.ts) ----
struct BassPatch {
    std::string name;
    std::vector<std::pair<std::string, float>> params; // overrides on defaults
    std::vector<uint8_t> patterns;                     // BL_PATTERN_BYTES
    std::vector<int> chain;                            // e.g. {0}
};

// ACID LINE, RUBBER SUB, NEON SQUELCH — same order as FACTORY_PATCHES.
const std::vector<BassPatch>& bassFactoryPatches();

// defaultBassParams() + patch overrides (resolved via bassIdFromString).
BassParamArray applyBassPatch(const BassPatch& patch);

} // namespace fable
