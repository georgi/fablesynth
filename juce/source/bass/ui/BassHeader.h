#pragma once
#include "../../ui/VisualTimer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "BassUiModel.h"
#include "../../ui/BrowserButton.h"
#include "../../ui/Controls.h"
#include "../../ui/Theme.h"
#include "../../ui/TransportButton.h"

// BL-1 header — port of src/bass/components/Header.tsx / bass.css (.bl-header):
// wordmark (acid-green SYNTH), patch stepper (prev/next drive the host
// programs), voice-mode hint, scope, MIDI LED, BPM readout with SYNC tag,
// SWING + VOL knobs. The web's SAVE button and power overlay are dropped
// (host provides state and audio start; factory patches are host programs —
// same trimming as DrumHeader).
namespace fui {

// Post-FX oscilloscope fed by BassAudioProcessor::readScope.
class BassScopeView : public juce::Component, private fui::VisualTimer {
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
class BassBpmReadout : public juce::Component, private fui::VisualTimer {
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
    void paintReadout(juce::Graphics&);
    juce::Image readoutLayer_;
    float readoutScale_ = 0;
    juce::Colour readoutAccent_;
    int cachedShown_ = -1;
    bool cachedSync_ = false;

    int  shown() const;
    void nudge(float deltaNorm);
    BassUiModel& proc;
    juce::RangedAudioParameter* param = nullptr;
    float lastY = 0;
    int   lastShown = -1;
    bool  lastSync = false;
};

class BassHeader : public juce::Component, private fui::VisualTimer {
public:
    explicit BassHeader(BassUiModel&);
    ~BassHeader() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
    // BROWSE opens the patch drawer; the rack owns the drawer and shows its state here.
    std::function<void()> onBrowse;
    void setBrowseOpen(bool open) { browseBtn.setToggleState(open, juce::dontSendNotification); }
private:
    void timerCallback() override;
    void paintHeader(juce::Graphics&);
    juce::Image headerLayer_;
    float headerScale_ = 0;
    juce::Colour headerAccent_;
    juce::String cachedName_;
    bool cachedMidi_ = false, cachedSync_ = false, cachedDirty_ = false;
    BassUiModel& proc;
    juce::TextButton prevBtn{"<"}, nextBtn{">"};
    BrowserButton browseBtn{"BROWSE", BrowserButton::Browse};
    BassScopeView scope;
    BassBpmReadout bpm;
    TransportButton transport;
    Knob swing, vol;
    juce::Rectangle<int> brandArea, patchNameArea, voiceModeArea, scopeBox, midiArea, syncArea;
    bool lastMidi = false, lastSync = false, lastDirty = false;
    int  lastProgram = -1;
};

} // namespace fui
