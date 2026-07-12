#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SeqProcessor.h"
#include "ui/SeqHeader.h"
#include "ui/TrackHeadsView.h"
#include "ui/SceneGridView.h"
#include "ui/SeqFooterView.h"
#include "../ui/LookAndFeel.h"

// The SQ-4 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/seq/seq.css, #seq-rack at its 1460px max-width). The
// editor scales it to the window so the layout stays pixel-faithful — same
// scheme as the WT-1/DR-1/BL-1 racks. Tasks 11-13 replace the remaining
// placeholder children below with the real views; this task only fixes their
// geometry.
class SeqRack : public juce::Component {
public:
    static constexpr int LW = 1460, LH = 920;
    explicit SeqRack(SeqAudioProcessor&);
    void resized() override;

    fui::SeqHeader& getHeader() { return header; }
    fui::TrackHeadsView& getHeads() { return trackHeads; }
    fui::SceneGridView& getGrid() { return sceneGrid; }
    fui::SeqFooterView& getFooter() { return footer; }

private:
    fui::SeqHeader header;
    fui::TrackHeadsView trackHeads;
    fui::SceneGridView sceneGrid;
    fui::SeqFooterView footer;
    juce::Component hint, clipEdit;
};

class SeqEditor : public juce::AudioProcessorEditor {
public:
    explicit SeqEditor(SeqAudioProcessor&);
    ~SeqEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    SeqRack& getRack() { return rack; } // for the host test
    fui::SeqHeader& header() { return rack.getHeader(); } // for the host test
    fui::TrackHeadsView& heads() { return rack.getHeads(); } // for the host test
    fui::SceneGridView& grid() { return rack.getGrid(); } // for the host test
    fui::SeqFooterView& footer() { return rack.getFooter(); } // for the host test

private:
    fui::DarkLNF lnf;
    SeqRack rack;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqEditor)
};
