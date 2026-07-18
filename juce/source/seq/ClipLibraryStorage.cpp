#include "ClipLibraryStorage.h"
#include "dsp/ClipLibrary.gen.h"

#include <set>
#include <cmath>

namespace fable {
namespace {

const char* machineName(Machine m) {
    return m == Machine::DR1 ? "DR1" : m == Machine::BL1 ? "BL1" : "WT1";
}

bool parseMachine(const juce::String& s, Machine& m) {
    if (s == "DR1") { m = Machine::DR1; return true; }
    if (s == "BL1") { m = Machine::BL1; return true; }
    if (s == "WT1") { m = Machine::WT1; return true; }
    return false;
}

bool isIntegerVar(const juce::var& v) {
    if (v.isInt() || v.isInt64()) return true;
    return v.isDouble() && std::isfinite((double)v)
        && std::floor((double)v) == (double)v;
}

juce::File sourceFile(const juce::File& root, ClipSource source) {
    return root.getChildFile(source == ClipSource::user ? "user-clips.json" : "imported-clips.json");
}

juce::var entryToVar(const ClipLibraryEntry& e) {
    auto* o = new juce::DynamicObject();
    o->setProperty("id", juce::String(e.id));
    o->setProperty("name", juce::String(e.name));
    o->setProperty("machine", machineName(e.machine));
    o->setProperty("bars", e.bars);
    o->setProperty("pattern", juce::Base64::toBase64(e.bytes.data(), e.bytes.size()));
    o->setProperty("family", juce::String(e.family));
    o->setProperty("role", juce::String(e.role));
    o->setProperty("energy", e.energy);
    juce::Array<juce::var> tags;
    for (const auto& tag : e.tags) tags.add(juce::String(tag));
    o->setProperty("tags", tags);
    if (e.root >= 0) o->setProperty("root", e.root);
    if (!e.scale.empty()) o->setProperty("scale", juce::String(e.scale));
    o->setProperty("transpose", e.transpose);
    return juce::var(o);
}

bool entryFromVar(const juce::var& v, ClipLibraryEntry& e, juce::String& error) {
    auto* o = v.getDynamicObject();
    if (o == nullptr) { error = "clip must be an object"; return false; }
    const std::set<juce::String> allowed { "id", "name", "machine", "bars", "pattern",
        "family", "role", "energy", "tags", "root", "scale", "transpose" };
    for (const auto& p : o->getProperties())
        if (allowed.count(p.name.toString()) == 0) {
            error = "unknown field " + p.name.toString(); return false;
        }
    for (const auto* required : { "id", "name", "machine", "bars", "pattern", "family",
                                  "role", "energy", "tags", "transpose" })
        if (!o->hasProperty(required)) { error = "missing " + juce::String(required); return false; }

    for (const auto* key : { "id", "name", "machine", "pattern", "family", "role" })
        if (!v[key].isString()) { error = juce::String(key) + " must be a string"; return false; }
    if (!isIntegerVar(v["bars"])) { error = "bars must be an integer"; return false; }
    if (!isIntegerVar(v["energy"])) { error = "energy must be an integer"; return false; }
    if (o->hasProperty("root") && !isIntegerVar(v["root"])) {
        error = "root must be an integer"; return false;
    }
    // `-1` is an in-memory sentinel for an omitted root.  It is not a valid
    // serialized value: the web schema accepts only explicit pitch classes.
    if (o->hasProperty("root") && ((int)v["root"] < 0 || (int)v["root"] > 11)) {
        error = "root out of range"; return false;
    }
    if (o->hasProperty("scale") && !v["scale"].isString()) {
        error = "scale must be a string"; return false;
    }

    e.id = v["id"].toString().toStdString();
    if (v["name"].toString().trim().isEmpty()) { error = "invalid name"; return false; }
    e.name = v["name"].toString().toStdString();
    if (!parseMachine(v["machine"].toString(), e.machine)) { error = "unknown machine"; return false; }
    e.bars = (int)v["bars"];
    const auto pattern = v["pattern"].toString();
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, pattern)) { error = "invalid base64"; return false; }
    e.bytes.resize(decoded.getDataSize());
    if (!e.bytes.empty()) std::memcpy(e.bytes.data(), decoded.getData(), e.bytes.size());
    if (juce::Base64::toBase64(e.bytes.data(), e.bytes.size()) != pattern) {
        error = "pattern is not canonical base64"; return false;
    }
    e.family = v["family"].toString().toStdString();
    e.role = v["role"].toString().toStdString();
    e.energy = (int)v["energy"];
    if (!v["tags"].isArray()) { error = "tags must be an array"; return false; }
    for (const auto& tag : *v["tags"].getArray()) {
        if (!tag.isString()) { error = "tag must be a string"; return false; }
        e.tags.push_back(tag.toString().toStdString());
    }
    e.root = o->hasProperty("root") ? (int)v["root"] : -1;
    if (o->hasProperty("scale") && v["scale"].toString().trim().isEmpty()) {
        error = "invalid scale"; return false;
    }
    e.scale = o->hasProperty("scale") ? v["scale"].toString().toStdString() : std::string{};
    if (!v["transpose"].isBool()) { error = "transpose must be boolean"; return false; }
    e.transpose = (bool)v["transpose"];
    const auto reason = validateClipLibraryEntry(e);
    if (!reason.empty()) { error = reason; return false; }
    return true;
}

juce::String normalized(const std::string& s) { return juce::String(s).trim().toLowerCase(); }

} // namespace

ClipLibraryStorage::ClipLibraryStorage(juce::File root) : root_(std::move(root)) { reload(); }

