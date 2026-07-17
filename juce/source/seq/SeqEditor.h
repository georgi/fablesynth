#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SeqProcessor.h"
#include "ui/SeqHeader.h"
#include "ui/TrackHeadsView.h"
#include "ui/SceneGridView.h"
#include "ui/SeqFooterView.h"
#include "ui/DeviceFocusView.h"
#include "../ui/LookAndFeel.h"

#include <utility>

// The SQ-4 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/seq/seq.css, #seq-rack at its 1460px max-width). The
// editor scales it to the window so the layout stays pixel-faithful — same
// scheme as the WT-1/DR-1/BL-1 racks.
//
// Two layouts share one anatomy (docs/.../sq4-device-focus-design.md §2):
// session mode is header + heads + full scene grid + footer; focus mode is
// header + heads (now a device tab strip, focused head lit) + a single-row
// mini strip for the focused scene + the native device surface filling the
// rest (the footer hides). There is no FLIP animation — the relayout is
// instant, which is the JUCE port of the web's animated collapse.
class SeqRack : public juce::Component {
public:
    static constexpr int LW = 1460, LH = 920;
    explicit SeqRack(SeqAudioProcessor&);
    void resized() override;

    fui::SeqHeader& getHeader() { return header; }
    fui::TrackHeadsView& getHeads() { return trackHeads; }
    fui::SceneGridView& getGrid() { return sceneGrid; }
    fui::SeqFooterView& getFooter() { return footer; }
    fui::DeviceFocusView& getDeviceFocus() { return deviceFocus; }

    void enterFocus(int track, int scene);
    void exitFocus();
    void setFocusScene(int scene);

private:
    fui::SeqHeader header;
    fui::TrackHeadsView trackHeads;
    fui::SceneGridView sceneGrid;
    fui::SeqFooterView footer;
    fui::DeviceFocusView deviceFocus;
    juce::Component hint;

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

    fui::DarkLNF lnf;
    SeqAudioProcessor& proc_;
    SeqRack rack;

    int focusTrack_ = -1, focusScene_ = -1;
    int lastFocusScene_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqEditor)
};
