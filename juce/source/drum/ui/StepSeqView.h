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

// One-entry-per-gesture undo snapshot: every step of every pattern/pad plus
// the chain, so cross-pattern verbs (duplicate-to-next-bar, bar-chip
// move/copy, sequence-length changes) restore coherently. Coarse (like the
// SQ-4 session-JSON snapshot decision) — acceptable for v1.
struct DrStepSnapshot {
    fable::StepBytes steps;   // DR_NPATTERNS * DR_NPADS * DR_STEPS, pattern-major
    std::vector<int> chain;
};

// 2-D rectangle selection (step × pad-lane) over the edit pattern — port of the
// web DR-1 grid (src/shared/seqEdit.ts pad-rect verbs + useSeqRectSelect /
// useDrumGhostPaste). Shift-drag sweeps a rectangle; a drag from inside it moves
// the whole block (Alt copies); the floating CUT/COPY/DUP/DEL menu drives ghost
// paste; verbs mirror the web store. Standalone editing plus SQ-4 hosted clips.
class StepSeqView : public juce::Component, private juce::Timer {
public:
    explicit StepSeqView(DrumUiModel&);
    explicit StepSeqView(DrumAudioProcessor&); // standalone-test compatibility
    ~StepSeqView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Web store handlers — public so the host test drives the exact code
    // paths a mouse click takes.
    void toggleStep(int step);              // store.toggleStep (selected pad)
    void toggleStep(int pad, int step);     // store.toggleStep (explicit lane)
    void patternClick(int i);               // choose bar to edit
    void setSequenceLength(int bars);        // play bars 1 through N
    // RAND: randomize the selected pad's row in the edit pattern. The rng
    // (returning [0,1)) is injectable so the host test is deterministic;
    // default is the system RNG.
    void randomizePad(std::function<float()> rng = nullptr);

    // ---- rectangle selection, verbs, gesture commits ----
    // Public so the host test drives them the same way it drives toggleStep/
    // patternClick, without fabricating MouseEvents.
    void setSelection(const fable::PadRectSel&);  // commit a rectangle (sweep release / Cmd-A)
    void selectAllPattern();                       // Cmd-A: whole grid (all steps × all pads)
    void clearSelection();                         // Esc / menu ✕
    bool hasSelection() const { return hasRect_; }
    fable::PadRectNorm selection() const { return fable::padRectNorm(rect_); }

    void copySelection();                   // Cmd-C: capture the rect (else whole pattern)
    void cutSelection();                    // Cmd-X: capture + clear in one undo entry
    void pasteSelection();                  // Cmd-V: paste the clipboard at the anchor
    void duplicateSelection();              // Cmd-D (rect → right) / duplicatePattern (none)
    void duplicatePattern();                // Cmd-D with no selection: copy to next bar
    void deleteSelection();                 // Delete/Backspace
    bool hasClipboard() const { return clipboard_.valid; }

    // Block move (drag from inside the rect) and bar-chip drag on the pattern
    // row — the direct "gesture completed" entry points mouseUp calls, exposed
    // so tests drive the math without pixel hit-testing.
    void commitBlockMove(int dStep, int dPad, bool copy);
    void movePattern(int fromBar, int toBar, bool copy);

    // Ghost paste (menu CUT/COPY → drop on click), mirroring useDrumGhostPaste.
    void beginGhostPaste(bool cut);
    void dropGhost(int atStep, int atPad);
    bool ghostActive() const { return ghost_; }

