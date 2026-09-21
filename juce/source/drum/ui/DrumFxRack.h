#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumUiModel.h"
#include "../../ui/Controls.h"

// Two independently recalled FX layers: a selected-pad insert and the
// post-mix drum-group strip. Both reuse the same module controls; OUT belongs
// only to the selected-pad layer.
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
        Group(DrumUiModel&, const juce::String& prefix, const char* fx, const char* title,
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
    bool groupMode_ = false;
    juce::TextButton padFxButton_ { "PAD FX" }, groupFxButton_ { "GROUP FX" };
    juce::OwnedArray<Group> groups;         // drive comp ott chorus delay reverb
    std::unique_ptr<Stepper> outSelector;    // compact selected-pad routing control
    juce::Rectangle<int> outBounds;
    juce::String lastSig;
    juce::Rectangle<int> padTitleArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumFxRack)
};

} // namespace fui
