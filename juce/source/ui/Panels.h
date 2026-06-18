#pragma once
#include "Controls.h"
#include "Displays.h"
#include "../WavetableView.h"
#include "../PluginProcessor.h"

// Rack panels — 1:1 ports of src/components/panels/*.tsx, laid out to match the
// CSS grid in src/index.css.
namespace fui {

using APVTS = juce::AudioProcessorValueTreeState;

// Shared: panel background + section header (power led + title + right widget).
void paintPanelBg(juce::Graphics&, juce::Component&);
void paintHeaderTitle(juce::Graphics&, juce::Rectangle<int>, const juce::String&, juce::Colour);

class OscPanel : public juce::Component {
public:
    OscPanel(APVTS&, FableAudioProcessor&, int oscIndex, juce::String prefix, Accent, juce::String title);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    juce::String title, prefix; Accent accent;
    PowerButton power; Stepper tableStep; WavetableView wt; VSlider pos;
    juce::OwnedArray<Knob> knobs;
    juce::Rectangle<int> titleArea;
};

class UtilPanel : public juce::Component {
public:
    explicit UtilPanel(APVTS&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    PowerButton subPow, noisePow; Stepper subShape, noiseType;
    Knob subOct, subLevel, noiseLevel;
    juce::Rectangle<int> subHead, noiseHead;
};

class FilterPanel : public juce::Component {
public:
    explicit FilterPanel(APVTS&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Block {
        Block(APVTS&, juce::String prefix, juce::String label);
        juce::String label;
        PowerButton power; Stepper type;
        juce::OwnedArray<Knob> knobs;
        juce::Rectangle<int> labelArea;
        void layout(juce::Rectangle<int>);
        void paintLabel(juce::Graphics&);
    };
    Stepper route; FilterView view;
    Block f1, f2;
    juce::Rectangle<int> titleArea;
};

class EnvPanel : public juce::Component {
public:
    EnvPanel(APVTS&, juce::String base, juce::String title, juce::Colour viewAccent, Accent knobAccent);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    juce::String title;
    EnvView view; juce::OwnedArray<Knob> knobs;
    juce::Rectangle<int> titleArea;
};

class LfoPanel : public juce::Component {
public:
    explicit LfoPanel(APVTS&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Block {
        Block(APVTS&, juce::String id, juce::String title, Accent);
        juce::String title;
        Stepper shape; LfoView view; Knob rate;
        juce::Rectangle<int> titleArea;
        void layout(juce::Rectangle<int>);
        void paintTitle(juce::Graphics&);
    };
    Block l1, l2;
};

class MatrixPanel : public juce::Component {
public:
    explicit MatrixPanel(APVTS&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Row {
        Row(APVTS&, int slot);
        juce::ComboBox src, dst;
        std::unique_ptr<APVTS::ComboBoxAttachment> srcAtt, dstAtt;
        std::unique_ptr<Knob> amt;
    };
    juce::OwnedArray<Row> rows;
    juce::Rectangle<int> titleArea;
};

class FxPanel : public juce::Component {
public:
    explicit FxPanel(APVTS&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Module {
        Module(APVTS&, juce::String fx, juce::String title, juce::StringArray knobIds);
        juce::String title;
        PowerButton power;
        juce::OwnedArray<Knob> knobs;
        juce::Rectangle<int> bounds, titleArea;
        void layout(juce::Rectangle<int>);
        void paintModule(juce::Graphics&);
    };
    juce::OwnedArray<Module> modules;
};

class TopBar : public juce::Component, private juce::Timer {
public:
    TopBar(APVTS&, FableAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    FableAudioProcessor& proc;
    juce::TextButton prev{"<"}, next{">"}, save{"SAVE"};
    juce::ComboBox presets;
    ScopeView scope; SpectrumView spectrum;
    Knob master;
    juce::Rectangle<int> brandArea, scopeBox, specBox, statusArea;
    int lastVoices = -1; bool lastMidi = false;
};

} // namespace fui
