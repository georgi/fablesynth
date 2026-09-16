#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumUiModel.h"
#include "../../ui/Controls.h"

// Selected-pad FX rack + OUT routing summary — port of src/drum/components/
// {FxRack,OutPanel}.tsx. Six power+knob groups (DRIVE COMP OTT CHORUS DELAY
// REVERB) bound to the pad fx.* params, plus a read-only OUT panel listing
// which pads feed MAIN / AUX 1-4 (now real multi-out routing). Modeled on the
// WT-1 FxPanel (ui/Panels.cpp).
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

    void timerCallback() override;          // OUT panel refresh (sig-diffed)
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuild();
    juce::String routeSignature() const;    // pad->out assignments + pad names
    void paintOutPanel(juce::Graphics&);

    DrumUiModel& proc;
    bool routingOnly_ = false;
    juce::OwnedArray<Group> groups;         // drive comp ott chorus delay reverb
    juce::Rectangle<int> outBounds;
    juce::String lastSig;
    juce::Rectangle<int> padTitleArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumFxRack)
};

} // namespace fui
