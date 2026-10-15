#pragma once
// PATTERN PRESET dropdown for the WT-1 / BL-1 sequencer heads — the same
// look as DR-1's StepSeqView combo. The prompt returns after each load so the
// same entry can be picked again.

#include "Theme.h"
#include "../dsp/StepNotation.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace fui {

inline void setupPatternPresetCombo(juce::ComboBox& combo,
                                    const std::vector<fable::NotatedPreset>& presets,
                                    std::function<void(const fable::NotatedPreset&)> load) {
    combo.setTextWhenNothingSelected("PATTERN PRESET");
    combo.setTooltip("Load a pattern preset without changing the sound");
    combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0d1017));
    combo.setColour(juce::ComboBox::textColourId, col::textDim);
    combo.setColour(juce::ComboBox::outlineColourId, col::line);
    for (int i = 0; i < (int)presets.size(); ++i) combo.addItem(presets[(size_t)i].name, i + 1);
    combo.onChange = [&combo, &presets, load = std::move(load)] {
        const int index = combo.getSelectedId() - 1;
        if (index < 0 || index >= (int)presets.size()) return;
        load(presets[(size_t)index]);
        combo.setSelectedId(0, juce::dontSendNotification);
    };
}

} // namespace fui
