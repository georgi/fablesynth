#pragma once
#include "VisualTimer.h"
#include "DeviceUiModel.h"
#include "Theme.h"
#include "Controls.h"
#include "ArpWidgets.h"
#include "ChordTrackView.h"
#include "CompBrowserView.h"
#include "ProgressionBrowserView.h"

namespace fui {
// Shared WT/BL surface (web ArpPanel.tsx): a top bar, then three numbered
// cards read top to bottom: 1 NOTES, 2 PATTERN, 3 FEEL. All writes go through
// the UI model, so hosted edits belong to the selected clip and never
// overwrite its sequencer bytes.
class ArpPanel final : public juce::Component, private fui::VisualTimer {
public:
    template<class Model>
    ArpPanel(Model& m, bool bass) : model(m), bass_(bass), octave_(bass ? 2 : 3),
        live_(bass ? juce::Colour(0xff4dff9e) : col::acA),
        playing([&m] { return m.sequencerPlaying(); }), current([&m] { return m.currentStep(); }),
        play([&m](bool b) { m.setSequencerPlaying(b); }),
        chainBars([&m] { return std::clamp((int)m.chain().size(), 1, 4); }),
        chordTrack(bass ? juce::Colour(0xff4dff9e) : col::acA), progressions(bass ? juce::Colour(0xff4dff9e) : col::acA),
        compPicker("comp pattern", bass ? juce::Colour(0xff4dff9e) : col::acA),
        gate("GATE", live_, 5, 95, 65), swing("SWING", live_, 0, 100, 0), strum("STRUM", live_, 0, fable::kStrumMaxMs, 0) {
        const bool hosted = m.capabilities().hosted;
        // Chord track.
        chordTrack.settings = [this] { return model.arpSettings(); };
        chordTrack.commit = [this](const fable::ArpSettings& a) { model.setArpSettings(a); refresh(); };
        chordTrack.bars = [this] { return bars(); };
        if (hosted) chordTrack.setBars = [this](int n) { change([n](auto& a) { a.chordBars = std::clamp(n, 1, 4); }); };
        else chordTrack.setBars = [&m](int n) { std::vector<int> c((size_t)std::clamp(n, 1, 4)); for (int i = 0; i < (int)c.size(); ++i) c[(size_t)i] = i; m.setChain(std::move(c)); };
        chordTrack.linkPeers = [this] { return model.arpChordLinkPeers(); };
        chordTrack.playhead = [this] { return running() && current() >= 0 ? fable::arpStepPos(model.arpSettings().rate, bars(), current()) : -1.0; };
        chordTrack.onBrowse = [this] { progressions.setVisible(true); progressions.toFront(true); };
        addChildComponent(chordTrack);
        progressions.chordKey = [this] { return model.arpSettings().chordKey; };
        progressions.current = [this] { return chordTrack.currentPreset(); };
        progressions.load = [this](int i) { chordTrack.loadPreset(i); };
        progressions.onClose = [this] { progressions.setVisible(false); };
        addChildComponent(progressions);
        // Top bar.
        transport.setName("Launch arp clip"); transport.onClick = [this] { play(!playing() && !model.arpQueued()); };
        addChildComponent(transport);
        tempo.setName("Tempo BPM"); tempo.setTooltip("Arpeggiator tempo"); tempo.setEditable(true);
        tempo.setJustificationType(juce::Justification::centredLeft); tempo.setFont(arp::font(12));
        tempo.setColour(juce::Label::backgroundColourId, juce::Colour(0xff090d14)); tempo.setColour(juce::Label::textColourId, arp::c::value);
        tempo.setColour(juce::Label::outlineColourId, juce::Colours::white.withAlpha(.125f));
        tempo.onTextChange = [this] { setParameter("seq.bpm", std::clamp(tempo.getText().getDoubleValue() > 0 ? tempo.getText().getDoubleValue() : 120.0, 60.0, 200.0)); };
        if (!hosted) addAndMakeVisible(tempo);
        // 1 NOTES
        input.setName("Arpeggiator input");
        std::vector<arp::Segmented::Option> inputs {{1, "NOTE LIST", "Pick a fixed set of notes"}};
        if (!hosted) inputs.push_back({2, "PLAYED KEYS", "Use the keys you hold on MIDI or the keyboard"});
        inputs.push_back({3, "CHORD TRACK", "Draw a chord progression"});
        input.setOptions(inputs, live_);
        input.onChange = [this](int id) { change([&](auto& a) { a.keys = id == 2; a.chords = id == 3; }); };
        addAndMakeVisible(input);
        auto button = [&](arp::Button& b, const char* text, std::function<void()> f) {
            b.setButtonText(text); b.setName(text); b.live = live_; b.onClick = std::move(f); addChildComponent(b);
        };
        button(editNotes, "EDIT NOTES", [this] { editing_ = !editing_; refresh(); resized(); });
        button(latch, "LATCH", [this] { change([](auto& a) { a.latch = !a.latch; }); });
        button(store, "KEEP AS NOTE LIST", [this] { auto liveArp = model.arpLiveSettings(); change([&](auto& a) { a.notes = liveArp.notes; a.count = liveArp.count; a.keys = false; }); });
        button(clear, "CLEAR", [this] { if (model.arpSettings().keys) model.clearArpKeys(); else change([](auto& a) { a.count = 0; }); });
        button(clearList, "CLEAR", [this] { change([](auto& a) { a.count = 0; }); });
        button(seed, "NEW SEED", [this] { change([](auto& a) { ++a.seed; }); });
        for (auto* b : {&lower, &higher}) { b->live = live_; addChildComponent(b); }
        lower.setButtonText(juce::String::fromUTF8("−")); higher.setButtonText("+");
        lower.setName("Lower keyboard octave"); higher.setName("Raise keyboard octave");
        lower.onClick = [this] { octave_ = std::max(1, octave_ - 1); refresh(); };
        higher.onClick = [this] { octave_ = std::min(6, octave_ + 1); refresh(); };
        for (int n = 0; n < 12; ++n) {
            heldKeys[n] = -1;
            auto& k = keys[(size_t)n]; k.live = live_; k.sharp = n == 1 || n == 3 || n == 6 || n == 8 || n == 10; k.textSize = 10;
            addChildComponent(k);
            k.onClick = [this, n] {
                if (model.arpSettings().keys) return;
                change([&](auto& a) {
                    const int note = (octave_ + 1) * 12 + n;
                    auto it = std::find(a.notes.begin(), a.notes.begin() + a.count, note);
                    if (it != a.notes.begin() + a.count) { std::move(it + 1, a.notes.begin() + a.count, it); --a.count; }
                    else if (a.count < 128) a.notes[a.count++] = note;
                });
            };
            k.onStateChange = [this, n] {
                auto& key = keys[(size_t)n];
                if (key.isDown() && heldKeys[n] < 0 && model.arpSettings().keys) {
                    heldKeys[n] = (octave_ + 1) * 12 + n; model.arpKeyInput(heldKeys[n], true);
                } else if (!key.isDown() && heldKeys[n] >= 0) {
                    model.arpKeyInput(heldKeys[n], false); heldKeys[n] = -1;
                }
            };
        }
        // 2 PATTERN
        auto combo = [&](juce::ComboBox& c, const juce::String& name, const juce::StringArray& items) {
            c.setName(name); c.setTooltip(name); c.addItemList(items, 1); addAndMakeVisible(c);
        };
        combo(order, "Arpeggiator order", {juce::String::fromUTF8("↑ UP"), juce::String::fromUTF8("↓ DOWN"), juce::String::fromUTF8("↕ UP / DOWN"), "AS PLAYED", "RANDOM"});
        combo(octaves, "Arpeggiator octaves", {"1 OCTAVE", "2 OCTAVES", "3 OCTAVES"});
        order.onChange = [this] { change([&](auto& a) { a.order = order.getSelectedId() - 1; }); };
        octaves.onChange = [this] { change([&](auto& a) { a.octaves = octaves.getSelectedId(); }); };
        compPicker.onStep = [this](int d) { stepPattern(d); };
        compPicker.onBrowse = [this] { browser.setVisible(true); browser.toFront(true); };
        addChildComponent(compPicker);
        browser.settings = [this] { return model.arpSettings(); };
        browser.commit = [this](const fable::ArpSettings& a) { model.setArpSettings(a); refresh(); };
        browser.onClose = [this] { browser.setVisible(false); };
        addChildComponent(browser);
        addAndMakeVisible(phrase); addAndMakeVisible(lanes);
        // 3 FEEL
        combo(rate, "Arpeggiator rate", {"1/4", "1/8 DOTTED", "1/8", "1/8 TRIPLET", "1/16 DOTTED", "1/16", "1/16 TRIPLET", "1/32"});
        rate.onChange = [this] { change([&](auto& a) { a.rate = rates[(size_t)std::max(0, rate.getSelectedId() - 1)]; }); };
        gate.format = [](double v) { return juce::String((int)v) + "%"; };
        gate.onChange = [this](double v) { change([&](auto& a) { a.gate = v / 100; }); };
        swing.setTitle(hosted ? "GLOBAL SWING" : "SWING");
        swing.format = [](double v) { return juce::String((int)v) + "%"; };
        swing.onChange = [this](double v) {
            if (model.capabilities().hosted) model.setArpSwing(v / 100);
            else setParameter(bass_ ? "master.swing" : "seq.swing", v / 100);
        };
        strum.format = [](double v) { return v < .5 ? juce::String("OFF") : juce::String((int)v) + " MS"; };
        strum.onChange = [this](double v) { change([&](auto& a) { a.strum = (int)v; }); };
        addAndMakeVisible(gate); addAndMakeVisible(swing); addChildComponent(strum);
        stroke.setName("Strum direction");
        stroke.setOptions({{1, juce::String::fromUTF8("↑"), "Strum low to high"}, {2, juce::String::fromUTF8("↓"), "Strum high to low"}, {3, juce::String::fromUTF8("↕"), "Alternate like a guitarist"}}, live_);
        stroke.onChange = [this](int id) { change([&](auto& a) { a.strumDir = id - 1; }); };
        voicing.setName("Comp voicing");
        voicing.setOptions({{1, "CLOSE", "Chord notes packed together"}, {2, "DROP 2", "Inner notes dropped an octave"},
                            {3, "DROP 2+4", "Inner notes dropped an octave"}, {4, "OPEN", "Every other note raised an octave"}}, live_);
        voicing.onChange = [this](int id) { change([&](auto& a) { a.voicing = id - 1; }); };
        addChildComponent(stroke); addChildComponent(voicing);
        refresh(); startVisualTimer(*this, fui::kAnimationHz);
    }
    ~ArpPanel() override { for (int n : heldKeys) if (n >= 0) model.arpKeyInput(n, false); }
    // Re-reads the model now instead of on the next visual frame.
    void syncFromModel() { refresh(); }

