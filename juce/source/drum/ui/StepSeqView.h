#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumUiModel.h"
#include "../../ui/Theme.h"

// Step sequencer strip + selected-pad bar — ports of src/drum/components/
// StepSeq.tsx and SelBar.tsx (+ drum.css .dr-stepseq / .step* / .dr-selbar).
// Bar/length semantics mirror store.ts exactly:
//  - toggleStep: cycle the (editPattern, selectedPad, step) cell 0 -> 1 -> 2 -> 0.
//  - bar click selects one of bars 1-4 for editing without changing playback.
//  - sequence length plays bars 1 through N, where N is clamped to 1-4.
namespace fui {

// Selected-pad bar above the OSC row — port of SelBar.tsx: cyan LED,
// "PAD NN" mini head, pad name, and a right-aligned factory patch stepper
// (prev/next + name readout, kit-stepper styling from DrumHeader). Selecting
// a pad resets the readout to "—" like the web store's selectPad.
class SelBarView : public juce::Component, private juce::Timer {
public:
    explicit SelBarView(DrumUiModel&);
    ~SelBarView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;    // repaint on selection / pad-name change
    void stepPatch(int dir);          // cycle the factory bank onto the selected pad
    DrumUiModel& proc;
    juce::TextButton prevBtn{"<"}, nextBtn{">"};
    juce::Rectangle<int> patchNameArea;
    int patchIndex_ = -1;             // -1 = no patch selected -> "—"
    int lastSel_ = -1;
    int lastProgram_ = -1;            // kit load clears the patch readout too
    uint32_t lastPatchContextRevision_ = 0;
    juce::String lastName_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SelBarView)
};

class StepSeqView : public juce::Component, private juce::Timer {
public:
    explicit StepSeqView(DrumUiModel&);
    explicit StepSeqView(DrumAudioProcessor&); // standalone-test compatibility
    ~StepSeqView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    // Web store handlers — public so the host test drives the exact code
    // paths a mouse click takes.
    void toggleStep(int step);              // store.toggleStep
    void patternClick(int i);               // choose bar to edit
    void setSequenceLength(int bars);        // play bars 1 through N

    // Hit-test geometry (public for the host test).
    juce::Rectangle<int> transportBounds() const;
    juce::Rectangle<int> patternBounds(int i) const;
    juce::Rectangle<int> sequenceLengthBounds() const;
    juce::Rectangle<int> stepBounds(int step) const;

private:
    void timerCallback() override;          // 30 Hz playhead / state watcher
    std::unique_ptr<DrumUiModel> ownedModel;
    DrumUiModel& proc;
    juce::uint32 lastSig_ = 0xffffffffu;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSeqView)
};

} // namespace fui
