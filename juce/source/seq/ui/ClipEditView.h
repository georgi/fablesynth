#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../SeqProcessor.h"
#include "../../ui/Theme.h"

#include <vector>

// SQ-4 focus-mode clip editor — the device panel that replaces the launcher
// grid + footer when a track is focused (docs/.../sq4-device-focus-design.md
// §2-§3). It renders the target (scene, track) clip's pattern for editing:
//
//  - DR-1 target: 16 pads x 16 steps, cell click cycles off -> on -> accent,
//    rows labeled with the kit's pad names, 4-step group shading, and a
//    playhead column highlight when the clip is live.
//  - BL-1 / WT-1 target: 12 note lanes x 16 steps (tap sets the lane note,
//    tap the lit cell clears it) plus per-step OCT / ACC / TIE(SLIDE) rows,
//    PitchSeqView's interaction conventions.
//  - Bar chips 1..bars select the visible bar; a >4-bar clip is view-only
//    (edits ignored) with a lock banner, chips still page through every bar.
//  - Empty target: a centered "+ CREATE CLIP" button writes a blank clip.
//
// Every edit mutates a local byte copy then calls
// conductor().updateClipBytes(scene, track, bytes, bars) — the conductor
// routes it to the live/queued/idle engine and the session doc is the source
// of truth. A 30 Hz timer re-reads the clip so external changes reflect
// (guarded against clobbering our own just-written bytes).
//
// No FLIP transition (spec §2): entering/leaving focus is an instant relayout
// in SeqRack::resized(), which is the JUCE port of the web's animated collapse.
namespace fui {

class ClipEditView : public juce::Component, private juce::Timer {
public:
    explicit ClipEditView(SeqAudioProcessor&);
    ~ClipEditView() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    // Point the editor at a clip (scene, track); {-1,-1} clears it. Reloads
    // the local byte copy from the session doc.
    void setTarget(int scene, int track);

    // Test handles (also the real click targets, wired from mouseDown).
    void toggleDrumCell(int pad, int step);   // off <-> on
    void cycleDrumCell(int pad, int step);    // off -> on -> accent -> off
    void toggleNoteCell(int lane, int step);  // lane = note 0..11; tap sets, tap-lit clears
    void toggleAcc(int step);
    void toggleTie(int step);
    void setOct(int step, int oct);           // oct in -1..+1
    void barsStep(int d);                     // +/- one bar, clamped 1..SQ_HOSTED_MAX_BARS
    void barClick(int b);                     // select visible/edit bar
    void createClipClick();

private:
    static constexpr int kSteps = fable::SQ_STEPS_PER_BAR; // 16
    static constexpr int kPads  = fable::SQ_DR1_PADS;      // 16
    static constexpr int kLanes = 12;

    void timerCallback() override;
    void reload();                            // read clip from session -> bytes_
    void commit();                            // updateClipBytes(scene_,track_,bytes_,bars_)

    fable::Machine machine() const;
    bool isDrum() const { return machine() == fable::Machine::DR1; }
    bool live() const;                        // ownerOf(track_) == scene_
    bool editable() const { return hasClip_ && bars_ <= fable::SQ_HOSTED_MAX_BARS; }
    int  playStep() const;                    // current playhead step within the visible bar, or -1
    int  barOffset() const { return editBar_ * fable::sqBytesPerBar(machine()); }

    void layoutBars();
    void paintBars(juce::Graphics&);
    void paintDrumGrid(juce::Graphics&);
    void paintNoteGrid(juce::Graphics&);
    void paintCreate(juce::Graphics&);

    juce::String padLabel(int pad) const;

    SeqAudioProcessor& proc_;
    int scene_ = -1, track_ = -1;
    int editBar_ = 0;
    int bars_ = 1;
    bool hasClip_ = false;
    std::vector<uint8_t> bytes_;              // local editable copy (source: doc)
    std::vector<uint8_t> lastWritten_;        // guards the timer against reload-clobber

    // geometry (recomputed in resized()).
    juce::Rectangle<int> barsRow, barsMinus, barsPlus;
    juce::Rectangle<int> barChip[fable::SQ_HOSTED_MAX_BARS];
    juce::Rectangle<int> gridArea, createBtn;
    juce::Rectangle<int> octRow, accRow, tieRow; // note-machine sub-rows

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipEditView)
};

} // namespace fui