    void resized() override {
        const auto a = model.arpSettings();
        const bool hosted = model.capabilities().hosted;
        auto area = getLocalBounds().reduced(10, 8);
        // Two passes: comfortable sizes, then dense ones when they do not fit.
        for (int pass = 0; pass < 2; ++pass) {
            dense_ = pass == 1;
            if (layout(area, a, hosted, true) <= area.getHeight() || dense_) break;
        }
        layout(area, a, hosted, false);
        browser.setBounds(getLocalBounds().reduced(4));
        progressions.setBounds(getLocalBounds().reduced(4));
        repaint();
    }
    // The ARP and CHORDS tabs share this panel; each shows its own style.
    void setChordsView(bool chords) {
        if (chords == chordsView_) return;
        chordsView_ = chords;
        refresh();
    }
    void paint(juce::Graphics& g) override {
        g.setColour(col::panelLo); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8);
        const auto a = model.arpSettings(), liveArp = a.keys ? model.arpLiveSettings() : a;
        const bool hosted = model.capabilities().hosted;
        // Top bar.
        g.setColour(col::text); g.setFont(fui::dispFont(13)); g.drawText(compMode_ ? "CHORD COMPING" : "ARPEGGIATOR", title_, juce::Justification::centredLeft);
        const bool ready = chordMode_ ? a.chordCount > 0 : liveArp.count > 0;
        const juce::String status = !active() ? juce::String::fromUTF8("OFF · SWITCH ON WITH THE TAB LED") : model.arpQueued() ? "QUEUED" : !ready ? (chordMode_ ? "DRAW CHORDS" : "WAITING FOR NOTES") : running() ? "RUNNING" : "READY";
        auto st = status_;
        auto led = st.removeFromRight(10).withSizeKeepingCentre(5, 5).toFloat();
        g.setColour(running() ? live_ : juce::Colour(0xff516077)); g.fillEllipse(led);
        if (running()) { g.setColour(live_.withAlpha(.3f)); g.drawEllipse(led.expanded(2), 2); }
        arp::label(g, status, st.withTrimmedRight(8), arp::c::status, 9, false, juce::Justification::centredRight);
        if (hosted) arp::label(g, juce::String::fromUTF8("SQ-4 CLOCK · ") + juce::String((int)std::lround(model.arpTempo())) + " BPM", tempoLabel_, arp::c::status, 9, false, juce::Justification::centredRight);
        else arp::label(g, "BPM", tempoLabel_, arp::c::status, 9, false, juce::Justification::centredRight);
        // Cards.
        arp::drawCard(g, card1_, 1, "NOTES", live_, headH());
        arp::drawCard(g, card2_, 2, "PATTERN", live_, headH());
        arp::drawCard(g, card3_, 3, "FEEL", live_, headH());
        arp::label(g, notesHint(a), hint1_, arp::c::hint, 9, false, juce::Justification::centredRight);
        if (compMode_) arp::label(g, nowPlaying(), hint2_, arp::c::hint, 9, false, juce::Justification::centredRight);
        else {
            arp::label(g, "ORDER", orderLabel_, arp::c::label); arp::label(g, "RANGE", rangeLabel_, arp::c::label);
            arp::label(g, chordMode_ ? juce::String(cycle_.length) + juce::String::fromUTF8(" STEPS · RESTARTS ON EACH CHORD") : juce::String("16 STEPS"), hint2_, arp::c::hint, 9, false, juce::Justification::centredRight);
        }
        arp::label(g, "RATE", rateLabel_, arp::c::label);
        if (compMode_) { arp::label(g, "STROKE", strokeLabel_, arp::c::label); arp::label(g, "VOICING", voicingLabel_, arp::c::label); }
        if (!chordMode_) paintPool(g, liveArp);
    }

private:
    // ---- Card 2 bodies --------------------------------------------------
    // The generated phrase (web .arp-display): one column per step of the
    // page holding the playhead, each note a gate-long bar at its pitch.
    struct Phrase final : juce::Component {
        ArpPanel& p; explicit Phrase(ArpPanel& owner) : p(owner) { setInterceptsMouseClicks(false, false); }
        void paint(juce::Graphics& g) override {
            auto r = getLocalBounds().toFloat();
            g.setColour(arp::c::display); g.fillRoundedRectangle(r, 5);
            const auto& page = p.page_;
            int low = p.bass_ ? 36 : 48, high = low + 12;
            bool any = false;
            for (int i = 0; i < 16; ++i) if (const int n = page.notes[(size_t)i]; n >= 0) { low = any ? std::min(low, n) : std::min(low, n); high = std::max(high, n); any = true; }
            high = std::max(high, low + 12);
            auto reg = r.removeFromLeft(62);
            g.setColour(arp::c::label); g.setFont(arp::font(9));
            g.drawText(name(high), reg.reduced(6, 10).toNearestInt(), juce::Justification::topLeft);
            g.drawText(name(low), reg.reduced(6, 10).toNearestInt(), juce::Justification::bottomLeft);
            g.setColour(juce::Colours::white.withAlpha(.024f));
            for (int q = 1; q < 4; ++q) g.fillRect(r.getX(), r.getBottom() - r.getHeight() * q / 4, r.getWidth(), 1.0f);
            const float w = r.getWidth() / 16;
            const int cur = p.cursor_ >= 0 ? p.cursor_ % 16 : -1;
            const auto a = p.model.arpSettings();
            for (int i = 0; i < 16; ++i) {
                auto col = juce::Rectangle<float>(r.getX() + i * w, r.getY(), w, r.getHeight());
                g.setColour(juce::Colours::white.withAlpha(i % 4 == 0 ? .1f : .024f)); g.fillRect(col.getX(), col.getY(), 1.0f, col.getHeight());
                if (cur == i) { g.setColour(p.live_.withAlpha(.047f)); g.fillRect(col); g.setColour(p.live_.withAlpha(.4f)); g.fillRect(col.getX(), col.getY(), 1.0f, col.getHeight()); }
                const int n = page.notes[(size_t)i];
                if (n < 0) continue;
                const bool rest = !a.hits[(size_t)i], accented = a.accents[(size_t)i];
                const float y = col.getY() + col.getHeight() * (1 - .08f) - (float)(n - low) / (float)(high - low) * col.getHeight() * .66f - 12;
                auto bar = juce::Rectangle<float>(col.getX() + w * .05f, y, std::max(w * .12f, w * (float)a.gate * .9f), 12.0f);
                const float alpha = rest ? .18f : 1.0f;
                if (cur == i && !rest) { g.setColour(p.live_); g.fillRoundedRectangle(bar, 2); }
                else { g.setColour(p.live_.withAlpha(.075f * alpha)); g.fillRoundedRectangle(bar, 2); }
                g.setColour(p.live_.withAlpha(.53f * alpha)); g.drawRoundedRectangle(bar.reduced(.5f), 2, 1);
                if (accented) { g.setColour(p.live_.withAlpha(.53f * alpha)); g.fillRect(bar.withHeight(3.0f).reduced(1, 0)); }
                if (p.bass_ && a.slides[(size_t)i] && a.hits[(size_t)i] && a.hits[(size_t)((i + 15) % 16)]) {
                    g.setColour(p.live_); g.setFont(arp::font(12)); g.drawText(juce::String::fromUTF8("↗"), (int)bar.getX() - 15, (int)bar.getY() - 8, 14, 14, juce::Justification::centred);
                }
                g.setColour(juce::Colour(0xffb4c6d9).withAlpha(rest ? .3f : 1.0f)); g.setFont(arp::font(9));
                g.drawText(name(n), (int)bar.getX(), (int)bar.getY() - 15, (int)w + 20, 13, juce::Justification::centredLeft);
            }
            g.setColour(juce::Colours::white.withAlpha(.063f)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 5, 1);
        }
    };
    // Step rows (web .arp-grid): numbers, then lanes or comp voices.
    struct Lanes final : juce::Component {
        ArpPanel& p; explicit Lanes(ArpPanel& owner) : p(owner) {}
        enum class Kind { Chords, Steps, Voice, Hits, Accents, Slides, Ties, Divider };
        struct Row { Kind kind; juce::String label; int bit = 0; int height = 0; };
        std::vector<Row> rows;
        int height() const { int h = 0; for (const auto& r : rows) h += r.height + 3; return std::max(0, h - 3); }
        int tallRows() const { int n = 0; for (const auto& r : rows) n += r.height > 15 ? 1 : 0; return std::max(1, n); }
        juce::Rectangle<float> cell(int y, int h, int i) const {
            const float x0 = 62.0f, w = (getWidth() - x0 + 3) / 16.0f;
            return {x0 + i * w, (float)y, w - 3, (float)h};
        }
        void paint(juce::Graphics& g) override {
            const auto a = p.model.arpSettings();
            const int cur = p.cursor_ >= 0 ? p.cursor_ % 16 : -1;
            int held = 0; std::array<int, 16> heldMasks {};
            for (int i = 0; i < 16; ++i) heldMasks[(size_t)i] = held = a.ties[(size_t)i] ? held : a.compMask[(size_t)i];
            int y = 0;
            for (const auto& row : rows) {
                if (row.kind != Kind::Divider && row.kind != Kind::Chords)
                    arp::label(g, row.label, {0, y, 60, row.height}, row.kind == Kind::Steps ? juce::Colours::transparentBlack : arp::c::label, 9, true);
                for (int i = 0; i < 16 && row.kind != Kind::Divider; ++i) {
                    auto c = cell(y, row.height, i);
                    switch (row.kind) {
                        case Kind::Steps: {
                            const bool beat = i % 4 == 0;
                            g.setColour(cur == i ? p.live_ : beat ? arp::c::status : arp::c::dim); g.setFont(arp::font(9, beat));
                            g.drawText(beat ? juce::String(i / 4 + 1) : juce::String::fromUTF8("·"), c.toNearestInt(), juce::Justification::centred);
                            break;
                        }
                        case Kind::Chords: {
                            const auto* ch = p.stepChords_[(size_t)i];
                            if (ch && ch != (i ? p.stepChords_[(size_t)i - 1] : nullptr)) {
                                g.setColour(arp::chordHue(ch->root, .7f, .6f).withAlpha(.7f)); g.fillRect(c.getX(), c.getY(), 2.0f, c.getHeight());
                                g.setColour(arp::chordHue(ch->root, .8f, .72f)); g.setFont(arp::font(10, true));
                                g.drawText(ChordTrackView::chordName(*ch), (int)c.getX() + 6, (int)c.getY(), 80, (int)c.getHeight(), juce::Justification::centredLeft);
                            }
                            break;
                        }
                        case Kind::Voice: {
                            const bool tied = a.ties[(size_t)i], strike = !tied && (a.compMask[(size_t)i] & row.bit), hold = tied && (heldMasks[(size_t)i] & row.bit);
                            const int nx = (i + 1) % 16;
                            const bool continues = (strike || hold) && a.ties[(size_t)nx] && (heldMasks[(size_t)nx] & row.bit) && nx != 0;
                            auto body = c; if (continues) body = body.withRight(body.getRight() + 3);
                            if (strike) {
                                g.setColour(a.accents[(size_t)i] ? p.live_ : p.live_.withAlpha(.6f)); g.fillRoundedRectangle(body, continues ? 0.0f : 3.0f);
                                g.setColour(p.live_); g.drawRoundedRectangle(c.reduced(.5f), 3, 1);
                            } else if (hold) {
                                g.setColour(p.live_.withAlpha(.2f)); g.fillRect(body);
                                g.setColour(p.live_.withAlpha(.3f)); g.fillRect(body.withHeight(1.0f)); g.fillRect(body.withY(body.getBottom() - 1).withHeight(1.0f));
                            } else drawCell(g, c, i, cur);
                            break;
                        }
                        default: {
                            const auto& lane = row.kind == Kind::Hits ? a.hits : row.kind == Kind::Accents ? a.accents : row.kind == Kind::Slides ? a.slides : a.ties;
                            const bool on = lane[(size_t)i];
                            const auto tone = row.kind == Kind::Accents ? arp::c::accent : p.live_;
                            drawCell(g, c, i, cur);
                            if (on) { g.setColour(tone.withAlpha(.1f)); g.fillRoundedRectangle(c, 3); g.setColour(tone.withAlpha(.35f)); g.drawRoundedRectangle(c.reduced(.5f), 3, 1); }
                            if (on) {
                                g.setColour(tone); g.setFont(arp::font(row.kind == Kind::Ties ? 15.0f : 12.0f));
                                g.drawText(juce::String::fromUTF8(row.kind == Kind::Ties ? "⁀" : row.kind == Kind::Slides ? "↗" : "●"), c.toNearestInt(), juce::Justification::centred);
                            }
                        }
                    }
                }
                y += row.height + 3;
            }
        }
        void drawCell(juce::Graphics& g, juce::Rectangle<float> c, int i, int cur) const {
            g.setColour(cur == i ? arp::c::cellNow : arp::c::cell); g.fillRoundedRectangle(c, 3);
            g.setColour(juce::Colours::white.withAlpha(.05f)); g.drawRoundedRectangle(c.reduced(.5f), 3, 1);
            if (i % 4 == 0) { g.setColour(juce::Colours::white.withAlpha(.14f)); g.fillRect(c.getX(), c.getY() + 2, 1.0f, c.getHeight() - 4); }
        }
        void mouseDown(const juce::MouseEvent& e) override {
            int y = 0;
            for (const auto& row : rows) {
                if (e.y >= y && e.y < y + row.height) {
                    for (int i = 0; i < 16; ++i) if (cell(y, row.height, i).expanded(1.5f, 0).contains(e.position)) { click(row, i); return; }
                    return;
                }
                y += row.height + 3;
            }
        }
        void click(const Row& row, int i) {
            p.change([&](auto& a) {
                switch (row.kind) {
                    case Kind::Voice: {
                        if (a.ties[(size_t)i]) {
                            // A click on a held step strikes there instead, keeping the other held voices.
                            int held = 0; for (int k = 0; k <= i; ++k) held = a.ties[(size_t)k] ? held : a.compMask[(size_t)k];
                            a.ties[(size_t)i] = false; a.compMask[(size_t)i] = (uint8_t)(held ^ row.bit);
                        } else a.compMask[(size_t)i] = (uint8_t)(a.compMask[(size_t)i] ^ row.bit);
                        break;
                    }
                    case Kind::Hits: a.hits[(size_t)i] = !a.hits[(size_t)i]; break;
                    case Kind::Accents: a.accents[(size_t)i] = !a.accents[(size_t)i]; break;
                    case Kind::Slides: a.slides[(size_t)i] = !a.slides[(size_t)i]; break;
                    case Kind::Ties: a.ties[(size_t)i] = !a.ties[(size_t)i]; break;
                    default: break;
                }
            });
        }
    };
    // Transport square (web .pb-btn).
    struct Transport final : juce::Button {
        Transport() : juce::Button("Launch arp clip") {}
        bool running = false, queued = false; juce::Colour live {col::acA};
        void paintButton(juce::Graphics& g, bool over, bool) override {
            auto r = getLocalBounds().toFloat().reduced(1);
            g.setColour(running ? live.withAlpha(.14f) : arp::c::field); g.fillRoundedRectangle(r, 5);
            g.setColour(running || over ? live.withAlpha(.6f) : juce::Colours::white.withAlpha(.12f)); g.drawRoundedRectangle(r.reduced(.5f), 5, 1);
            g.setColour(running || queued ? live : arp::c::value);
            const auto icon = r.withSizeKeepingCentre(12, 12);
            g.fillPath(running || queued ? fui::iconStop(icon) : fui::iconPlay(icon));
        }
    };

