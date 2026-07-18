#include "ClipLibrary.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <stdexcept>

namespace fable {

const std::array<const char*, 10> SQ_DR1_CLIP_ROLES {{
    "four-on-floor", "breakbeat", "half-time", "electro", "percussion",
    "hats", "fill", "build-up", "sparse", "experimental"
}};

const std::array<const char*, 10> SQ_BL1_CLIP_ROLES {{
    "acid", "sub", "arpeggio", "ostinato", "syncopated",
    "sustained", "sliding", "minimal", "fill", "transition"
}};

const std::array<const char*, 10> SQ_WT1_CLIP_ROLES {{
    "lead", "chord", "pad-pulse", "arpeggio", "hook",
    "countermelody", "bass", "texture", "riser", "transition"
}};

const std::array<const char*, 9> SQ_CLIP_FAMILIES {{
    "techno", "house", "electro", "breaks", "acid", "ambient", "lo-fi",
    "cinematic", "experimental"
}};

const std::array<const char*, 18> SQ_CLIP_TAGS {{
    "dark", "bright", "warm", "cold", "sparse", "dense", "syncopated",
    "straight", "triplet-feel", "driving", "hypnotic", "melodic", "atonal",
    "peak-time", "build-up", "breakdown", "groovy", "glitchy"
}};

namespace {

template <size_t N>
std::vector<std::string> roleVector(const std::array<const char*, N>& roles) {
    return std::vector<std::string>(roles.begin(), roles.end());
}

const std::vector<std::string> dr1Roles = roleVector(SQ_DR1_CLIP_ROLES);
const std::vector<std::string> bl1Roles = roleVector(SQ_BL1_CLIP_ROLES);
const std::vector<std::string> wt1Roles = roleVector(SQ_WT1_CLIP_ROLES);
const std::vector<std::string> noRoles;

bool validMachine(Machine machine) {
    return machine == Machine::DR1 || machine == Machine::BL1 || machine == Machine::WT1;
}

bool validId(const std::string& id) {
    if (id.empty() || !std::isalnum((unsigned char)id.front())) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == ':' || c == '-';
    });
}

std::string validatePattern(const ClipLibraryEntry& entry) {
    const int expected = entry.bars * sqBytesPerBar(entry.machine);
    if ((int)entry.bytes.size() != expected)
        return "pattern is " + std::to_string(entry.bytes.size())
             + " bytes, expected " + std::to_string(expected);

    if (entry.machine == Machine::DR1) {
        for (size_t i = 0; i < entry.bytes.size(); ++i)
            if (entry.bytes[i] > 2)
                return "invalid DR1 value at byte " + std::to_string(i);
        return "";
    }

    for (int bar = 0; bar < entry.bars; ++bar) {
        const int lanes = entry.machine == Machine::WT1 ? SQ_WT_POLY_LANES : 1;
        for (int step = 0; step < SQ_STEPS_PER_BAR; ++step) for (int lane = 0; lane < lanes; ++lane) {
            const int offset = entry.machine == Machine::WT1 ? sqWtNoteIdx(bar, step, lane) : sqNoteIdx(bar, step);
            const int duration = (entry.bytes[(size_t)offset] >> 2) & 0x3f;
            if (duration < 1 || duration > 63)
                return "invalid note flags at byte " + std::to_string(offset);
            const uint8_t noteByte = entry.bytes[(size_t)offset + 1];
            if ((noteByte & 0x7f) > 11 || (entry.machine == Machine::WT1 && noteByte > 11))
                return "invalid note at byte " + std::to_string(offset + 1);
            if (entry.bytes[(size_t)offset + 2] > 2)
                return "invalid octave at byte " + std::to_string(offset + 2);
        }
    }
    return "";
}

} // namespace

const std::vector<std::string>& sqClipRoles(Machine machine) {
    switch (machine) {
        case Machine::DR1: return dr1Roles;
        case Machine::BL1: return bl1Roles;
        case Machine::WT1: return wt1Roles;
    }
    return noRoles;
}

bool sqIsKnownClipRole(Machine machine, const std::string& role) {
    const auto& roles = sqClipRoles(machine);
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}

bool sqIsKnownClipFamily(const std::string& family) {
    return std::find(SQ_CLIP_FAMILIES.begin(), SQ_CLIP_FAMILIES.end(), family)
        != SQ_CLIP_FAMILIES.end();
}

bool sqIsKnownClipTag(const std::string& tag) {
    return std::find(SQ_CLIP_TAGS.begin(), SQ_CLIP_TAGS.end(), tag) != SQ_CLIP_TAGS.end();
}

