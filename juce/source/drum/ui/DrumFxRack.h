#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumUiModel.h"
#include "../../ui/Controls.h"

// Selected-pad FX rack + OUT routing control. The compact routing-only form
// is one row bound to the selected pad's MAIN / AUX 1-4 parameter. The full
// form retains the six power+knob FX groups and routing summary.
namespace fui {

class DrumFxRack : public juce::Component, private juce::Timer, private juce::ChangeListener {
public:
    explicit DrumFxRack(DrumUiModel&, bool routingOnly = false);
    ~DrumFxRack() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // One .fx-group: power LED + title head, then a row of Sm knobs.
    struct Group {
        Group(DrumUiModel&, const char* fx, const char* title,
              std::initializer_list<const char*> knobIds);
        juce::String title;
        PowerButton power;
        juce::OwnedArray<Knob> knobs;
        juce::Rectangle<int> bounds, titleArea;
        void layout(juce::Rectangle<int>);
        void paintGroup(juce::Graphics&);
    };

    void timerCallback() override;          // full OUT summary refresh (sig-diffed)
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuild();
    juce::String routeSignature() const;    // pad->out assignments + pad names
    void paintOutPanel(juce::Graphics&);

    DrumUiModel& proc;
    bool routingOnly_ = false;
    juce::OwnedArray<Group> groups;         // drive comp ott chorus delay reverb
    std::unique_ptr<Stepper> outSelector;    // compact selected-pad routing control
    juce::Rectangle<int> outBounds;
    juce::String lastSig;
    juce::Rectangle<int> padTitleArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumFxRack)
};

} // namespace fui