    // ---- Layout ----------------------------------------------------------
    int headH() const { return dense_ ? 24 : 28; }
    int pad() const { return dense_ ? 8 : 11; }
    // Places every control; returns the height used. `measure` only measures.
    int layout(juce::Rectangle<int> area, const fable::ArpSettings& a, bool hosted, bool measure) {
        const int top0 = area.getY(), gap = dense_ ? 6 : 10, P = pad(), H = headH();
        auto place = [measure](juce::Component& c, juce::Rectangle<int> r) { if (!measure) c.setBounds(r); };
        // Top bar.
        auto top = area.removeFromTop(dense_ ? 30 : 34);
        if (!measure) {
            auto t = top;
            if (hosted) { transport.setBounds(t.removeFromLeft(34).withSizeKeepingCentre(32, 30)); t.removeFromLeft(12); }
            title_ = t.removeFromLeft(190);
            if (!hosted) { tempo.setBounds(t.removeFromRight(56).reduced(0, 4)); tempoLabel_ = t.removeFromRight(36).withTrimmedRight(8); }
            else tempoLabel_ = t.removeFromRight(190);
            t.removeFromRight(16);
            status_ = t.removeFromRight(170);
        }
        area.removeFromTop(gap);
        // 1 NOTES
        const bool keyboard = !chordMode_ && (a.keys || editing_);
        const int body1 = chordMode_ ? chordTrack.preferredHeight(dense_) : 32 + (keyboard ? (dense_ ? 6 + 30 : 10 + 40) : 0);
        card1_ = area.removeFromTop(P + H + (dense_ ? 6 : 10) + body1 + P);
        if (!measure) {
            auto head = card1_.reduced(12, 0).withTrimmedTop(P).withHeight(H);
            head.removeFromLeft(18 + 10 + arp::labelWidth("NOTES", 10) + 16);
            input.setBounds(head.removeFromLeft(input.preferredWidth()).reduced(0, 1));
            hint1_ = head.withTrimmedLeft(12);
            auto body = card1_.reduced(12, 0).withTrimmedTop(P + H + (dense_ ? 6 : 10)).withHeight(body1);
            if (chordMode_) chordTrack.setBounds(body);
            else {
                auto source = body.removeFromTop(32);
                for (auto* b : {&clear, &store, &latch, &editNotes}) if (b->isVisible()) { b->setBounds(source.removeFromRight(b->preferredWidth()).reduced(0, 3)); source.removeFromRight(8); }
                pool_ = source;
                if (keyboard) {
                    body.removeFromTop(dense_ ? 6 : 10);
                    auto row = body;
                    lower.setBounds(row.removeFromLeft(26).withSizeKeepingCentre(26, 24)); octLabel_ = row.removeFromLeft(50); higher.setBounds(row.removeFromLeft(26).withSizeKeepingCentre(26, 24));
                    row.removeFromLeft(14);
                    if (clearList.isVisible()) { clearList.setBounds(row.removeFromRight(clearList.preferredWidth()).reduced(0, (row.getHeight() - 26) / 2)); row.removeFromRight(14); }
                    const int kw = row.getWidth() / 12;
                    for (auto& k : keys) k.setBounds(row.removeFromLeft(kw).withTrimmedRight(3));
                } else octLabel_ = {};
            }
        }
        area.removeFromTop(gap);
        // 3 FEEL sits below PATTERN; measure it first so PATTERN can flex.
        const int card3H = P + H + P;
        // 2 PATTERN
        // The phrase display stops at 130 px; spare height beyond that makes the lanes taller (up to +10 px each).
        const int minDisplay = compMode_ ? 0 : 60, maxDisplay = compMode_ ? 0 : 130;
        int lanesH = 0, fixed2 = 0, room = 0;
        auto measureCard2 = [&](int boost) {
            rebuildRows(a, boost);
            lanesH = lanes.height();
            fixed2 = P + H + (dense_ ? 6 : 10) + lanesH + (compMode_ ? 0 : 8) + P;
            room = area.getHeight() - card3H - gap - fixed2;
        };
        measureCard2(0);
        if (!dense_ && room > maxDisplay) measureCard2(std::min(10, (room - maxDisplay) / (3 * lanes.tallRows())));
        const int display = compMode_ ? 0 : std::clamp(room, minDisplay, maxDisplay);
        card2_ = area.removeFromTop(fixed2 + display);
        if (!measure) {
            auto head = card2_.reduced(12, 0).withTrimmedTop(P).withHeight(H);
            head.removeFromLeft(18 + 10 + arp::labelWidth("PATTERN", 10) + 16);
            if (compMode_) { compPicker.setBounds(head.removeFromLeft(std::min(380, head.getWidth() / 2))); }
            else {
                orderLabel_ = head.removeFromLeft(46); order.setBounds(head.removeFromLeft(150).reduced(0, 1)); head.removeFromLeft(16);
                rangeLabel_ = head.removeFromLeft(46); octaves.setBounds(head.removeFromLeft(124).reduced(0, 1)); head.removeFromLeft(12);
                if (seed.isVisible()) { seed.setBounds(head.removeFromLeft(seed.preferredWidth()).reduced(0, 2)); head.removeFromLeft(12); }
            }
            hint2_ = head.withTrimmedLeft(12);
            auto body = card2_.reduced(12, 0).withTrimmedTop(P + H + (dense_ ? 6 : 10)).withTrimmedBottom(P);
            if (!compMode_) { place(phrase, body.removeFromTop(display)); body.removeFromTop(8); }
            place(lanes, body.removeFromTop(lanesH));
        }
        area.removeFromTop(gap);
        card3_ = area.removeFromTop(card3H);
        if (!measure) {
            auto head = card3_.reduced(12, 0).withTrimmedTop(P).withHeight(H);
            head.removeFromLeft(18 + 10 + arp::labelWidth("FEEL", 10) + 22);
            rateLabel_ = head.removeFromLeft(38); rate.setBounds(head.removeFromLeft(150).reduced(0, 1)); head.removeFromLeft(22);
            gate.setBounds(head.removeFromLeft(130)); head.removeFromLeft(22);
            swing.setBounds(head.removeFromLeft(130)); head.removeFromLeft(22);
            if (compMode_) {
                strum.setBounds(head.removeFromLeft(130)); head.removeFromLeft(22);
                strokeLabel_ = head.removeFromLeft(50); stroke.setBounds(head.removeFromLeft(stroke.preferredWidth() + 12).reduced(0, 1)); head.removeFromLeft(14);
                voicingLabel_ = head.removeFromLeft(56); voicing.setBounds(head.removeFromLeft(std::min(head.getWidth(), voicing.preferredWidth() + 10)).reduced(0, 1));
            }
        }
        return card3_.getBottom() - top0;
    }
    void rebuildRows(const fable::ArpSettings& a, int boost = 0) {
        using K = Lanes::Kind;
        const int voiceH = (dense_ ? 17 : 24) + boost, laneH = (dense_ ? 16 : 22) + boost;
        lanes.rows.clear();
        if (compMode_) {
            if (chordMode_) lanes.rows.push_back({K::Chords, "CHORD", 0, 15});
            lanes.rows.push_back({K::Steps, "", 0, 12});
            for (const auto& r : compRows) lanes.rows.push_back({K::Voice, r.name, r.bit, voiceH});
            lanes.rows.push_back({K::Divider, "", 0, 2});
            lanes.rows.push_back({K::Ties, "HOLD", 0, laneH});
            lanes.rows.push_back({K::Accents, "ACCENT", 0, laneH});
        } else {
            lanes.rows.push_back({K::Steps, "", 0, 12});
            lanes.rows.push_back({K::Hits, "PLAY", 0, laneH});
            lanes.rows.push_back({K::Accents, "ACCENT", 0, laneH});
            if (bass_) lanes.rows.push_back({K::Slides, "SLIDE", 0, laneH});
        }
        juce::ignoreUnused(a);
    }

