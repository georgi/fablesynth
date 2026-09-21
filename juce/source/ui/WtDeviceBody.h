#pragma once

#include "Panels.h"
#include "NoteSeqView.h"
#include "WtUiModel.h"
#include "FxChain.h"
#include "ArpPanel.h"

// Reusable WT-1 sound-design and note-sequencer surface. The standalone
// editor adds its own header around this body; SQ-4 embeds the body directly.
class WtDeviceBody : public juce::Component {
public:
    static constexpr int LW = 1520, LH = 893;

    explicit WtDeviceBody(fui::WtUiModel&,
                          std::function<HostTransport()> transportProvider = {});
    void resized() override;

    std::function<void(int)> onEditTable;
    fui::NoteSeqView& noteSeq() { return seq; }

private:
    enum class Page { edit, fx, sequencer, arp };

    juce::Rectangle<int> colArea(int c0, int span, int y, int h) const;
    void selectPage(Page page);

    fui::WtUiModel& model_;
    fui::OscPanel oscA, oscB;
    fui::UtilPanel util;
    fui::FilterPanel filter;
    fui::EnvPanel env1, env2;
    fui::LfoPanel lfos;
    fui::MatrixPanel matrix;
    fui::FxChain fx;
    fui::NoteSeqView seq;
    fui::ArpPanel arp;
    juce::TextButton editPage_ { "EDIT" };
    juce::TextButton fxPage_ { "FX CHAIN" };
    juce::TextButton sequencerPage_ { "SEQUENCER" };
    juce::TextButton arpPage_ { "ARP" };
    Page page_ = Page::edit;
};
