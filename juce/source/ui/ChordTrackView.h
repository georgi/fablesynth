#pragma once
#include <functional>
#include "../dsp/Arp.h"
#include "ArpWidgets.h"
#include "Theme.h"

namespace fui {
// Arp chord track (web ChordTrack.tsx): a progression picker and the track's
// key, octave, length, link and snap above the grid. Drag empty space to draw
// a chord, drag a chord to move it, drag its right edge to resize it. Below
// the grid, the selected chord's root, type and inversion.
class ChordTrackView final : public juce::Component {
public:
    using Settings = fable::ArpSettings;
    std::function<Settings()> settings;
    std::function<void(const Settings&)> commit;
    std::function<int()> bars;
    std::function<void(int)> setBars;     // empty when the length is not editable
    std::function<double()> playhead;     // sixteenths, or -1
    std::function<int()> linkPeers;       // SQ-4: other linked tracks in the scene; -1 hides LINK
    std::function<void()> onBrowse;
    std::function<juce::String(int)> scaleOf;  // scale mode: the scale a chord sets (web scaleOf)

    explicit ChordTrackView(juce::Colour accent) : accent_(accent), picker("chord progression", accent) {
        picker.onStep = [this](int d) { stepPreset(d); };
        picker.onBrowse = [this] { if (onBrowse) onBrowse(); };
        addAndMakeVisible(picker);
        auto combo = [&](juce::ComboBox& c, const char* name) { c.setName(name); c.setTooltip(name); addAndMakeVisible(c); };
        combo(key, "Chord key"); combo(snap, "Chord snap"); combo(type, "Chord type"); combo(root, "Chord root");
        for (int i = 0; i < 12; ++i) { key.addItem(pitch(i), i + 1); root.addItem(pitch(i), i + 1); }
        snap.addItemList({"1/16", "1/8", "1/4", "1/2", "BAR"}, 1); snap.setSelectedId(3, juce::dontSendNotification);
        for (int i = 0; i < fable::kChordQualities; ++i) type.addItem(fable::chordQualities()[(size_t)i].name, i + 1);
        key.onChange = [this] {
            auto a = settings(); const int next = key.getSelectedId() - 1; if (next < 0 || next == a.chordKey) return;
            for (int i = 0; i < a.chordCount; ++i) { auto& c = a.chordList[(size_t)i]; c.root = (uint8_t)((c.root + next - a.chordKey + 12) % 12); }
            a.chordKey = next; commit(a);
        };
        type.onChange = [this] { editSelected([&](fable::ArpChord& c) { c.quality = (uint8_t)std::max(0, type.getSelectedId() - 1); c.inversion = 0; }); };
        root.onChange = [this] { editSelected([&](fable::ArpChord& c) { c.root = (uint8_t)std::max(0, root.getSelectedId() - 1); }); };
        auto button = [&](arp::Button& b, const juce::String& text, const juce::String& name, std::function<void()> f) {
            b.setButtonText(text); b.setName(name); b.setTooltip(name); b.live = accent_; b.onClick = std::move(f); addAndMakeVisible(b);
        };
        button(octDown, juce::String::fromUTF8("−"), "Lower chord octave", [this] { auto a = settings(); if (a.chordOctave > 1) { --a.chordOctave; commit(a); } });
        button(octUp, "+", "Raise chord octave", [this] { auto a = settings(); if (a.chordOctave < 5) { ++a.chordOctave; commit(a); } });
        button(invDown, juce::String::fromUTF8("−"), "Previous inversion", [this] { editSelected([](fable::ArpChord& c) { if (c.inversion) --c.inversion; }); });
        button(invUp, "+", "Next inversion", [this] { editSelected([](fable::ArpChord& c) {
            if (c.inversion + 1 < fable::chordQualities()[c.quality].count) ++c.inversion; }); });
        button(remove, "DELETE", "Delete chord", [this] {
            auto a = settings(); if (sel_ < 0 || sel_ >= a.chordCount) return;
            std::move(a.chordList.begin() + sel_ + 1, a.chordList.begin() + a.chordCount, a.chordList.begin() + sel_);
            --a.chordCount; sel_ = std::max(0, sel_ - 1); commit(a);
        });
        button(link, "LINK TRACKS", "Share this progression with the other linked tracks in this scene", [this] {
            auto a = settings(); a.chordLink = !a.chordLink; commit(a); refresh();
        });
        link.setName("Link chord tracks");
        lengths.setName("Sequence length");
        lengths.setOptions({{1, "1 BAR", "1 bar"}, {2, "2 BARS", "2 bars"}, {3, "3 BARS", "3 bars"}, {4, "4 BARS", "4 bars"}}, accent_);
        lengths.onChange = [this](int n) { if (setBars) setBars(n); };
        addAndMakeVisible(lengths);
    }
    int preferredHeight(bool dense) const { return dense ? 28 + 4 + 36 + 4 + 28 : 28 + 8 + 48 + 8 + 30 + 16; }
    void resized() override {
        dense_ = getHeight() < preferredHeight(false);
        auto area = getLocalBounds();
        auto tools = area.removeFromTop(28);
        // Left to right like the web toolbar: picker, KEY, OCT, length, LINK, SNAP.
        const int fixed = 34 + 62 + 18 + 26 + 52 + 26 + 18 + (lengths.isVisible() ? lengths.preferredWidth() + 18 : 0)
                        + (link.isVisible() ? std::max(118, link.preferredWidth()) + 18 : 0) + 42 + 72;
        picker.setBounds(tools.removeFromLeft(std::clamp(tools.getWidth() - fixed - 18, 200, 340))); tools.removeFromLeft(18);
        keyLabel_ = tools.removeFromLeft(34); key.setBounds(tools.removeFromLeft(62).reduced(0, 2)); tools.removeFromLeft(18);
        octDown.setBounds(tools.removeFromLeft(26).reduced(0, 4)); octLabel_ = tools.removeFromLeft(52); octUp.setBounds(tools.removeFromLeft(26).reduced(0, 4));
        tools.removeFromLeft(18);
        if (lengths.isVisible()) { lengths.setBounds(tools.removeFromLeft(lengths.preferredWidth()).reduced(0, 1)); tools.removeFromLeft(18); }
        if (link.isVisible()) { link.setBounds(tools.removeFromLeft(std::max(118, link.preferredWidth())).reduced(0, 2)); tools.removeFromLeft(18); }
        snapLabel_ = tools.removeFromLeft(42); snap.setBounds(tools.removeFromLeft(72).reduced(0, 2));
        area.removeFromTop(dense_ ? 4 : 8);
        help_ = dense_ ? juce::Rectangle<int>() : area.removeFromBottom(16);
        auto editor = area.removeFromBottom(dense_ ? 28 : 30);
        editor_ = editor;
        area.removeFromBottom(dense_ ? 4 : 8);
        grid_ = area;
        editor = editor.reduced(10, 0);
        selLabel_ = editor.removeFromLeft(58); nameBounds_ = editor.removeFromLeft(78);
        rootLabel_ = editor.removeFromLeft(36); root.setBounds(editor.removeFromLeft(62).reduced(0, 3)); editor.removeFromLeft(16);
        typeLabel_ = editor.removeFromLeft(36); type.setBounds(editor.removeFromLeft(150).reduced(0, 3)); editor.removeFromLeft(16);
        invDown.setBounds(editor.removeFromLeft(26).reduced(0, 5)); invLabel_ = editor.removeFromLeft(50); invUp.setBounds(editor.removeFromLeft(26).reduced(0, 5));
        remove.setBounds(editor.removeFromRight(70).reduced(0, 4));
        voicingBounds_ = editor.reduced(14, 0);
    }
    void paint(juce::Graphics& g) override {
        const auto a = shown(); const int total = cells();
        arp::label(g, "KEY", keyLabel_, arp::c::label);
        arp::label(g, "SNAP", snapLabel_, arp::c::label);
        g.setColour(arp::c::status); g.setFont(arp::font(9)); g.drawText("OCT " + juce::String(a.chordOctave), octLabel_, juce::Justification::centred);
        // Grid: sixteenth hairlines, beat lines, bar numbers.
        g.setColour(arp::c::display); g.fillRoundedRectangle(grid_.toFloat(), 5);
        g.setColour(juce::Colours::white.withAlpha(.024f));
        for (int s = 1; s < total; ++s) g.fillRect(xAt(s) - 1, (float)grid_.getY() + 1, 1.0f, (float)grid_.getHeight() - 2);
        for (int b = 0; b < total / 4; ++b) {
            const float x = xAt(b * 4);
            g.setColour(juce::Colours::white.withAlpha(b % 4 == 0 ? .15f : .063f)); g.fillRect(x, (float)grid_.getY(), 1.0f, (float)grid_.getHeight());
            if (b % 4 == 0) { g.setColour(arp::c::hint); g.setFont(arp::font(8)); g.drawText(juce::String(b / 4 + 1), (int)x + 4, grid_.getY() + 1, 20, 11, juce::Justification::centredLeft); }
        }
        const double pos = playhead ? playhead() : -1;
        for (int i = 0; i < a.chordCount; ++i) {
            const auto& c = a.chordList[(size_t)i]; if (c.start >= total) continue;
            const int len = std::min<int>(c.length, total - c.start);
            auto r = juce::Rectangle<float>(xAt(c.start), grid_.getY() + 13.0f, xAt(c.start + len) - xAt(c.start), grid_.getHeight() - 17.0f).reduced(1, 0);
            const bool sounding = pos >= c.start && pos < c.start + len, selected = !drag_ && i == sel_;
            g.setColour(sounding ? arp::chordHue(c.root, .7f, .5f).withAlpha(.38f) : arp::chordHue(c.root, .6f, .4f).withAlpha(.18f)); g.fillRoundedRectangle(r, 3);
            g.setColour(juce::Colours::white.withAlpha(.13f));
            g.fillRect(r.withLeft(r.getRight() - 7).reduced(0, 1));
            g.setColour(selected ? accent_ : arp::chordHue(c.root, .7f, .62f).withAlpha(.5f)); g.drawRoundedRectangle(r.reduced(.5f), 3, selected ? 1.5f : 1.0f);
            g.setColour(arp::c::value); g.setFont(arp::font(11, true));
            const auto scale = scaleOf ? scaleOf(i) : juce::String();
            if (scale.isEmpty()) g.drawText(chordName(c), r.reduced(8, 0).toNearestInt(), juce::Justification::centredLeft, true);
            else {
                // Name, then its scale in a smaller, dimmer label: stacked when
                // the block is tall enough, else on the same line.
                auto t = r.reduced(8, 0).toNearestInt();
                const bool stack = t.getHeight() >= 28;
                const auto name = stack ? t.removeFromTop(t.getHeight() / 2 + 2) : t.removeFromLeft(juce::jmin(t.getWidth(), 10 + chordName(c).length() * 8));
                g.drawText(chordName(c), name, stack ? juce::Justification::bottomLeft : juce::Justification::centredLeft, true);
                g.setColour(arp::c::label); g.setFont(arp::font(8));
                g.drawText(scale, t, stack ? juce::Justification::topLeft : juce::Justification::centredLeft, true);
            }
        }
        if (pos >= 0) { g.setColour(accent_); g.fillRect(xAt(0) + (float)(pos / total) * grid_.getWidth() - 1, (float)grid_.getY(), 2.0f, (float)grid_.getHeight()); }
        g.setColour(juce::Colours::white.withAlpha(.075f)); g.drawRoundedRectangle(grid_.toFloat().reduced(.5f), 5, 1);
        // Selected-chord editor.
        g.setColour(juce::Colour(0xff0d131d)); g.fillRoundedRectangle(editor_.toFloat(), 5);
        const auto now = settings();
        if (sel_ >= 0 && sel_ < now.chordCount) {
            const auto& c = now.chordList[(size_t)sel_];
            arp::label(g, "SELECTED", selLabel_, arp::c::hint, 8, false);
            g.setColour(arp::chordHue(c.root, .8f, .72f)); g.setFont(arp::font(16, true)); g.drawText(chordName(c), nameBounds_, juce::Justification::centredLeft);
            arp::label(g, "ROOT", rootLabel_, arp::c::label); arp::label(g, "TYPE", typeLabel_, arp::c::label);
            g.setColour(arp::c::status); g.setFont(arp::font(9)); g.drawText("INV " + juce::String(c.inversion), invLabel_, juce::Justification::centred);
            std::array<int, 6> v {}; const int n = fable::chordNotes(c, (now.chordOctave + 1) * 12, v);
            juce::String voicing; for (int i = 0; i < n; ++i) voicing << (i ? juce::String::fromUTF8(" · ") : juce::String()) << noteName(v[(size_t)i]);
            g.setColour(juce::Colour(0xff9eafc5)); g.setFont(arp::font(10)); g.drawText(voicing, voicingBounds_, juce::Justification::centredLeft);
        } else {
            g.setColour(arp::c::label); g.setFont(arp::font(11));
            g.drawText("Drag across the empty track to draw a chord.", editor_.reduced(10, 0), juce::Justification::centredLeft);
        }
        if (!help_.isEmpty())
            arp::label(g, juce::String::fromUTF8("DRAG EMPTY SPACE TO DRAW · DRAG A CHORD TO MOVE · DRAG ITS RIGHT EDGE TO RESIZE · DELETE KEY REMOVES"),
                       help_, arp::c::help, 8, false);
    }
    void refresh() {
        const auto a = settings();
        if (sel_ >= a.chordCount) sel_ = a.chordCount - 1;
        key.setSelectedId(a.chordKey + 1, juce::dontSendNotification);
        octDown.setEnabled(a.chordOctave > 1); octUp.setEnabled(a.chordOctave < 5);
        const bool editableLength = (bool)setBars;
        if (lengths.isVisible() != editableLength) { lengths.setVisible(editableLength); resized(); }
        lengths.setSelectedId(bars(), juce::dontSendNotification);
        const int peers = linkPeers ? linkPeers() : -1;
        if (link.isVisible() != (peers >= 0)) { link.setVisible(peers >= 0); resized(); }
        if (peers >= 0) {
            link.setToggleState(a.chordLink, juce::dontSendNotification);
            link.setButtonText(a.chordLink ? (peers ? juce::String::fromUTF8("LINKED · ") + juce::String(peers + 1) + " TRACKS" : juce::String::fromUTF8("LINKED · ONLY TRACK"))
                                           : peers ? "LINK (" + juce::String(peers) + " LINKED)" : juce::String("LINK TRACKS"));
        }
        const int preset = fable::progressionIndex(a.chordList, a.chordCount, a.chordKey);
        if (preset >= 0) picker.setText(fable::progressions()[(size_t)preset].mood, juce::String::fromUTF8(fable::progressions()[(size_t)preset].name));
        else picker.setText(a.chordCount ? "CUSTOM PROGRESSION" : "START FROM A PROGRESSION", {});
        const bool has = sel_ >= 0 && sel_ < a.chordCount;
        for (auto* c : std::initializer_list<juce::Component*>{&type, &root, &invDown, &invUp, &remove}) c->setVisible(has);
        if (has) {
            const auto& c = a.chordList[(size_t)sel_];
            type.setSelectedId(c.quality + 1, juce::dontSendNotification); root.setSelectedId(c.root + 1, juce::dontSendNotification);
            invDown.setEnabled(c.inversion > 0); invUp.setEnabled(c.inversion + 1 < fable::chordQualities()[c.quality].count);
        }
        repaint();
    }
    void loadPreset(int i) {
        const auto& list = fable::progressions(); if (i < 0 || i >= (int)list.size()) return;
        auto a = settings();
        const int need = fable::progressionChords(list[(size_t)i], a.chordKey, a.chordList, a.chordCount);
        sel_ = 0; commit(a);
        if (setBars && need != bars()) setBars(need);
        refresh();
    }
    int currentPreset() const { const auto a = settings(); return fable::progressionIndex(a.chordList, a.chordCount, a.chordKey); }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!grid_.contains(e.getPosition())) return;
        const auto a = settings(); const int cell = cellAt(e.x);
        for (int i = 0; i < a.chordCount; ++i) {
            const auto& c = a.chordList[(size_t)i]; if (c.start >= cells() || cell < c.start || cell >= c.start + c.length) continue;
            sel_ = i;
            const bool edge = e.x >= xAt(std::min<int>(cells(), c.start + c.length)) - 8;
            drag_ = Drag {edge ? Drag::Resize : Drag::Move, i, cell, c};
            draft_ = shaped(cell); repaint(); return;
        }
        drag_ = Drag {Drag::Draw, -1, cell, {}}; draft_ = shaped(cell); repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (drag_) { draft_ = shaped(cellAt(e.x)); repaint(); } }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!drag_) return;
        auto next = shaped(cellAt(e.x)); drag_.reset(); draft_.reset();
        commit(next); refresh();
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k != juce::KeyPress::deleteKey && k != juce::KeyPress::backspaceKey) return false;
        remove.triggerClick(); return true;
    }
    void mouseMove(const juce::MouseEvent& e) override {
        const auto a = settings(); const int cell = cellAt(e.x);
        auto cursor = grid_.contains(e.getPosition()) ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor;
        for (int i = 0; i < a.chordCount && grid_.contains(e.getPosition()); ++i) {
            const auto& c = a.chordList[(size_t)i];
            if (cell >= c.start && cell < c.start + c.length)
                cursor = e.x >= xAt(std::min<int>(cells(), c.start + c.length)) - 8 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
        }
        setMouseCursor(cursor);
    }
    int selected() const { return sel_; }
    static juce::String chordName(const fable::ArpChord& c) {
        return pitch(c.root) + juce::String::fromUTF8(fable::chordQualities()[c.quality].label) + (c.inversion ? "/" + juce::String(c.inversion) : juce::String());
    }
