#pragma once
#include "../agent/AgentPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumProcessor.h"
#include "DrumDeviceBody.h"
#include "ui/DrumHeader.h"
#include "../ui/LookAndFeel.h"

// The DR-1 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/drum/drum.css). The editor scales it to the window so the
// layout stays pixel-faithful — same scheme as the WT-1 Rack (PluginEditor.h).
class DrumRack : public juce::Component {
public:
    // Reference extent for the editor and FX snapshots. Sequencer expands further.
    static constexpr int LW = 1460, LH = DrumDeviceBody::workspaceHeight;
    explicit DrumRack(fui::DrumUiModel&);
    void resized() override;
    void setDisplayScale(float scale) { body.setDisplayScale(scale); }
    int logicalHeight() const { return body.preferredHeight(); }
    std::function<void()> onPreferredHeightChanged;

private:
    fui::DrumHeader header;
    DrumDeviceBody body;
};

class DrumEditor : public juce::AudioProcessorEditor,
                   public juce::DragAndDropContainer {
public:
    explicit DrumEditor(DrumAudioProcessor&);
    ~DrumEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    DrumRack& getRack() { return rack; }

private:
    void fitWindowToPage();
    fui::DarkLNF lnf;
    std::unique_ptr<fui::DrumUiModel> model;
    DrumRack rack;
    fable::AgentOverlay agentOverlay;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumEditor)
};
