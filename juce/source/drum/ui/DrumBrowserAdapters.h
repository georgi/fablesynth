#pragma once
#include "../../ui/PresetBrowser.h"
#include "../../ui/BrowserGlyphs.h"
#include "../../ui/BrowserSupport.h"
#include "../../ui/BrowserBackend.h"
#include "../dsp/DrumPatches.h"

#include <array>

// DR-1 adapters for the shared preset browser — ports of
// src/drum/components/browser/ + src/drum/drumBrowser.ts.
//   KitBrowserAdapter:   rows load factory kits as host programs. A snapshot
//     holds every parameter and the pad names (a kit load keeps the sequence),
//     plus the program index and dirty flag.
//   PadPatchAdapter:     rows load factory pad patches onto the selected pad.
//     A snapshot holds that pad's patch fields, the pad, and the dirty flag.
namespace fui {

class KitBrowserAdapter final : public BrowserAdapter {
public:
    explicit KitBrowserAdapter(std::unique_ptr<BrowserBackend>);

    juce::String title() const override { return "DR-1 kits"; }
    juce::String glyphLegend() const override { return juce::String::fromUTF8("kick \xc2\xb7 snare \xc2\xb7 hat \xc2\xb7 toms"); }
    juce::StringArray detailLegend() const override { return { "KICK", "SNARE", "HAT", "TOMS" }; }
    juce::String favoritesKey() const override { return "fablesynth.drumKitFavorites"; }
    const juce::StringArray& roles() const override { return roles_; }
    const juce::StringArray& characters() const override { return characters_; }
    const std::vector<BrowserItem>& items() const override { return items_; }
    int currentIndex() const override { return backend_->loadedIndex(); }

    void load(int item) override { backend_->load(item); }
    std::unique_ptr<BrowserSnapshot> capture() const override { return backend_->capture(); }
    void restore(const BrowserSnapshot& s) override { backend_->restore(s); }

    std::vector<AuditionEvent> auditionPlan(int item) const override;
    void fire(const AuditionEvent& e) override { if (e.on) backend_->noteOn(e.id, e.velocity); }
    void releaseAll() override {}
    bool auditionBlocked() const override { return backend_->auditionBlocked(); }
    juce::String auditionHint(bool phraseOn) const override;

    void drawItemGlyph(juce::Graphics&, int item, juce::Rectangle<float>, juce::Colour, bool fill) const override;
    void drawLiveGlyph(juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool fill) const override;
    std::vector<std::pair<juce::String, juce::String>> liveReadout() const override;
    std::uint64_t liveRevision() const override;

private:
    template <class Get> static glyph::KitData kitFrom(Get&&);
    glyph::KitData liveKit() const;

    std::unique_ptr<BrowserBackend> backend_;
    std::vector<BrowserItem> items_;
    juce::StringArray roles_, characters_;
    std::vector<glyph::KitData> glyphs_;
};

class PadPatchAdapter final : public BrowserAdapter {
public:
    // The pad patch readout ("which patch is loaded") lives in the selected-pad bar.
    PadPatchAdapter(std::unique_ptr<BrowserBackend>, std::function<int()> getPatch, std::function<void(int)> setPatch);

    juce::String title() const override { return "DR-1 pad patches"; }
    juce::String glyphLegend() const override { return juce::String::fromUTF8("pitch \xc2\xb7 amp \xc2\xb7 filter \xc2\xb7 source"); }
    juce::StringArray detailLegend() const override { return { "PITCH", "AMP", "FILTER", "SOURCE" }; }
    juce::String favoritesKey() const override { return "fablesynth.drumPatchFavorites"; }
    const juce::StringArray& roles() const override { return roles_; }
    const juce::StringArray& characters() const override { return characters_; }
    const std::vector<BrowserItem>& items() const override { return items_; }
    int currentIndex() const override { return getPatch_(); }

    void load(int item) override;
    std::unique_ptr<BrowserSnapshot> capture() const override;
    void restore(const BrowserSnapshot&) override;
    bool canVary() const override { return true; }
    void vary(int item) override;

    std::vector<AuditionEvent> auditionPlan(int item) const override;
    void fire(const AuditionEvent& e) override { if (e.on) backend_->noteOn(e.id, e.velocity); }
    void releaseAll() override {}
    bool auditionBlocked() const override { return backend_->auditionBlocked(); }
    juce::String auditionHint(bool phraseOn) const override;

    void drawItemGlyph(juce::Graphics&, int item, juce::Rectangle<float>, juce::Colour, bool fill) const override;
    void drawLiveGlyph(juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool fill) const override;
    std::vector<std::pair<juce::String, juce::String>> liveReadout() const override;
    std::uint64_t liveRevision() const override;

    static const std::vector<browser::VaryRule>& varyRules();
    void seedRandom(juce::int64 s) { rng_.setSeed(s); }
    // Absolute (pid, value) pairs of patch `item` on `pad`, defaults filled in.
    std::vector<std::pair<int, float>> patchFields(int item, int pad) const;
    // Absolute values of the selected pad's patch fields right now.
    std::vector<std::pair<int, float>> liveFields(int pad) const;

private:
    struct Snap : BrowserSnapshot {
        int pad = 0;
        std::vector<std::pair<int, float>> fields;
    };
    template <class Get> static glyph::PadData padFrom(Get&&);
    glyph::PadData livePad() const;

    std::unique_ptr<BrowserBackend> backend_;
    std::function<int()> getPatch_;
    std::function<void(int)> setPatch_;
    std::vector<BrowserItem> items_;
    juce::StringArray roles_, characters_;
    std::vector<glyph::PadData> glyphs_;
    juce::Random rng_;
};

} // namespace fui
