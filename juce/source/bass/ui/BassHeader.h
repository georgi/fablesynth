#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BassUiModel.h"
#include "../../ui/Controls.h"
#include "../../ui/Theme.h"

// BL-1 header — port of src/bass/components/Header.tsx / bass.css (.bl-header):
// wordmark (acid-green SYNTH), patch stepper (prev/next drive the host
// programs), voice-mode hint, scope, MIDI LED, BPM readout with SYNC tag,
// SWING + VOL knobs. The web's SAVE button and power overlay are dropped
// (host provides state and audio start; factory patches are host programs —
// same trimming as DrumHeader).
namespace fui {

// Post-FX oscilloscope fed by BassAudioProcessor::readScope.
class BassScopeView : public juce::Component, private juce::Timer {
public:
    explicit BassScopeView(BassUiModel&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override; // repaints only while audio is present
    BassUiModel& proc;
    bool wasActive_ = true;
};

// BPM readout — drag-to-edit integer bound to seq.bpm. While the host reports
// a tempo it displays getHostBpm() and ignores edits; BassHeader lights SYNC.
class BassBpmReadout : public juce::Component, private juce::Timer {
public:
    explicit BassBpmReadout(BassUiModel&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void timerCallback() override;
    int  shown() const;
    void nudge(float deltaNorm);
    BassUiModel& proc;
    juce::RangedAudioParameter* param = nullptr;
    float lastY = 0;
    int   lastShown = -1;
    bool  lastSync = false;
};

class BassHeader : public juce::Component, private juce::Timer {
public:
    explicit BassHeader(BassUiModel&);
    ~BassHeader() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    BassUiModel& proc;
    juce::TextButton prevBtn{"<"}, nextBtn{">"};
    BassScopeView scope;
    BassBpmReadout bpm;
    Knob swing, vol;
    juce::Rectangle<int> brandArea, patchNameArea, voiceModeArea, scopeBox, midiArea, syncArea;
    bool lastMidi = false, lastSync = false, lastDirty = false;
    int  lastProgram = -1;
};

} // namespace fui
