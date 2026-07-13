#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumProcessor.h"
#include "ui/DrumHeader.h"
#include "ui/PadGrid.h"
#include "ui/PadStrip.h"
#include "ui/DrumPanels.h"
#include "ui/StepSeqView.h"
#include "ui/DrumFxRack.h"
#include "../ui/LookAndFeel.h"

// The DR-1 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/drum/drum.css). The editor scales it to the window so the
// layout stays pixel-faithful — same scheme as the WT-1 Rack (PluginEditor.h).
class DrumDeviceBody : public juce::Component {
public:
    explicit DrumDeviceBody(fui::DrumUiModel&);
    void resized() override;

private:
    fui::PadGrid pads;
    fui::PadStrip padStrip;
    fui::DrumOscPanel oscA, oscB;
    fui::DrumNoisePanel noise;
    fui::DrumPitchEnvPanel pitchEnv;
    fui::DrumAmpEnvPanel ampEnv;
    fui::DrumFilterPanel filter;
    fui::DrumModPanel mod;
    fui::SelBarView selBar;
    fui::StepSeqView stepSeq;
    fui::DrumFxRack fxRack;
};

class DrumRack : public juce::Component {
public:
    static constexpr int LW = 1460, LH = 880;
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

private:
    fui::DarkLNF lnf;
    std::unique_ptr<fui::DrumUiModel> model;
    DrumRack rack;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumEditor)
};
