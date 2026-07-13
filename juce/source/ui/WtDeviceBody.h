#pragma once

#include "Panels.h"
#include "NoteSeqView.h"
#include "WtUiModel.h"

// Reusable WT-1 sound-design and note-sequencer surface. The standalone
// editor adds its own header around this body; SQ-4 embeds the body directly.
class WtDeviceBody : public juce::Component {
public:
    static constexpr int LW = 1400, LH = 1243;

    explicit WtDeviceBody(fui::WtUiModel&,
                          std::function<HostTransport()> transportProvider = {});
    void resized() override;

    std::function<void(int)> onEditTable;
    fui::NoteSeqView& noteSeq() { return seq; }

private:
    juce::Rectangle<int> colArea(int c0, int span, int y, int h) const;

    fui::OscPanel oscA, oscB;
    fui::UtilPanel util;
    fui::FilterPanel filter;
    fui::EnvPanel env1, env2;
    fui::LfoPanel lfos;
    fui::MatrixPanel matrix;
    fui::FxPanel fx;
    fui::NoteSeqView seq;
};
