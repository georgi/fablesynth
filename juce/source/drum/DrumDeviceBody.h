#pragma once

#include "ui/PadGrid.h"
#include "ui/PadStrip.h"
#include "ui/DrumPanels.h"
#include "ui/StepSeqView.h"
#include "ui/DrumFxRack.h"
#include "../ui/FxChain.h"

// Reusable DR-1 machine surface. It depends only on DrumUiModel and can be
// composed by either the standalone rack or SQ-4 without processor symbols.
class DrumDeviceBody : public juce::Component, private juce::ChangeListener {
public:
    explicit DrumDeviceBody(fui::DrumUiModel&);
    ~DrumDeviceBody() override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    fui::DrumUiModel& model_;
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
    fui::FxChain fxRack;
    fui::DrumFxRack routing;
    fui::DevicePageTabs pages;
};