std::string validateClipLibraryEntry(const ClipLibraryEntry& entry) {
    if (!validId(entry.id)) return "invalid id";
    if (entry.name.empty()) return "name is empty";
    if (!validMachine(entry.machine)) return "unknown machine";
    if (!sqIsKnownClipFamily(entry.family)) return "unknown family " + entry.family;
    if (!sqIsKnownClipRole(entry.machine, entry.role)) return "unknown role " + entry.role;
    if (entry.energy < 1 || entry.energy > 5) return "energy out of range";
    if (entry.bars < 1 || entry.bars > SQ_MAX_BARS) return "bars out of range";
    if (entry.root < -1 || entry.root > 11) return "root out of range";
    if (entry.machine == Machine::DR1 && entry.root != -1)
        return "DR1 clips cannot have a root";
    if (entry.transpose && entry.root < 0)
        return "transpose requires a root";
    if (entry.root >= 0 && entry.scale.empty())
        return "root requires a scale";
    std::unordered_set<std::string> tags;
    for (const auto& tag : entry.tags) {
        if (!sqIsKnownClipTag(tag)) return "unknown tag " + tag;
        if (!tags.insert(tag).second) return "duplicate tag " + tag;
    }

    return validatePattern(entry);
}

std::string validateClipLibrary(const std::vector<ClipLibraryEntry>& entries) {
    std::unordered_set<std::string> ids;
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string reason = validateClipLibraryEntry(entries[i]);
        if (!reason.empty()) return "entry " + std::to_string(i) + ": " + reason;
        if (!ids.insert(entries[i].id).second)
            return "entry " + std::to_string(i) + ": duplicate id " + entries[i].id;
    }
    return "";
}

namespace {
int positiveMod(int n, int d) { return ((n % d) + d) % d; }

size_t stepOffset(const ClipLibraryEntry& entry, int step, int lane = 0) {
    const int bar = step / SQ_STEPS_PER_BAR, local = step % SQ_STEPS_PER_BAR;
    return (size_t)(entry.machine == Machine::DR1
        ? sqDr1Idx(bar, lane, local)
        : entry.machine == Machine::WT1 ? sqWtNoteIdx(bar, local, lane) : sqNoteIdx(bar, local));
}

bool stepOn(const ClipLibraryEntry& entry, const std::vector<uint8_t>& bytes,
            int step, int lane) {
    const auto o = stepOffset(entry, step, lane);
    return entry.machine == Machine::DR1 ? bytes[o] != 0 : (bytes[o] & 1) != 0;
}
} // namespace

