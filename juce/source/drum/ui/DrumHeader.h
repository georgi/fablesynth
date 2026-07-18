#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumUiModel.h"
#include "../../ui/Controls.h"
#include "../../ui/Theme.h"

// DR-1 header — port of src/drum/components/Header.tsx / drum.css (.dr-header):
// wordmark, kit stepper (prev/next drive the host programs), scope, MIDI LED,
// BPM readout with SYNC tag, SWING + VOL knobs. The web's STEP/PADS mode
// toggle, SAVE button and power overlay are dropped (host provides transport,
// MIDI and audio start; factory kits are host programs — spec §UI).
namespace fui {

// Post-FX oscilloscope fed by DrumAudioProcessor::readScope. Same drawing as
// the WT-1 ScopeView (Displays.cpp), which is hard-bound to FableAudioProcessor.
class DrumScopeView : public juce::Component, private juce::Timer {
public:
    explicit DrumScopeView(DrumUiModel&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override; // repaints only while audio is present
    DrumUiModel& proc;
    bool wasActive_ = true;
};

// BPM readout — drag-to-edit integer bound to seq.bpm (web shows a stepper;
// the plan swaps it for a drag box). While the host reports a tempo it
// displays getHostBpm() and ignores edits; DrumHeader lights the SYNC tag.
class BpmReadout : public juce::Component, private juce::Timer {
public:
    explicit BpmReadout(DrumUiModel&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void timerCallback() override;
    int  shown() const;                    // host bpm when synced, else the param
    void nudge(float deltaNorm);
    DrumUiModel& proc;
    juce::RangedAudioParameter* param = nullptr;
    float lastY = 0;
    int   lastShown = -1;
    bool  lastSync = false;
};

class DrumHeader : public juce::Component, private juce::Timer {
public:
    explicit DrumHeader(DrumUiModel&);
    ~DrumHeader() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    DrumUiModel& proc;
    juce::TextButton prevBtn{"<"}, nextBtn{">"};
    DrumScopeView scope;
    BpmReadout bpm;
    Knob swing, vol;
    juce::Rectangle<int> brandArea, kitLabelArea, kitNameArea, scopeBox, midiArea, syncArea;
    bool lastMidi = false, lastSync = false, lastDirty = false;
    int  lastProgram = -1;
};

} // namespace fui
