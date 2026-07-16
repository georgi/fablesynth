#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BassUiModel.h"
#include "../../ui/Controls.h"

// Master FX rack — port of src/bass/components/BassFxRack.tsx. Four
// power+knob groups (DRIVE CHORUS DELAY REVERB) bound to the global fx.*
// params, with the web's dim per-group notes ("POST-ACCENT", "NO COMP ·
// ACCENTS LIVE"). Modeled on DrumFxRack minus the compressor + OUT panel.
namespace fui {

class BassFxRack : public juce::Component {
public:
    explicit BassFxRack(BassUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // One .fx-group: power LED + title head (+ dim note), then a row of Sm knobs.
    struct Group {
        Group(BassUiModel&, const char* fx, const char* title, const char* note,
              std::initializer_list<const char*> knobIds);
        juce::String title, note;
        PowerButton power;
        juce::OwnedArray<Knob> knobs;
        juce::Rectangle<int> bounds, titleArea;
        void layout(juce::Rectangle<int>);
        void paintGroup(juce::Graphics&);
    };

    juce::OwnedArray<Group> groups;         // drive chorus delay reverb
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFxRack)
};

} // namespace fui
