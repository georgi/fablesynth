#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumUiModel.h"
#include "../../ui/Controls.h"

// Selected-pad mix strip — port of src/drum/components/PadStrip.tsx:
// panel head "PAD" with CHOKE, then LVL / PAN / V→LVL / V→MOD knobs. Output
// routing lives in the dedicated one-row OUT selector below. All five controls
// bind to pad<sel>.* parameter ids, so they are
// destroyed and recreated whenever the processor broadcasts a selection
// change (the APVTS attachment scheme rebinding by id string).
namespace fui {

class PadStrip : public juce::Component, private juce::ChangeListener {
public:
    explicit PadStrip(DrumUiModel&);
    ~PadStrip() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuild();

    DrumUiModel& proc;
    std::unique_ptr<Stepper> choke;
    juce::OwnedArray<Knob> knobs;   // lvl, pan, v2l, v2m
    juce::Rectangle<int> headArea, chokeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadStrip)
};

} // namespace fui
