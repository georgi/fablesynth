#include "BassBrowserAdapter.h"
#include "../../ui/BrowserSupport.h"
#include "../../ui/Format.h"

namespace fui {

using namespace fable;

namespace {
// BASS_PHRASE (src/bass/patchBrowser.ts): [semitones from A1, start beat, length in beats, accent].
// A note that starts while the last one still sounds slides into place (legato).
struct PhraseNote { int semi; float start, len; bool accent; };
constexpr int kPhraseSemi = -3;   // A1; the store's semitone 0 is C2
const PhraseNote kPhrase[] = {
    { 0, 0, 0.5f, true }, { 0, 0.75f, 0.25f, false }, { 12, 1.5f, 0.25f, false }, { 3, 2, 0.6f, false },
    { 5, 2.5f, 0.5f, false }, { 7, 3, 0.4f, true }, { 0, 3.5f, 0.5f, false },
};
} // namespace

const std::vector<browser::VaryRule>& BassBrowserAdapter::varyRules() {
    // Pitch, levels, and FX stay fixed so a variant keeps the patch's register and balance.
    static const std::vector<browser::VaryRule> rules = {
        { "osc.pos", 0.15f }, { "osc.detune", 0.08f },
        { "flt.cut", 0.6f }, { "flt.res", 0.08f }, { "flt.env", 0.1f },
        { "fenv.dec", 0.4f }, { "aenv.dec", 0.4f }, { "aenv.sus", 0.08f }, { "aenv.rel", 0.4f },
        { "lfo.depth", 0.05f }, { "acc.amt", 0.1f },
    };
    return rules;
}

template <class Get>
glyph::WaveData BassBrowserAdapter::waveFrom(Get&& get) {
    glyph::WaveData d;
    d.table = (int)get(BL_OSC_TABLE);
    d.pos = get(BL_OSC_POS);
    d.filterType = (int)get(BL_FLT_TYPE);   // BL-1 shares WT-1's filter type indices 0..4
    d.cutoff = get(BL_FLT_CUT);
    d.res = get(BL_FLT_RES);
    d.env[0] = get(BL_AENV_ATT); d.env[1] = get(BL_AENV_DEC); d.env[2] = get(BL_AENV_SUS); d.env[3] = get(BL_AENV_REL);
    d.lfoDepth = std::min(1.0f, get(BL_LFO_DEPTH));
    return d;
}

BassBrowserAdapter::BassBrowserAdapter(std::unique_ptr<BrowserBackend> backend) : backend_(std::move(backend)) {
    std::vector<juce::String> names;
    for (const auto& patch : bassFactoryPatches()) names.emplace_back(patch.name);
    items_ = makeBrowserItems(browserTags::bl1, names);
    roles_ = browser::splitWords(browserTags::bl1.roles);
    characters_ = browser::splitWords(browserTags::bl1.characters);
    for (const auto& patch : bassFactoryPatches()) {
        params_.push_back(applyBassPatch(patch));
        glyphs_.push_back(waveFrom([&](int id) { return params_.back()[(size_t)id]; }));
    }
    rng_.setSeedRandomly();
}

glyph::WaveData BassBrowserAdapter::liveWave() const {
    return waveFrom([this](int id) { return backend_->value(id); });
}

std::vector<float> BassBrowserAdapter::patchValues(int item) const {
    return std::vector<float>(params_[(size_t)item].begin(), params_[(size_t)item].end());
}

void BassBrowserAdapter::vary(int item) {
    if (item < 0 || item >= (int)params_.size()) return;
    // A variant replaces the sound only; the patterns and chain stay as they are.
    auto values = patchValues(item);
    for (const auto& rule : varyRules()) {
        const int id = bassIdFromString(rule.id);
        if (id >= 0) values[(size_t)id] = browser::nudgeValue(bassParamInfo()[(size_t)id], values[(size_t)id], rule.amount, rng_.nextFloat() * 2 - 1);
    }
    backend_->applyVariant(item, values);
}

std::vector<AuditionEvent> BassBrowserAdapter::auditionPlan(int) const {
    const float bpm = backend_->bpm() > 0 ? backend_->bpm() : 120.0f;
    const double beat = 60.0 / bpm * 1000.0;
    std::vector<AuditionEvent> out;
    for (const auto& n : kPhrase) {
        const int semi = kPhraseSemi + n.semi;
        out.push_back({ n.start * beat, true, semi, n.accent ? 1.0f : 0.72f });
        out.push_back({ (n.start + n.len) * beat, false, semi, 0.0f });
    }
    return out;
}

void BassBrowserAdapter::fire(const AuditionEvent& e) {
    if (e.on) { held_.insert(e.id); backend_->noteOn(e.id, e.velocity); }
    else { held_.erase(e.id); backend_->noteOff(e.id); }
}

void BassBrowserAdapter::releaseAll() {
    for (int n : held_) backend_->noteOff(n);
    held_.clear();
}

juce::String BassBrowserAdapter::auditionHint(bool phraseOn) const {
    if (backend_->auditionBlocked()) return backend_->blockedHint("patch");
    return phraseOn ? "A short bass line plays on select." : "Play the keys to hear patches.";
}

void BassBrowserAdapter::drawItemGlyph(juce::Graphics& g, int item, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    const auto& d = glyphs_[(size_t)item];
    glyph::drawWave(g, r, c, fill, d, backend_->table(d.table));
}

void BassBrowserAdapter::drawLiveGlyph(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    const auto d = liveWave();
    glyph::drawWave(g, r, c, fill, d, backend_->table(d.table));
}

std::vector<std::pair<juce::String, juce::String>> BassBrowserAdapter::liveReadout() const {
    const auto d = liveWave();
    const auto& tables = TABLE_NAMES;
    const juce::String wave = d.table >= 0 && d.table < (int)tables.size() ? juce::String(tables[(size_t)d.table]) : juce::String("USER");
    const auto& types = BASS_FILTER_TYPES;
    const juce::String type = d.filterType >= 0 && d.filterType < (int)types.size() ? juce::String(types[(size_t)d.filterType]) : juce::String();
    return {
        { "Wave", wave + browser::kDot + fmtPct(d.pos) },
        { "Filter", type + browser::kDot + fmtHz(d.cutoff) },
        { "Reso", fmtPct(d.res) },
        { "A / D / R", fmtSec(d.env[0]) + " / " + fmtSec(d.env[1]) + " / " + fmtSec(d.env[3]) },
        { "Sustain", fmtPct(d.env[2]) },
        { "LFO depth", d.lfoDepth > 0 ? fmtPct(d.lfoDepth) : juce::String("off") },
    };
}

std::uint64_t BassBrowserAdapter::liveRevision() const {
    const auto d = liveWave();
    return browser::hashFloats({ (float)d.table, d.pos, (float)d.filterType, d.cutoff, d.res, d.env[0], d.env[1], d.env[2], d.env[3],
                                 d.lfoDepth, (float)backend_->loadedIndex(),
                                 (float)backend_->transportRevision() });
}

} // namespace fui
