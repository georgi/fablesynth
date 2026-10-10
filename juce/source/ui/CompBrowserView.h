#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "../dsp/Arp.h"

namespace fui {
// Comp pattern browser (web CompBrowser): style families, variation filter,
// search and a list with each pattern's voice grid. Clicking or stepping with
// the arrow keys loads a pattern; the arp keeps playing while you browse.
class CompBrowserView final : public juce::Component, private juce::ListBoxModel {
public:
    std::function<fable::ArpSettings()> settings;
    std::function<void(const fable::ArpSettings&)> commit;
    std::function<void()> onClose;

    CompBrowserView() {
        search.setTextToShowWhenEmpty("SEARCH STYLE OR NAME", col::textDim);
        search.setName("Search comp patterns");
        search.onTextChange = [this] { rebuild(); };
        search.onReturnKey = [this] { close(); };
        search.onEscapeKey = [this] { close(); };
        addAndMakeVisible(search);
        random.setButtonText("RANDOM"); done.setButtonText("DONE");
        random.onClick = [this] { if (!shown.empty()) load(shown[(size_t)juce::Random::getSystemRandom().nextInt((int)shown.size())]); };
        done.onClick = [this] { close(); };
        addAndMakeVisible(random); addAndMakeVisible(done);
        for (int v = 0; v <= (int)fable::compVariations().size(); ++v) {
            auto* b = variations.add(new juce::TextButton(v == 0 ? "ALL" : fable::compVariations()[(size_t)v - 1]));
            b->setClickingTogglesState(false);
            b->onClick = [this, v] { variation = v - 1; rebuild(); };
            addAndMakeVisible(b);
        }
        families.setModel(&familyModel); families.setRowHeight(22); addAndMakeVisible(families);
        familyModel.owner = this;
        list.setModel(this); list.setRowHeight(30); list.setName("Comp patterns"); addAndMakeVisible(list);
        families.selectRow(0);
        rebuild();
    }
    ~CompBrowserView() override { list.setModel(nullptr); families.setModel(nullptr); }

