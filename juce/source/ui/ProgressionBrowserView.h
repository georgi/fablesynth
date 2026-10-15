#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ArpWidgets.h"
#include "../dsp/Arp.h"

namespace fui {
// Chord progression browser (web ProgressionBrowser): mood families, search
// and a list with each progression's chords in the current key. Clicking or
// stepping with the arrow keys loads one; the arp keeps playing meanwhile.
class ProgressionBrowserView final : public juce::Component, private juce::ListBoxModel {
public:
    std::function<int()> chordKey;
    std::function<int()> current;           // library index on the chord track, or -1
    std::function<void(int)> load;
    std::function<void()> onClose;
    // SQ-4 scene browser (web title / setKey): names the scene and changes
    // its key. Empty title = the clip's own chord track.
    std::function<juce::String()> titleText;
    std::function<void(int)> setKey;

    explicit ProgressionBrowserView(juce::Colour accent) : live(accent) {
        search.setTextToShowWhenEmpty("SEARCH MOOD, NUMERALS OR CHORD", col::textDim);
        search.setName("Search chord progressions");
        search.onTextChange = [this] { rebuild(); };
        search.onReturnKey = [this] { close(); };
        search.onEscapeKey = [this] { close(); };
        addAndMakeVisible(search);
        for (auto* b : {&random, &done}) { b->live = live; addAndMakeVisible(b); }
        random.onClick = [this] { if (!shown.empty() && load) { load(shown[(size_t)juce::Random::getSystemRandom().nextInt((int)shown.size())]); refreshList(); } };
        done.nav = true; done.onClick = [this] { close(); };
        for (auto* lb : {&families, &list}) { lb->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack); lb->setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack); }
        search.setColour(juce::TextEditor::backgroundColourId, arp::c::field); search.setColour(juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha(.125f));
        search.setColour(juce::TextEditor::focusedOutlineColourId, live.withAlpha(.6f)); search.setFont(arp::font(11));
        families.setModel(&familyModel); families.setRowHeight(24); addAndMakeVisible(families);
        familyModel.owner = this;
        list.setModel(this); list.setRowHeight(34); list.setName("Chord progressions"); addAndMakeVisible(list);
        families.selectRow(0);
        keyBox.setName("Scene key"); keyBox.setTooltip("Scene key");
        for (int i = 0; i < 12; ++i) keyBox.addItem(pitchName(i), i + 1);
        keyBox.onChange = [this] {
            const int k = keyBox.getSelectedId() - 1;
            if (k >= 0 && k != key() && setKey) { setKey(k); rebuild(); }
        };
        addChildComponent(keyBox);
        rebuild();
    }
    ~ProgressionBrowserView() override { list.setModel(nullptr); families.setModel(nullptr); }

    void visibilityChanged() override {
        if (!isVisible()) return;
        keyBox.setVisible((bool)setKey); keyBox.setSelectedId(key() + 1, juce::dontSendNotification);
        resized(); rebuild(); search.grabKeyboardFocus();
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::returnKey) { close(); return true; }
        if (k == juce::KeyPress::downKey || k == juce::KeyPress::upKey) { step(k == juce::KeyPress::downKey ? 1 : -1); return true; }
        return false;
    }
    void resized() override {
        auto r = getLocalBounds().reduced(16, 14);
        auto head = r.removeFromTop(28);
        done.setBounds(head.removeFromRight(64).reduced(2, 1)); random.setBounds(head.removeFromRight(80).reduced(2, 1));
        // The scene title takes what it needs (plus the count), leaving the KEY and a short search.
        const int need = heading().isNotEmpty() ? arp::labelWidth(titleString(), 11) + 8 + 112 : 300;
        title = head.removeFromLeft(juce::jmax(300, juce::jmin(need, head.getWidth() - (setKey ? 110 : 0) - 70)));
        if (setKey) { head.removeFromLeft(10); keyLabel = head.removeFromLeft(30); keyBox.setBounds(head.removeFromLeft(62).reduced(0, 2)); head.removeFromLeft(8); }
        search.setBounds(head.reduced(4, 0));
        r.removeFromTop(10);
        foot = r.removeFromBottom(16);
        r.removeFromBottom(6);
        families.setBounds(r.removeFromLeft(170)); r.removeFromLeft(10);
        list.setBounds(r);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xf7080c13)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8);
        g.setColour(live.withAlpha(.25f)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 8, 1);
        const auto text = titleString();
        const int titleW = std::min(title.getWidth() - 90, arp::labelWidth(text, 11));
        arp::label(g, text, title.withWidth(titleW), live, 11, true);
        if (setKey) arp::label(g, "KEY", keyLabel, juce::Colour(0xff8394aa), 9, true);
        static const char* keys[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
        g.setColour(juce::Colour(0xff8394aa)); g.setFont(arp::font(11));
        g.drawText(juce::String((int)shown.size()) + " / " + juce::String((int)fable::progressions().size())
                   + juce::String::fromUTF8(" · IN ") + keys[std::clamp(key(), 0, 11)],
                   title.withTrimmedLeft(titleW + 8), juce::Justification::centredLeft);
        arp::label(g, juce::String::fromUTF8(heading().isNotEmpty()
                       ? "CLICK TO LOAD · ↑ ↓ STEP THROUGH THE LIST · LOADING REPLACES THE CHORDS OF EVERY LINKED TRACK · ENTER / ESC CLOSES"
                       : "CLICK TO LOAD · ↑ ↓ STEP THROUGH THE LIST · LOADING REPLACES THE CHORD TRACK · ENTER / ESC CLOSES"),
                   foot, juce::Colour(0xff66778d), 8, false);
    }

