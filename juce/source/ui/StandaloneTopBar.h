#pragma once
#include "VisualTimer.h"

#include "BrowserButton.h"
#include "Controls.h"
#include "Displays.h"
#include "TransportButton.h"
#include "../PluginProcessor.h"

namespace fui {

class TopBar : public juce::Component, private fui::VisualTimer {
public:
    TopBar(ParameterSource, FableAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    // BROWSE opens the preset drawer; the rack owns the drawer and shows its state here.
    std::function<void()> onBrowse;
    void refreshProgram() { presets.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    void setBrowseOpen(bool open) { browse.setToggleState(open, juce::dontSendNotification); }
private:
    void timerCallback() override;
    void paintStatic(juce::Graphics&);
    void paintDynamic(juce::Graphics&);
    juce::Image staticLayer, dynamicLayer;
    float staticScale_ = 0, dynamicScale_ = 0;
    int cachedVoices_ = -1;
    bool cachedMidi_ = false, cachedDirty_ = false;
    FableAudioProcessor& proc;
    juce::TextButton prev{"<"}, next{">"};
    BrowserButton browse{"BROWSE", BrowserButton::Browse};
    juce::ComboBox presets;
    ScopeView scope; SpectrumView spectrum;
    TransportButton transport;
    Knob bpm, swing, master;
    juce::Rectangle<int> brandArea, dirtyArea, scopeBox, specBox, statusArea;
    int lastVoices = -1; bool lastMidi = false, lastDirty = false;
};

} // namespace fui
