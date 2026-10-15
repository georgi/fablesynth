#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BrowserButton.h"
#include "BrowserTags.gen.h"
#include "Theme.h"
#include "VisualTimer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>

// Preset browser drawer — port of src/components/presetBrowser/PresetBrowserShell.tsx.
// One shared component; each machine supplies a BrowserAdapter (items, load,
// snapshot, glyphs, audition). Clicking a row loads the sound at once. KEEP
// keeps what you hear. REVERT and A / B compare against the sound that was
// loaded when the drawer opened or last kept. VARY applies an unsaved variant.
namespace fui {

struct BrowserItem {
    juce::String name, role;
    juce::StringArray tags;
    bool user = false;
    juce::String badge;   // row label in place of the number, e.g. "IMP" for imported presets
};

// What a captured sound must carry. Machines derive their own state from it.
struct BrowserSnapshot {
    virtual ~BrowserSnapshot() = default;
    int index = -1;       // loaded item at capture time (-1: none)
    bool dirty = false;
};

// One audition event, scheduled `ms` after the audition starts. `id` is a note
// (WT-1, BL-1) or a pad (DR-1).
struct AuditionEvent {
    double ms = 0;
    bool on = true;
    int id = 0;
    float velocity = 1.0f;
};

class BrowserAdapter {
public:
    virtual ~BrowserAdapter() = default;

    virtual juce::String title() const = 0;                // list heading
    virtual juce::String glyphLegend() const = 0;          // names the row glyph cells
    virtual juce::StringArray detailLegend() const = 0;    // four labels under the glyph strip
    virtual juce::String favoritesKey() const = 0;
    virtual const juce::StringArray& roles() const = 0;
    virtual const juce::StringArray& characters() const = 0;
    virtual const std::vector<BrowserItem>& items() const = 0;
    virtual int currentIndex() const = 0;                  // item loaded now, -1 for none

    virtual void load(int item) = 0;
    virtual std::unique_ptr<BrowserSnapshot> capture() const = 0;
    virtual void restore(const BrowserSnapshot&) = 0;
    virtual bool canVary() const { return false; }
    virtual void vary(int /*item*/) {}

    // Audition: the browser schedules the plan on the message thread and hands
    // each event to fire(), which must use a lock-free path to the audio thread.
    virtual std::vector<AuditionEvent> auditionPlan(int item) const = 0;
    virtual void fire(const AuditionEvent&) = 0;
    virtual void releaseAll() = 0;                         // end held notes
    virtual bool auditionBlocked() const = 0;              // sequencer or host transport runs
    virtual juce::String auditionHint(bool phraseOn) const = 0;

    // Four-cell fingerprints. drawItemGlyph draws a row; drawLiveGlyph draws the
    // sound loaded now (detail strip).
    virtual void drawItemGlyph(juce::Graphics&, int item, juce::Rectangle<float>, juce::Colour, bool fill) const = 0;
    virtual void drawLiveGlyph(juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool fill) const = 0;
    virtual std::vector<std::pair<juce::String, juce::String>> liveReadout() const = 0;
    // Changes whenever the live glyph or readout would change.
    virtual std::uint64_t liveRevision() const = 0;

    // Optional preset import (WT-1 "IMPORT FROM SERUM..."). An empty label hides it.
    virtual juce::String importLabel() const { return {}; }
    virtual void startImport() {}
    virtual void cancelImport() {}
    virtual bool importRunning() const { return false; }
    virtual juce::String pollImport() { return {}; }           // progress or result; may refresh items()
    virtual std::uint64_t itemsRevision() const { return 0; }  // changes when items() changes
};

// Name -> role and character tags from a generated tag table. Items keep the
// order of `names`; a name missing from the table stays untagged.
std::vector<BrowserItem> makeBrowserItems(const fable::browserTags::Table&, const std::vector<juce::String>& names);
juce::StringArray tagWords(const char* spaceSeparated);

// Favorites persist per machine in the user's settings folder.
struct BrowserFavorites {
    static juce::File& overrideFile();   // tests point this at a temp file
    static std::set<juce::String> load(const juce::String& key);
    static void save(const juce::String& key, const std::set<juce::String>&);
};

class PresetBrowser : public juce::Component, private juce::ListBoxModel, private VisualTimer {
public:
    enum class Mode { Kept, Audition, Variant, B };

