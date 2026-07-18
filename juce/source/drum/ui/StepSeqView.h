#pragma once
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumUiModel.h"
#include "../../ui/StepEditOps.h"
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

// Contiguous step-range selection on the currently-selected pad's row (decision
// 6). `selectAll` represents Cmd-A ("whole pattern, all pads" per the web
// contract) — since this view only ever draws one pad's lane, it paints as the
// whole visible row; the select-all flag is cleared the moment the selection
// narrows (a fresh Shift-click/-drag or Esc).
struct DrStepSelection {
    bool active = false;
    bool selectAll = false;
    int from = 0, to = 0;      // inclusive step range; meaningless when selectAll
};

// One-entry-per-gesture undo snapshot: every step of every pattern/pad plus
// the chain, so cross-pattern verbs (duplicate-to-next-bar, bar-chip
// move/copy, sequence-length changes) restore coherently. Coarse (like the
// SQ-4 session-JSON snapshot decision) — acceptable for v1.
struct DrStepSnapshot {
    fable::StepBytes steps;   // DR_NPATTERNS * DR_NPADS * DR_STEPS, pattern-major
    std::vector<int> chain;
};

class StepSeqView : public juce::Component, private juce::Timer {
public:
    explicit StepSeqView(DrumUiModel&);
    explicit StepSeqView(DrumAudioProcessor&); // standalone-test compatibility
    ~StepSeqView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Web store handlers — public so the host test drives the exact code
    // paths a mouse click takes.
    void toggleStep(int step);              // store.toggleStep
    void patternClick(int i);               // choose bar to edit
    void setSequenceLength(int bars);        // play bars 1 through N
    // RAND: randomize the selected pad's row in the edit pattern. The rng
    // (returning [0,1)) is injectable so the host test is deterministic;
    // default is the system RNG.
    void randomizePad(std::function<float()> rng = nullptr);

    // ---- decision 6: selection, verbs, drag results ----
    // Public so the host test drives them the same way it drives toggleStep/
    // patternClick above, without needing to fabricate MouseEvents (no test in
    // this tree does — see resizeStep in NoteSeqView/PitchSeqView).
    void extendSelection(int step);         // Shift-click set/extend + Shift-sweep
    void selectAllPattern();                // Cmd-A
    void clearSelection();                  // Esc (only when a selection exists)
    bool hasSelection() const { return sel_.active; }
    bool isSelectAll() const { return sel_.selectAll; }
    int selectionFrom() const { return sel_.from; }
    int selectionTo() const { return sel_.to; }

    void copySelection();                   // Cmd-C
    void cutSelection();                    // Cmd-X
    void pasteSelection();                  // Cmd-V
    void duplicateSelection();              // Cmd-D (range) / duplicatePattern (none)
    void duplicatePattern();                // Cmd-D with no selection: copy to next bar
    void deleteSelection();                 // Delete/Backspace
    bool hasClipboard() const { return clipboard_.valid; }

    // Step-range drag-shift (move; Alt = copy), and the bar-chip drag/copy on
    // the pattern-select row — the direct "gesture completed" entry points
    // mouseUp calls, exposed so tests can drive the same math without pixel
    // hit-testing.
    void shiftSelection(int destStep, bool copy);
    void movePattern(int fromBar, int toBar, bool copy);

    void undo();                            // Cmd-Z
    void redo();                            // Shift-Cmd-Z
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // Hit-test geometry (public for the host test).
    juce::Rectangle<int> transportBounds() const;
    juce::Rectangle<int> patternBounds(int i) const;
    juce::Rectangle<int> sequenceLengthBounds() const;
    juce::Rectangle<int> randButtonBounds() const;
    juce::Rectangle<int> stepBounds(int step) const;

private:
    void timerCallback() override;          // 30 Hz playhead / state watcher

    bool stepInSelection(int step) const;
    fable::StepLayout padLayout(int pad) const;
    fable::StepBytes buildPatternBuffer(int pat) const;
    void applyPatternBuffer(int pat, const fable::StepBytes&);
    fable::StepBytes shiftPatternBuffer(const fable::StepBytes& basePattern, int pad,
                                         int from, int to, int dest, bool copy) const;
    DrStepSnapshot captureSnapshot() const;
    void restoreSnapshot(const DrStepSnapshot&);
    void pushHistoryEntry();                // snapshot the pre-mutation state

    std::unique_ptr<DrumUiModel> ownedModel;
    DrumUiModel& proc;
    juce::uint32 lastSig_ = 0xffffffffu;
    int lastClipIdentity_ = 0;
    bool haveClipIdentity_ = false;

    DrStepSelection sel_;
    int selAnchor_ = 0;                     // Shift-click/-sweep anchor (sel_.from/to derive from this)
    bool selecting_ = false;                // true while a Shift-drag sweep is live

    struct Clipboard {
        bool valid = false;
        bool wholePattern = false;          // true: all-pad pattern block; false: single-lane range
        fable::StepBytes data;
    } clipboard_;

    fable::StepEditHistory<DrStepSnapshot> history_;

    // Drag state shared by the step-range shift and the bar-chip move/copy —
    // armed on mouseDown, resolved on mouseUp, cancellable with Esc.
    struct DragState {
        enum class Kind { none, stepShift, barChip } kind = Kind::none;
        juce::Point<int> startPos;
        bool crossed = false;               // past the ~4px move threshold
        // stepShift
        int originStep = -1, pad = 0, pat = 0;
        int selFrom = 0, selTo = 0;
        fable::StepBytes basePattern;        // pre-drag pattern buffer (live-preview baseline)
        DrStepSnapshot preDrag;               // pre-drag full snapshot (history + Esc-cancel)
        // barChip
        int fromBar = -1, hoverBar = -1;
    } drag_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSeqView)
};

} // namespace fui
