#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Controls.h"
#include "Theme.h"
#include "WtUiModel.h"
#include "StepEditOps.h"
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
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Web store handlers — public so the host test drives the exact code
    // paths a mouse click takes.
    void toggleCell(int step, int note);    // store.toggleCell
    void cycleStepOct(int step);            // store.cycleStepOct
    void toggleStepAcc(int step);           // store.toggleStepAcc
    void resizeStep(int step, int duration); // duration block length, 1..63
    void cancelResize();
    void randomize();                       // store.randomizeSeq (RAND button)
    void patternClick(int i);               // choose bar to edit
    void setSequenceLength(int bars);        // play bars 1 through N

    // Step-range edit verbs (editing-concept.md decision 6) — public so the
    // host test drives the exact code paths a keypress/drag takes. All
    // operate on the edit pattern via StepEditOps over a full 4-pattern byte
    // snapshot, funnelled back through model.setSequenceStep (the model's own
    // mutation chokepoint).
    void copySteps();                       // store.copySteps
    void cutSteps();                        // store.cutSteps
    void pasteSteps();                      // store.pasteSteps
    void duplicateSteps();                  // store.duplicateSteps
    void deleteSteps();                     // store.deleteSteps
    void selectAllSteps();                  // store.selectAllSteps (Cmd-A)
    void clearStepSel();                    // store.clearStepSel (Esc)
    void undoSteps();                       // store.undoSeq
    void redoSteps();                       // store.redoSeq
    void shiftStepSel(int dest, bool copy); // store.shiftStepSel (drag-move release)
    void movePattern(int from, int to, bool copy); // store.movePattern (bar-chip drag release)

    bool hasStepSelection() const { return hasSel_; }
    int stepSelFrom() const { return hasSel_ ? std::min(selFrom_, selTo_) : -1; }
    int stepSelTo() const { return hasSel_ ? std::max(selFrom_, selTo_) : -1; }

    // Hit-test geometry (public for the host test).
    juce::Rectangle<int> transportBounds() const;
    juce::Rectangle<int> patternBounds(int i) const;
    juce::Rectangle<int> sequenceLengthBounds() const;
    juce::Rectangle<int> randBounds() const;
    juce::Rectangle<int> colBounds(int step) const;      // one step column
    juce::Rectangle<int> cellBounds(int step, int note) const;
    juce::Rectangle<int> octBounds(int step) const;
    juce::Rectangle<int> accBounds(int step) const;
    juce::Rectangle<int> resizeBounds(int step) const;
    juce::Rectangle<int> stepNumBounds(int step) const;  // the .ns-step-num selection strip

private:
    void timerCallback() override;          // 30 Hz playhead / state watcher

    // Full pattern-buffer snapshot/restore (StepEditOps operates on packed
    // byte blocks; the model only exposes per-step get/set, so every verb
    // round-trips the whole 4-pattern block through these helpers).
    struct SeqSnapshot { std::vector<uint8_t> patterns; std::vector<int> chain; };
    std::vector<uint8_t> snapshotAllPatterns() const;
    void applyAllPatterns(const std::vector<uint8_t>&);
    SeqSnapshot snapshot() const;
    void restore(const SeqSnapshot&);

    int stepAtX(int x) const;               // inverse of colBounds, for drag hit-testing
    void handleStepNumDown(int step, const juce::MouseEvent&);
    void cancelStepDrag();                  // Esc: drop an in-flight sweep/move/bar-drag uncommitted

    WtUiModel& model;
    Stepper root_;
    juce::Random rng_;
    juce::uint32 lastSig_ = 0xffffffffu;
    int resizeStep_ = -1, resizeStartDuration_ = 1;

    // Step-range selection (contiguous, in the edit pattern). selFrom_ is the
    // sweep/shift-click anchor and may be > selTo_; use stepSelFrom/To (or
    // std::min/max) for the normalized range.
    bool hasSel_ = false;
    int selFrom_ = 0, selTo_ = 0;
    bool sweeping_ = false;                 // Shift-drag extending the selection

    // Content drag-move of the current selection (mouseDown inside it,
    // without Shift); committed once via shiftStepSel on mouseUp.
    bool moving_ = false;
    int moveSelFrom_ = 0, moveOrigin_ = 0, moveHover_ = 0;
    bool moveAlt_ = false;

    // Bar-chip (pattern selector) drag: move/Alt-copy a pattern onto another,
    // like SequenceLengthControl.tsx's chip drag. Plain click still selects
    // the edit pattern (patternClick) when no drag crossed the threshold.
    int barDragFrom_ = -1;
    bool barDragging_ = false;
    int barDropTarget_ = -1;

    struct StepClipboard { bool isPattern = false; std::vector<uint8_t> data; };
    StepClipboard clipboard_;
    fable::StepEditHistory<SeqSnapshot> history_;
    int lastClipIdentity_ = 0;              // detects SQ-4 focus retargeting this view

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteSeqView)
};

} // namespace fui
