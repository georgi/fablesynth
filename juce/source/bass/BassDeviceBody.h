#pragma once

#include "ui/BassPanels.h"
#include "ui/PitchSeqView.h"
#include "ui/BassFxRack.h"
#include "../ui/FxChain.h"
#include "../ui/ArpPanel.h"

// Reusable BL-1 machine surface. It depends only on BassUiModel and can be
// composed by either the standalone rack or SQ-4 without processor symbols.
class BassDeviceBody : public juce::Component {
public:
    explicit BassDeviceBody(fui::BassUiModel&);
    void resized() override;
    fui::PitchSeqView& pitchSeq() { return seq; }

private:
    enum class Page { edit, fx, sequencer, arp };

    void selectPage(Page page);

    fui::BassUiModel& model_;
    fui::BassOscPanel osc;
    fui::BassSubPanel sub;
    fui::BassFilterPanel filter;
    fui::BassEnvPanel env;
    fui::BassLfoPanel lfo;
    fui::BassAccentPanel accent;
    fui::BassKeysPanel keys;
    fui::PitchSeqView seq;
    fui::FxChain fxRack;
    fui::ArpPanel arp;
    juce::TextButton editPage_ { "EDIT" };
    juce::TextButton fxPage_ { "FX CHAIN" };
    juce::TextButton sequencerPage_ { "SEQUENCER" };
    juce::TextButton arpPage_ { "ARP" };
    Page page_ = Page::edit;
};
