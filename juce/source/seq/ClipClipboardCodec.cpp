// SQ-4 clip-clipboard <-> JSON codec (see ClipClipboardCodec.h). Mirrors
// SessionCodec's base64/JSON handling for the clip payloads.
#include "ClipClipboardCodec.h"

namespace fable {

static const char* machineStr(Machine m) {
    return m == Machine::DR1 ? "DR1" : m == Machine::BL1 ? "BL1" : "WT1";
}
static bool machineFromStr(const juce::String& s, Machine& out) {
    if (s == "DR1") { out = Machine::DR1; return true; }
    if (s == "BL1") { out = Machine::BL1; return true; }
    if (s == "WT1") { out = Machine::WT1; return true; }
    return false;
}

juce::String clipClipboardToJson(const ClipClipboardData& data) {
    auto* root = new juce::DynamicObject();
    root->setProperty("fable", "sq4-clips");
    root->setProperty("v", 1);

    juce::Array<juce::var> machines;
    for (Machine m : data.machines) machines.add(machineStr(m));
    root->setProperty("machines", machines);

    juce::Array<juce::var> rows;
    for (size_t r = 0; r < data.cells.size(); ++r) {
        juce::Array<juce::var> row;
        for (size_t c = 0; c < data.cells[r].size(); ++c) {
            if (c < data.hasCell[r].size() && data.hasCell[r][c]) {
                const auto& clip = data.cells[r][c];
                auto* co = new juce::DynamicObject();
                co->setProperty("name", juce::String(clip.name));
                co->setProperty("bars", clip.bars);
                co->setProperty("pattern",
                    juce::Base64::toBase64(clip.bytes.data(), clip.bytes.size()));
                row.add(juce::var(co));
            } else {
                row.add(juce::var()); // null slot (empty source cell)
            }
        }
        rows.add(juce::var(row));
    }
    root->setProperty("cells", rows);

    return juce::JSON::toString(juce::var(root));
}

bool clipClipboardFromJson(const juce::String& json, ClipClipboardData& out) {
    juce::var v = juce::JSON::parse(json);
    if (!v.isObject()) return false;
    if (v.getProperty("fable", "").toString() != "sq4-clips") return false;
    if ((int)v.getProperty("v", 0) != 1) return false;

    ClipClipboardData d;
    const juce::var& machines = v.getProperty("machines", juce::var());
    if (!machines.isArray() || machines.getArray()->isEmpty()) return false;
    for (const auto& mv : *machines.getArray()) {
        Machine m;
        if (!machineFromStr(mv.toString(), m)) return false;
        d.machines.push_back(m);
    }

    const juce::var& rows = v.getProperty("cells", juce::var());
    if (!rows.isArray() || rows.getArray()->isEmpty()) return false;
    for (const auto& rv : *rows.getArray()) {
        if (!rv.isArray()) return false;
        if ((size_t)rv.getArray()->size() != d.machines.size()) return false;
        std::vector<ClipData> row;
        std::vector<bool> has;
        for (int c = 0; c < rv.getArray()->size(); ++c) {
            const juce::var& cv = (*rv.getArray())[c];
            if (cv.isObject()) {
                ClipData cd;
                cd.name = cv.getProperty("name", "").toString().toStdString();
                cd.bars = (int)cv.getProperty("bars", 0);
                if (!(cd.bars >= 1 && cd.bars <= SQ_MAX_BARS)) return false;
                juce::MemoryOutputStream raw;
                juce::Base64::convertFromBase64(raw, cv.getProperty("pattern", "").toString());
                cd.bytes.assign((const uint8_t*)raw.getData(),
                                (const uint8_t*)raw.getData() + raw.getDataSize());
                if ((int)cd.bytes.size() != cd.bars * sqBytesPerBar(d.machines[(size_t)c]))
                    return false;
                row.push_back(std::move(cd));
                has.push_back(true);
            } else if (cv.isVoid() || cv.isUndefined()) {
                row.emplace_back();
                has.push_back(false);
            } else {
                return false;
            }
        }
        d.cells.push_back(std::move(row));
        d.hasCell.push_back(std::move(has));
    }

    out = std::move(d);
    return true;
}

} // namespace fable
