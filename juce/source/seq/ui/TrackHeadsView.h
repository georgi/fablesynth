#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../SeqProcessor.h"
#include "../../ui/Theme.h"

#include <functional>

// SQ-4 track header row — port of src/seq/components/TrackHeads.tsx + seq.css
// (scenes card + one card per track: live LED, name, machine/patch chips,
// mute/solo, xs vol knob). Painted directly and hit-tested in mouseDown, same
// scheme as SeqHeader — the whole slot is a single 1424x60 strip and every
// element is a small click/drag target, not a juce::Component child.
//
// vol-knob authority: SeqProcessor::pollSessionParams() (SeqProcessor.cpp)
// is the one place that writes conductor().setTrackVol — it polls the
// "vol0".."vol3" APVTS params each 30 Hz tick and pushes changed values into
// the conductor. So the conductor's per-track volume is a *read-only mirror*
// of those params from this view's perspective: dragging the knob here drives
// the APVTS param (setValueNotifyingHost, proper begin/end gesture, mirroring
// SeqHeader's master VOL knob) and lets the existing poll carry the value into
// the conductor, instead of calling conductor().setTrackVol directly (which
// the next poll tick would have no reason to touch, but would also leave the
// param and conductor silently disagreeing on host automation).
namespace fui {

class TrackHeadsView : public juce::Component, private juce::Timer {
public:
    explicit TrackHeadsView(SeqAudioProcessor&);
    ~TrackHeadsView() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // Test handles (also the real click targets, wired from mouseDown).
    void muteClick(int t);
    void soloClick(int t);
    void headClick(int t);
    void patchStep(int t, int d);

    // headClick(t) calls onFocusTrack(t) if set (SeqEditor wires it to
    // enterFocus). The heads row is hidden entirely in focus mode (SeqRack::
    // enterFocus/exitFocus), so there is no focused-track glow or back-button
    // state here any more -- the back affordance lives in SceneGridView's
    // focus strip now.
    std::function<void(int)> onFocusTrack;

private:
    void timerCallback() override { repaint(); }

    juce::RangedAudioParameter* volParam(int t) const;
    float volValue(int t) const;

    void paintScenesCard(juce::Graphics&);
    void paintTrack(juce::Graphics&, int t);
    void paintKnob(juce::Graphics&, juce::Rectangle<int>, float v);

    SeqAudioProcessor& proc;

    juce::Rectangle<int> sceneCard;
    juce::Rectangle<int> trackHead[4], led[4], nameRow[4], patchPrev[4], patchNext[4],
        muteBtn[4], soloBtn[4], volKnob[4];

    int hoverTrack_ = -1;   // name row hovered -> show the edit (✎) affordance

    enum class Drag { None, Vol };
    Drag dragging_ = Drag::None;
    int dragTrack_ = -1;
    float lastY_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackHeadsView)
};

} // namespace fui
