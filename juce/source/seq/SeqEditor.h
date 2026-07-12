#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SeqProcessor.h"
#include "../ui/LookAndFeel.h"

// The SQ-4 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/seq/seq.css, #seq-rack at its 1460px max-width). The
// editor scales it to the window so the layout stays pixel-faithful — same
// scheme as the WT-1/DR-1/BL-1 racks. Tasks 10-13 replace the placeholder
// children below with the real views; this task only fixes their geometry.
class SeqRack : public juce::Component {
public:
    static constexpr int LW = 1460, LH = 920;
    explicit SeqRack(SeqAudioProcessor&);
    void resized() override;

private:
    juce::Component header, trackHeads, sceneGrid, footer, hint, clipEdit;
};

class SeqEditor : public juce::AudioProcessorEditor {
public:
    explicit SeqEditor(SeqAudioProcessor&);
    ~SeqEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    SeqRack& getRack() { return rack; } // for the host test

private:
    fui::DarkLNF lnf;
    SeqRack rack;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqEditor)
};
