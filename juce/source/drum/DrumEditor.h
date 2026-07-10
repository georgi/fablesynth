#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumProcessor.h"
#include "ui/DrumHeader.h"
#include "ui/PadGrid.h"
#include "ui/PadStrip.h"
#include "ui/DrumPanels.h"
#include "../ui/LookAndFeel.h"

// The DR-1 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/drum/drum.css). The editor scales it to the window so the
// layout stays pixel-faithful — same scheme as the WT-1 Rack (PluginEditor.h).
class DrumRack : public juce::Component {
public:
    static constexpr int LW = 1460, LH = 880;
    explicit DrumRack(DrumAudioProcessor&);
    void resized() override;

private:
    // Theme-painted stand-in for the sections Tasks 10-13 mount.
    class Placeholder : public juce::Component {
    public:
        explicit Placeholder(juce::String l) : label(std::move(l)) {}
        void paint(juce::Graphics&) override;
    private:
        juce::String label;
    };

    fui::DrumHeader header;
    fui::PadGrid pads;
    fui::PadStrip padStrip;
    fui::DrumOscPanel oscA, oscB;
    fui::DrumNoisePanel noise;
    fui::DrumPitchEnvPanel pitchEnv;
    fui::DrumAmpEnvPanel ampEnv;
    fui::DrumFilterPanel filter;
    fui::DrumModPanel mod;
    Placeholder selBar{"SELECT BAR"},
                stepSeq{"STEP SEQ"}, fxRack{"FX RACK / OUT"};
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
    DrumRack rack;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumEditor)
};