    // ---- Painting helpers --------------------------------------------------
    juce::String notesHint(const fable::ArpSettings& a) const {
        if (chordMode_) return juce::String(bars()) + (bars() > 1 ? " BARS, LOOPED" : " BAR, LOOPED");
        if (a.keys) return a.latch ? "LATCHED" : "HOLD KEYS TO PLAY";
        return {};
    }
    juce::String nowPlaying() const {
        const int s = cursor_ >= 0 ? cursor_ % 16 : -1;
        if (s < 0 || !page_.comp || page_.stackCount[(size_t)s] == 0) return "CLICK THE GRID TO ADD OR REMOVE NOTES";
        std::vector<int> v(page_.stack[(size_t)s].begin(), page_.stack[(size_t)s].begin() + page_.stackCount[(size_t)s]);
        std::sort(v.begin(), v.end());
        juce::String out; for (size_t i = 0; i < v.size(); ++i) out << (i ? juce::String::fromUTF8(" · ") : juce::String()) << name(v[i]);
        return out;
    }
    void paintPool(juce::Graphics& g, const fable::ArpSettings& liveArp) {
        auto r = pool_;
        const int s = cursor_ >= 0 ? cursor_ % 16 : -1;
        const int sounding = s >= 0 && (page_.comp || liveArp.hits[(size_t)s]) ? page_.notes[(size_t)s] : -1;
        if (liveArp.count == 0) {
            g.setColour(arp::c::label); g.setFont(arp::font(11));
            g.drawText(liveArp.keys ? "Play a chord on MIDI or the keyboard below." : "Add notes to begin.", r, juce::Justification::centredLeft);
        }
        for (int i = 0; i < liveArp.count; ++i) {
            const int n = liveArp.notes[(size_t)i];
            bool on = n == sounding;
            if (page_.comp && s >= 0) for (int k = 0; k < page_.stackCount[(size_t)s]; ++k) on = on || page_.stack[(size_t)s][(size_t)k] == n;
            const auto text = name(n);
            const int w = juce::GlyphArrangement::getStringWidthInt(arp::font(12), text) + 22;
            if (w > r.getWidth()) break;
            auto chip = r.removeFromLeft(w).withSizeKeepingCentre(w, 28).toFloat();
            r.removeFromLeft(6);
            g.setColour(juce::Colour(0xff101d28)); g.fillRoundedRectangle(chip, 4);
            g.setColour(on ? live_ : juce::Colour(0xff385163)); g.drawRoundedRectangle(chip.reduced(.5f), 4, 1);
            g.setColour(on ? live_ : juce::Colour(0xffc4dce8)); g.setFont(arp::font(12)); g.drawText(text, chip, juce::Justification::centred);
        }
        if (!octLabel_.isEmpty()) {
            g.setColour(arp::c::status); g.setFont(arp::font(9)); g.drawText("OCT " + juce::String(octave_), octLabel_, juce::Justification::centred);
        }
    }