    void undo();                            // Cmd-Z
    void redo();                            // Shift-Cmd-Z
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // Hit-test geometry (public for the host test).
    juce::Rectangle<int> transportBounds() const;
    juce::Rectangle<int> patternBounds(int i) const;
    juce::Rectangle<int> sequenceLengthBounds() const;
    juce::Rectangle<int> randButtonBounds() const;
    // stepBounds(step) addresses the selected pad's lane; the two-argument form
    // any lane. Lanes run high pad to low, top to bottom (see laneOfPad).
    juce::Rectangle<int> stepBounds(int step) const;
    juce::Rectangle<int> stepBounds(int pad, int step) const;
    juce::Rectangle<int> laneBounds(int pad) const;
    juce::Rectangle<int> laneNameBounds(int pad) const;
    juce::Rectangle<int> gridBounds() const;               // union of all lane × step cells
    juce::Rectangle<int> selMenuBounds() const;            // floating CUT/COPY/DUP/DEL/✕ toolbar
    juce::Rectangle<int> selMenuButton(int i) const;       // one of the 5 menu buttons
    static int laneOfPad(int pad);
    int laneHeight() const;
    static int padOfLane(int lane);

private:
    void timerCallback() override;          // 30 Hz playhead / state watcher

    void cancelGesture();                   // Esc: drop any in-flight sweep/move/ghost uncommitted
    bool inRect(int step, int pad) const;   // inside the pending/committed rect?
    // Point → grid cell. cellAt requires an exact hit; cellClamp always yields a
    // valid (pad, step) by snapping to the nearest lane/column (drag tracking).
    bool cellAt(juce::Point<int> pos, int& pad, int& step) const;
    void cellClamp(juce::Point<int> pos, int& pad, int& step) const;

    fable::StepLayout gridLayout() const;
    fable::StepBytes buildPatternBuffer(int pat) const;
    void applyPatternBuffer(int pat, const fable::StepBytes&);
    DrStepSnapshot captureSnapshot() const;
    void restoreSnapshot(const DrStepSnapshot&);
    void pushHistoryEntry();                // snapshot the pre-mutation state

    std::unique_ptr<DrumUiModel> ownedModel;
    DrumUiModel& proc;
    juce::uint32 lastSig_ = 0xffffffffu;
    int lastClipIdentity_ = 0;
    bool haveClipIdentity_ = false;

    // Committed rectangle (may be stored un-normalized; read via padRectNorm()).
    bool hasRect_ = false;
    fable::PadRectSel rect_;

    // Shift-drag sweep in progress (hook-local until mouseUp; never touches the
    // model mid-gesture). pending_ is the live anchor/head rectangle.
    bool sweeping_ = false;
    fable::PadRectSel pending_;

    // Block-move of the whole rectangle (mouseDown inside it, no Shift).
    bool moveArmed_ = false, moving_ = false;
    int moveOriginStep_ = 0, moveOriginPad_ = 0, moveHoverStep_ = 0, moveHoverPad_ = 0;

    // Deferred single-cell toggle: a press that neither swept nor moved falls
    // through to a step toggle on mouseUp.
    int downStep_ = -1, downPad_ = -1;

    // Ghost paste: the menu's CUT/COPY picks the rect up; the captured cells
    // trail the cursor until the next click drops them.
    bool ghost_ = false, ghostCut_ = false, ghostHasHover_ = false;
    fable::PadRectCells ghostData_;
    fable::PadRectSel ghostSrc_;            // source rect to clear on drop (CUT only)
    int ghostHoverStep_ = 0, ghostHoverPad_ = 0;

    struct Clipboard {
        bool valid = false;
        bool wholePattern = false;          // true: all-pad pattern block; false: rectangle
        fable::PadRectCells rect;
        fable::StepBytes pattern;
    } clipboard_;

    // Paste anchor fallback: the last cell touched (mirrors the web lastCell).
    bool hasLastCell_ = false;
    int lastCellStep_ = 0, lastCellPad_ = 0;

    fable::StepEditHistory<DrStepSnapshot> history_;

    // Bar-chip (pattern selector) drag — move/Alt-copy a pattern onto another.
    struct DragState {
        enum class Kind { none, barChip } kind = Kind::none;
        juce::Point<int> startPos;
        bool crossed = false;               // past the ~4px move threshold
        int fromBar = -1, hoverBar = -1;
    } drag_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSeqView)
};

} // namespace fui
