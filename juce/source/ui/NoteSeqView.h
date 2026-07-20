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
    void mouseMove(const juce::MouseEvent&) override;
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

    // 2-D rectangle (step × note-lane) edit verbs — port of the web note grid
    // (src/shared/seqEdit.ts + useSeqRectSelect / useSeqNoteDrag /
    // useSeqGhostPaste). Public so the host test drives the exact code paths a
    // keypress / drag / menu click takes. All operate on the edit pattern via
    // StepEditOps' rect verbs over a full 4-pattern byte snapshot, funnelled
    // back through model.setSequenceStep (the model's own mutation chokepoint).
    void copySel();                         // Cmd-C: capture the rect (also refreshes the clipboard)
    void cutSel();                          // Cmd-X: capture + clear in one undo entry
    void pasteSel();                        // Cmd-V: paste the clipboard at the paste anchor
    void duplicateSel();                    // Cmd-D: copy the rect immediately to its right
    void deleteSel();                       // Delete/Backspace: clear the in-band lit cells
    void selectAllCells();                  // Cmd-A: whole grid (all steps × all lanes)
    void clearSelection();                  // Esc / menu ✕
    void undoEdit();                        // Cmd-Z
    void redoEdit();                        // Shift-Cmd-Z
    void movePattern(int from, int to, bool copy); // bar-chip drag release

    // Gesture commits (mouseUp / drop) — exposed so the host test can drive
    // them without synthesising raw drags.
    void commitNoteMove(int srcStep, int srcNote, int destStep, int destNote, bool copy);
    void commitBlockMove(int dStep, int dNote, bool copy);   // move the whole rect
    void beginGhostPaste(bool cut);         // menu CUT/COPY: pick the rect up, ghost follows the cursor
    void dropGhost(int atStep, int atNote); // drop the carried ghost at (step, note)

    // Selection / clipboard queries (host test + menu placement).
    bool hasSelection() const { return hasRect_; }
    fable::RectNorm selection() const { return fable::rectNorm(rect_); }
    void setSelection(const fable::RectSel& r);   // commit a rectangle (sweep release / Cmd-A)
    bool hasClipboard() const { return clip_.valid; }
    bool ghostActive() const { return ghost_; }

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
    juce::Rectangle<int> stepNumBounds(int step) const;  // the .ns-step-num label strip
    juce::Rectangle<int> gridBounds() const;             // the whole 16×12 lane area
    juce::Rectangle<int> selMenuBounds() const;          // floating CUT/COPY/DUP/DEL/✕ toolbar
    juce::Rectangle<int> selMenuButton(int i) const;     // one of the 5 menu buttons

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
    int noteAtY(int y) const;               // inverse of cellBounds' lane math (clamped 0..11)
    bool inRect(int step, int note) const;  // is (step,note) inside the pending/committed rect?
    int grabNoteAt(int step, int note) const; // origin step of a note grabbable at (step,note), or -1
    void toggleAt(int step, int note);      // deferred single-cell toggle + paste-anchor update
    void cancelGesture();                   // Esc: drop any in-flight sweep/drag/ghost uncommitted

    WtUiModel& model;
    Stepper root_;
    juce::Random rng_;
    juce::uint32 lastSig_ = 0xffffffffu;
    int resizeStep_ = -1, resizeStartDuration_ = 1;

    // 2-D rectangle selection (step × note lane) over the edit pattern. rect_
    // may be stored un-normalized (anchor/head); read it via rectNorm().
    bool hasRect_ = false;
    fable::RectSel rect_;

    // Shift-drag sweep in progress (hook-local until mouseUp; never touches the
    // model mid-gesture). pending_ is the live anchor/head rectangle.
    bool sweeping_ = false;
    fable::RectSel pending_;

    // Note drag of a single lit cell: armed on mouseDown, becomes active once
    // the pointer reaches a different cell (so a plain tap still toggles).
    bool noteDragArmed_ = false, noteDragActive_ = false;
    int ndSrcStep_ = 0, ndSrcNote_ = 0, ndGrabStep_ = 0, ndOverStep_ = 0, ndOverNote_ = 0;

    // Block-move of the whole rectangle (mouseDown inside it, without Shift).
    bool moveArmed_ = false, moving_ = false;
    int moveOriginStep_ = 0, moveOriginNote_ = 0, moveHoverStep_ = 0, moveHoverNote_ = 0;

    // The deferred single-cell toggle target: a lane press that neither swept
    // nor dragged falls through to a note toggle on mouseUp.
    int downStep_ = -1, downNote_ = -1;

    // Ghost paste: the menu's CUT/COPY picks the rect up; the captured cells
    // trail the cursor (any lane/step) until the next click drops them.
    bool ghost_ = false, ghostCut_ = false, ghostHasHover_ = false;
    fable::RectCells ghostData_;
    fable::RectSel ghostSrc_;               // source rect to clear on drop (CUT only)
    int ghostHoverStep_ = 0, ghostHoverNote_ = 0;

    // Bar-chip (pattern selector) drag: move/Alt-copy a pattern onto another,
    // like SequenceLengthControl.tsx's chip drag.
    int barDragFrom_ = -1;
    bool barDragging_ = false;
    int barDropTarget_ = -1;

    // Cmd-V clipboard: a captured rectangle, or the whole edit pattern when
    // there is no selection.
    struct RectClipboard {
        bool valid = false;
        bool isPattern = false;
        fable::RectCells rect;
        std::vector<uint8_t> pattern;
    };
    RectClipboard clip_;

    // Paste anchor fallback (useSeqEditKeys: last cell clicked/dragged).
    bool hasLastCell_ = false;
    int lastCellStep_ = 0, lastCellNote_ = 0;

    fable::StepEditHistory<SeqSnapshot> history_;
    int lastClipIdentity_ = 0;              // detects SQ-4 focus retargeting this view

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteSeqView)
};

} // namespace fui
