#include "ImportedLibrary.h"

namespace serumimport {

juce::File importedFolder() {
    const auto env = juce::SystemStats::getEnvironmentVariable("FABLE_IMPORTED_DIR", {});
    if (env.isNotEmpty()) return juce::File(env);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support/FableSynth/Imported");
}

juce::File indexFile(const juce::File& folder) { return folder.getChildFile("index.json"); }

static juce::var entryJson(const ImportedEntry& e, const juce::File& folder) {
    auto* o = new juce::DynamicObject();
    o->setProperty("name", e.name);
    o->setProperty("category", e.category);
    o->setProperty("file", e.file.getRelativePathFrom(folder));
    auto* params = new juce::DynamicObject();
    for (const auto& [k, v] : e.params) params->setProperty(juce::Identifier(k), v);
    o->setProperty("params", juce::var(params));
    return juce::var(o);
}

std::vector<ImportedEntry> loadIndex(const juce::File& folder) {
    std::vector<ImportedEntry> out;
    const auto json = juce::JSON::parse(indexFile(folder));
    if (auto* list = json["presets"].getArray())
        for (const auto& v : *list) {
            ImportedEntry e;
            e.name = v["name"].toString();
            e.category = v["category"].toString();
            e.file = folder.getChildFile(v["file"].toString());
            if (auto* params = v["params"].getDynamicObject())
                for (const auto& kv : params->getProperties())
                    e.params.emplace_back(kv.name.toString().toStdString(), (float)(double)kv.value);
            if (e.name.isNotEmpty() && e.file.existsAsFile()) out.push_back(std::move(e));
        }
    return out;
}

bool updateIndex(const juce::File& folder, const std::vector<ImportedEntry>& changed) {
    auto entries = loadIndex(folder); // drops entries whose file is gone
    for (const auto& c : changed) {
        auto it = std::find_if(entries.begin(), entries.end(), [&](const ImportedEntry& e) { return e.file == c.file; });
        if (it != entries.end()) *it = c; else entries.push_back(c);
    }
    std::sort(entries.begin(), entries.end(), [](const ImportedEntry& a, const ImportedEntry& b) {
        return a.category != b.category ? a.category < b.category : a.name.compareNatural(b.name) < 0;
    });
    juce::Array<juce::var> list;
    for (const auto& e : entries) list.add(entryJson(e, folder));
    auto* root = new juce::DynamicObject();
    root->setProperty("v", 1);
    root->setProperty("presets", list);
    return indexFile(folder).replaceWithText(juce::JSON::toString(juce::var(root), true));
}

bool loadPreset(const ImportedEntry& e, ImportedPreset& out) {
    return fromJson(juce::JSON::parse(e.file), out);
}

juce::String roleForCategory(const juce::String& category) {
    const auto c = category.toLowerCase();
    if (c.startsWith("bass")) return "bass";
    if (c.startsWith("lead")) return "lead";
    if (c.startsWith("pad")) return "pad";
    if (c.startsWith("pluck")) return "pluck";
    if (c.startsWith("synth") || c.startsWith("key") || c.startsWith("seq")) return "keys";
    return "fx";
}

juce::String tagForCategory(const juce::String& category) {
    const auto c = category.toLowerCase().trim(), role = roleForCategory(category);
    if (c.contains("(")) return c.fromFirstOccurrenceOf("(", false, false).upToFirstOccurrenceOf(")", false, false).trim();
    if (c.startsWith(role) || (role == "pluck" && c.startsWith("pluck")) || (role == "fx" && c == "fx")) return {};
    return c;
}

} // namespace serumimport
