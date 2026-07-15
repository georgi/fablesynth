#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Controls.h"
#include "Theme.h"
#include "WtUiModel.h"
#include "../dsp/NoteSeq.h"

// The WT-1 16-step note sequencer panel — port of
// src/components/panels/SeqPanel.tsx (+ index.css .ns-*): 12 note lanes per
// step (tap = set note, tap again = rest), per-step octave / accent rows,
// bars 1-4 + sequence length, RAND, playhead cursor and the ROOT clock
// column. Note length lives in each step's packed `duration` bits (the
// milestone-3 piano roll will surface it as drag-resizable blocks). BPM /
// SWING live in the WT-1 top bar. Pattern/chain semantics mirror src/store.ts.
namespace fui {

class NoteSeqView : public juce::Component, private juce::Timer {
public:
    explicit NoteSeqView(WtUiModel&);
    ~NoteSeqView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    // Web store handlers — public so the host test drives the exact code
    // paths a mouse click takes.
    void toggleCell(int step, int note);    // store.toggleCell
    void cycleStepOct(int step);            // store.cycleStepOct
    void toggleStepAcc(int step);           // store.toggleStepAcc
    void randomize();                       // store.randomizeSeq (RAND button)
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

private:
    void timerCallback() override;          // 30 Hz playhead / state watcher
    WtUiModel& model;
    Stepper root_;
    juce::Random rng_;
    juce::uint32 lastSig_ = 0xffffffffu;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteSeqView)
};

} // namespace fui
