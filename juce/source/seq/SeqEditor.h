#pragma once
#include "../ui/VisualTimer.h"
#include "../agent/AgentPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include "SeqProcessor.h"
#include "ui/SeqHeader.h"
#include "ui/TrackHeadsView.h"
#include "ui/SceneGridView.h"
#include "ui/SeqFooterView.h"
#include "ui/DeviceFocusView.h"
#include "ui/MasterFxView.h"
#include "ui/SceneChordsOverlay.h"
#include "../ui/LookAndFeel.h"

#include <functional>
#include <utility>

// The web's .sq-hint legend line (SeqApp.tsx): tiny contextual key/mouse help
// under the rack. Text comes from a provider so the quant value stays current;
// 2 Hz repaint is plenty for copy changes.
class HintBar : public juce::Component, private fui::VisualTimer {
public:
    HintBar() { startVisualTimer(*this, 2); setInterceptsMouseClicks(false, false); }
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

// The SQ-4 rack: all sections laid out at a fixed logical size derived from
// the web grid, but compacted horizontally for the native editor. The editor
// scales it to the window so the layout stays pixel-faithful — same
// scheme as the WT-1/DR-1/BL-1 racks.
//
// Two layouts share one anatomy (docs/.../sq4-device-focus-design.md §2,
// re-pitched 2026-07-21 to match the web's left-rail focus mode): session
// mode is header + heads + full scene grid + footer; focus mode keeps the
// header and the heads row (the heads double as the instrument switcher, the
// open one ringed) and replaces the grid + footer with a narrow vertical
// launcher rail (SceneGridView in its singleRow_ mode) alongside the native
// device surface, which takes all remaining width. The switch between the
// two is instant — see applyLayout() — matching the web, which has no
// focus enter/exit animation either.
class SeqRack : public juce::Component {
public:
    // One logical height for both views, so opening a device never resizes
    // the window: the session grid's height (six 73 px rows). Top to bottom
    // (see applyLayout()): header 14 + 44; heads row 67 + 60; content top 136;
    // content 550 (taller device bodies such as BASS scroll); gap 8; hint 14;
    // margin 14. 136 + 550 + 8 + 14 + 14 = 722.
    static constexpr int LW = 1290, LH = 722;
    explicit SeqRack(SeqAudioProcessor&);
    void resized() override;
    int logicalHeight() const { return LH; }

    fui::SeqHeader& getHeader() { return header; }
    fui::TrackHeadsView& getHeads() { return trackHeads; }
    fui::SceneGridView& getGrid() { return sceneGrid; }
    fui::SeqFooterView& getFooter() { return footer; }
    fui::DeviceFocusView& getDeviceFocus() { return deviceFocus; }
    HintBar& getHint() { return hint; }
    fui::SceneChordsOverlay& getSceneChords() { return sceneChords; }

    void enterFocus(int track, int scene);
    void exitFocus();
    void setFocusScene(int scene);
    void toggleMasterFx();
    bool masterFxOpen() const { return masterFxMode_; }

private:
    fui::SeqHeader header;
    fui::TrackHeadsView trackHeads;
    fui::SceneGridView sceneGrid;
    fui::SeqFooterView footer;
    fui::DeviceFocusView deviceFocus;
    fui::MasterFxView masterFx;
    HintBar hint;
    fui::SceneChordsOverlay sceneChords; // scene progression picker, over the whole rack

    void applyLayout();

    bool focusMode_ = false;
    bool masterFxMode_ = false;
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

    fable::AgentOverlay agentOverlay;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqEditor)
};
