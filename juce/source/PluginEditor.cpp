#include "PluginEditor.h"
#include "dsp/Presets.h"

FableAudioProcessorEditor::FableAudioProcessorEditor(FableAudioProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), generic(p) {
    title.setText("FABLESYNTH WT-1", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, juce::Colour(0xff4de8ff));
    addAndMakeVisible(title);

    for (int i = 0; i < (int)fable::factoryPresets().size(); ++i)
        presets.addItem(fable::factoryPresets()[i].name, i + 1);
    presets.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.onChange = [this] { proc.setCurrentProgram(presets.getSelectedId() - 1); };
    addAndMakeVisible(presets);

    addAndMakeVisible(generic);
    setResizable(true, true);
    setSize(620, 720);
}

void FableAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0a0c10));
    g.setColour(juce::Colour(0xff14181f));
    g.fillRect(getLocalBounds().removeFromTop(44));
}

void FableAudioProcessorEditor::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(44).reduced(8, 6);
    title.setBounds(top.removeFromLeft(230));
    presets.setBounds(top.removeFromRight(220));
    generic.setBounds(r);
}
