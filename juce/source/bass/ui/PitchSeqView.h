#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BassUiModel.h"
#include "../../ui/Theme.h"

// The 16-step pitch sequencer — port of src/bass/components/PitchSeq.tsx
// (+ bass.css .bl-seq-*): 12 note lanes per step (tap = set note, tap again =
// rest), per-step octave / accent / slide rows, bars 1-4 + sequence length,
// RAND, playhead cursor and glowing slide connectors between tied steps.
// Pattern/chain semantics mirror src/bass/store.ts exactly.
namespace fui {

class PitchSeqView : public juce::Component, private juce::Timer {
public:
    explicit PitchSeqView(BassUiModel&);
    ~PitchSeqView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Web store handlers — public so the host test drives the exact code
    // paths a mouse click takes.
    void toggleCell(int step, int note);    // store.toggleCell
    void cycleStepOct(int step);            // store.cycleStepOct
    void toggleStepAcc(int step);           // store.toggleStepAcc
    void toggleStepSlide(int step);         // store.toggleStepSlide
    void resizeStep(int step, int duration); // preserves accent + slide
    void cancelResize();
    void randomize();                       // store.randomize (RAND button)
    void patternClick(int i);               // choose bar to edit
    void setSequenceLength(int bars);        // play bars 1 through N

    // Hit-test geometry (public for the host test).
    juce::Rectangle<int> transportBounds() const;
    juce::Rectangle<int> patternBounds(int i) const;
    juce::Rectangle<int> sequenceLengthBounds() const;
    juce::Rectangle<int> randBounds() const;
    juce::Rectangle<int> colBounds(int step) const;      // one step column
    juce::Rectangle<int> cellBounds(int step, int note) const;
    juce::Rectangle<int> octBounds(int step) const;
    juce::Rectangle<int> accBounds(int step) const;
    juce::Rectangle<int> slideBounds(int step) const;
    juce::Rectangle<int> resizeBounds(int step) const;

private:
    void timerCallback() override;          // 30 Hz playhead / state watcher
    BassUiModel& proc;
    juce::Random rng_;
    juce::uint32 lastSig_ = 0xffffffffu;
    int resizeStep_ = -1, resizeStartDuration_ = 1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchSeqView)
};

} // namespace fui
