#pragma once

#include "ui/BassPanels.h"
#include "ui/PitchSeqView.h"
#include "ui/BassFxRack.h"

// Reusable BL-1 machine surface. It depends only on BassUiModel and can be
// composed by either the standalone rack or SQ-4 without processor symbols.
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
