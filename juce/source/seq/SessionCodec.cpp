// SQ-4 session <-> JSON codec (web SessionDoc v:1 schema, src/seq/protocol.ts
// SessionDoc). The JUCE layer owns base64 (juce::Base64) and JSON
// (juce::JSON) — the pure model (SeqModel.h) stays JUCE-free.
#include "SessionCodec.h"
#include "../dsp/Presets.h"
#include "../bass/dsp/BassPatches.h"
#include "../drum/dsp/DrumKits.h"

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
static const char* quantStr(Quant q) {
    return q == Quant::Bar ? "1 BAR" : q == Quant::Quarter ? "1/4" : "OFF";
}
static Quant quantFromStr(const juce::String& s) {
    if (s == "1/4") return Quant::Quarter;
    if (s == "OFF") return Quant::Off;
    return Quant::Bar;
}
static juce::String colorStr(uint32_t argb) {
    return "#" + juce::String::toHexString((int)(argb & 0x00ffffffu)).paddedLeft('0', 6);
}
static uint32_t colorFromStr(const juce::String& s) {
    juce::String hex = s.startsWithChar('#') ? s.substring(1) : s;
    return 0xff000000u | (uint32_t)(hex.getHexValue32() & 0x00ffffff);
}

// Exports are intentionally self-contained.  The UI may retain a compact
// factory-bank reference, but a .json session must still load with the exact
// sound if its factory-bank order ever changes in another build.
template <typename Array, typename Info>
static void writeParams(juce::DynamicObject& target, const Array& values, const Info& info) {
    for (size_t i = 0; i < values.size(); ++i)
        target.setProperty(juce::Identifier(info[i].pid), values[i]);
}

static void writeEmbeddedPatchParams(juce::DynamicObject& target, Machine machine, const PatchRef& patch) {
    if (!patch.factory) {
        // Inline patches may be sparse when imported from an older session;
        // make the next export a full, stand-alone machine state.
        switch (machine) {
            case Machine::DR1: writeParams(target, defaultDrumParams(), drumParamInfo()); break;
            case Machine::BL1: writeParams(target, defaultBassParams(), bassParamInfo()); break;
            case Machine::WT1: writeParams(target, defaultParams(), paramInfo()); break;
        }
        for (const auto& kv : patch.params)
            target.setProperty(juce::Identifier(kv.first), kv.second);
        return;
    }
    const int index = juce::jmax(0, patch.index);
    switch (machine) {
        case Machine::DR1: {
            const auto& bank = factoryKits();
            writeParams(target, applyKit(bank[(size_t)juce::jmin(index, (int)bank.size() - 1)]), drumParamInfo());
            break;
        }
        case Machine::BL1: {
            const auto& bank = bassFactoryPatches();
            writeParams(target, applyBassPatch(bank[(size_t)juce::jmin(index, (int)bank.size() - 1)]), bassParamInfo());
            break;
        }
        case Machine::WT1: {
            const auto& bank = factoryPresets();
            writeParams(target, applyPreset(bank[(size_t)juce::jmin(index, (int)bank.size() - 1)]), paramInfo());
            break;
        }
    }
}

juce::String sessionToJson(const SessionData& s, bool embedFactoryPatches) {
    auto* root = new juce::DynamicObject();
    root->setProperty("v", 1);
    root->setProperty("name", juce::String(s.name));
    root->setProperty("bpm", s.bpm);
    root->setProperty("swing", s.swing);
    root->setProperty("quant", quantStr(s.quant));

    juce::Array<juce::var> tracks;
    for (const auto& t : s.tracks) {
        auto* to = new juce::DynamicObject();
        to->setProperty("machine", machineStr(t.machine));
        to->setProperty("name", juce::String(t.name));
        to->setProperty("color", colorStr(t.color));
        to->setProperty("gain", t.gain);
        auto* patch = new juce::DynamicObject();
        if (t.patch.factory && !embedFactoryPatches) {
            patch->setProperty("kind", "factory");
            patch->setProperty("index", t.patch.index);
        } else {
            patch->setProperty("kind", "inline");
            // Web contract: { kind:"inline", data:{ params:{ name:number,... } } }.
            auto* paramsObj = new juce::DynamicObject();
            writeEmbeddedPatchParams(*paramsObj, t.machine, t.patch);
            auto* dataObj = new juce::DynamicObject();
            dataObj->setProperty("params", juce::var(paramsObj));
            patch->setProperty("data", juce::var(dataObj));
        }
        to->setProperty("patch", juce::var(patch));
        tracks.add(juce::var(to));
    }
    root->setProperty("tracks", tracks);

    juce::Array<juce::var> scenes;
    for (const auto& sc : s.scenes) {
        auto* so = new juce::DynamicObject();
        so->setProperty("name", juce::String(sc.name));
        juce::Array<juce::var> clips;
        for (size_t t = 0; t < sc.clips.size(); ++t) {
            if (t < sc.hasClip.size() && sc.hasClip[t]) {
                const auto& c = sc.clips[t];
                auto* co = new juce::DynamicObject();
                co->setProperty("name", juce::String(c.name));
                co->setProperty("bars", c.bars);
                co->setProperty("pattern",
                    juce::Base64::toBase64(c.bytes.data(), c.bytes.size()));
                clips.add(juce::var(co));
            } else {
                clips.add(juce::var()); // null slot
            }
        }
        so->setProperty("clips", clips);
        if (!sc.pass.empty()) {
            juce::Array<juce::var> pass;
            for (int pt : sc.pass) pass.add(pt);
            so->setProperty("pass", pass);
        }
        scenes.add(juce::var(so));
    }
    root->setProperty("scenes", scenes);

    return juce::JSON::toString(juce::var(root));
}

