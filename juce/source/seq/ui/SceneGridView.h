#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../SeqProcessor.h"
#include "../ClipClipboardCodec.h"
#include "../../ui/Theme.h"

#include <functional>

// SQ-4 launcher grid — port of src/seq/components/SceneRow.tsx + seq.css.
// Six scene rows, each a scene card (launch/mute/stop, dots, status) followed
// by one clip cell per track. Painted directly and hit-tested in mouseDown,
// same scheme as TrackHeadsView -- the whole slot is a single 1424x478 strip
// (SeqRack::applyLayout's session-mode scene-grid section).
//
// Local geometry (matches TrackHeadsView.cpp's column table): row r at
// y = r*82, h=73; scene card x=0 w=218; clip cell for track t at
// x=218+9+t*(292+9) w=292.
namespace fui {

class SceneGridView : public juce::Component, public juce::DragAndDropTarget, private juce::Timer {
public:
    explicit SceneGridView(SeqAudioProcessor&);
    ~SceneGridView() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // Test handles (also the real click targets, wired from mouseDown).
    void cellClick(int s, int t);
    void cellRightClick(int s, int t);
    void cellEditClick(int s, int t);
    void sceneLaunch(int s);
    void sceneMute(int s);
    void sceneStop(int s);
    // Focus-strip trigger zone (railTrigger[s]) test handle -- mirrors
    // cellClick/sceneLaunch: mouseDown's singleRow_ branch routes here too.
    // Launches the scene (quantized) without retargeting focus.
    void railTriggerClick(int s) { sceneLaunch(s); }
    // Geometry test hooks for the focus-strip trigger/retarget rects.
    juce::Rectangle<int> railTriggerR(int s) const { return railTrigger[s]; }
    juce::Rectangle<int> railChipR(int s) const { return railChip[s]; }
    // Sets hover state exactly as mouseMove would; (-1,-1) clears. Public so
    // headless tests can drive the hover-revealed chips/affordance below.
    void hoverCell(int s, int t);

    std::function<void(int, int)> onEditClip;

    // Test hook: true when cell (s,t) is live (owned by s) and fully audible
    // (not muted/soloed-out/scene-muted) -- the same gate paintFilledCell
    // uses to dim the cell and show MUTED, exposed without pixel probing.
    bool cellAudible(int s, int t) const;
    bool cellStopping(int s, int t) const;

    // Focus strip: the scene card + clip cells disappear entirely (the heads
    // row is hidden too -- SeqRack::enterFocus); this component instead
    // renders one horizontal strip: a "< SESSION" back chip followed by the
    // 6 scene chips, spanning the full rack width.
    // `track` is the focused track (-1 when unknown): the rail draws its clip
    // playhead on whichever chip currently owns it.
    void setSingleRow(int s, int track = -1) {
        singleRow_ = true; singleRowScene_ = s; focusTrack_ = track;
        hoverCellS_ = hoverCellT_ = -1; resized(); repaint();
    }
    // Progress (0..1) of the focused track through its playing clip, or -1 when
    // nothing is playing there. Public so the host test can assert it.
    float railProgress(int scene) const;
    void clearSingleRow() { singleRow_ = false; hoverCellS_ = hoverCellT_ = -1; resized(); repaint(); }
    std::function<void(int)> onRailScene;
    // Back chip's click target -- relocated from TrackHeadsView's SCENES
    // card now that the heads row is invisible in focus.
    std::function<void()> onExitFocus;

    // ---- selection rectangle (editing-concept decision 4) -------------------
    // Anchor/head cell pair; plain click launches AND anchors, Cmd-click
    // selects only, Shift-click extends. All are also test handles.
    void selectCell(int s, int t);           // anchor = head = (s, t)
    void extendSelection(int s, int t);      // head only (anchor kept)
    void clearSelection();
    bool hasSelection() const { return selAnchorS_ >= 0; }
    bool cellSelected(int s, int t) const;
    void selectAll();
    void moveSelection(int ds, int dt, bool extend); // arrow / Shift-arrow nav

    // ---- editing verbs over the selection (routed from SeqEditor keys) ------
    // Copy/cut serialise the selected rectangle to juce::SystemClipboard as
    // tagged JSON (ClipClipboardCodec) with an in-process fallback for
    // headless runs; paste/duplicate anchor the payload top-left and skip
    // machine-mismatched cells. Each mutating verb pushes exactly ONE undo
    // snapshot (before mutating) and only when something will change.
    bool selCopy();
    bool selCut();
    bool selPaste();
    bool selDuplicate();     // paste one scene below the selection's bottom edge
    bool selDelete();

