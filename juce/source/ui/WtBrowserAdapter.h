#pragma once
#include "PresetBrowser.h"
#include "BrowserGlyphs.h"
#include "BrowserSupport.h"
#include "BrowserBackend.h"

#include <set>

// WT-1 adapter for the shared preset browser — port of
// src/components/presetBrowser/PresetBrowser.tsx + src/presetBrowser.ts.
// Rows load factory presets as host programs. A snapshot holds every parameter
// plus the program index and dirty flag (a WT-1 preset is parameters only).
namespace fui {

class WtBrowserAdapter final : public BrowserAdapter {
public:
    explicit WtBrowserAdapter(std::unique_ptr<BrowserBackend>);
    ~WtBrowserAdapter() override;

    juce::String title() const override { return "WT-1 presets"; }
    juce::String glyphLegend() const override { return juce::String::fromUTF8("wave \xc2\xb7 filter \xc2\xb7 env \xc2\xb7 lfo"); }
    juce::StringArray detailLegend() const override { return { "WAVE", "FILTER", "ENV", "LFO" }; }
    juce::String favoritesKey() const override { return "fablesynth.presetFavorites"; }
    const juce::StringArray& roles() const override { return roles_; }
    const juce::StringArray& characters() const override { return characters_; }
    const std::vector<BrowserItem>& items() const override { return items_; }
    int currentIndex() const override;

    void load(int item) override;
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

    // Imported presets: listed after the factory items when the backend can
    // load them; FableSerumImport converts the user's own Serum presets.
    juce::String importLabel() const override;
    void startImport() override;
    void cancelImport() override;
    bool importRunning() const override;
    juce::String pollImport() override;
    std::uint64_t itemsRevision() const override { return itemsRevision_; }
    void reloadImported();
    int factoryCount() const { return factoryCount_; }

    // Tests: the exact parameters VARY may move, and a fixed random stream.
    static const std::vector<browser::VaryRule>& varyRules();
    void seedRandom(juce::int64 s) { rng_.setSeed(s); }
    std::vector<float> presetValues(int item) const;

private:
    template <class Get> static glyph::WaveData waveFrom(Get&&);
    glyph::WaveData liveWave() const;

    std::unique_ptr<BrowserBackend> backend_;
    std::vector<BrowserItem> items_;
    juce::StringArray roles_, characters_;
    std::vector<fable::ParamArray> params_;
    std::vector<glyph::WaveData> glyphs_;
    std::set<int> held_;
    juce::Random rng_;

    class ImportRun;
    int factoryCount_ = 0;
    std::vector<serumimport::ImportedEntry> imported_;
    std::uint64_t itemsRevision_ = 0;
    std::unique_ptr<ImportRun> run_;
    juce::String status_;
};

} // namespace fui