bool sessionFromJson(const juce::String& json, SessionData& out) {
    juce::var v = juce::JSON::parse(json);
    if (!v.isObject()) return false;
    if ((int)v.getProperty("v", 0) != 1) return false;
    auto* root = v.getDynamicObject();
    if (root == nullptr) return false;

    SessionData s;
    s.name = v.getProperty("name", "").toString().toStdString();
    s.bpm = (double)v.getProperty("bpm", 122.0);
    s.swing = (double)v.getProperty("swing", 0.0);
    s.quant = quantFromStr(v.getProperty("quant", "1 BAR").toString());

    const juce::var& tracks = v.getProperty("tracks", juce::var());
    if (!tracks.isArray()) return false;
    for (const auto& tv : *tracks.getArray()) {
        TrackData td;
        Machine m;
        if (!machineFromStr(tv.getProperty("machine", "").toString(), m)) return false;
        td.machine = m;
        td.name = tv.getProperty("name", "").toString().toStdString();
        td.color = colorFromStr(tv.getProperty("color", "#ffffff").toString());
        td.gain = (float)tv.getProperty("gain", 0.8);
        const juce::var& pv = tv.getProperty("patch", juce::var());
        if (pv.getProperty("kind", "factory").toString() == "inline") {
            td.patch.factory = false;
            if (auto* dataObj = pv.getProperty("data", juce::var()).getDynamicObject()) {
                // Web schema nests the params under data.params; accept that,
                // and fall back to the legacy JUCE-flat form (params written
                // directly into data) so any prior saved state keeps loading.
                juce::DynamicObject* paramsObj =
                    dataObj->getProperty("params").getDynamicObject();
                if (paramsObj == nullptr) paramsObj = dataObj;
                for (const auto& prop : paramsObj->getProperties())
                    td.patch.params[prop.name.toString().toStdString()] = (float)prop.value;
            }
        } else {
            td.patch.factory = true;
            td.patch.index = (int)pv.getProperty("index", 0);
        }
        s.tracks.push_back(std::move(td));
    }

    const juce::var& scenes = v.getProperty("scenes", juce::var());
    if (!scenes.isArray()) return false;
    for (const auto& scv : *scenes.getArray()) {
        SceneData sc;
        sc.name = scv.getProperty("name", "").toString().toStdString();
        const juce::var& clips = scv.getProperty("clips", juce::var());
        if (!clips.isArray()) return false;
        for (const auto& cv : *clips.getArray()) {
            if (cv.isObject()) {
                ClipData cd;
                cd.name = cv.getProperty("name", "").toString().toStdString();
                cd.bars = (int)cv.getProperty("bars", 1);
                juce::MemoryOutputStream raw;
                juce::Base64::convertFromBase64(raw, cv.getProperty("pattern", "").toString());
                cd.bytes.assign((const uint8_t*)raw.getData(),
                                (const uint8_t*)raw.getData() + raw.getDataSize());
                sc.clips.push_back(std::move(cd));
                sc.hasClip.push_back(true);
            } else {
                sc.clips.emplace_back();
                sc.hasClip.push_back(false);
            }
        }
        const juce::var& pass = scv.getProperty("pass", juce::var());
        if (pass.isArray())
            for (const auto& pv2 : *pass.getArray()) sc.pass.push_back((int)pv2);
        s.scenes.push_back(std::move(sc));
    }

    if (!validateSession(s).empty()) return false;
    out = std::move(s);
    return true;
}

} // namespace fable
