#pragma once
// Scene chord progression picker — port of src/seq/components/SceneChordChip.tsx
// (dialog half; the chip itself is painted by SceneGridView). A scrim over
// the rack with the progression browser, titled with the scene and its linked
// tracks. Loading a progression (in the scene's key) or changing the KEY
// writes chords, chordKey and chordBars into every linked clip of the scene
// through Conductor::setSceneChords (updateClipArp), so live clips hot-swap.
// One undo snapshot per opening, taken before the first write.

#include "../SeqProcessor.h"
#include "../../ui/ProgressionBrowserView.h"
#include "../../ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace fui {

class SceneChordsOverlay final : public juce::Component {
public:
    explicit SceneChordsOverlay(SeqAudioProcessor& p) : proc(p), browser(col::acA) {
        browser.chordKey = [this] { return sceneKey(); };
        browser.current = [this] {
            const auto* a = sceneArp();
            return a ? fable::progressionIndex(a->chordList, a->chordCount, a->chordKey) : -1;
        };
        browser.load = [this](int i) {
            if (i < 0 || i >= (int)fable::progressions().size()) return;
            std::array<fable::ArpChord, fable::kMaxArpChords> chords {}; int count = 0;
            const int key = sceneKey();
            const int bars = fable::progressionChords(fable::progressions()[(size_t)i], key, chords, count);
            write(chords, count, key, bars);
        };
        browser.setKey = [this](int key) {
            const auto* a = sceneArp(); if (!a) return;
            auto chords = a->chordList;
            for (int i = 0; i < a->chordCount; ++i)
                chords[(size_t)i].root = (uint8_t)((chords[(size_t)i].root + key - a->chordKey + 12) % 12);
            write(chords, a->chordCount, key, a->chordBars);
        };
        browser.titleText = [this] {
            const auto& scenes = proc.conductor().session().scenes;
            if (scene_ < 0 || scene_ >= (int)scenes.size()) return juce::String();
            const int n = (int)proc.conductor().sceneChordTracks(scene_).size();
            return juce::String(scenes[(size_t)scene_].name) + juce::String::fromUTF8(" · ")
                 + juce::String(n) + (n == 1 ? " LINKED TRACK" : " LINKED TRACKS");
        };
        browser.onClose = [this] { close(); };
        addAndMakeVisible(browser);
        setInterceptsMouseClicks(true, true);
    }

    void open(int s) {
        scene_ = s; snapshotTaken_ = false;
        if (!sceneArp()) return;
        setVisible(true); toFront(true);
        browser.setVisible(false); browser.setVisible(true); // re-reads key, title and list
    }
    void close() { setVisible(false); scene_ = -1; }
    bool isOpen() const { return isVisible(); }
    int scene() const { return scene_; }
    ProgressionBrowserView& getBrowser() { return browser; } // for the host test

    void resized() override {
        browser.setBounds(getLocalBounds().withSizeKeepingCentre(juce::jmin(820, getWidth() - 32), juce::jmin(560, getHeight() - 32)));
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black.withAlpha(.67f)); }
    void mouseDown(const juce::MouseEvent& e) override { if (!browser.getBounds().contains(e.getPosition())) close(); }

private:
    // The scene progression: the first linked clip's chord track (web sceneChords).
    const fable::ArpSettings* sceneArp() const {
        if (scene_ < 0) return nullptr;
        const auto tracks = proc.conductor().sceneChordTracks(scene_);
        if (tracks.empty()) return nullptr;
        return &proc.conductor().session().scenes[(size_t)scene_].clips[(size_t)tracks[0]].arp;
    }
    int sceneKey() const { const auto* a = sceneArp(); return a ? a->chordKey : 0; }
    void write(const std::array<fable::ArpChord, fable::kMaxArpChords>& chords, int count, int key, int bars) {
        if (!sceneArp()) { close(); return; }
        if (!snapshotTaken_) { proc.pushUndoSnapshot(); snapshotTaken_ = true; }
        proc.conductor().setSceneChords(scene_, chords, count, key, bars);
        browser.repaint();
    }

    SeqAudioProcessor& proc;
    ProgressionBrowserView browser;
    int scene_ = -1;
    bool snapshotTaken_ = false;
};

} // namespace fui
