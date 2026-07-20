#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumProcessor.h"
#include "DrumDeviceBody.h"
#include "ui/DrumHeader.h"
#include "../ui/LookAndFeel.h"

// The DR-1 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/drum/drum.css). The editor scales it to the window so the
// layout stays pixel-faithful — same scheme as the WT-1 Rack (PluginEditor.h).
class DrumRack : public juce::Component {
public:
    // LH grew from 880 when the step sequencer went from one lane to all 16
    // (drum.css .dr-lanes); every panel above keeps its original geometry.
    static constexpr int LW = 1460, LH = 1174;
    explicit DrumRack(fui::DrumUiModel&);
    void resized() override;

private:
    fui::DrumHeader header;
    DrumDeviceBody body;
};

class DrumEditor : public juce::AudioProcessorEditor,
                   public juce::DragAndDropContainer {
public:
    explicit DrumEditor(DrumAudioProcessor&);
    ~DrumEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    DrumRack& getRack() { return rack; }

private:
    fui::DarkLNF lnf;
    std::unique_ptr<fui::DrumUiModel> model;
    DrumRack rack;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumEditor)
};
