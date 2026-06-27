#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Panels.h"
#include "ui/LookAndFeel.h"
#include "ui/WavetableEditor.h"

// The rack: all panels laid out at a fixed logical size matching the web CSS
// grid. The editor scales it to the window so the layout stays pixel-faithful.
class Rack : public juce::Component {
public:
    static constexpr int LW = 1400, LH = 1053;
    Rack(juce::AudioProcessorValueTreeState&, FableAudioProcessor&);
    void resized() override;

    // Forwarded from either oscillator panel's ✎ button (arg = osc index).
    std::function<void(int)> onEditTable;
private:
    juce::Rectangle<int> colArea(int c0, int span, int y, int h) const;
    fui::TopBar  topBar;
    fui::OscPanel oscA, oscB;
    fui::UtilPanel util;
    fui::FilterPanel filter;
    fui::EnvPanel env1, env2;
    fui::LfoPanel lfos;
    fui::MatrixPanel matrix;
    fui::FxPanel fx;
};

class FableAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::DragAndDropContainer {
public:
    explicit FableAudioProcessorEditor(FableAudioProcessor&);
    ~FableAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    fui::DarkLNF lnf;
    Rack rack;
    fui::WavetableEditor wtEditor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FableAudioProcessorEditor)
};
