#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SeqProcessor.h"
#include "ui/SeqHeader.h"
#include "ui/TrackHeadsView.h"
#include "ui/SceneGridView.h"
#include "ui/SeqFooterView.h"
#include "ui/DeviceFocusView.h"
#include "../ui/LookAndFeel.h"

#include <functional>
#include <utility>

// The web's .sq-hint legend line (SeqApp.tsx): tiny contextual key/mouse help
// under the rack. Text comes from a provider so the quant value stays current;
// 2 Hz repaint is plenty for copy changes.
class HintBar : public juce::Component, private juce::Timer {
public:
    HintBar() { startTimerHz(2); setInterceptsMouseClicks(false, false); }
    ~HintBar() override { stopTimer(); }
    void setProvider(std::function<juce::String()> p) { provider_ = std::move(p); repaint(); }
    void paint(juce::Graphics& g) override {
        if (!provider_) return;
        g.setColour(fui::col::textHint.withAlpha(0.85f));
        g.setFont(fui::monoFont(8.0f));
        fui::drawSpaced(g, provider_(), getLocalBounds(), 1.1f, juce::Justification::centred);
    }
private:
    void timerCallback() override {
        auto now = provider_ ? provider_() : juce::String();
        if (now != last_) { last_ = now; repaint(); }
    }
    std::function<juce::String()> provider_;
    juce::String last_;
};

// The SQ-4 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/seq/seq.css, #seq-rack at its 1460px max-width). The
// editor scales it to the window so the layout stays pixel-faithful — same
// scheme as the WT-1/DR-1/BL-1 racks.
//
// Two layouts share one anatomy (docs/.../sq4-device-focus-design.md §2,
// re-pitched 2026-07-18 to match the web's single-strip focus mode): session
// mode is header + heads + full scene grid + footer; focus mode hides the
// heads row and the full grid entirely, replacing them with one horizontal
// strip (a "< SESSION" back chip + the 6 scene chips, reusing SceneGridView
// in its singleRow_ mode) directly under the header, with the native device
// surface filling the rest (the footer hides too). The collapse between the
// two is eased over ~180ms — see applyLayout() — the JUCE analogue of the
// web's animated FLIP collapse.
class SeqRack : public juce::Component, private juce::Timer {
public:
    static constexpr int LW = 1460, LH = 722;
    // Focus-mode logical height. The web rack auto-grows to viewport height
    // in focus mode (SeqApp.tsx); the JUCE analogue grows the editor window
    // instead (see SeqEditor::enterFocus/exitFocus). Derivation, top to
    // bottom of the focus rack (see SeqRack::applyLayout()'s focus geometry):
    //   header            14 + 44           = 58  (bottom)
    //   focus strip       67 + 30           = 97  (bottom)
    //   gap                                 + 8
    //   DeviceFocusView                     + 800 (38 toolbar + content, sized
    //                                                so BASS -- 1460x931
    //                                                logical, the tallest
    //                                                hosted body -- renders
    //                                                at ~0.92 scale, near-1:1)
    //   gap                                 + 8
    //   hint line                           + 14
    //   bottom margin                       + 14
    //   ----------------------------------------------
    //   97 + 8 + 800 + 8 + 14 + 14          = 941
    static constexpr int LHF = 941;
    explicit SeqRack(SeqAudioProcessor&);
    void resized() override;
    int logicalHeight() const { return focusMode_ ? LHF : LH; }

    fui::SeqHeader& getHeader() { return header; }
    fui::TrackHeadsView& getHeads() { return trackHeads; }
    fui::SceneGridView& getGrid() { return sceneGrid; }
    fui::SeqFooterView& getFooter() { return footer; }
    fui::DeviceFocusView& getDeviceFocus() { return deviceFocus; }
    HintBar& getHint() { return hint; }

    void enterFocus(int track, int scene);
    void exitFocus();
    void setFocusScene(int scene);

private:
    fui::SeqHeader header;
    fui::TrackHeadsView trackHeads;
    fui::SceneGridView sceneGrid;
    fui::SeqFooterView footer;
    fui::DeviceFocusView deviceFocus;
    HintBar hint;

    void timerCallback() override;
    void applyLayout();
    float focusT_ = 0.0f, focusTarget_ = 0.0f; // 0 = session, 1 = focus

    bool focusMode_ = false;
    int focusTrack_ = -1;
};

// The editor is the DragAndDropContainer for the grid's clip-block drags
// (same idiom as the WT-1/DR-1 editors' mod-chip drags — ui/Modulation.cpp).
class SeqEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer {
public:
    explicit SeqEditor(SeqAudioProcessor&);
    ~SeqEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    // Focus state (pure UI; the web keeps it in the store). {-1,-1} = session.
    void enterFocus(int t, int s = -1);
    void exitFocus();
    void focusScene(int s);
    std::pair<int, int> focus() const { return { focusTrack_, focusScene_ }; }

    SeqRack& getRack() { return rack; } // for the host test
    fui::SeqHeader& header() { return rack.getHeader(); } // for the host test
    fui::TrackHeadsView& heads() { return rack.getHeads(); } // for the host test
    fui::SceneGridView& grid() { return rack.getGrid(); } // for the host test
    fui::SeqFooterView& footer() { return rack.getFooter(); } // for the host test
    fui::DeviceFocusView& deviceFocus() { return rack.getDeviceFocus(); } // for the host test

private:
    int clampScene(int s) const;
    void growToFocusSize();
    void shrinkToSessionSize();

    fui::DarkLNF lnf;
    SeqAudioProcessor& proc_;
    SeqRack rack;

    int focusTrack_ = -1, focusScene_ = -1;
    int lastFocusScene_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqEditor)
};
