#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../DrumProcessor.h"
#include "../../ui/Theme.h"

// Step sequencer strip + selected-pad bar — ports of src/drum/components/
// StepSeq.tsx and SelBar.tsx (+ drum.css .dr-stepseq / .step* / .dr-selbar).
// Pattern/chain semantics mirror store.ts exactly:
//  - toggleStep: cycle the (editPattern, selectedPad, step) cell 0 -> 1 -> 2 -> 0.
//  - pattern click, not chaining (store.setEditPattern): select the pattern
//    for editing AND reset the play chain to just that pattern.
//  - pattern click while CHAIN is lit (store.chainClick): the first click
//    replaces the chain ("chainFresh"), later clicks append; every click also
//    moves the edit pattern and pushes the chain to the engine immediately.
//  - CHAIN toggle-off (store.setChaining): commit the built chain (falls back
//    to [editPattern] if somehow empty).
namespace fui {

// Selected-pad bar above the OSC row — port of SelBar.tsx: cyan LED,
// "PAD NN" mini head, pad name, and a right-aligned factory patch stepper
// (prev/next + name readout, kit-stepper styling from DrumHeader). Selecting
// a pad resets the readout to "—" like the web store's selectPad.
class SelBarView : public juce::Component, private juce::Timer {
public:
    explicit SelBarView(DrumAudioProcessor&);
    ~SelBarView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;    // repaint on selection / pad-name change
    void stepPatch(int dir);          // cycle the factory bank onto the selected pad
    DrumAudioProcessor& proc;
    juce::TextButton prevBtn{"<"}, nextBtn{">"};
    juce::Rectangle<int> patchNameArea;
    int patchIndex_ = -1;             // -1 = no patch selected -> "—"
    int lastSel_ = -1;
    int lastProgram_ = -1;            // kit load clears the patch readout too
    juce::String lastName_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SelBarView)
};

class StepSeqView : public juce::Component, private juce::Timer {
public:
    explicit StepSeqView(DrumAudioProcessor&);
    ~StepSeqView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    // Web store handlers — public so the host test drives the exact code
    // paths a mouse click takes.
    void toggleStep(int step);              // store.toggleStep
    void patternClick(int i);               // store.chainClick
    void setChaining(bool on);              // store.setChaining
    bool isChaining() const { return chaining_; }

    // Hit-test geometry (public for the host test).
    juce::Rectangle<int> transportBounds() const;
    juce::Rectangle<int> patternBounds(int i) const;
    juce::Rectangle<int> chainToggleBounds() const;
    juce::Rectangle<int> stepBounds(int step) const;

private:
    void timerCallback() override;          // 30 Hz playhead / state watcher
    DrumAudioProcessor& proc;
    bool chaining_ = false;
    bool chainFresh_ = false;               // next chained click replaces the chain
    juce::uint32 lastSig_ = 0xffffffffu;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSeqView)
};

} // namespace fui