private:
    struct FamilyModel final : juce::ListBoxModel {
        ProgressionBrowserView* owner = nullptr;
        int getNumRows() override { return (int)fable::progressionFamilies().size() + 1; }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
            const juce::String name = row == 0 ? "ALL" : fable::progressionFamilies()[(size_t)row - 1].name;
            int count = 0;
            for (const auto& p : fable::progressions()) count += row == 0 || fable::progressionFamily(p) == row - 1;
            auto r = juce::Rectangle<float>(0, 1.5f, (float)w - 2, (float)h - 3);
            g.setColour(selected ? owner->live.withAlpha(.08f) : arp::c::field); g.fillRoundedRectangle(r, 3);
            g.setColour(selected ? owner->live.withAlpha(.45f) : juce::Colours::white.withAlpha(.07f)); g.drawRoundedRectangle(r.reduced(.5f), 3, 1);
            arp::label(g, name, {8, 0, w - 44, h}, selected ? owner->live : juce::Colour(0xff93a3b9), 9, true);
            g.setColour(juce::Colour(0xff5f6f86)); g.setFont(arp::font(9)); g.drawText(juce::String(count), w - 40, 0, 32, h, juce::Justification::centredRight);
        }
        void selectedRowsChanged(int row) override { if (owner) { owner->family = row - 1; owner->rebuild(); } }
    };

    int key() const { return chordKey ? chordKey() : 0; }
    int getNumRows() override { return (int)shown.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override {
        if (row < 0 || row >= (int)shown.size()) return;
        const int index = shown[(size_t)row];
        const auto& p = fable::progressions()[(size_t)index];
        const bool now = current && index == current();
        auto r = juce::Rectangle<float>(0, 1, (float)w - 4, (float)h - 2);
        g.setColour(now ? live.withAlpha(.09f) : arp::c::field); g.fillRoundedRectangle(r, 4);
        if (now) { g.setColour(live.withAlpha(.6f)); g.drawRoundedRectangle(r.reduced(.5f), 4, 1); }
        const int gridW = std::min(320, std::max(160, w - 520)), familyW = 130, metaW = 150;
        const int nameW = std::max(120, w - gridW - familyW - metaW - 60);
        int x = 10;
        g.setColour(now ? juce::Colours::white : juce::Colour(0xffc4d0de)); g.setFont(arp::font(11));
        g.drawText(p.mood, x, 0, nameW, h, juce::Justification::centredLeft, true); x += nameW + 14;
        g.setColour(arp::c::hint); g.setFont(arp::font(9));
        g.drawText(juce::String::fromUTF8(p.name), x, 0, familyW, h, juce::Justification::centredLeft, true); x += familyW + 14;
        std::array<fable::ArpChord, fable::kMaxArpChords> chords {}; int count = 0;
        const int bars = fable::progressionChords(p, key(), chords, count);
        drawTimeline(g, chords, count, juce::Rectangle<float>((float)x, 5, (float)gridW, (float)h - 10)); x += gridW + 14;
        arp::label(g, juce::String(bars) + (bars > 1 ? " BARS" : " BAR") + juce::String::fromUTF8(" · ") + juce::String(count) + " CHORDS",
                   {x, 0, std::max(0, w - x - 6), h}, arp::c::hint, 9, false);
    }
    // Four bars of chord blocks, coloured by root like the chord track.
    static void drawTimeline(juce::Graphics& g, const std::array<fable::ArpChord, fable::kMaxArpChords>& chords, int count, juce::Rectangle<float> r) {
        const float cell = r.getWidth() / 64.0f;
        g.setColour(juce::Colours::white.withAlpha(.13f));
        for (int b = 0; b < 4; ++b) g.fillRect(r.getX() + b * 16 * cell, r.getY(), 1.0f, r.getHeight());
        for (int i = 0; i < count; ++i) {
            const auto& c = chords[(size_t)i];
            auto block = juce::Rectangle<float>(r.getX() + c.start * cell, r.getY(), c.length * cell, r.getHeight()).reduced(1.5f, 0);
            g.setColour(arp::chordHue(c.root, .6f, .4f).withAlpha(.28f)); g.fillRoundedRectangle(block, 2);
            g.setColour(arp::chordHue(c.root, .7f, .62f).withAlpha(.55f)); g.drawRoundedRectangle(block.reduced(.5f), 2, 1);
            g.setColour(arp::c::value); g.setFont(arp::font(9, true));
            g.drawText(chordName(c), block.reduced(4, 0).toNearestInt(), juce::Justification::centredLeft, true);
        }
    }
public:
    static juce::String pitchName(int i) {
        static const char* n[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
        return n[((i % 12) + 12) % 12];
    }
    static juce::String chordName(const fable::ArpChord& c) {
        return pitchName(c.root) + juce::String::fromUTF8(fable::chordQualities()[c.quality].label);
    }
private:
    juce::String heading() const { return titleText ? titleText() : juce::String(); }
    juce::String titleString() const {
        return heading().isNotEmpty() ? juce::String::fromUTF8("SCENE CHORDS · ") + heading() : juce::String("CHORD PROGRESSIONS");
    }
    void listBoxItemClicked(int row, const juce::MouseEvent&) override { if (row >= 0 && row < (int)shown.size() && load) { load(shown[(size_t)row]); refreshList(); } }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { close(); }
    void returnKeyPressed(int) override { close(); }

    void step(int d) {
        if (shown.empty() || !load) return;
        const auto it = std::find(shown.begin(), shown.end(), current ? current() : -1);
        const int at = it == shown.end() ? (d > 0 ? -1 : 0) : (int)(it - shown.begin());
        load(shown[(size_t)((at + d + (int)shown.size()) % (int)shown.size())]); refreshList();
    }
    void refreshList() {
        const auto it = std::find(shown.begin(), shown.end(), current ? current() : -1);
        if (it != shown.end()) list.scrollToEnsureRowIsOnscreen((int)(it - shown.begin()));
        list.repaint(); repaint();
    }
    void rebuild() {
        const auto query = search.getText().trim().toUpperCase();
        shown.clear();
        const auto& lib = fable::progressions();
        for (int i = 0; i < (int)lib.size(); ++i) {
            const auto& p = lib[(size_t)i];
            const int f = fable::progressionFamily(p);
            if (family >= 0 && f != family) continue;
            if (query.isNotEmpty()) {
                std::array<fable::ArpChord, fable::kMaxArpChords> chords {}; int count = 0;
                fable::progressionChords(p, key(), chords, count);
                juce::String hay = juce::String::fromUTF8(p.name) + " " + p.mood + " " + (f >= 0 ? fable::progressionFamilies()[(size_t)f].name : "");
                for (int k = 0; k < count; ++k) hay << " " << chordName(chords[(size_t)k]);
                if (!hay.toUpperCase().contains(query)) continue;
            }
            shown.push_back(i);
        }
        list.updateContent(); refreshList();
        keyBox.setSelectedId(key() + 1, juce::dontSendNotification);
    }
    void close() { if (onClose) onClose(); }

    juce::Colour live;
    juce::TextEditor search;
    arp::Button random {arp::Button::Kind::Ghost, "RANDOM"}, done {arp::Button::Kind::Ghost, "DONE"};
    FamilyModel familyModel;
    juce::ListBox families, list;
    std::vector<int> shown;
    int family = -1;
    juce::ComboBox keyBox;
    juce::Rectangle<int> title, foot, keyLabel;
};
}
