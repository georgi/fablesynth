#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../SeqProcessor.h"
#include "../../ui/Theme.h"

#include <functional>

// SQ-4 launcher grid — port of src/seq/components/SceneRow.tsx + seq.css.
// Six scene rows, each a scene card (launch/mute/stop, dots, status) followed
// by one clip cell per track. Painted directly and hit-tested in mouseDown,
// same scheme as TrackHeadsView -- the whole slot is a single 1424x630 strip.
//
// Local geometry (matches TrackHeadsView.cpp's column table): row r at
// y = r*105, h=96; scene card x=0 w=218; clip cell for track t at
// x=218+9+t*(292+9) w=292.
namespace fui {

class SceneGridView : public juce::Component, private juce::Timer {
public:
    explicit SceneGridView(SeqAudioProcessor&);
    ~SceneGridView() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    // Test handles (also the real click targets, wired from mouseDown).
    void cellClick(int s, int t);
    void cellRightClick(int s, int t);
    void cellEditClick(int s, int t);
    void sceneLaunch(int s);
    void sceneMute(int s);
    void sceneStop(int s);

    std::function<void(int, int)> onEditClip;

    // Test hook: true when cell (s,t) is live (owned by s) and fully audible
    // (not muted/soloed-out/scene-muted) -- the same gate paintFilledCell
    // uses to dim the cell and show MUTED, exposed without pixel probing.
    bool cellAudible(int s, int t) const;

    // Focus mini-strip: render only scene s's row, with a compact 2x3 rail of
    // numbered scene chips immediately to its left.
    void setSingleRow(int s) { singleRow_ = true; singleRowScene_ = s; resized(); repaint(); }
    void clearSingleRow() { singleRow_ = false; resized(); repaint(); }
    std::function<void(int)> onRailScene;

private:
    static constexpr int kScenes = 6, kTracks = 4;

    void timerCallback() override { repaint(); }

    bool isPassThrough(int s, int t) const;

    void layoutRow(int s);
    void layoutRail();

    void paintSceneCard(juce::Graphics&, int s);
    void paintCell(juce::Graphics&, int s, int t);
    void paintEmptyCell(juce::Graphics&, int s, int t);
    void paintFilledCell(juce::Graphics&, int s, int t);
    void paintRail(juce::Graphics&);

    SeqAudioProcessor& proc;

    bool singleRow_ = false;
    int singleRowScene_ = 0;

    // scene-card sub-regions
    juce::Rectangle<int> sceneCardR[kScenes], launchBtn[kScenes], muteBtnR[kScenes], stopBtnR[kScenes],
        idArea[kScenes], dotsArea[kScenes];
    // clip-cell regions
    juce::Rectangle<int> cellR[kScenes][kTracks], editGlyph[kScenes][kTracks];
    // rail (single-row mode)
    juce::Rectangle<int> railArea, railChip[kScenes];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneGridView)
};

} // namespace fui