private:
    struct Drag { enum Mode { Draw, Move, Resize } mode; int idx, from; fable::ArpChord orig; };
    static juce::String pitch(int r) { static const char* n[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"}; return n[r % 12]; }
    static juce::String noteName(int n) { return pitch(n % 12) + juce::String(n / 12 - 1); }
    void stepPreset(int d) {
        const int n = (int)fable::progressions().size(), at = currentPreset();
        loadPreset((std::max(at, d > 0 ? -1 : 0) + d + n) % n);
    }
    int cells() const { return std::clamp(bars ? bars() : 1, 1, 4) * 16; }
    int snapCells() const { static const int s[] = {1, 2, 4, 8, 16}; return s[std::clamp(snap.getSelectedId() - 1, 0, 4)]; }
    float xAt(int cell) const { return grid_.getX() + (float)cell / cells() * grid_.getWidth(); }
    int cellAt(int x) const { return std::clamp((int)std::floor((double)(x - grid_.getX()) / std::max(1, grid_.getWidth()) * cells()), 0, cells() - 1); }
    Settings shown() const { return draft_ ? *draft_ : settings(); }
    Settings shaped(int cell) {
        auto a = settings(); const int total = cells(), sn = snapCells();
        auto down = [sn](int v) { return (int)std::floor((double)v / sn) * sn; };
        const auto& d = *drag_;
        if (d.idx >= 0) { std::move(a.chordList.begin() + d.idx + 1, a.chordList.begin() + a.chordCount, a.chordList.begin() + d.idx); --a.chordCount; }
        fable::ArpChord c;
        if (d.mode == Drag::Draw) {
            const int lo = down(std::min(d.from, cell)), hi = std::min(total, down(std::max(d.from, cell)) + sn);
            const auto now = settings();
            const bool has = sel_ >= 0 && sel_ < now.chordCount;
            c = has ? now.chordList[(size_t)sel_] : fable::ArpChord {0, 0, (uint8_t)now.chordKey, 1, 0};
            c.start = (uint8_t)lo; c.length = (uint8_t)(hi - lo);
        } else if (d.mode == Drag::Resize) {
            c = d.orig; c.length = (uint8_t)(std::max(d.orig.start + 1, std::min(total, down(cell) + sn)) - d.orig.start);
        } else {
            c = d.orig; c.start = (uint8_t)std::clamp(d.orig.start + down(cell - d.from + sn / 2), 0, std::max(0, total - d.orig.length));
        }
        fable::placeChord(a.chordList, a.chordCount, c);
        for (int i = 0; i < a.chordCount; ++i) {
            const auto& o = a.chordList[(size_t)i];
            if (o.start == c.start && o.length == c.length && o.root == c.root && o.quality == c.quality) sel_ = i;
        }
        return a;
    }
    template<class Fn> void editSelected(Fn&& f) {
        auto a = settings(); if (sel_ < 0 || sel_ >= a.chordCount) return;
        f(a.chordList[(size_t)sel_]); commit(a); refresh();
    }
    juce::Colour accent_;
    arp::Picker picker;
    juce::ComboBox key, snap, type, root;
    arp::Button octDown {arp::Button::Kind::Step}, octUp {arp::Button::Kind::Step}, invDown {arp::Button::Kind::Step}, invUp {arp::Button::Kind::Step};
    arp::Button remove {arp::Button::Kind::Ghost}, link {arp::Button::Kind::Ghost};
    arp::Segmented lengths;
    juce::Rectangle<int> grid_, editor_, help_, nameBounds_, voicingBounds_, keyLabel_, snapLabel_, octLabel_, selLabel_, rootLabel_, typeLabel_, invLabel_;
    std::optional<Drag> drag_;
    std::optional<Settings> draft_;
    int sel_ = 0;
    bool dense_ = false;
};
}
