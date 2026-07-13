#pragma once

#include "ui/PadGrid.h"
#include "ui/PadStrip.h"
#include "ui/DrumPanels.h"
#include "ui/StepSeqView.h"
#include "ui/DrumFxRack.h"

// Reusable DR-1 machine surface. It depends only on DrumUiModel and can be
// composed by either the standalone rack or SQ-4 without processor symbols.
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
