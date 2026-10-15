#pragma once
// Scale mode for the WT-1 / BL-1 note sequencers — port of
// src/components/ScaleStrip.tsx (+ scaleStrip.css). ScaleStrip is one row:
// SCALE OFF / KEY / CHORDS / FOLLOW; KEY mode adds the KEY (it transposes the
// chord track, like the chord track's KEY) and the scale type (AUTO names the
// scale it resolves to); CHORDS and FOLLOW add the HOME type and a hint.
// ScalePanel stacks the strip and, in CHORDS and FOLLOW, the full chord-track editor whose
// blocks also name each chord's scale. The owner supplies and commits settings.

#include "../dsp/Scales.h"
#include "ChordTrackView.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace fui {

class ScaleStrip : public juce::Component {
public:
    std::function<fable::ArpSettings()> settings;
    std::function<void(const fable::ArpSettings&)> commit;

    static constexpr int kRowH = 26;
    enum Hit { none = -1, off = 0, key = 1, chords = 2, follow = 3 };
    static constexpr int kModes = 4;
    static bool usesChordTrack(int mode) { return mode == 2 || mode == 3; }

    ScaleStrip() {
        auto combo = [&](juce::ComboBox& c, const char* name) { c.setName(name); c.setTooltip(name); addChildComponent(c); };
        combo(key_, "Scale key"); combo(type_, "Scale type");
        for (int i = 0; i < 12; ++i) key_.addItem(pitch(i), i + 1);
        key_.onChange = [this] {
            auto a = settings(); const int next = key_.getSelectedId() - 1;
            if (next < 0 || next == a.chordKey) return;
            for (int i = 0; i < a.chordCount; ++i) { auto& c = a.chordList[(size_t)i]; c.root = (uint8_t)((c.root + next - a.chordKey + 12) % 12); }
            a.chordKey = next; commit(a); refresh();
        };
        type_.onChange = [this] {
            auto a = settings(); const int next = type_.getSelectedId() - 2;
            if (type_.getSelectedId() <= 0 || next == a.scaleType) return;
            a.scaleType = next; commit(a); refresh();
        };
    }

    juce::Rectangle<int> modeBounds(int m) const { return { 52 + m * 62, 2, 62, kRowH - 4 }; }
    Hit hitAt(juce::Point<int> p) const {
        for (int m = 0; m < kModes; ++m) if (modeBounds(m).contains(p)) return (Hit)m;
        return none;
    }
    // store.updateArp: CHORDS and FOLLOW use the chord track, so they also
    // select the chord input. FOLLOW takes the current chords as the ones the
    // notes are written over; clicking it again re-anchors.
    void press(Hit h) {
        if (h == none || !settings) return;
        auto a = settings();
        if (a.scaleMode == (int)h && h != follow) return;
        a.scaleMode = (int)h;
        if (usesChordTrack(h)) { a.chords = true; a.keys = false; }
        if (h == follow) { a.followList = a.chordList; a.followCount = std::clamp(a.chordCount, 0, fable::kMaxArpChords); }
        commit(a); refresh();
    }
    void mouseDown(const juce::MouseEvent& e) override { press(hitAt(e.getPosition())); }