juce::File ClipLibraryStorage::defaultRoot() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("FableSynth").getChildFile("SQ-4").getChildFile("clip-library");
}

juce::String ClipLibraryStorage::sourceName(ClipSource source) {
    return source == ClipSource::factory ? "FACTORY" : source == ClipSource::user ? "USER" : "IMPORTED";
}

juce::String ClipLibraryStorage::encodeSqclip(const std::vector<ClipLibraryEntry>& clips) {
    auto* root = new juce::DynamicObject();
    root->setProperty("v", 1);
    juce::Array<juce::var> values;
    for (const auto& clip : clips) values.add(entryToVar(clip));
    root->setProperty("clips", values);
    return juce::JSON::toString(juce::var(root), true) + "\n";
}

bool ClipLibraryStorage::decodeSqclip(const juce::String& text,
                                     std::vector<ClipLibraryEntry>& out,
                                     juce::String& error) {
    const auto v = juce::JSON::parse(text);
    auto* root = v.getDynamicObject();
    if (root == nullptr) { error = "Invalid .sqclip JSON"; return false; }
    if (root->getProperties().size() != 2 || !root->hasProperty("v") || !root->hasProperty("clips")) {
        error = "library must contain only v and clips"; return false;
    }
    if (!isIntegerVar(v["v"]) || (int)v["v"] != 1 || !v["clips"].isArray()) {
        error = "unknown clip library version"; return false;
    }
    std::vector<ClipLibraryEntry> decoded;
    for (const auto& item : *v["clips"].getArray()) {
        ClipLibraryEntry entry;
        if (!entryFromVar(item, entry, error)) return false;
        decoded.push_back(std::move(entry));
    }
    const auto reason = validateClipLibrary(decoded);
    if (!reason.empty()) { error = reason; return false; }
    out = std::move(decoded);
    return true;
}

bool ClipLibraryStorage::reload(juce::String* error) {
    std::vector<ClipLibraryEntry> nextUser, nextImported;
    for (auto source : { ClipSource::user, ClipSource::imported }) {
        const auto file = sourceFile(root_, source);
        if (!file.existsAsFile()) continue;
        juce::String why;
        auto& dst = source == ClipSource::user ? nextUser : nextImported;
        if (!decodeSqclip(file.loadFileAsString(), dst, why)) {
            if (error) *error = sourceName(source) + ": " + why;
            return false;
        }
    }
    std::set<std::string> ids;
    std::set<juce::String> names;
    for (const auto& source : { std::cref(factoryClipLibrary()), std::cref(nextUser), std::cref(nextImported) })
        for (const auto& clip : source.get()) {
            if (!ids.insert(clip.id).second || !names.insert(normalized(clip.name)).second) {
                if (error) *error = "duplicate clip id or name";
                return false;
            }
        }
    users_ = std::move(nextUser); imported_ = std::move(nextImported);
    return true;
}

std::vector<SourcedClip> ClipLibraryStorage::all() const {
    std::vector<SourcedClip> result;
    for (const auto& clip : factoryClipLibrary()) result.push_back({ clip, ClipSource::factory });
    for (const auto& clip : users_) result.push_back({ clip, ClipSource::user });
    for (const auto& clip : imported_) result.push_back({ clip, ClipSource::imported });
    return result;
}

bool ClipLibraryStorage::uniqueAgainstAll(const ClipLibraryEntry& entry, juce::String* error) const {
    for (const auto& clip : all()) if (clip.entry.id == entry.id || normalized(clip.entry.name) == normalized(entry.name)) {
        if (error) *error = clip.entry.id == entry.id ? "Duplicate clip id" : "Duplicate clip name";
        return false;
    }
    return true;
}

bool ClipLibraryStorage::persist(ClipSource source, juce::String* error) {
    if (root_.createDirectory().failed()) { if (error) *error = "Could not create clip library folder"; return false; }
    const auto& entries = source == ClipSource::user ? users_ : imported_;
    if (!sourceFile(root_, source).replaceWithText(encodeSqclip(entries))) {
        if (error) *error = "Could not write clip library"; return false;
    }
    return true;
}

bool ClipLibraryStorage::addUser(ClipLibraryEntry entry, juce::String* error) {
    const auto reason = validateClipLibraryEntry(entry);
    if (!reason.empty()) { if (error) *error = reason; return false; }
    if (!uniqueAgainstAll(entry, error)) return false;
    users_.push_back(std::move(entry));
    if (persist(ClipSource::user, error)) return true;
    users_.pop_back(); return false;
}

bool ClipLibraryStorage::importSqclip(const juce::String& text, juce::String* error) {
    std::vector<ClipLibraryEntry> clips;
    juce::String why;
    if (!decodeSqclip(text, clips, why)) { if (error) *error = why; return false; }
    if (clips.empty()) {
        if (error) *error = "A .sqclip file must contain at least one clip";
        return false;
    }
    for (const auto& clip : clips) if (!uniqueAgainstAll(clip, error)) return false;
    // Also reject name collisions within the incoming batch.
    std::set<juce::String> names;
    for (const auto& clip : clips) if (!names.insert(normalized(clip.name)).second) {
        if (error) *error = "Duplicate clip name"; return false;
    }
    const auto oldSize = imported_.size();
    imported_.insert(imported_.end(), clips.begin(), clips.end());
    if (persist(ClipSource::imported, error)) return true;
    imported_.resize(oldSize); return false;
}

bool ClipLibraryStorage::exportSqclip(const ClipLibraryEntry& entry, const juce::File& file,
                                     juce::String* error) const {
    if (!file.replaceWithText(encodeSqclip({ entry }))) {
        if (error) *error = "Could not export .sqclip"; return false;
    }
    return true;
}

} // namespace fable
