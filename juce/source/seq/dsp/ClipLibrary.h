// Portable SQ-4 clip-library schema and taxonomy.  This layer deliberately
// carries decoded bytes only; JSON/base64 persistence belongs to the host.
#pragma once

#include "SeqProtocol.h"

#include <array>
#include <string>
#include <vector>

namespace fable {

struct ClipLibraryEntry {
    std::string id;              // stable slug/UUID
    std::string name;
    Machine machine = Machine::DR1;
    int bars = 1;
    std::vector<uint8_t> bytes;

    std::string family;
    std::string role;
    int energy = 1;              // 1 (low) .. 5 (high)
    std::vector<std::string> tags;

    int root = -1;               // MIDI note, or -1 when not applicable
    std::string scale;
    bool transpose = false;
};

enum class ClipTransformKind {
    transpose, rotate, reverse, densityHalf, densityDouble, accentShift,
    humanize, extractBar, repeat, drumLaneRemap
};

// Always returns a fresh entry/byte buffer. The source is never changed.
// `amount` is semitones/steps/bar-index/bar-count depending on the operation;
// humanize uses it as a deterministic seed.
ClipLibraryEntry transformClipLibraryEntry(const ClipLibraryEntry& source,
                                           ClipTransformKind kind,
                                           int amount = 1);

// Canonical v1 roles. Keep these stable: they are serialized strings and are
// shared by factory content, browser filters, and import validation.
extern const std::array<const char*, 10> SQ_DR1_CLIP_ROLES;
extern const std::array<const char*, 10> SQ_BL1_CLIP_ROLES;
extern const std::array<const char*, 10> SQ_WT1_CLIP_ROLES;
extern const std::array<const char*, 9> SQ_CLIP_FAMILIES;
extern const std::array<const char*, 18> SQ_CLIP_TAGS;

// Returns the role taxonomy for a machine. An invalid Machine value yields an
// empty vector rather than silently selecting a taxonomy.
const std::vector<std::string>& sqClipRoles(Machine machine);
bool sqIsKnownClipRole(Machine machine, const std::string& role);
bool sqIsKnownClipFamily(const std::string& family);
bool sqIsKnownClipTag(const std::string& tag);

// Returns "" when valid, otherwise a stable human-readable reason. Besides
// metadata, this checks byte length and the byte-level SQ-4 wire format.
std::string validateClipLibraryEntry(const ClipLibraryEntry& entry);

// Entry validation plus unique, non-empty IDs across the complete library.
std::string validateClipLibrary(const std::vector<ClipLibraryEntry>& entries);

} // namespace fable