ClipLibraryEntry transformClipLibraryEntry(const ClipLibraryEntry& source,
                                           ClipTransformKind kind, int amount) {
    if (!validateClipLibraryEntry(source).empty())
        throw std::invalid_argument("invalid source clip");
    ClipLibraryEntry out = source;
    const int total = source.bars * SQ_STEPS_PER_BAR;
    const int lanes = source.machine == Machine::DR1 ? 16
        : source.machine == Machine::WT1 ? SQ_WT_POLY_LANES : 1;
    const int stride = source.machine == Machine::DR1 ? 1 : 3;

    if (kind == ClipTransformKind::transpose) {
        if (source.machine == Machine::DR1) throw std::invalid_argument("DR1 cannot transpose");
        for (size_t o = 0; o < out.bytes.size(); o += 3) {
            if ((out.bytes[o] & 1) == 0) continue;
            const uint8_t slide = out.bytes[o + 1] & 0x80;
            int64_t shifted = (int)(out.bytes[o + 1] & 0x7f)
                            + 12 * ((int)out.bytes[o + 2] - 1)
                            + (int64_t)amount;
            if (shifted > 23) shifted -= ((shifted - 23 + 11) / 12) * 12;
            if (shifted < -12) shifted += ((-12 - shifted + 11) / 12) * 12;
            const int encoded = (int)shifted + 12;
            out.bytes[o + 1] = (uint8_t)(slide | (encoded % 12));
            out.bytes[o + 2] = (uint8_t)(encoded / 12);
        }
        if (out.root >= 0) out.root = positiveMod(out.root + amount, 12);
    } else if (kind == ClipTransformKind::rotate || kind == ClipTransformKind::reverse) {
        std::vector<uint8_t> bytes(out.bytes.size());
        for (int lane = 0; lane < lanes; ++lane) for (int src = 0; src < total; ++src) {
            const int dst = kind == ClipTransformKind::rotate
                ? positiveMod(src + amount, total) : total - 1 - src;
            std::copy_n(source.bytes.begin() + (ptrdiff_t)stepOffset(source, src, lane), stride,
                        bytes.begin() + (ptrdiff_t)stepOffset(source, dst, lane));
        }
        out.bytes = std::move(bytes);
    } else if (kind == ClipTransformKind::densityHalf) {
        for (int lane = 0; lane < lanes; ++lane) {
            int active = 0;
            for (int step = 0; step < total; ++step) if (stepOn(source, source.bytes, step, lane)) {
                if ((active++ & 1) != 0) {
                    const auto o = stepOffset(source, step, lane);
                    if (source.machine == Machine::DR1) out.bytes[o] = 0;
                    else out.bytes[o] &= (uint8_t)~1u;
                }
            }
        }
    } else if (kind == ClipTransformKind::densityDouble) {
        for (int lane = 0; lane < lanes; ++lane) for (int step = 0; step < total; ++step) {
            if (!stepOn(source, source.bytes, step, lane)) continue;
            const int next = positiveMod(step + 1, total);
            if (stepOn(source, source.bytes, next, lane)) continue;
            const auto from = stepOffset(source, step, lane), to = stepOffset(source, next, lane);
            std::copy_n(source.bytes.begin() + (ptrdiff_t)from, stride,
                        out.bytes.begin() + (ptrdiff_t)to);
        }
    } else if (kind == ClipTransformKind::accentShift) {
        for (int lane = 0; lane < lanes; ++lane) {
            std::vector<int> accents;
            for (int step = 0; step < total; ++step) {
                const auto o = stepOffset(source, step, lane);
                if (source.machine == Machine::DR1 ? source.bytes[o] == 2 : (source.bytes[o] & 2) != 0)
                    accents.push_back(step);
                if (source.machine == Machine::DR1) { if (out.bytes[o] == 2) out.bytes[o] = 1; }
                else out.bytes[o] &= (uint8_t)~2u;
            }
            for (int step : accents) for (int n = 0; n < total; ++n) {
                const int dst = positiveMod(step + amount + n, total);
                if (!stepOn(source, out.bytes, dst, lane)) continue;
                const auto o = stepOffset(source, dst, lane);
                if (source.machine == Machine::DR1) out.bytes[o] = 2; else out.bytes[o] |= 2;
                break;
            }
        }
    } else if (kind == ClipTransformKind::humanize) {
        // Xorshift's all-zero state is a fixed point. Map that one seed to a
        // non-zero state so a valid deterministic seed still humanizes rather
        // than accenting every active event.
        uint32_t state = amount == 0 ? 1u : (uint32_t)amount;
        auto random = [&state] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
        for (size_t o = 0; o < out.bytes.size(); o += (size_t)stride) {
            const bool on = source.machine == Machine::DR1 ? out.bytes[o] != 0 : (out.bytes[o] & 1) != 0;
            if (on && (random() & 3u) == 0)
                source.machine == Machine::DR1 ? out.bytes[o] = (out.bytes[o] == 2 ? 1 : 2)
                                               : out.bytes[o] ^= 2;
        }
    } else if (kind == ClipTransformKind::extractBar) {
        if (amount < 0 || amount >= source.bars) throw std::out_of_range("bar");
        const int size = sqBytesPerBar(source.machine);
        out.bytes.assign(source.bytes.begin() + amount * size, source.bytes.begin() + (amount + 1) * size);
        out.bars = 1;
    } else if (kind == ClipTransformKind::repeat) {
        if (amount < 1 || amount > SQ_MAX_BARS) throw std::out_of_range("bars");
        const int size = sqBytesPerBar(source.machine);
        out.bytes.resize((size_t)(amount * size));
        for (int bar = 0; bar < amount; ++bar)
            std::copy_n(source.bytes.begin() + (bar % source.bars) * size, size,
                        out.bytes.begin() + bar * size);
        out.bars = amount;
    } else if (kind == ClipTransformKind::drumLaneRemap) {
        if (source.machine != Machine::DR1) throw std::invalid_argument("DR1 only");
        std::vector<uint8_t> bytes(out.bytes.size());
        for (int lane = 0; lane < 16; ++lane) for (int step = 0; step < total; ++step) {
            const int dstLane = lane == 0 ? 1 : lane == 1 ? 0 : lane == 2 ? 3 : lane == 3 ? 2
                              : lane == 4 ? 5 : lane == 5 ? 4 : lane;
            bytes[stepOffset(source, step, dstLane)] = source.bytes[stepOffset(source, step, lane)];
        }
        out.bytes = std::move(bytes);
    }
    return out;
}

} // namespace fable