    void visibilityChanged() override { if (isVisible()) { rebuild(); search.grabKeyboardFocus(); } }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::returnKey) { close(); return true; }
        if (k == juce::KeyPress::downKey || k == juce::KeyPress::upKey) { step(k == juce::KeyPress::downKey ? 1 : -1); return true; }
        return false;
    }
    void resized() override {
        auto r = getLocalBounds().reduced(14, 12);
        auto head = r.removeFromTop(26);
        title = head.removeFromLeft(230);
        done.setBounds(head.removeFromRight(70).reduced(2, 0)); random.setBounds(head.removeFromRight(80).reduced(2, 0));
        search.setBounds(head.reduced(4, 0));
        r.removeFromTop(8);
        auto chips = r.removeFromTop(22);
        const int w = chips.getWidth() / variations.size();
        for (auto* b : variations) b->setBounds(chips.removeFromLeft(w).reduced(2, 0));
        r.removeFromTop(8);
        foot = r.removeFromBottom(16);
        families.setBounds(r.removeFromLeft(180)); r.removeFromLeft(10);
        list.setBounds(r);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(col::bg.withAlpha(.97f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8);
        g.setColour(col::acA.withAlpha(.3f)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 8, 1);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.setColour(col::acA);
        g.drawText("COMP PATTERNS", title, juce::Justification::centredLeft);
        g.setFont(11); g.setColour(col::textDim);
        g.drawText(juce::String((int)shown.size()) + " / " + juce::String((int)fable::compLibrary().size()), title.withTrimmedLeft(120), juce::Justification::centredLeft);
        g.setFont(9); g.setColour(col::textDim);
        g.drawText("CLICK TO LOAD - UP / DOWN STEPS THROUGH THE LIST - PATTERNS KEEP PLAYING WHILE YOU BROWSE - ENTER / ESC CLOSES", foot, juce::Justification::centredLeft);
    }

private:
    struct FamilyModel final : juce::ListBoxModel {
        CompBrowserView* owner = nullptr;
        int getNumRows() override { return (int)fable::compFamilies().size() + 1; }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
            const juce::String name = row == 0 ? "ALL" : fable::compFamilies()[(size_t)row - 1];
            int count = 0;
            for (const auto& e : fable::compLibrary()) count += row == 0 || name == e.family;
            g.setColour(selected ? col::acA.withAlpha(.12f) : col::panelLo); g.fillRoundedRectangle(1, 1, (float)w - 2, (float)h - 2, 3);
            g.setFont(10); g.setColour(selected ? col::acA : col::textHint);
            g.drawText(name, 8, 0, w - 40, h, juce::Justification::centredLeft);
            g.setColour(col::textDim); g.drawText(juce::String(count), w - 36, 0, 30, h, juce::Justification::centredRight);
        }
        void selectedRowsChanged(int row) override { if (owner) { owner->family = row - 1; owner->rebuild(); } }
    };

    int getNumRows() override { return (int)shown.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override {
        if (row < 0 || row >= (int)shown.size()) return;
        const int index = shown[(size_t)row];
        const auto& e = fable::compLibrary()[(size_t)index];
        const bool current = index == currentIndex();
        g.setColour(current ? col::acA.withAlpha(.1f) : col::panelLo); g.fillRoundedRectangle(1, 1, (float)w - 2, (float)h - 2, 4);
        if (current) { g.setColour(col::acA.withAlpha(.6f)); g.drawRoundedRectangle(1.5f, 1.5f, (float)w - 3, (float)h - 3, 4, 1); }
        const int gridW = 320, familyW = 130, nameW = std::max(150, w - gridW - familyW - 230);
        g.setFont(11); g.setColour(current ? juce::Colours::white : col::text);
        g.drawText(e.groove, 9, 0, nameW, h, juce::Justification::centredLeft);
        if (juce::String(e.variation) != "ORIGINAL") {
            const int nameWidth = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), e.groove);
            g.setFont(9); g.setColour(col::acA);
            g.drawText(e.variation, 9 + nameWidth + 7, 0, std::max(0, nameW - nameWidth - 7), h, juce::Justification::centredLeft);
        }
        g.setFont(9); g.setColour(col::textDim);
        g.drawText(e.family, 9 + nameW + 8, 0, familyW, h, juce::Justification::centredLeft);
        drawGrid(g, e, juce::Rectangle<float>((float)(9 + nameW + familyW + 16), 3, (float)gridW, (float)h - 6));
        static const char* rates[] = {"1/4", "1/8 DOT", "1/8", "1/8 TRI", "1/16 DOT", "1/16", "1/16 TRI", "1/32"};
        static const double rateValues[] = {1, .75, .5, 1.0 / 3, .375, .25, 1.0 / 6, .125};
        static const char* voicings[] = {"CLOSE", "DROP 2", "DROP 2+4", "OPEN"};
        static const char* dirs[] = {"U", "D", "A"};
        juce::String meta;
        for (int i = 0; i < 8; ++i) if (rateValues[i] == e.rate) meta = rates[i];
        meta << " - " << voicings[std::clamp(e.voicing, 0, 3)];
        if (e.strum) meta << " - " << e.strum << "MS " << dirs[std::clamp(e.strumDir, 0, 2)];
        const int metaX = 9 + nameW + familyW + 16 + gridW + 12;
        g.setColour(col::textDim); g.drawText(meta, metaX, 0, std::max(0, w - metaX - 6), h, juce::Justification::centredLeft);
    }
    // 16 steps x 4 voice rows (TOP, MID, LOW, BASS): dots strike, bars hold.
    static void drawGrid(juce::Graphics& g, const fable::CompLibraryEntry& e, juce::Rectangle<float> r) {
        const auto q = fable::compPattern(e.steps);
        static const int bits[] = {fable::kCompTop, fable::kCompMid, fable::kCompLow, fable::kCompBass};
        const float cw = r.getWidth() / 16, rh = r.getHeight() / 4;
        g.setColour(juce::Colours::white.withAlpha(.13f));
        for (int b = 0; b < 4; ++b) g.fillRect(r.getX() + b * 4 * cw, r.getY(), 1.0f, r.getHeight());
        int held = 0;
        for (int i = 0; i < 16; ++i) {
            held = q.ties[(size_t)i] ? held : q.masks[(size_t)i];
            for (int row = 0; row < 4; ++row) {
                if (!(held & bits[row])) continue;
                const float x = r.getX() + i * cw, y = r.getY() + row * rh;
                if (q.ties[(size_t)i]) { g.setColour(col::acA.withAlpha(.35f)); g.fillRect(x - cw * .5f, y + rh * .5f - 1, cw, 2.0f); }
                else { g.setColour(q.accents[(size_t)i] ? col::acB : col::acA.withAlpha(.8f)); g.fillRoundedRectangle(x + cw * .15f, y + rh * .1f, cw * .7f, rh * .8f, 1); }
            }
        }
    }
    void listBoxItemClicked(int row, const juce::MouseEvent&) override { if (row >= 0 && row < (int)shown.size()) load(shown[(size_t)row]); }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { close(); }
    void returnKeyPressed(int) override { close(); }
    void selectedRowsChanged(int row) override { if (!programmatic && row >= 0 && row < (int)shown.size()) load(shown[(size_t)row]); }

    int currentIndex() const {
        if (!settings) return -1;
        const auto a = settings();
        return a.comp ? fable::compLibraryIndex(a) : -1;
    }
    void load(int index) {
        if (!settings || !commit) return;
        auto a = settings(); fable::applyCompEntry(a, fable::compLibrary()[(size_t)index]); commit(a);
        syncSelection(); list.repaint();
    }
    void step(int d) {
        if (shown.empty()) return;
        const auto it = std::find(shown.begin(), shown.end(), currentIndex());
        const int at = it == shown.end() ? (d > 0 ? -1 : 0) : (int)(it - shown.begin());
        load(shown[(size_t)((at + d + (int)shown.size()) % (int)shown.size())]);
    }
    void syncSelection() {
        const auto it = std::find(shown.begin(), shown.end(), currentIndex());
        const juce::ScopedValueSetter<bool> guard(programmatic, true);
        if (it == shown.end()) list.deselectAllRows();
        else { const int row = (int)(it - shown.begin()); list.selectRow(row); list.scrollToEnsureRowIsOnscreen(row); }
    }
    void rebuild() {
        const auto query = search.getText().trim().toUpperCase();
        shown.clear();
        const auto& lib = fable::compLibrary();
        for (int i = 0; i < (int)lib.size(); ++i) {
            const auto& e = lib[(size_t)i];
            if (family >= 0 && juce::String(e.family) != fable::compFamilies()[(size_t)family]) continue;
            if (variation >= 0 && juce::String(e.variation) != fable::compVariations()[(size_t)variation]) continue;
            if (query.isNotEmpty() && !(juce::String(e.name) + " " + e.family).contains(query)) continue;
            shown.push_back(i);
        }
        for (int v = 0; v < variations.size(); ++v) variations[v]->setToggleState(v - 1 == variation, juce::dontSendNotification);
        list.updateContent(); syncSelection(); list.repaint(); repaint();
    }
    void close() { if (onClose) onClose(); }

    juce::TextEditor search;
    juce::TextButton random, done;
    juce::OwnedArray<juce::TextButton> variations;
    FamilyModel familyModel;
    juce::ListBox families, list;
    std::vector<int> shown;
    int family = -1, variation = -1;
    bool programmatic = false;
    juce::Rectangle<int> title, foot;
};
}