    // ---- State -------------------------------------------------------------
    static fable::ArpSettings withoutChords(fable::ArpSettings a) { a.chords = false; return a; }
    template<class Fn> void change(Fn&& f) { auto a = model.arpSettings(); f(a); model.setArpSettings(a); refresh(); }
    static juce::String name(int n) { static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; return n < 0 ? juce::String::fromUTF8("—") : juce::String(names[n % 12]) + juce::String(n / 12 - 1); }
    void setParameter(const char* id, double v) { if (auto* p = model.parameters().parameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((float)v)); p->endChangeGesture(); } }
    double parameter(const char* id, double fallback) const { auto* p = model.parameters().parameter(id); return p ? p->convertFrom0to1(p->getValue()) : fallback; }
    int bars() const { return model.capabilities().hosted ? std::clamp(model.arpSettings().chordBars, 1, 4) : chainBars(); }
    void timerCallback() override { refresh(); }
    // The arp cycle and the 16-step page holding the playhead.
    void compilePage(const fable::ArpSettings& a, int cursor) {
        const auto liveArp = a.keys ? model.arpLiveSettings() : a;
        auto shown = chordMode_ ? liveArp : withoutChords(liveArp);
        shown.comp = compMode_; // the page shows its own tab's style, whichever mode plays
        cycle_ = fable::compileArp(shown, bars());
        const int page = cursor >= 0 ? (cursor / 16) % std::max(1, cycle_.length / 16) : 0;
        page_ = cycle_;
        std::copy(cycle_.notes.begin() + page * 16, cycle_.notes.begin() + page * 16 + 16, page_.notes.begin());
        if (cycle_.comp) {
            std::copy(cycle_.stack.begin() + page * 16, cycle_.stack.begin() + page * 16 + 16, page_.stack.begin());
            std::copy(cycle_.stackCount.begin() + page * 16, cycle_.stackCount.begin() + page * 16 + 16, page_.stackCount.begin());
            std::copy(cycle_.lens.begin() + page * 16, cycle_.lens.begin() + page * 16 + 16, page_.lens.begin());
        }
        // The chord under each step of the shown page.
        stepChords_.fill(nullptr);
        if (chordMode_) {
            chordsCopy_ = a.chordList; chordCount_ = a.chordCount;
            const int total = bars() * 16;
            for (int i = 0; i < 16; ++i) {
                const double pos = fable::arpStepPos(a.rate, bars(), page * 16 + i) + 1e-6;
                for (int k = 0; k < chordCount_; ++k) {
                    const auto& c = chordsCopy_[(size_t)k];
                    if (pos >= c.start && pos < std::min(total, c.start + c.length)) { stepChords_[(size_t)i] = &chordsCopy_[(size_t)k]; break; }
                }
            }
        }
    }
    void refresh() {
        const auto a = model.arpSettings(); const bool hosted = model.capabilities().hosted;
        bool relayout = false;
        const bool chordMode = a.chords, compMode = chordsView_ && !bass_;
        if (chordMode != chordMode_) { chordMode_ = chordMode; chordTrack.setVisible(chordMode); relayout = true; }
        if (compMode != compMode_) {
            compMode_ = compMode;
            for (auto* c : std::initializer_list<juce::Component*>{&compPicker, &strum, &stroke, &voicing}) c->setVisible(compMode);
            for (auto* c : std::initializer_list<juce::Component*>{&order, &octaves, &phrase}) c->setVisible(!compMode);
            if (!compMode) browser.setVisible(false);
            relayout = true;
        }
        if (!chordMode) progressions.setVisible(false);
        // NOTES card controls.
        const bool stored = !a.keys && !chordMode, keysMode = a.keys && !chordMode, keyboard = keysMode || (stored && editing_);
        auto show = [&](juce::Component& c, bool on) { if (c.isVisible() != on) { c.setVisible(on); relayout = true; } };
        show(editNotes, stored); show(latch, keysMode); show(store, keysMode); show(clear, keysMode);
        show(clearList, stored && editing_); show(lower, keyboard); show(higher, keyboard);
        for (auto& k : keys) show(k, keyboard);
        show(seed, !compMode_ && a.order == 4);
        show(transport, hosted);
        editNotes.setButtonText(editing_ ? "DONE" : "EDIT NOTES"); editNotes.setToggleState(editing_, juce::dontSendNotification);
        latch.setToggleState(a.latch, juce::dontSendNotification);
        input.setSelectedId(a.chords ? 3 : a.keys ? 2 : 1, juce::dontSendNotification);
        order.setSelectedId(a.order + 1, juce::dontSendNotification);
        const auto it = std::find(rates.begin(), rates.end(), a.rate); rate.setSelectedId((int)(it - rates.begin()) + 1, juce::dontSendNotification);
        octaves.setSelectedId(a.octaves, juce::dontSendNotification);
        const int preset = fable::compLibraryIndex(a);
        if (preset < 0) compPicker.setText("CUSTOM PATTERN", {});
        else {
            const auto& e = fable::compLibrary()[(size_t)preset];
            compPicker.setText(e.groove, juce::String(e.variation) != "ORIGINAL" ? juce::String(e.variation) : juce::String(e.family));
        }
        voicing.setSelectedId(a.voicing + 1, juce::dontSendNotification); stroke.setSelectedId(a.strumDir + 1, juce::dontSendNotification);
        strum.setValue(a.strum); gate.setValue(a.gate * 100);
        swing.setValue((hosted ? model.arpSwing() : parameter(bass_ ? "master.swing" : "seq.swing", 0)) * 100);
        if (!hosted && !tempo.isBeingEdited()) tempo.setText(juce::String((int)std::lround(parameter("seq.bpm", 120))), juce::dontSendNotification);
        transport.live = live_; transport.running = playing(); transport.queued = model.arpQueued();
        for (int i = 0; i < 12; ++i) {
            const int n = (octave_ + 1) * 12 + i;
            keys[(size_t)i].setButtonText(name(n));
            const auto keyPool = a.keys ? model.arpLiveSettings() : a;
            keys[(size_t)i].setToggleState(std::find(keyPool.notes.begin(), keyPool.notes.begin() + keyPool.count, n) != keyPool.notes.begin() + keyPool.count, juce::dontSendNotification);
        }
        if (chordMode_) chordTrack.refresh();
        if (relayout) resized();
        // Repaint the generated views only when their inputs change: a blind
        // repaint at the animation rate redrew every lane.
        const auto sig = paintSignature(a);
        const int cursor = running() ? current() : -1;
        if (sig != lastPaintSig_ || relayout) {
            lastPaintSig_ = sig; compilePage(a, cursor);
            repaint(); phrase.repaint(); lanes.repaint(); transport.repaint();
        } else if (cursor != lastPaintCursor_) {
            if (lastPaintCursor_ / 16 != cursor / 16 || (cursor < 0) != (lastPaintCursor_ < 0)) compilePage(a, cursor);
            cursor_ = cursor;
            phrase.repaint(); lanes.repaint(); transport.repaint();
            if (chordMode_) chordTrack.repaint();
            repaint(pool_); repaint(status_); repaint(hint2_);
        }
        cursor_ = cursor;
        lastPaintCursor_ = cursor;
    }
    uint64_t paintSignature(const fable::ArpSettings& a) const {
        const auto liveArp = a.keys ? model.arpLiveSettings() : a;
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        mix((uint64_t)a.keys); mix((uint64_t)a.chords); mix((uint64_t)a.comp); mix((uint64_t)a.latch); mix((uint64_t)editing_);
        mix((uint64_t)playing()); mix((uint64_t)active()); mix((uint64_t)compMode_); mix((uint64_t)model.arpQueued()); mix((uint64_t)std::llround(model.arpTempo()));
        for (int i = 0; i < 16; ++i) mix((uint64_t)a.compMask[(size_t)i] | (uint64_t)a.ties[(size_t)i] << 4);
        mix((uint64_t)a.voicing | (uint64_t)a.strumDir << 4); mix((uint64_t)bars()); mix((uint64_t)a.chordKey); mix((uint64_t)a.chordOctave);
        for (int i = 0; i < a.chordCount; ++i) { const auto& c = a.chordList[(size_t)i]; mix((uint64_t)c.start | (uint64_t)c.length << 8 | (uint64_t)c.root << 16 | (uint64_t)c.quality << 24 | (uint64_t)c.inversion << 32); }
        mix((uint64_t)liveArp.order); mix((uint64_t)liveArp.octaves); mix((uint64_t)liveArp.count);
        mix(liveArp.seed); mix((uint64_t)std::llround(liveArp.rate * 1e6)); mix((uint64_t)std::llround(liveArp.gate * 1e6));
        for (int i = 0; i < liveArp.count; ++i) mix((uint64_t)liveArp.notes[(size_t)i]);
        for (int i = 0; i < 16; ++i) mix((uint64_t)liveArp.hits[(size_t)i] | (uint64_t)liveArp.accents[(size_t)i] << 1 | (uint64_t)liveArp.slides[(size_t)i] << 2);
        return h;
    }
    void stepPattern(int d) {
        const int n = (int)fable::compLibrary().size(), at = fable::compLibraryIndex(model.arpSettings());
        change([&](auto& a) { fable::applyCompEntry(a, fable::compLibrary()[(size_t)((std::max(at, d > 0 ? -1 : 0) + d + n) % n)]); });
    }
    struct CompRow { const char* name; int bit; };
    static constexpr CompRow compRows[4] {{"TOP", fable::kCompTop}, {"MID", fable::kCompMid}, {"LOW", fable::kCompLow}, {"BASS", fable::kCompBass}};

    // The page plays only while its own mode is the one switched on.
    bool active() const { const auto a = model.arpSettings(); return a.enabled && (bass_ || a.comp == chordsView_); }
    bool running() const { return playing() && active(); }
    DeviceUiModel& model; bool bass_; int octave_;
    bool chordsView_ = false;
    juce::Colour live_;
    std::function<bool()> playing; std::function<int()> current; std::function<void(bool)> play; std::function<int()> chainBars;
    const std::array<double, 8> rates {{1, .75, .5, 1.0/3, .375, .25, 1.0/6, .125}};
    ChordTrackView chordTrack;
    ProgressionBrowserView progressions;
    CompBrowserView browser;
    // Top bar.
    Transport transport;
    juce::Label tempo;
    // 1 NOTES
    arp::Segmented input;
    arp::Button editNotes {arp::Button::Kind::Ghost}, latch {arp::Button::Kind::Ghost}, store {arp::Button::Kind::Ghost},
                clear {arp::Button::Kind::Ghost}, clearList {arp::Button::Kind::Ghost}, seed {arp::Button::Kind::Ghost};
    arp::Button lower {arp::Button::Kind::Step}, higher {arp::Button::Kind::Step};
    std::array<arp::Button, 12> keys {{arp::Button::Kind::Key, arp::Button::Kind::Key, arp::Button::Kind::Key, arp::Button::Kind::Key,
                                       arp::Button::Kind::Key, arp::Button::Kind::Key, arp::Button::Kind::Key, arp::Button::Kind::Key,
                                       arp::Button::Kind::Key, arp::Button::Kind::Key, arp::Button::Kind::Key, arp::Button::Kind::Key}};
    std::array<int, 12> heldKeys {};
    // 2 PATTERN
    juce::ComboBox order, octaves;
    arp::Picker compPicker;
    Phrase phrase {*this};
    Lanes lanes {*this};
    // 3 FEEL
    juce::ComboBox rate;
    arp::FeelSlider gate, swing, strum;
    arp::Segmented stroke, voicing;

    juce::Rectangle<int> title_, status_, tempoLabel_, card1_, card2_, card3_, hint1_, hint2_, pool_, octLabel_,
                         orderLabel_, rangeLabel_, rateLabel_, strokeLabel_, voicingLabel_;
    fable::ArpPattern cycle_, page_;
    std::array<fable::ArpChord, fable::kMaxArpChords> chordsCopy_ {};
    int chordCount_ = 0;
    std::array<const fable::ArpChord*, 16> stepChords_ {};
    uint64_t lastPaintSig_ = 0;
    int lastPaintCursor_ = -1, cursor_ = -1;
    bool chordMode_ = false, compMode_ = false, editing_ = false, dense_ = false;
};

