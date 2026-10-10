#pragma once
#include "../../ui/PresetBrowser.h"
#include "../../ui/BrowserGlyphs.h"
#include "../../ui/BrowserSupport.h"
#include "../../ui/BrowserBackend.h"
#include "../dsp/BassPatches.h"

#include <array>
#include <set>

// BL-1 adapter for the shared preset browser — port of
// src/bass/components/BassPatchBrowser.tsx + src/bass/patchBrowser.ts. A patch
// is the whole machine state (params + patterns + chain), so a snapshot holds
// the parameters, the four pitch patterns, the chain length and edit pattern,
// plus the program index and dirty flag.
namespace fui {

class BassBrowserAdapter final : public BrowserAdapter {
public:
    explicit BassBrowserAdapter(std::unique_ptr<BrowserBackend>);

    juce::String title() const override { return "BL-1 patches"; }
    juce::String glyphLegend() const override { return juce::String::fromUTF8("wave \xc2\xb7 filter \xc2\xb7 env \xc2\xb7 lfo"); }
    juce::StringArray detailLegend() const override { return { "WAVE", "FILTER", "ENV", "LFO" }; }
    juce::String favoritesKey() const override { return "fablesynth.bassPatchFavorites"; }
    const juce::StringArray& roles() const override { return roles_; }
    const juce::StringArray& characters() const override { return characters_; }
    const std::vector<BrowserItem>& items() const override { return items_; }
    int currentIndex() const override { return backend_->loadedIndex(); }

    void load(int item) override { backend_->load(item); }
    std::unique_ptr<BrowserSnapshot> capture() const override { return backend_->capture(); }
    void restore(const BrowserSnapshot& s) override { releaseAll(); backend_->restore(s); }
    bool canVary() const override { return true; }
    void vary(int item) override;

    std::vector<AuditionEvent> auditionPlan(int item) const override;
    void fire(const AuditionEvent&) override;
    void releaseAll() override;
    bool auditionBlocked() const override { return backend_->auditionBlocked(); }
    juce::String auditionHint(bool phraseOn) const override;

    void drawItemGlyph(juce::Graphics&, int item, juce::Rectangle<float>, juce::Colour, bool fill) const override;
    void drawLiveGlyph(juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool fill) const override;
    std::vector<std::pair<juce::String, juce::String>> liveReadout() const override;
    std::uint64_t liveRevision() const override;

    static const std::vector<browser::VaryRule>& varyRules();
    void seedRandom(juce::int64 s) { rng_.setSeed(s); }
    std::vector<float> patchValues(int item) const;

private:
    template <class Get> static glyph::WaveData waveFrom(Get&&);
    glyph::WaveData liveWave() const;

    std::unique_ptr<BrowserBackend> backend_;
    std::vector<BrowserItem> items_;
    juce::StringArray roles_, characters_;
    std::vector<fable::BassParamArray> params_;
    std::vector<glyph::WaveData> glyphs_;
    std::set<int> held_;
    juce::Random rng_;
};

} // namespace fui
