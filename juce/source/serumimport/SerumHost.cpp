#include "SerumHost.h"

#include <cmath>

namespace serumimport {

// One cycle of MIDI note 36 is exactly one 2048-sample WT-1 frame at this rate.
static const double CAPTURE_SR = 2048.0 * 440.0 * std::pow(2.0, (36 - 69) / 12.0);

juce::File findSerumPlugin() {
    for (auto dir : { juce::File("/Library/Audio/Plug-Ins/VST3"),
                      juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/VST3"),
                      juce::File("C:\\Program Files\\Common Files\\VST3") }) {
        auto f = dir.getChildFile("Serum.vst3");
        if (f.exists()) return f;
    }
    return {};
}

juce::Array<juce::File> findSerumPresetRoots() {
    juce::Array<juce::File> out;
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    for (auto dir : { juce::File("/Library/Audio/Presets/Xfer Records/Serum Presets/Presets"),
                      home.getChildFile("Library/Audio/Presets/Xfer Records/Serum Presets/Presets"),
                      docs.getChildFile("Xfer/Serum Presets/Presets") })
        if (dir.isDirectory()) out.add(dir);
    return out;
}

bool SerumHost::load(const juce::File& vst3, juce::String& error) {
    formats_.addFormat(new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> descs;
    juce::VST3PluginFormat vst3Format;
    vst3Format.findAllTypesForFile(descs, vst3.getFullPathName());
    if (descs.isEmpty()) { error = "Serum.vst3 could not be scanned"; return false; }
    sr_ = CAPTURE_SR;
    plugin_ = formats_.createPluginInstance(*descs[0], sr_, block_, error);
    if (!plugin_) return false;
    plugin_->setPlayConfigDetails(0, 2, sr_, block_);
    plugin_->prepareToPlay(sr_, block_);
    for (auto* p : plugin_->getParameters()) {
        byName_[p->getName(64)] = p;
        initValues_.push_back(p->getValue());
    }
    return true;
}

// The VST3 component state of Serum is the .fxp chunk itself; JUCE wraps it in
// an XML envelope, so swap the IComponent payload.
void SerumHost::setChunk(const juce::MemoryBlock& chunk) {
    juce::MemoryBlock mb;
    plugin_->getStateInformation(mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int)mb.getSize());
    if (!xml) return;
    if (auto* c = xml->getChildByName("IComponent")) {
        c->deleteAllTextElements();
        c->addTextElement(chunk.toBase64Encoding());
    }
    if (auto* e = xml->getChildByName("IEditController")) xml->removeChildElement(e, true);
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary(*xml, out);
    plugin_->setStateInformation(out.getData(), (int)out.getSize());
}

void SerumHost::set(const juce::String& name, float v) {
    if (auto it = byName_.find(name); it != byName_.end()) it->second->setValueNotifyingHost(v);
}
void SerumHost::setInit(const juce::String& name) {
    if (auto it = byName_.find(name); it != byName_.end())
        it->second->setValueNotifyingHost(initValues_[(size_t)it->second->getParameterIndex()]);
}
float SerumHost::get(const juce::String& name) const {
    auto it = byName_.find(name);
    return it != byName_.end() ? it->second->getValue() : 0.0f;
}

void SerumHost::render(int samples, std::vector<float>* outL) {
    juce::AudioBuffer<float> buf(2, block_);
    juce::MidiBuffer midi;
    while (samples > 0) {
        const int n = std::min(samples, block_);
        buf.setSize(2, n, false, false, true);
        buf.clear();
        plugin_->processBlock(buf, midi);
        if (outL) outL->insert(outL->end(), buf.getReadPointer(0), buf.getReadPointer(0) + n);
        samples -= n;
    }
}

void SerumHost::note(int n, bool on) {
    juce::AudioBuffer<float> buf(2, 64);
    juce::MidiBuffer midi;
    midi.addEvent(on ? juce::MidiMessage::noteOn(1, n, (juce::uint8)127) : juce::MidiMessage::noteOff(1, n), 0);
    buf.clear();
    plugin_->processBlock(buf, midi);
}

std::vector<SerumParam> SerumHost::readPreset(const FxpData& fxp) {
    setChunk(fxp.chunk);
    std::vector<SerumParam> out;
    for (auto* p : plugin_->getParameters()) {
        auto text = p->getText(p->getValue(), 64);
        const auto label = p->getLabel();
        if (label.isNotEmpty()) text << " " << label;
        out.push_back({ p->getName(64).toStdString(), p->getValue(), text.trim().toStdString() });
    }
    return out;
}

std::vector<float> SerumHost::captureOsc(const FxpData& fxp, char osc, int frames) {
    setChunk(fxp.chunk);
    const juce::String X = juce::String::charToString(osc), O = osc == 'A' ? "B" : "A";
    if (get("Osc " + X + " On") < 0.5f) return {};
    // Neutral voice around this oscillator; its stored warp stays (baked in).
    set("Osc " + O + " On", 0); set("Osc N On", 0); set("Osc S On", 0); set("Filter On", 0);
    for (auto fx : { "Dist", "Flg", "Phs", "Cho", "Dly", "Comp", "Rev", "EQ", "FX Fil", "Hyp" })
        set(juce::String(fx) + " Enable", 0);
    for (int m = 1; m <= 32; ++m) setInit(m < 10 ? "Mod " + juce::String(m) + " amt" : "Mod" + juce::String(m) + " amt");
    for (auto q : { " Octave", " Semi", " Fine", " CoarsePit", " Vol", " Pan", " Unison" }) setInit(X + q);
    set(X + " RandPhase", 0); set(X + " Phase", 0);
    for (auto q : { "Env1 Atk", "Env1 Hold", "Env1 Dec", "Env1 Sus", "Env1 Rel", "Mast.Tun", "PortTime", "MasterVol",
                    "Pitch Bend", "Mod Wheel", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Amp." })
        setInit(q);
    set(X + " WTPos", 0);
    render(4096);
    note(36, true);         // renders 64 samples with the note-on
    render(2048 * 8 - 64);  // frames then start on whole cycles: phase 0 at frame start
    std::vector<float> out;
    out.reserve((size_t)frames * 2048);
    for (int k = 0; k < frames; ++k) {
        set(X + " WTPos", frames == 1 ? 0.0f : (float)k / (float)(frames - 1));
        render(2048 * 3);   // let Serum's parameter smoothing settle
        render(2048, &out);
    }
    note(36, false);
    render(2048 * 16);
    return out;
}

} // namespace serumimport
