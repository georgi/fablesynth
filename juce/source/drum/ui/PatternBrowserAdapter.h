#pragma once
#include "../../ui/PresetBrowser.h"
#include "DrumUiModel.h"

#include <array>
#include <functional>

// DR-1 pattern browser adapter — port of src/drum/components/browser/PatternBrowser.tsx.
// Roles are song sections (main, intro, fill …) and tags are genre families.
// A row replaces the sequence (steps, chain, lane rhythm) through the model and
// keeps the kit and BPM, so the same adapter serves the standalone DR-1 and a
// DR-1 hosted in SQ-4, where the load rewrites the focused scene's clip. A
// snapshot holds the whole sequence and the edit bar for KEEP / REVERT / A-B.
// While the sequencer (or the hosting SQ-4) plays, the new pattern plays in
// place; when it is stopped, selecting a row plays the first bar on the kit.
namespace fui {

class PatternBrowserAdapter final : public BrowserAdapter {
public:
    struct Hooks {
        std::function<float()> bpm;           // audition tempo
        std::function<bool()> hostPlaying;    // standalone: the plugin host's transport runs
        std::function<void()> loaded;         // after a load or restore changed the sequence
    };
    PatternBrowserAdapter(DrumUiModel&, Hooks);

    juce::String title() const override { return "DR-1 patterns"; }
    juce::String glyphLegend() const override;
    juce::StringArray detailLegend() const override;
    juce::String favoritesKey() const override { return "fablesynth.drumPatternFavorites"; }
    const juce::StringArray& roles() const override { return roles_; }
    const juce::StringArray& characters() const override { return characters_; }
    const std::vector<BrowserItem>& items() const override { return items_; }
    int currentIndex() const override { return loaded_; }

    void load(int item) override;
    std::unique_ptr<BrowserSnapshot> capture() const override;
    void restore(const BrowserSnapshot&) override;

    std::vector<AuditionEvent> auditionPlan(int item) const override;
    void fire(const AuditionEvent& e) override { if (e.on) model_.triggerPad(e.id, e.velocity); }
    void releaseAll() override {}
    bool auditionBlocked() const override;
    juce::String auditionHint(bool phraseOn) const override;

    void drawItemGlyph(juce::Graphics&, int item, juce::Rectangle<float>, juce::Colour, bool fill) const override;
    void drawLiveGlyph(juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool fill) const override;
    std::vector<std::pair<juce::String, juce::String>> liveReadout() const override;
    std::uint64_t liveRevision() const override;

    // The steps of `item` differ from the live sequence (an edit since the load).
    bool liveDiffersFrom(int item) const;

private:
    struct Snap : BrowserSnapshot {
        DrumSequence sequence;
        int editPattern = 0;
    };
    // Glyph rows: kick/sub, backbeat (snare, clap, rim), hats and ride, the rest.
    using Rows = std::vector<std::array<uint8_t, 4 * 16>>;   // one entry per bar
    static Rows rowsOf(const std::vector<uint8_t>& steps, int bars);
    static void drawRows(juce::Graphics&, const Rows&, juce::Rectangle<float>, juce::Colour, bool fill);

    DrumUiModel& model_;
    Hooks hooks_;
    std::vector<BrowserItem> items_;
    juce::StringArray roles_, characters_;
    std::vector<Rows> glyphs_;
    int loaded_ = -1;
};

} // namespace fui