    // Re-reads the settings; repaints only when they changed.
    void refresh() {
        if (!settings) return;
        const auto a = settings();
        juce::uint32 sig = 17;
        auto mix = [&sig](int v) { sig = sig * 31u + (juce::uint32)(v + 2); };
        mix(a.scaleMode); mix(a.scaleType); mix(a.chordKey); mix(a.chordCount);
        for (int i = 0; i < a.chordCount; ++i) {
            const auto& c = a.chordList[(size_t)i]; mix(c.start); mix(c.length); mix(c.root); mix(c.quality);
        }
        mix(a.followCount);
        for (int i = 0; i < a.followCount; ++i) {
            const auto& c = a.followList[(size_t)i]; mix(c.start); mix(c.length); mix(c.root); mix(c.quality);
        }
        if (sig == sig_) return;
        sig_ = sig; a_ = a;
        key_.setVisible(a.scaleMode == 1); type_.setVisible(a.scaleMode != 0);
        key_.setSelectedId(a.chordKey + 1, juce::dontSendNotification);
        type_.clear(juce::dontSendNotification);
        auto autoA = a; autoA.scaleType = -1;
        type_.addItem(juce::String::fromUTF8("AUTO · ") + juce::String::fromUTF8(fable::scaleDefs()[(size_t)fable::resolvedKeyScale(autoA)].name), 1);
        for (int i = 0; i < fable::kScaleCount; ++i) type_.addItem(juce::String::fromUTF8(fable::scaleDefs()[(size_t)i].name), i + 2);
        type_.setSelectedId(a.scaleType + 2, juce::dontSendNotification);
        resized(); repaint();
    }

    void resized() override {
        int x = modeBounds(kModes - 1).getRight() + 18;
        if (a_.scaleMode == 1) { keyLabel_ = { x, 0, 30, kRowH }; key_.setBounds(x + 30, 2, 62, kRowH - 4); x += 30 + 62 + 16; }
        typeLabel_ = { x, 0, 44, kRowH }; type_.setBounds(x + 44, 2, 188, kRowH - 4); x += 44 + 188 + 16;
        tailX_ = x;
    }

    void paint(juce::Graphics& g) override {
        const auto border = juce::Colour(0x4d8b95a8), face = juce::Colour(0xff151a24), legend = juce::Colour(0xffa9b3c6);
        g.setFont(seqFont(9.0f)); g.setColour(legend);
        drawSpaced(g, "SCALE", { 0, 0, 48, kRowH }, 1.2f);
        static const char* labels[] { "OFF", "KEY", "CHORDS", "FOLLOW" };
        for (int m = 0; m < kModes; ++m) {
            const auto r = modeBounds(m); const bool lit = a_.scaleMode == m;
            g.setColour(lit ? juce::Colour(0xff17303a) : face); g.fillRect(r);
            g.setColour(lit ? col::acA.withAlpha(.6f) : border); g.drawRect(r);
            g.setColour(lit ? col::acA : col::text); g.setFont(seqFont(10.0f));
            drawSpaced(g, labels[m], r, .8f, juce::Justification::centred);
        }
        if (a_.scaleMode == 0) return;
        g.setColour(legend); g.setFont(seqFont(9.0f));
        if (a_.scaleMode == 1) drawSpaced(g, "KEY", keyLabel_, 1.2f);
        drawSpaced(g, usesChordTrack(a_.scaleMode) ? "HOME" : "SCALE", typeLabel_, 1.2f);
        const auto tail = juce::Rectangle<int>(tailX_, 0, juce::jmax(0, getWidth() - tailX_), kRowH);
        if (a_.scaleMode == 1) {
            const fable::ScaleSegment k { 0, 16, a_.chordKey, fable::resolvedKeyScale(a_), -1 };
            drawSpaced(g, label(k) + juce::String::fromUTF8(" · NOTES SNAP AT PLAYBACK"), tail, .4f);
        } else if (a_.scaleMode == 3) {
            drawSpaced(g, juce::String::fromUTF8("NOTES KEEP THEIR PLACE AGAINST THE CHORDS · A NEW CHORD MOVES THEM · CLICK FOLLOW TO RE-ANCHOR"), tail, .4f);
        } else {
            drawSpaced(g, juce::String::fromUTF8("EACH CHORD SETS THE SCALE · GAPS USE HOME"), tail, .4f);
        }
    }

    static juce::String pitch(int i) {
        static const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return names[((i % 12) + 12) % 12];
    }
    static juce::String label(const fable::ScaleSegment& s) {
        return pitch(s.root) + " " + juce::String::fromUTF8(fable::scaleDefs()[(size_t)s.scale].name);
    }

private:
    juce::ComboBox key_, type_;
    juce::Rectangle<int> keyLabel_, typeLabel_;
    int tailX_ = 0;
    fable::ArpSettings a_;
    juce::uint32 sig_ = 0;
};

