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
    void mouseMove(const juce::MouseEvent&) override;
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

    // ---- 2-D rectangle (step × note-lane) editing (port of the web note grid:
    // src/shared/seqEdit.ts + useSeqRectSelect / useSeqNoteDrag /
    // useSeqGhostPaste). Public so the host test drives the exact code paths a
    // keypress / drag / menu click takes. The slide flag (byte1 bit7) rides
    // along untouched through every rect verb. ----
    void copySel();                         // Cmd-C / menu COPY
    void cutSel();                          // Cmd-X
    void pasteSel();                        // Cmd-V
    void duplicateSel();                    // Cmd-D / menu DUP
    void deleteSel();                       // Delete / menu DEL
    void selectAllCells();                  // Cmd-A
    void clearSelection();                  // Esc / menu ✕
    void undoEdit();                        // Cmd-Z
    void redoEdit();                        // Shift-Cmd-Z
    void moveBar(int fromPattern, int toPattern, bool copy); // bar-chip drag

    void commitNoteMove(int srcStep, int srcNote, int destStep, int destNote, bool copy);
    void commitBlockMove(int dStep, int dNote, bool copy);
    void beginGhostPaste(bool cut);
    void dropGhost(int atStep, int atNote);

    bool hasSelection() const { return hasRect_; }
    fable::RectNorm selection() const { return fable::rectNorm(rect_); }
    void setSelection(const fable::RectSel&);
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
    juce::Rectangle<int> slideBounds(int step) const;
    juce::Rectangle<int> resizeBounds(int step) const;
    juce::Rectangle<int> gridBounds() const;
    juce::Rectangle<int> selMenuBounds() const;
    juce::Rectangle<int> selMenuButton(int i) const;

private:
    void timerCallback() override;          // 30 Hz playhead / state watcher
    void pushHistory() { history_.push(proc.patternBytes()); }
    int stepAt(int x) const;                // inverse of colBounds (clamped 0..STEPS-1)
    int noteAt(int y) const;                // inverse of cellBounds' lane math
    int patternIndexAt(juce::Point<int>) const; // which bar chip, or -1
    bool inRect(int step, int note) const;
    int grabNoteAt(int step, int note) const;
    void toggleAt(int step, int note);
    void cancelGesture();

    BassUiModel& proc;
    juce::Random rng_;
    juce::uint32 lastSig_ = 0xffffffffu;
    int resizeStep_ = -1, resizeStartDuration_ = 1;

    // 2-D rectangle selection over the edit pattern (anchor/head; normalize
    // via rectNorm).
    bool hasRect_ = false;
    fable::RectSel rect_;
    bool sweeping_ = false;
    fable::RectSel pending_;

    bool noteDragArmed_ = false, noteDragActive_ = false;
    int ndSrcStep_ = 0, ndSrcNote_ = 0, ndGrabStep_ = 0, ndOverStep_ = 0, ndOverNote_ = 0;

    bool moveArmed_ = false, moving_ = false;
    int moveOriginStep_ = 0, moveOriginNote_ = 0, moveHoverStep_ = 0, moveHoverNote_ = 0;

    int downStep_ = -1, downNote_ = -1;

    bool ghost_ = false, ghostCut_ = false, ghostHasHover_ = false;
    fable::RectCells ghostData_;
    fable::RectSel ghostSrc_;
    int ghostHoverStep_ = 0, ghostHoverNote_ = 0;

    struct RectClipboard {
        bool valid = false;
        bool isPattern = false;
        fable::RectCells rect;
        std::vector<uint8_t> pattern;
    };
    RectClipboard clip_;
    bool hasLastCell_ = false;
    int lastCellStep_ = 0, lastCellNote_ = 0;

    fable::StepEditHistory<std::vector<uint8_t>> history_;
    int lastPatternSrc_ = std::numeric_limits<int>::min(); // sentinel: never matches a real id

    int barDragFrom_ = -1;                            // bar-chip drag in progress
    bool barDragStarted_ = false;
    int barDragHover_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchSeqView)
};

} // namespace fui