// The SEQUENCER, ARP, and CHORDS tab LEDs: switching one mode on switches
// the others off. All three may be off. ARP and CHORDS are the arp in its
// arpeggio and comping styles; BL-1 is monophonic and shows no CHORDS LED.
struct PlaybackToggles {
    explicit PlaybackToggles(DeviceUiModel& m)
        : sequencer([&m] { return m.seqEnabled() && !m.arpSettings().enabled; },
                    [&m](bool on) {
                        if (auto a = m.arpSettings(); on && a.enabled) { a.enabled = false; m.setArpSettings(a); }
                        m.setSeqEnabled(on);
                    }, Accent::A),
          arpeggiator([&m] { const auto a = m.arpSettings(); return a.enabled && !a.comp; }, arpMode(m, false), Accent::A),
          chords([&m] { const auto a = m.arpSettings(); return a.enabled && a.comp; }, arpMode(m, true), Accent::A) {
        sequencer.setName("Sequencer on/off"); arpeggiator.setName("Arpeggiator on/off"); chords.setName("Chord comping on/off");
    }
    // Places each LED inside the right end of its tab button.
    void place(const juce::Component& seqTab, const juce::Component& arpTab, const juce::Component* chordsTab = nullptr) {
        sequencer.setBounds(seqTab.getBounds().removeFromRight(24).withSizeKeepingCentre(22, 22));
        arpeggiator.setBounds(arpTab.getBounds().removeFromRight(24).withSizeKeepingCentre(22, 22));
        if (chordsTab) chords.setBounds(chordsTab->getBounds().removeFromRight(24).withSizeKeepingCentre(22, 22));
    }
    PowerButton sequencer, arpeggiator, chords;
private:
    static std::function<void(bool)> arpMode(DeviceUiModel& m, bool comp) {
        return [&m, comp](bool on) {
            auto a = m.arpSettings();
            a.enabled = on;
            if (on) a.comp = comp;
            m.setArpSettings(a);
            if (on) m.setSeqEnabled(false);
        };
    }
};
}