// The strip plus, in CHORDS and FOLLOW, the chord track the scales follow.
class ScalePanel : public juce::Component {
public:
    std::function<fable::ArpSettings()> settings;
    std::function<void(const fable::ArpSettings&)> commit;
    std::function<int()> bars;            // chord-track length in bars
    std::function<void(int)> setBars;     // standalone: sequence length; hosted: chordBars
    std::function<int()> linkPeers;       // SQ-4 scene chord link, -1 hides LINK
    std::function<int()> playhead;        // sequencer sixteenth, or -1
    std::function<void()> onBrowse;       // open the progression browser
    std::function<void()> onHeightChanged;
    ChordTrackView chords;
    ScaleStrip strip;

    explicit ScalePanel(juce::Colour accent) : chords(accent) {
        strip.settings = [this] { return settings(); };
        strip.commit = [this](const fable::ArpSettings& a) { commit(a); refresh(); };
        chords.settings = [this] { return settings(); };
        chords.commit = [this](const fable::ArpSettings& a) { commit(a); strip.refresh(); };
        chords.bars = [this] { return bars ? bars() : 4; };
        chords.setBars = [this](int n) { if (setBars) setBars(n); };
        chords.linkPeers = [this] { return linkPeers ? linkPeers() : -1; };
        chords.playhead = [this] {
            const int p = playhead ? playhead() : -1;
            return p < 0 ? -1.0 : (double)(p % (juce::jlimit(1, 4, bars ? bars() : 4) * 16));
        };
        chords.onBrowse = [this] { if (onBrowse) onBrowse(); };
        // Each chord block names its scale (web ChordTrack scaleOf).
        chords.scaleOf = [this](int i) -> juce::String {
            const auto segs = fable::scaleSegments(settings(), bars ? bars() : 4);
            for (int k = 0; k < segs.count; ++k)
                if (segs.seg[(size_t)k].chord == i) return juce::String::fromUTF8(fable::scaleDefs()[(size_t)segs.seg[(size_t)k].scale].name);
            return {};
        };
        addAndMakeVisible(strip); addChildComponent(chords);
    }
    bool chordsShown() const { return ScaleStrip::usesChordTrack(mode_); }
    int preferredHeight() const { return ScaleStrip::kRowH + (chordsShown() ? 6 + chords.preferredHeight(true) : 0); }
    void resized() override {
        strip.setBounds(0, 0, getWidth(), ScaleStrip::kRowH);
        chords.setBounds(0, ScaleStrip::kRowH + 6, getWidth(), chords.preferredHeight(true));
    }
    // Timer-driven: syncs the strip, the chord track and its playhead.
    void refresh() {
        if (!settings) return;
        strip.refresh();
        const int mode = settings().scaleMode;
        if (mode != mode_) {
            mode_ = mode; chords.setVisible(ScaleStrip::usesChordTrack(mode));
            if (onHeightChanged) onHeightChanged();
        }
        if (!chordsShown()) return;
        const int head = playhead ? playhead() : -1;
        if (head != head_) { head_ = head; chords.repaint(); }
        const auto a = settings();
        juce::uint32 sig = 17;
        auto mix = [&sig](int v) { sig = sig * 31u + (juce::uint32)(v + 2); };
        mix(a.chordKey); mix(a.chordOctave); mix(a.chordCount); mix(a.chordLink ? 1 : 0); mix(a.scaleType); mix(bars ? bars() : 4);
        mix(linkPeers ? linkPeers() : -1);
        for (int i = 0; i < a.chordCount; ++i) {
            const auto& c = a.chordList[(size_t)i]; mix(c.start); mix(c.length); mix(c.root); mix(c.quality); mix(c.inversion);
        }
        if (sig != chordSig_) { chordSig_ = sig; chords.refresh(); }
    }

private:
    int mode_ = -1, head_ = -2;
    juce::uint32 chordSig_ = 0;
};

} // namespace fui
