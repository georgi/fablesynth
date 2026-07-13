#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "BassProcessor.h"
#include "ui/BassHeader.h"
#include "ui/BassPanels.h"
#include "ui/PitchSeqView.h"
#include "ui/BassFxRack.h"
#include "../ui/LookAndFeel.h"

// The BL-1 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/bass/bass.css). The editor scales it to the window so the
// layout stays pixel-faithful — same scheme as the WT-1/DR-1 racks.
class BassDeviceBody : public juce::Component {
public:
    explicit BassDeviceBody(fui::BassUiModel&);
    void resized() override;
    fui::PitchSeqView& pitchSeq() { return seq; }

private:
    fui::BassOscPanel osc;
    fui::BassSubPanel sub;
    fui::BassFilterPanel filter;
    fui::BassEnvPanel env;
    fui::BassLfoPanel lfo;
    fui::BassAccentPanel accent;
    fui::BassKeysPanel keys;
    fui::PitchSeqView seq;
    fui::BassFxRack fxRack;
};

class BassRack : public juce::Component {
public:
    static constexpr int LW = 1460, LH = 931;
    explicit BassRack(fui::BassUiModel&);
    void resized() override;

    fui::PitchSeqView& pitchSeq() { return body.pitchSeq(); }   // for the host test

private:
    fui::BassHeader header;
    BassDeviceBody body;
};

class BassEditor : public juce::AudioProcessorEditor {
public:
    explicit BassEditor(BassAudioProcessor&);
    ~BassEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    BassRack& getRack() { return rack; }            // for the host test

private:
    fui::DarkLNF lnf;
    std::unique_ptr<fui::BassUiModel> model;
    BassRack rack;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassEditor)
};
