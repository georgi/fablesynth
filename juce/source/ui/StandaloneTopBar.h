#pragma once

#include "Controls.h"
#include "Displays.h"
#include "../PluginProcessor.h"

namespace fui {

class TopBar : public juce::Component, private juce::Timer {
public:
    TopBar(juce::AudioProcessorValueTreeState&, FableAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    FableAudioProcessor& proc;
    juce::TextButton prev{"<"}, next{">"};
    juce::ComboBox presets;
    ScopeView scope; SpectrumView spectrum;
    Knob bpm, swing, master;
    juce::Rectangle<int> brandArea, dirtyArea, scopeBox, specBox, statusArea;
    int lastVoices = -1; bool lastMidi = false, lastDirty = false;
};

} // namespace fui