    explicit PresetBrowser(BrowserAdapter&);
    ~PresetBrowser() override;

    std::function<void()> onClose;
    std::function<void()> onChanged;   // the loaded sound changed (the header's program name follows)

    // Reset filters, capture the loaded sound as the kept sound, show the drawer.
    void open();
    void close();
    bool isOpen() const { return isVisible(); }

    void resized() override;
    void paint(juce::Graphics&) override;
    // SQ-4 colours the drawer with the focused track's colour.
    void setAccent(juce::Colour);

    // ---- actions (the buttons call these; the tests drive them too) ----
    void selectItem(int item);
    void keep();
    void revert();
    void toggleAB();
    void vary();
    void setPhrase(bool on);
    Mode mode() const { return mode_; }
    int variantCount() const { return variantN_; }
    int visibleCount() const { return (int)visible_.size(); }
    int shownItem() const;
    void setQuery(const juce::String&);
    void toggleRole(const juce::String&);
    void toggleCharacter(const juce::String&);
    void toggleFavoritesOnly();
    void toggleFavorite(int item);
    bool isFavorite(int item) const;
    void stopAudition();
    void flushAudition();   // fire every pending audition event now (tests)
    int pendingAuditionCount() const { return (int)pending_.size(); }

private:
    // ListBoxModel
    int getNumRows() override { return (int)visible_.size(); }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    void timerCallback() override;
    void rebuildVisible();
    void refreshChips();
    void refreshDetail();
    void playAudition(int item);
    void tickAudition();
    juce::Colour accent() const { return accent_.isTransparent() ? accentA() : accent_; }
    juce::Colour accent_ = juce::Colours::transparentBlack;
    const juce::Image& rowGlyph(int item, bool selected, float scale);

    BrowserAdapter& a_;
    juce::TextEditor search_;
    std::vector<std::unique_ptr<BrowserButton>> roleChips_, charChips_;
    BrowserButton favoritesChip_{ "favorites", BrowserButton::Fav };
    BrowserButton importBtn_{ "IMPORT", BrowserButton::Action };
    juce::Rectangle<int> importStatusArea_;
    juce::String importStatus_;
    std::uint64_t itemsRevision_ = 0;
    BrowserButton keepBtn_{ "KEEP", BrowserButton::Primary }, revertBtn_{ "REVERT", BrowserButton::Action },
                  abBtn_{ "A / B", BrowserButton::Action }, varyBtn_{ "VARY", BrowserButton::Vary },
                  phraseBtn_{ juce::String::fromUTF8("\xe2\x99\xaa phrase"), BrowserButton::Chip },
                  closeBtn_{ "CLOSE", BrowserButton::Nav };
    juce::ListBox list_;

    std::set<juce::String> roleFilter_, charFilter_, favorites_;
    juce::String query_;
    bool favoritesOnly_ = false, phraseOn_ = true;
    std::vector<int> visible_;

    std::unique_ptr<BrowserSnapshot> kept_, audition_;
    Mode mode_ = Mode::Kept;
    int variantN_ = 0;
    std::uint64_t lastRevision_ = ~0ull;
    int lastShown_ = -2;

    // Row glyph cache: one image per (item, selected) at the current scale.
    std::vector<juce::Image> glyphCache_[2];
    float glyphScale_ = 0;
    juce::Colour glyphAccent_;
    juce::Rectangle<int> filterArea_, mainArea_, detailArea_, listHeadArea_, countArea_, nameArea_, stripArea_, legendArea_, kvArea_, hintArea_;

    struct Scheduler : juce::Timer {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    } scheduler_;
    std::vector<AuditionEvent> pending_;
    double auditionStartMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};

} // namespace fui
