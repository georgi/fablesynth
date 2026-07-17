#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BassUiModel.h"
#include "../../ui/StepEditOps.h"
#include "../../ui/Theme.h"
#include <limits>

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

    // ---- Decision-6: step-range selection + verbs (StepEditOps.h) ----
    // Public so the host test drives the exact code paths mouse/keyboard
    // input takes (same convention as the handlers above).
    bool hasSelection() const { return selAnchor_ >= 0; }
    int selectionLo() const { return juce::jmin(selAnchor_, selHead_); }
    int selectionHi() const { return juce::jmax(selAnchor_, selHead_); }
    void shiftClickStep(int step);          // Shift-click: set/extend the range
    void selectAll();                       // Cmd-A: whole pattern
    void clearSelection();                  // Esc
    void copySelection();                   // Cmd-C
    void cutSelection();                    // Cmd-X
    void pasteAtSelection();                // Cmd-V
    void duplicateSelection();              // Cmd-D
    void deleteSelection();                 // Delete / Backspace
    void undo();                            // Cmd-Z
    void redo();                            // Shift-Cmd-Z
    // Step-range drag-shift commit (mouseUp): move the selection to start at
    // destStep (Alt = copy); dest is clamped so content and selection agree.
    void shiftRangeTo(int destStep, bool copy);
    // Bar-chip drag commit (mouseUp): move fromPattern's content to
    // toPattern, swapping unless copy is set (Alt).
    void moveBar(int fromPattern, int toPattern, bool copy);

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
    void pushHistory() { history_.push(proc.patternBytes()); }
    int stepAt(int x) const;                // inverse of colBounds (clamped 0..STEPS-1)
    int patternIndexAt(juce::Point<int>) const; // which bar chip, or -1

    BassUiModel& proc;
    juce::Random rng_;
    juce::uint32 lastSig_ = 0xffffffffu;
    int resizeStep_ = -1, resizeStartDuration_ = 1;

    // Decision-6 state.
    int selAnchor_ = -1, selHead_ = -1;               // contiguous step-range selection
    std::vector<uint8_t> clipboard_;
    fable::StepEditHistory<std::vector<uint8_t>> history_;
    int lastPatternSrc_ = std::numeric_limits<int>::min(); // sentinel: never matches a real id

    bool stepDragActive_ = false;                     // step-range drag-shift in progress
    int stepDragAnchorCol_ = 0, stepDragHoverDest_ = -1;

    int barDragFrom_ = -1;                            // bar-chip drag in progress
    bool barDragStarted_ = false;
    int barDragHover_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchSeqView)
};

} // namespace fui
