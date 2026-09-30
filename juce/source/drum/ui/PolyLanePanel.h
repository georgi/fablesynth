#pragma once
#include "DrumUiModel.h"
#include "../../ui/Theme.h"
#include <functional>

namespace fui {
class PolyButton : public juce::TextButton {
public:
    enum class Style { segment, power, history, view, stepper };
    PolyButton(const juce::String& text, Style buttonStyle) : juce::TextButton(text), style(buttonStyle) {}
    void paintButton(juce::Graphics&, bool hovered, bool down) override;
    Style style;
    bool first = false, last = false;
};
// Sequence numbers deliberately bypass APVTS: a gesture edits one complete sequence.
class SequenceNumberInput : public juce::Component {
public:
    SequenceNumberInput(const juce::String& name);
    void setValue(int v, int hi);
    void resized() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override { return true; }
    std::function<void(int)> onChange;
    std::function<void()> onBegin, onEnd;
    std::function<void(juce::String)> onError;
    int minimum = 0;
    bool wrap = false;
private:
    void commit();
    juce::String caption;
    PolyButton minus{juce::String::fromUTF8("\xe2\x80\xb9"), PolyButton::Style::stepper}, plus{juce::String::fromUTF8("\xe2\x80\xba"), PolyButton::Style::stepper};
    juce::TextEditor input;
    int value = 0, maximum = 16;
};

class PolyLanePanel : public juce::Component, private juce::Timer {
public:
    explicit PolyLanePanel(DrumUiModel&);
    ~PolyLanePanel() override { stopTimer(); }
    std::function<void()> beforeEdit, onUndo, onRedo;
    void resized() override;
    void paint(juce::Graphics&) override;
    void updateFromModel() { refresh(); }
    bool keyPressed(const juce::KeyPress&) override { return true; }
    // SOURCE wraps below ROTATE under 1000 px; very narrow panels need a
    // fourth row when FIT's CYCLE group is visible.
    static int preferredHeight(int width) { return width < 500 ? 232 : width < 1000 ? 190 : 118; }
private:
    void timerCallback() override;
    void refresh();
    void edit(const std::function<void(DrumSequence&, fable::DrumLaneRhythm&)>&, bool activate = true);
    DrumUiModel& model;
    juce::Label error;
    PolyButton undo{juce::String::fromUTF8("\xe2\x86\xb6"), PolyButton::Style::history}, redo{juce::String::fromUTF8("\xe2\x86\xb7"), PolyButton::Style::history};
    PolyButton enabled{"OFF", PolyButton::Style::power}, grid{"GRID", PolyButton::Style::segment}, fit{"FIT", PolyButton::Style::segment};
    PolyButton one{"1 BAR", PolyButton::Style::segment}, two{"2 BARS", PolyButton::Style::segment};
    PolyButton view{"VIEW SOURCE", PolyButton::Style::view};
    std::array<PolyButton, 4> source{{ {"1", PolyButton::Style::segment}, {"2", PolyButton::Style::segment},
                                        {"3", PolyButton::Style::segment}, {"4", PolyButton::Style::segment} }};
    SequenceNumberInput steps{"STEPS"}, rotation{"ROTATE"};
    bool refreshing = false, gesture = false, gestureSaved = false;
    int lastPad = -1;
    bool on = false, fitMode = false;
    int laneSteps = 16, sourceBar = 0, cycleBeats = 4, selectedPad = 0;
    juce::Rectangle<int> meterBounds, readoutBounds, titleBounds, padBounds;
    juce::Rectangle<int> modeLabel, stepsLabel, cycleLabel, rotationLabel, sourceLabel;
};
}