    // ---- drag-and-drop (editing-concept decision 5) -------------------------
    // The grid is its own DragAndDropTarget; SeqEditor is the container.
    // A >=4px drag of a filled cell starts a block drag (the selection if the
    // grab is inside it, else the single cell); Alt at drop = copy; only
    // machine-compatible target cells highlight; Escape cancels.
    bool isDragActive() const { return dragActive_ && !dragCancelled_; }
    void cancelActiveDrag();
    // The drop verb itself (also the headless test handle): move/copy the
    // block grabbed at (fromS,fromT) so the grab lands on (toS,toT).
    bool dropCells(int fromS, int fromT, int toS, int toT, bool copy);

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails& d) override { itemDragMove(d); }
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

private:
    static constexpr int kScenes = 6, kTracks = 4;

    void timerCallback() override { repaint(); }

    bool isPassThrough(int s, int t) const;

    void layoutRow(int s);
    void layoutFocusStrip();

    void paintSceneCard(juce::Graphics&, int s);
    void paintCell(juce::Graphics&, int s, int t);
    void paintEmptyCell(juce::Graphics&, int s, int t);
    void paintFilledCell(juce::Graphics&, int s, int t);
    void paintFocusStrip(juce::Graphics&);
    void paintRail(juce::Graphics&);

    // Block geometry shared by the drop verb and the drag-highlight paint:
    // the rectangle that travels (selection if it contains the grab, else the
    // grab cell alone), normalized to s0<=s1, t0<=t1.
    void dragBlock(int fromS, int fromT, int& s0, int& t0, int& s1, int& t1) const;
    bool dropHighlight(int s, int t) const;  // (s,t) is a compatible drag target
    int cellAt(juce::Point<int> pos, int& outT) const; // scene index or -1
    // Paste a clipboard rectangle anchored top-left at (atS,atT); skips
    // machine-mismatched / out-of-bounds / null cells; one undo snapshot when
    // anything applies. Shared by selPaste and selDuplicate.
    bool pasteData(const fable::ClipClipboardData&, int atS, int atT);
    fable::ClipClipboardData captureSelection() const;

    SeqAudioProcessor& proc;

    bool singleRow_ = false;
    int singleRowScene_ = 0;
    int focusTrack_ = -1;

    // selection (anchor/head; -1 = none)
    int selAnchorS_ = -1, selAnchorT_ = -1, selHeadS_ = -1, selHeadT_ = -1;

    // press/drag bookkeeping (mouseDown defers the launch to mouseUp so a
    // >=4px drag can suppress it — editing-concept: suppress launch on
    // drag-release)
    int pressedS_ = -1, pressedT_ = -1;
    bool pressedLaunch_ = false, didDrag_ = false;

    // hover state (mouseMove/mouseExit; drives hover-revealed edit/delete
    // chips and the empty-cell + affordance, web parity with .sq-cell-tools)
    int hoverCellS_ = -1, hoverCellT_ = -1;

    // active drag state
    bool dragActive_ = false, dragCancelled_ = false;
    int dragFromS_ = -1, dragFromT_ = -1;   // grabbed cell
    int hoverS_ = -1, hoverT_ = -1;         // current drop cell under the drag

    // In-process clipboard fallback: copy writes both this and the system
    // clipboard; paste falls back here when the system text isn't a tagged
    // clip document (headless runs without a pasteboard).
    juce::String localClipboard_;

    // scene-card sub-regions
    juce::Rectangle<int> sceneCardR[kScenes], launchBtn[kScenes], muteBtnR[kScenes], stopBtnR[kScenes],
        idArea[kScenes], dotsArea[kScenes];
    // clip-cell regions
    juce::Rectangle<int> cellR[kScenes][kTracks], editGlyph[kScenes][kTracks], trashGlyph[kScenes][kTracks];
    // focus strip (singleRow_ mode): back chip + the 6 scene chips, each with
    // a leading trigger zone (railTrigger[s]) ahead of the retarget chip body
    // (railChip[s]) -- web parity with .sq-rail-launch / .sq-rail-target.
    juce::Rectangle<int> backChipR, railChip[kScenes], railTrigger[kScenes];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneGridView)
};

} // namespace fui
