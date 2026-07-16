#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Panels.h"
#include "ui/LookAndFeel.h"
#include "ui/NoteSeqView.h"
#include "ui/WavetableEditor.h"
#include "ui/WtDeviceBody.h"
#include "ui/StandaloneTopBar.h"

// The rack: all panels laid out at a fixed logical size matching the web CSS
// grid. The editor scales it to the window so the layout stays pixel-faithful.
class Rack : public juce::Component {
public:
    // Compact WT rows plus the NOTE SEQ row, matching the web rack geometry.
    static constexpr int LW = 1520, LH = 982;
    Rack(fui::WtUiModel&, juce::AudioProcessorValueTreeState&, FableAudioProcessor&);
    void resized() override;

    // Forwarded from either oscillator panel's ✎ button (arg = osc index).
    std::function<void(int)> onEditTable;

    fui::NoteSeqView& noteSeq() { return body.noteSeq(); }   // exposed for the host test
private:
    fui::TopBar  topBar;
    WtDeviceBody body;
};

class FableAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::DragAndDropContainer {
public:
    explicit FableAudioProcessorEditor(FableAudioProcessor&);
    ~FableAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    Rack& getRack() { return rack; }              // exposed for the host test

private:
    fui::DarkLNF lnf;
    std::unique_ptr<fui::WtUiModel> model;
    Rack rack;
    fui::WavetableEditor wtEditor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FableAudioProcessorEditor)
};
