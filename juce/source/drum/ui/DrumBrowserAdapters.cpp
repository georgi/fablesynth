#include "DrumBrowserAdapters.h"
#include "../../ui/BrowserSupport.h"
#include "../../ui/Format.h"
#include "../dsp/DrumKits.h"

#include <algorithm>

namespace fui {

using namespace fable;

namespace {
constexpr int kKitVoicePads[3] = { 0, 2, 5 };    // kick, snare, closed hat
constexpr int kKitTomPads[3] = { 8, 9, 10 };

// One-bar kit groove (src/drum/drumBrowser.ts KIT_GROOVE): [pad, step in sixteenths, velocity].
struct Hit { int pad, step; float vel; };
const Hit kKitGroove[] = {
    { 0, 0, 1 }, { 0, 7, 0.8f }, { 0, 10, 0.9f },
    { 2, 4, 1 }, { 2, 12, 1 },
    { 5, 0, 0.85f }, { 5, 2, 0.55f }, { 5, 4, 0.85f }, { 5, 6, 0.55f }, { 5, 8, 0.85f }, { 5, 10, 0.55f }, { 5, 12, 0.85f },
    { 6, 14, 0.7f },
};
// Hits of the selected pad (PAD_HITS): [beat, velocity].
const std::pair<float, float> kPadHits[] = { { 0, 0.9f }, { 1, 0.65f }, { 2, 1 } };

glyph::AmpEnv ampFrom(const std::function<float(int)>& get, int pad) {
    return { get(dpid(pad, DP_AENV_ATT)), get(dpid(pad, DP_AENV_HOLD)), get(dpid(pad, DP_AENV_DEC)), get(dpid(pad, DP_AENV_CURVE)) };
}
} // namespace

// ================= kits =================

template <class Get>
glyph::KitData KitBrowserAdapter::kitFrom(Get&& get) {
    glyph::KitData d;
    const std::function<float(int)> f = get;
    for (int i = 0; i < 3; ++i) {
        d.voices[i] = ampFrom(f, kKitVoicePads[i]);
        d.toms[i] = get(dpid(kKitTomPads[i], DP_OSCA_TUNE));
    }
    return d;
}

KitBrowserAdapter::KitBrowserAdapter(std::unique_ptr<BrowserBackend> backend) : backend_(std::move(backend)) {
    std::vector<juce::String> names;
    for (const auto& kit : factoryKits()) names.emplace_back(kit.name);
    items_ = makeBrowserItems(browserTags::drKits, names);
    roles_ = browser::splitWords(browserTags::drKits.roles);
    characters_ = browser::splitWords(browserTags::drKits.characters);
    for (const auto& kit : factoryKits()) {
        const auto params = applyKit(kit);
        glyphs_.push_back(kitFrom([&](int id) { return params[(size_t)id]; }));
    }
}

glyph::KitData KitBrowserAdapter::liveKit() const {
    return kitFrom([this](int id) { return backend_->value(id); });
}

std::vector<AuditionEvent> KitBrowserAdapter::auditionPlan(int) const {
    const float bpm = backend_->bpm() > 0 ? backend_->bpm() : 120.0f;
    const double step = 60.0 / bpm / 4.0 * 1000.0;
    std::vector<AuditionEvent> out;
    for (const auto& h : kKitGroove) out.push_back({ h.step * step, true, h.pad, h.vel });
    return out;
}

juce::String KitBrowserAdapter::auditionHint(bool phraseOn) const {
    if (backend_->auditionBlocked()) return backend_->blockedHint("kit");
    return phraseOn ? "A one-bar groove plays on select." : "Play the pads to hear kits.";
}

void KitBrowserAdapter::drawItemGlyph(juce::Graphics& g, int item, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    glyph::drawKit(g, r, c, fill, glyphs_[(size_t)item]);
}
void KitBrowserAdapter::drawLiveGlyph(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    glyph::drawKit(g, r, c, fill, liveKit());
}

std::vector<std::pair<juce::String, juce::String>> KitBrowserAdapter::liveReadout() const {
    const auto d = liveKit();
    juce::StringArray toms;
    for (float t : d.toms) toms.add(fmtSigned(t));
    return {
        { "Kick decay", fmtSec(d.voices[0].dec) },
        { "Snare decay", fmtSec(d.voices[1].dec) },
        { "Hat decay", fmtSec(d.voices[2].dec) },
        { "Toms", toms.joinIntoString(" / ") + " st" },
        { "Tempo", juce::String(juce::roundToInt(backend_->bpm())) + " BPM" },
    };
}

std::uint64_t KitBrowserAdapter::liveRevision() const {
    const auto d = liveKit();
    std::uint64_t h = browser::hashFloats({ (float)backend_->loadedIndex(), backend_->bpm(), d.toms[0], d.toms[1], d.toms[2],
                                            (float)backend_->transportRevision() });
    for (const auto& v : d.voices) h = browser::hashFloats({ v.att, v.hold, v.dec, v.curve }, h);
    return h;
}

// ================= pad patches =================

const std::vector<browser::VaryRule>& PadPatchAdapter::varyRules() {
    // Only timbre moves: level, pan, routing, choke, sample choice, and FX stay fixed.
    static const std::vector<browser::VaryRule> rules = {
        { "oscA.fine", 0.04f }, { "oscB.fine", 0.04f },
        { "aenv.dec", 0.3f }, { "aenv.hold", 0.05f },
        { "penv.amt", 0.05f }, { "penv.dec", 0.3f },
        { "flt.cut", 0.5f }, { "flt.res", 0.06f },
        { "noise.color", 0.06f },
    };
    return rules;
}

template <class Get>
glyph::PadData PadPatchAdapter::padFrom(Get&& get) {
    // `get` takes a pad-relative field (DP_*).
    glyph::PadData d;
    d.penvAmt = get(DP_PENV_AMT);
    d.penvDec = get(DP_PENV_DEC);
    d.amp = { get(DP_AENV_ATT), get(DP_AENV_HOLD), get(DP_AENV_DEC), get(DP_AENV_CURVE) };
    d.filterOn = get(DP_FLT_ON) >= 0.5f;
    d.filterType = (int)get(DP_FLT_TYPE);
    d.cutoff = get(DP_FLT_CUT);
    d.res = get(DP_FLT_RES);
    d.mix[0] = get(DP_OSCA_LEVEL); d.mix[1] = get(DP_OSCB_LEVEL); d.mix[2] = get(DP_NOISE_LEVEL); d.mix[3] = get(DP_RING_MIX);
    return d;
}

PadPatchAdapter::PadPatchAdapter(std::unique_ptr<BrowserBackend> backend, std::function<int()> getPatch, std::function<void(int)> setPatch)
    : backend_(std::move(backend)), getPatch_(std::move(getPatch)), setPatch_(std::move(setPatch)) {
    std::vector<juce::String> names;
    for (const auto& patch : factoryPatches()) names.emplace_back(patch.name);
    items_ = makeBrowserItems(browserTags::drPads, names);
    roles_ = browser::splitWords(browserTags::drPads.roles);
    characters_ = browser::splitWords(browserTags::drPads.characters);
    for (int i = 0; i < (int)items_.size(); ++i) {
        const auto fields = patchFields(i, 0);   // pad 0: flat ids equal the field offsets
        std::array<float, DPAD_NFIELDS> v{};
        for (int f = 0; f < DPAD_NFIELDS; ++f) v[(size_t)f] = drumParamInfo()[(size_t)dpid(0, f)].def;
        for (const auto& [id, val] : fields) v[(size_t)id] = val;
        glyphs_.push_back(padFrom([&](int f) { return v[(size_t)f]; }));
    }
    rng_.setSeedRandomly();
}

glyph::PadData PadPatchAdapter::livePad() const {
    const int pad = backend_->selectedPad();
    return padFrom([&](int f) { return backend_->value(dpid(pad, f)); });
}

std::vector<std::pair<int, float>> PadPatchAdapter::patchFields(int item, int pad) const {
    return applyPatchToPad(pad, factoryPatches()[(size_t)item]);
}

std::vector<std::pair<int, float>> PadPatchAdapter::liveFields(int pad) const {
    std::vector<std::pair<int, float>> out;
    for (const auto& [id, v] : applyPatchToPad(pad, factoryPatches().front())) {
        (void)v;
        out.emplace_back(id, backend_->value(id));
    }
    return out;
}

void PadPatchAdapter::load(int item) {
    backend_->loadPadPatch(item);
    setPatch_(item);
}

std::unique_ptr<BrowserSnapshot> PadPatchAdapter::capture() const {
    auto s = std::make_unique<Snap>();
    s->index = getPatch_();
    s->dirty = backend_->dirty();
    s->pad = backend_->selectedPad();
    s->fields = liveFields(s->pad);
    return s;
}

void PadPatchAdapter::restore(const BrowserSnapshot& snap) {
    const auto& s = static_cast<const Snap&>(snap);
    backend_->applyPadFields(s.fields, s.dirty);
    // The patch readout names the selected pad's patch; leave it if the selection moved.
    if (backend_->selectedPad() == s.pad) setPatch_(s.index);
}

void PadPatchAdapter::vary(int item) {
    if (item < 0 || item >= (int)items_.size()) return;
    const int pad = backend_->selectedPad();
    auto fields = patchFields(item, pad);
    auto at = [&](const juce::String& rel) -> float* {
        const int id = drumIdFromString(("pad" + juce::String(pad) + "." + rel).toStdString());
        for (auto& f : fields) if (f.first == id) return &f.second;
        return nullptr;
    };
    auto val = [&](const char* rel) { auto* p = at(rel); return p ? *p : 0.0f; };
    for (const auto& rule : varyRules()) {
        const juce::String f(rule.id);
        // A parameter the sound does not use would add something new, not vary it.
        if (f.startsWith("oscB.") && val("oscB.level") == 0.0f) continue;
        if (f.startsWith("oscA.") && val("oscA.level") == 0.0f) continue;
        if (f == "noise.color" && val("noise.level") == 0.0f) continue;
        if (f.startsWith("flt.") && val("flt.on") == 0.0f) continue;
        if (f == "penv.amt" && val("penv.amt") == 0.0f) continue;
        if (auto* p = at(f)) {
            const int id = drumIdFromString(("pad" + juce::String(pad) + "." + f).toStdString());
            *p = browser::nudgeValue(drumParamInfo()[(size_t)id], *p, rule.amount, rng_.nextFloat() * 2 - 1);
        }
    }
    backend_->applyPadFields(fields, true);
}

std::vector<AuditionEvent> PadPatchAdapter::auditionPlan(int) const {
    const float bpm = backend_->bpm() > 0 ? backend_->bpm() : 120.0f;
    const int pad = backend_->selectedPad();
    std::vector<AuditionEvent> out;
    for (const auto& [beat, vel] : kPadHits) out.push_back({ beat * 60.0 / bpm * 1000.0, true, pad, vel });
    return out;
}

juce::String PadPatchAdapter::auditionHint(bool phraseOn) const {
    if (backend_->auditionBlocked()) return backend_->blockedHint("pad");
    return phraseOn ? "Three hits of the selected pad play on select." : "Play the pad to hear patches.";
}

void PadPatchAdapter::drawItemGlyph(juce::Graphics& g, int item, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    glyph::drawPad(g, r, c, fill, glyphs_[(size_t)item]);
}
void PadPatchAdapter::drawLiveGlyph(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool fill) const {
    glyph::drawPad(g, r, c, fill, livePad());
}

std::vector<std::pair<juce::String, juce::String>> PadPatchAdapter::liveReadout() const {
    const auto d = livePad();
    const auto& types = DRUM_FILTER_TYPES;
    const juce::String filter = d.filterOn
        ? juce::String(types[(size_t)juce::jlimit(0, (int)types.size() - 1, d.filterType)]) + browser::kDot + fmtHz(d.cutoff)
        : juce::String("off");
    return {
        { "Pitch env", d.penvAmt != 0.0f ? (d.penvAmt > 0 ? "+" : "") + juce::String(d.penvAmt, d.penvAmt == std::round(d.penvAmt) ? 0 : 1) + " st" + browser::kDot + fmtSec(d.penvDec) : juce::String("off") },
        { "Amp decay", fmtSec(d.amp.dec) },
        { "Filter", filter },
        { "Osc / sample", fmtPct(d.mix[0]) + " / " + fmtPct(d.mix[1]) },
        { "Noise / ring", fmtPct(d.mix[2]) + " / " + fmtPct(d.mix[3]) },
    };
}

std::uint64_t PadPatchAdapter::liveRevision() const {
    const auto d = livePad();
    return browser::hashFloats({ (float)backend_->selectedPad(), (float)getPatch_(), d.penvAmt, d.penvDec, d.amp.att, d.amp.hold, d.amp.dec, d.amp.curve,
                                 d.filterOn ? 1.0f : 0.0f, (float)d.filterType, d.cutoff, d.res, d.mix[0], d.mix[1], d.mix[2], d.mix[3],
                                 (float)backend_->transportRevision() });
}

} // namespace fui
