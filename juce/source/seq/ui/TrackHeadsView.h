#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../SeqProcessor.h"
#include "../../ui/Theme.h"

#include <functional>

// SQ-4 track header row — port of src/seq/components/TrackHeads.tsx + seq.css
// (scenes card + one card per track: live LED, name, machine chip, patch
// label, mute/solo, xs vol knob). Painted directly and hit-tested in
// mouseDown, same scheme as SeqHeader — the whole slot is a single 1424x60
// strip and every element is a small click/drag target, not a
// juce::Component child.
//
// The row stays visible in focus mode and doubles as the instrument switcher
// there: clicking a head opens that device, and the open one carries a
// track-tinted ring (setFocusTrack / .sq-track-head.active). The per-head
// patch stepper the web dropped is gone from the row too — patch changes go
// through DeviceFocusView's selector — but patchStep() survives as an action
// because the host test drives it directly.
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
    // enterFocus). The back affordance lives in SceneGridView's launcher rail.
    std::function<void(int)> onFocusTrack;

    // Which track is open in focus mode (-1 in session mode). Drives the
    // active ring only; clicking any head still switches focus.
    void setFocusTrack(int t);

private:
    void timerCallback() override { repaint(); }

    juce::RangedAudioParameter* volParam(int t) const;
    float volValue(int t) const;

    void paintScenesCard(juce::Graphics&);
    void paintTrack(juce::Graphics&, int t);
    void paintKnob(juce::Graphics&, juce::Rectangle<int>, float v);

    SeqAudioProcessor& proc;

    juce::Rectangle<int> sceneCard;
    // idCol is the whole clickable name/patch block (web: .sq-track-id-btn);
    // nameRow / patchRow are its two text lines.
    juce::Rectangle<int> trackHead[4], led[4], idCol[4], nameRow[4], patchRow[4],
        muteBtn[4], soloBtn[4], volKnob[4];

    int hoverTrack_ = -1;   // id column hovered -> show the edit (✎) affordance
    int focusTrack_ = -1;   // open device -> active ring

    enum class Drag { None, Vol };
    Drag dragging_ = Drag::None;
    int dragTrack_ = -1;
    float lastY_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackHeadsView)
};

} // namespace fui
