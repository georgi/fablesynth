#include "PluginEditor.h"
#include "dsp/Presets.h"

FableAudioProcessorEditor::FableAudioProcessorEditor(FableAudioProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p),
      viewA(p, 0, juce::Colour(0xff4de8ff)), viewB(p, 1, juce::Colour(0xffffa14d)),
      generic(p) {
    title.setText("FABLESYNTH WT-1", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, juce::Colour(0xff4de8ff));
    addAndMakeVisible(title);

    for (int i = 0; i < (int)fable::factoryPresets().size(); ++i)
        presets.addItem(fable::factoryPresets()[i].name, i + 1);
    presets.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.onChange = [this] { proc.setCurrentProgram(presets.getSelectedId() - 1); };
    addAndMakeVisible(presets);

    addAndMakeVisible(viewA);
    addAndMakeVisible(viewB);
    addAndMakeVisible(generic);
    setResizable(true, true);
    setSize(640, 860);
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

    auto views = r.removeFromTop(180).reduced(8, 6);
    const int gap = 8;
    auto left = views.removeFromLeft((views.getWidth() - gap) / 2);
    views.removeFromLeft(gap);
    viewA.setBounds(left);
    viewB.setBounds(views);

    generic.setBounds(r);
}
