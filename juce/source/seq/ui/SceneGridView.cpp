#include "SceneGridView.h"
#include "../../ui/Controls.h"
#include "../dsp/SeqModel.h"

#include <cmath>

namespace fui {

namespace {
// 0.8s pulse, matches the web's sq-qpulse keyframe (opacity 0.2..1).
float qpulse() {
    const double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    return static_cast<float>(0.2 + 0.8 * (0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * t / 0.8)));
}

// A slower triangular fade than the launch pulse. It is deliberately amber
// and contracts inward below, so a queued stop cannot be mistaken for a clip
// waiting to launch.
float stopPulse() {
    const double t = std::fmod(juce::Time::getMillisecondCounterHiRes() / 1000.0, 1.1) / 1.1;
    return static_cast<float>(t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0);
}
} // namespace

SceneGridView::SceneGridView(SeqAudioProcessor& p) : proc(p) { startTimerHz(30); }

// ---- actions (also the test handles) ---------------------------------------

bool SceneGridView::isPassThrough(int s, int t) const {
    const auto& scenes = proc.conductor().session().scenes;
    if (s < 0 || s >= (int)scenes.size()) return false;
    for (int x : scenes[(size_t)s].pass) if (x == t) return true;
    return false;
}

void SceneGridView::cellClick(int s, int t) {
    auto& cond = proc.conductor();
    const auto& scenes = cond.session().scenes;
    if (s < 0 || s >= (int)scenes.size() || t < 0 || t >= kTracks) return;

    if (!scenes[(size_t)s].hasClip[(size_t)t]) {
        if (!isPassThrough(s, t)) cond.stopTrack(t);
        return;
    }
    if (cond.ownerOf(t) == s) {
        if (cond.queueOf(t) != fable::SQ_STOP) cond.stopTrack(t);
    }
    else cond.launch(t, s);
}

void SceneGridView::cellRightClick(int s, int t) {
    const auto& scenes = proc.conductor().session().scenes;
    if (s < 0 || s >= (int)scenes.size() || t < 0 || t >= kTracks) return;
    if (scenes[(size_t)s].hasClip[(size_t)t]) return; // only empty cells toggle pass-through
    proc.conductor().togglePassThrough(s, t);
}

void SceneGridView::cellEditClick(int s, int t) { if (onEditClip) onEditClip(s, t); }

bool SceneGridView::cellAudible(int s, int t) const {
    const auto& cond = proc.conductor();
    return cond.ownerOf(t) == s && cond.trackAudible(t);
}

bool SceneGridView::cellStopping(int s, int t) const {
    const auto& cond = proc.conductor();
    return cond.ownerOf(t) == s && cond.queueOf(t) == fable::SQ_STOP;
}

void SceneGridView::sceneLaunch(int s) { proc.conductor().launchScene(s); }
void SceneGridView::sceneMute(int s)   { proc.conductor().toggleSceneMute(s); }
void SceneGridView::sceneStop(int s)   { proc.conductor().stopScene(s); }

// ---- selection --------------------------------------------------------------

void SceneGridView::selectCell(int s, int t) {
    const int scenes = (int)proc.conductor().session().scenes.size();
    if (s < 0 || s >= scenes || t < 0 || t >= kTracks) return;
    selAnchorS_ = selHeadS_ = s;
    selAnchorT_ = selHeadT_ = t;
    repaint();
}

void SceneGridView::extendSelection(int s, int t) {
    if (!hasSelection()) { selectCell(s, t); return; }
    const int scenes = (int)proc.conductor().session().scenes.size();
    if (s < 0 || s >= scenes || t < 0 || t >= kTracks) return;
    selHeadS_ = s;
    selHeadT_ = t;
    repaint();
}

void SceneGridView::clearSelection() {
    selAnchorS_ = selAnchorT_ = selHeadS_ = selHeadT_ = -1;
    repaint();
}

bool SceneGridView::cellSelected(int s, int t) const {
    if (!hasSelection()) return false;
    return s >= juce::jmin(selAnchorS_, selHeadS_) && s <= juce::jmax(selAnchorS_, selHeadS_)
        && t >= juce::jmin(selAnchorT_, selHeadT_) && t <= juce::jmax(selAnchorT_, selHeadT_);
}

void SceneGridView::selectAll() {
    const int scenes = (int)proc.conductor().session().scenes.size();
    if (scenes == 0) return;
    selAnchorS_ = 0; selAnchorT_ = 0;
    selHeadS_ = scenes - 1; selHeadT_ = kTracks - 1;
    repaint();
}

void SceneGridView::moveSelection(int ds, int dt, bool extend) {
    const int scenes = (int)proc.conductor().session().scenes.size();
    if (scenes == 0) return;
    if (!hasSelection()) { selectCell(0, 0); return; }
    const int s = juce::jlimit(0, scenes - 1, selHeadS_ + ds);
    const int t = juce::jlimit(0, kTracks - 1, selHeadT_ + dt);
    if (extend) extendSelection(s, t);
    else selectCell(s, t);
}

// ---- editing verbs -----------------------------------------------------------

fable::ClipClipboardData SceneGridView::captureSelection() const {
    fable::ClipClipboardData data;
    if (!hasSelection()) return data;
    const auto& sess = proc.conductor().session();
    const int s0 = juce::jmin(selAnchorS_, selHeadS_);
    const int s1 = juce::jmin((int)sess.scenes.size() - 1, juce::jmax(selAnchorS_, selHeadS_));
    const int t0 = juce::jmin(selAnchorT_, selHeadT_), t1 = juce::jmax(selAnchorT_, selHeadT_);
    if (s0 > s1) return data;
    for (int t = t0; t <= t1; ++t)
        data.machines.push_back(sess.tracks[(size_t)t].machine);
    for (int s = s0; s <= s1; ++s) {
        std::vector<fable::ClipData> row;
        std::vector<bool> has;
        for (int t = t0; t <= t1; ++t) {
            const bool h = sess.scenes[(size_t)s].hasClip[(size_t)t];
            has.push_back(h);
            row.push_back(h ? sess.scenes[(size_t)s].clips[(size_t)t] : fable::ClipData {});
        }
        data.cells.push_back(std::move(row));
        data.hasCell.push_back(std::move(has));
    }
    return data;
}

bool SceneGridView::selCopy() {
    if (!hasSelection()) return false;
    const juce::String json = fable::clipClipboardToJson(captureSelection());
    juce::SystemClipboard::copyTextToClipboard(json);
    localClipboard_ = json;
    return true;
}

bool SceneGridView::selDelete() {
    if (!hasSelection()) return false;
    auto& cond = proc.conductor();
    const auto& sess = cond.session();
    const int s0 = juce::jmin(selAnchorS_, selHeadS_);
    const int s1 = juce::jmin((int)sess.scenes.size() - 1, juce::jmax(selAnchorS_, selHeadS_));
    const int t0 = juce::jmin(selAnchorT_, selHeadT_), t1 = juce::jmax(selAnchorT_, selHeadT_);
    bool any = false;
    for (int s = s0; s <= s1; ++s)
        for (int t = t0; t <= t1; ++t)
            if (sess.scenes[(size_t)s].hasClip[(size_t)t]) any = true;
    if (!any) return false;
    proc.pushUndoSnapshot();
    for (int s = s0; s <= s1; ++s)
        for (int t = t0; t <= t1; ++t)
            if (cond.session().scenes[(size_t)s].hasClip[(size_t)t]) cond.deleteClip(s, t);
    repaint();
    return true;
}

bool SceneGridView::selCut() {
    if (!selCopy()) return false;
    selDelete(); // cut of an all-empty rectangle copies nulls, deletes nothing
    return true;
}

bool SceneGridView::pasteData(const fable::ClipClipboardData& data, int atS, int atT) {
    auto& cond = proc.conductor();
    const auto& sess = cond.session();
    const int scenes = (int)sess.scenes.size();
    struct Op { int s, t; const fable::ClipData* clip; };
    std::vector<Op> ops;
    for (int r = 0; r < (int)data.cells.size(); ++r) {
        for (int c = 0; c < (int)data.machines.size(); ++c) {
            if (!data.hasCell[(size_t)r][(size_t)c]) continue;    // null slot: paste nothing
            const int s = atS + r, t = atT + c;
            if (s < 0 || s >= scenes || t < 0 || t >= kTracks) continue; // clamp at edges
            if (sess.tracks[(size_t)t].machine != data.machines[(size_t)c]) continue; // skip column
            ops.push_back({ s, t, &data.cells[(size_t)r][(size_t)c] });
        }
    }
    if (ops.empty()) return false;
    proc.pushUndoSnapshot();
    for (const auto& op : ops) cond.pasteClip(op.s, op.t, *op.clip);
    repaint();
    return true;
}

bool SceneGridView::selPaste() {
    if (!hasSelection()) return false;
    fable::ClipClipboardData data;
    if (!fable::clipClipboardFromJson(juce::SystemClipboard::getTextFromClipboard(), data)
        && !fable::clipClipboardFromJson(localClipboard_, data))
        return false;
    return pasteData(data, juce::jmin(selAnchorS_, selHeadS_), juce::jmin(selAnchorT_, selHeadT_));
}

bool SceneGridView::selDuplicate() {
    if (!hasSelection()) return false;
    const auto data = captureSelection();
    const int s1 = juce::jmax(selAnchorS_, selHeadS_);
    const int t0 = juce::jmin(selAnchorT_, selHeadT_);
    return pasteData(data, s1 + 1, t0); // one scene below the bottom edge
}

// ---- drag-and-drop -----------------------------------------------------------

void SceneGridView::dragBlock(int fromS, int fromT, int& s0, int& t0, int& s1, int& t1) const {
    if (hasSelection() && cellSelected(fromS, fromT)) {
        s0 = juce::jmin(selAnchorS_, selHeadS_); s1 = juce::jmax(selAnchorS_, selHeadS_);
        t0 = juce::jmin(selAnchorT_, selHeadT_); t1 = juce::jmax(selAnchorT_, selHeadT_);
    } else {
        s0 = s1 = fromS;
        t0 = t1 = fromT;
    }
}

bool SceneGridView::dropCells(int fromS, int fromT, int toS, int toT, bool copy) {
    auto& cond = proc.conductor();
    const auto& sess = cond.session();
    const int scenes = (int)sess.scenes.size();
    if (fromS < 0 || fromS >= scenes || fromT < 0 || fromT >= kTracks) return false;
    if (toS < 0 || toS >= scenes || toT < 0 || toT >= kTracks) return false;
    if (!sess.scenes[(size_t)fromS].hasClip[(size_t)fromT]) return false;
    if (fromS == toS && fromT == toT) return false;

    int s0, t0, s1, t1;
    dragBlock(fromS, fromT, s0, t0, s1, t1);
    const int ds = toS - fromS, dt = toT - fromT;

    // capture sources first so overlapping move targets read pre-move bytes.
    // Only cells whose destination is in-grid and machine-compatible become
    // sources: skipped cells stay put (web gridEdit.ts moveWrites parity) —
    // they must never be deleted by the move branch below.
    struct Src { int s, t; fable::ClipData clip; };
    std::vector<Src> srcs;
    struct Op { int s, t; size_t src; };
    std::vector<Op> ops;
    for (int s = s0; s <= s1 && s < scenes; ++s) {
        for (int t = t0; t <= t1; ++t) {
            if (!sess.scenes[(size_t)s].hasClip[(size_t)t]) continue;
            const int ns = s + ds, nt = t + dt;
            if (ns < 0 || ns >= scenes || nt < 0 || nt >= kTracks) continue;
            if (sess.tracks[(size_t)nt].machine != sess.tracks[(size_t)t].machine) continue;
            srcs.push_back({ s, t, sess.scenes[(size_t)s].clips[(size_t)t] });
            ops.push_back({ ns, nt, srcs.size() - 1 });
        }
    }
    if (ops.empty()) return false;

    proc.pushUndoSnapshot();
    if (!copy)
        for (const auto& src : srcs)
            cond.deleteClip(src.s, src.t);
    for (const auto& op : ops) cond.pasteClip(op.s, op.t, srcs[op.src].clip);

    // selection follows the block, clamped into the grid
    selAnchorS_ = juce::jlimit(0, scenes - 1, s0 + ds);
    selAnchorT_ = juce::jlimit(0, kTracks - 1, t0 + dt);
    selHeadS_ = juce::jlimit(0, scenes - 1, s1 + ds);
    selHeadT_ = juce::jlimit(0, kTracks - 1, t1 + dt);
    repaint();
    return true;
}

bool SceneGridView::dropHighlight(int s, int t) const {
    if (!dragActive_ || dragCancelled_ || hoverS_ < 0 || dragFromS_ < 0) return false;
    const auto& sess = proc.conductor().session();
    int s0, t0, s1, t1;
    dragBlock(dragFromS_, dragFromT_, s0, t0, s1, t1);
    const int ds = hoverS_ - dragFromS_, dt = hoverT_ - dragFromT_;
    const int ss = s - ds, st = t - dt;   // the source cell that would land here
    if (ss < s0 || ss > s1 || st < t0 || st > t1) return false;
    if (ss >= (int)sess.scenes.size() || !sess.scenes[(size_t)ss].hasClip[(size_t)st]) return false;
    return sess.tracks[(size_t)t].machine == sess.tracks[(size_t)st].machine;
}

void SceneGridView::cancelActiveDrag() {
    if (!dragActive_) return;
    dragCancelled_ = true;
    hoverS_ = hoverT_ = -1;
    repaint();
}

int SceneGridView::cellAt(juce::Point<int> pos, int& outT) const {
    if (singleRow_) { outT = -1; return -1; } // focus strip has no clip cells
    for (int s = 0; s < kScenes; ++s) {
        for (int t = 0; t < kTracks; ++t)
            if (cellR[s][t].contains(pos)) { outT = t; return s; }
    }
    outT = -1;
    return -1;
}

bool SceneGridView::isInterestedInDragSource(const SourceDetails& d) {
    return d.description == "sq4-cells" && d.sourceComponent == this;
}

void SceneGridView::itemDragMove(const SourceDetails& d) {
    int t = -1;
    const int s = cellAt(d.localPosition, t);
    if (s != hoverS_ || t != hoverT_) { hoverS_ = s; hoverT_ = t; repaint(); }
}

void SceneGridView::itemDragExit(const SourceDetails&) {
    if (hoverS_ >= 0) { hoverS_ = hoverT_ = -1; repaint(); }
}

void SceneGridView::itemDropped(const SourceDetails& d) {
    itemDragMove(d);
    const bool copy = juce::ModifierKeys::getCurrentModifiers().isAltDown();
    if (!dragCancelled_ && hoverS_ >= 0)
        dropCells(dragFromS_, dragFromT_, hoverS_, hoverT_, copy);
    dragActive_ = dragCancelled_ = false;
    dragFromS_ = dragFromT_ = hoverS_ = hoverT_ = -1;
    repaint();
}

// ---- mouse -----------------------------------------------------------------

void SceneGridView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    const bool right = e.mods.isPopupMenu();

    if (singleRow_) {
        // Launcher column: the back chip plus the scene cards' own controls
        // (launch / mute / stop, same as session), with the card body
        // retargeting the editor. No clip cells here.
        if (!right) {
            if (backChipR.contains(pos)) { if (onExitFocus) onExitFocus(); return; }
            for (int s = 0; s < kScenes; ++s) {
                if (railTrigger[s].contains(pos)) { railTriggerClick(s); return; }
                if (muteBtnR[s].contains(pos))    { sceneMute(s); return; }
                if (stopBtnR[s].contains(pos))    { sceneStop(s); return; }
                if (railChip[s].contains(pos))    { if (onRailScene) onRailScene(s); return; }
            }
        }
        return;
    }

    const auto& scenes = proc.conductor().session().scenes;

    for (int s = 0; s < kScenes; ++s) {
        if (launchBtn[s].contains(pos)) { sceneLaunch(s); return; }
        if (muteBtnR[s].contains(pos))  { sceneMute(s); return; }
        if (stopBtnR[s].contains(pos))  { sceneStop(s); return; }
        for (int t = 0; t < kTracks; ++t) {
            const bool hasClip = s < (int)scenes.size() && scenes[(size_t)s].hasClip[(size_t)t];
            // Right-click always falls through to cellRightClick below: it's
            // a no-op on filled cells and the only way to toggle pass-through
            // on empty ones, so the hover chips/affordance only claim plain
            // left-clicks.
            if (!right && hasClip && editGlyph[s][t].contains(pos)) { cellEditClick(s, t); return; }
            if (!right && trashGlyph[s][t].contains(pos) && hasClip) {
                selectCell(s, t);   // route through the selection verb: one undo
                selDelete();        // snapshot + machine-safe clearing, already tested
                return;
            }
            if (!right && !hasClip) {
                // Empty-cell + affordance: top-right 24x24 corner of the cell,
                // matching where paintEmptyCell draws the + (rf.getRight()-12,
                // rf.getY()+12, 9x9). Opens the device focused on this cell.
                const juce::Rectangle<int> addCorner {
                    cellR[s][t].getRight() - 24, cellR[s][t].getY(), 24, 24
                };
                if (addCorner.contains(pos)) { if (onEditClip) onEditClip(s, t); return; }
            }
            if (cellR[s][t].contains(pos)) {
                if (right) { cellRightClick(s, t); return; }
                // editing-concept decision 4: plain click launches AND anchors
                // (launch deferred to mouseUp so a drag can suppress it),
                // Cmd-click selects only, Shift-click extends the rectangle.
                const bool cmd = e.mods.isCommandDown(), shift = e.mods.isShiftDown();
                if (shift) extendSelection(s, t);
                else selectCell(s, t);
                pressedS_ = s; pressedT_ = t;
                pressedLaunch_ = !cmd && !shift;
                didDrag_ = false;
                return;
            }
        }
    }
}

void SceneGridView::mouseDrag(const juce::MouseEvent& e) {
    if (pressedS_ < 0 || didDrag_) return;
    if (e.getDistanceFromDragStart() < 4) return;   // click/drag threshold
    const auto& scenes = proc.conductor().session().scenes;
    if (pressedS_ >= (int)scenes.size() || !scenes[(size_t)pressedS_].hasClip[(size_t)pressedT_])
        return;                                      // only filled cells drag
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
        if (!c->isDragAndDropActive()) {
            didDrag_ = true;
            dragActive_ = true;
            dragCancelled_ = false;
            dragFromS_ = pressedS_;
            dragFromT_ = pressedT_;
            hoverCell(-1, -1); // freeze-proof: block-drag owns the paint, not hover chips
            c->startDragging("sq4-cells", this);
        }
    }
}

void SceneGridView::mouseUp(const juce::MouseEvent&) {
    if (pressedS_ >= 0 && !didDrag_ && pressedLaunch_)
        cellClick(pressedS_, pressedT_);            // suppressed after a drag
    pressedS_ = pressedT_ = -1;
    didDrag_ = false;
}

void SceneGridView::hoverCell(int s, int t) {
    if (s == hoverCellS_ && t == hoverCellT_) return;
    hoverCellS_ = s; hoverCellT_ = t;
    repaint();
}

void SceneGridView::mouseMove(const juce::MouseEvent& e) {
    int t = -1;
    const int s = cellAt(e.getPosition(), t);
    hoverCell(s, t);
}

void SceneGridView::mouseExit(const juce::MouseEvent&) { hoverCell(-1, -1); }

// ---- layout ------------------------------------------------------------------

void SceneGridView::resized() {
    if (singleRow_) { layoutFocusStrip(); return; }
    for (int s = 0; s < kScenes; ++s) layoutRow(s);
}

void SceneGridView::layoutRow(int s) {
    const int y = s * 82;
    constexpr int rowX = 0, sceneWidth = 218, cellWidth = 292;
    sceneCardR[s] = { rowX, y, sceneWidth, 73 };
    for (int t = 0; t < kTracks; ++t)
        cellR[s][t] = { rowX + sceneWidth + 9 + t * (cellWidth + 9), y, cellWidth, 73 };

    // scene card internals (web SceneRow.tsx, card-relative): launch 32x32 at
    // (11,9); id column (num+name / status, split by paintSceneCard) at
    // x=51 w=99; mute/stop minis 22x22 at (155,14) and (180,14); dots+count
    // row along the bottom.
    const auto& card = sceneCardR[s];
    launchBtn[s] = { card.getX() + 11, card.getY() + 9, 32, 32 };
    idArea[s] = { card.getX() + 51, card.getY() + 12, 99, 27 };
    muteBtnR[s] = { card.getX() + 155, card.getY() + 14, 22, 22 };
    stopBtnR[s] = { card.getX() + 180, card.getY() + 14, 22, 22 };
    dotsArea[s] = { card.getX() + 10, card.getBottom() - 24, sceneWidth - 20, 16 };

    // clip cells: a 16x16 edit-glyph corner in the top-right, with the
    // trash-glyph the same size immediately to its left (4px gap).
    for (int t = 0; t < kTracks; ++t) {
        editGlyph[s][t] = { cellR[s][t].getRight() - 22, cellR[s][t].getY() + 6, 16, 16 };
        trashGlyph[s][t] = { editGlyph[s][t].getX() - 4 - 16, editGlyph[s][t].getY(), 16, 16 };
    }
}

// Launcher column: the session grid's lead column, stacked — a "< SESSION"
// back chip on top, then the same 218-wide scene cards at the same 73/82
// pitch, so focus mode reads as the session layout with the clip grid
// swapped for a device (web parity: .sq-launcher stacking SceneCard).
// railTrigger/railChip stay the rail's contract: the trigger is the card's
// launch button, the chip is the card body that retargets the editor.
void SceneGridView::layoutFocusStrip() {
    constexpr int cardW = 218, cardH = 73, pitch = 82, backH = 26, gap = 8;
    backChipR = { 0, 0, cardW, backH };
    const int top = backH + gap;
    for (int s = 0; s < kScenes; ++s) {
        sceneCardR[s] = { 0, top + s * pitch, cardW, cardH };
        const auto& card = sceneCardR[s];
        // Card internals: identical offsets to layoutRow, so the two views
        // paint the same card.
        launchBtn[s] = { card.getX() + 11, card.getY() + 9, 32, 32 };
        idArea[s] = { card.getX() + 51, card.getY() + 12, 99, 27 };
        muteBtnR[s] = { card.getX() + 155, card.getY() + 14, 22, 22 };
        stopBtnR[s] = { card.getX() + 180, card.getY() + 14, 22, 22 };
        dotsArea[s] = { card.getX() + 10, card.getBottom() - 24, cardW - 20, 16 };

        railTrigger[s] = launchBtn[s];
        railChip[s] = { launchBtn[s].getRight() + 4, card.getY(),
                        juce::jmax(1, muteBtnR[s].getX() - launchBtn[s].getRight() - 8), cardH };
    }
}

// ---- paint -------------------------------------------------------------------

void SceneGridView::paint(juce::Graphics& g) {
    if (singleRow_) { paintFocusStrip(g); return; }
    for (int s = 0; s < kScenes; ++s) {
        paintSceneCard(g, s);
        for (int t = 0; t < kTracks; ++t) paintCell(g, s, t);
    }
}

void SceneGridView::paintSceneCard(juce::Graphics& g, int s) {
    const auto& cond = proc.conductor();
    const auto& scenes = cond.session().scenes;
    const auto& tracks = cond.session().tracks;
    const auto& sc = scenes[(size_t)s];

    int clipTracks = 0, owned = 0;
    bool queued = false;
    for (int t = 0; t < kTracks; ++t) {
        if (sc.hasClip[(size_t)t]) {
            clipTracks++;
            if (cond.ownerOf(t) == s) owned++;
        }
        if (cond.queueOf(t) == s) queued = true;
    }
    const bool muted = cond.sceneMuted(s);
    const bool liveAny = owned > 0;
    const bool full = liveAny && owned == clipTracks;
    const bool hot = liveAny && !muted;

    auto rf = sceneCardR[s].toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff151924), rf.getX(), rf.getY(),
                                           juce::Colour(0xff0c0f16), rf.getX(), rf.getBottom(), false));
    g.fillRoundedRectangle(rf, 10.0f);
    g.setColour(hot ? juce::Colour(0xff4dff9e).withAlpha(0.35f) : col::line);
    g.drawRoundedRectangle(rf.reduced(0.5f), 10.0f, 1.0f);

    // launch button
    const bool anyOwner = owned > 0;
    auto lb = launchBtn[s].toFloat();
    g.setColour(anyOwner ? juce::Colour(0xff0e3120) : juce::Colour(0xff11141c));
    g.fillRoundedRectangle(lb, 8.0f);
    g.setColour(anyOwner ? juce::Colour(0xff4dff9e).withAlpha(0.6f) : juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(lb.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(queued ? col::text.withAlpha(qpulse()) : anyOwner ? juce::Colour(0xff4dff9e) : col::acN);
    g.fillPath(iconPlay(launchBtn[s].toFloat().withSizeKeepingCentre(8.0f, 10.0f)));

    // num + name / status
    auto id = idArea[s];
    auto nameRow = id.removeFromTop(id.getHeight() / 2);
    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    g.drawText(juce::String(s + 1).paddedLeft('0', 2), nameRow.removeFromLeft(18), juce::Justification::centredLeft);
    g.setColour(col::text);
    g.setFont(dispFont(9.0f));
    g.drawText(juce::String(sc.name), nameRow, juce::Justification::centredLeft);

    juce::String status;
    juce::Colour statusColour = col::textDim;
    if (muted && liveAny)      { status = juce::String::fromUTF8("LIVE \xc2\xb7 MUTED"); statusColour = col::acB; }
    else if (muted)            { status = "MUTED"; statusColour = col::acB; }
    else if (queued)           { status = "QUEUED"; statusColour = col::text; }
    else if (full)             { status = "LIVE"; statusColour = juce::Colour(0xff4dff9e); }
    else if (liveAny)          { status = "LIVE " + juce::String(owned) + "/" + juce::String(clipTracks); statusColour = juce::Colour(0xff4dff9e); }
    else                       { status = "READY"; statusColour = col::textDim; }
    g.setColour(statusColour);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, status, id, 1.2f);

    // M / S
    auto drawToggle = [&](juce::Rectangle<int> r, const char* txt, bool on, juce::Colour onColour) {
        auto rf2 = r.toFloat();
        g.setColour(on ? onColour : juce::Colour(0xff11141c));
        g.fillRoundedRectangle(rf2, 5.0f);
        g.setColour(on ? onColour : col::line);
        g.drawRoundedRectangle(rf2.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(on ? col::bg : col::textDim);
        g.setFont(monoFont(9.0f, true));
        g.drawText(txt, r, juce::Justification::centred);
    };
    drawToggle(muteBtnR[s], "M", muted, col::acB);
    { // stop button: same chrome as drawToggle, square icon instead of a letter
        auto rf2 = stopBtnR[s].toFloat();
        g.setColour(juce::Colour(0xff11141c));
        g.fillRoundedRectangle(rf2, 5.0f);
        g.setColour(col::line);
        g.drawRoundedRectangle(rf2.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(col::textDim);
        g.fillPath(iconStop(stopBtnR[s].toFloat().withSizeKeepingCentre(7.0f, 7.0f)));
    }

    // dots + clip count
    auto dr = dotsArea[s];
    for (int t = 0; t < kTracks; ++t) {
        auto d = dr.removeFromLeft(9).withSizeKeepingCentre(7, 7);
        dr.removeFromLeft(6);
        const bool has = t < (int)tracks.size() && sc.hasClip[(size_t)t];
        const juce::Colour tc { has ? tracks[(size_t)t].color : 0xff1a1f2au };
        const bool lit = has && cellAudible(s, t);
        g.setColour(lit ? tc : has ? tc.withAlpha(0.27f) : juce::Colour(0xff1a1f2a));
        g.fillEllipse(d.toFloat());
    }
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    juce::String cnt = juce::String(clipTracks) + (clipTracks == 1 ? " CLIP" : " CLIPS");
    g.drawText(cnt, dr, juce::Justification::centredRight);
}

void SceneGridView::paintCell(juce::Graphics& g, int s, int t) {
    const auto& scenes = proc.conductor().session().scenes;
    if (scenes[(size_t)s].hasClip[(size_t)t]) paintFilledCell(g, s, t);
    else paintEmptyCell(g, s, t);

    auto rf = cellR[s][t].toFloat().reduced(0.5f);
    // Compatible drop target during a drag: soft fill + violet ring.
    if (dropHighlight(s, t)) {
        g.setColour(col::acF.withAlpha(0.12f));
        g.fillRoundedRectangle(rf, 10.0f);
        g.setColour(col::acF.withAlpha(0.9f));
        g.drawRoundedRectangle(rf.reduced(1.0f), 9.0f, 1.6f);
    }
    // Selection outline — violet (col::acF), inset, so it reads apart from the
    // solid track-colour live border and the pulsing queued/stopping rings.
    if (cellSelected(s, t)) {
        g.setColour(col::acF);
        g.drawRoundedRectangle(rf.reduced(2.0f), 8.0f, 1.8f);
    }
}

void SceneGridView::paintEmptyCell(juce::Graphics& g, int s, int t) {
    const auto& tracks = proc.conductor().session().tracks;
    const juce::Colour tc { tracks[(size_t)t].color };
    const bool pass = isPassThrough(s, t);

    auto rf = cellR[s][t].toFloat().reduced(0.5f);
    // A stop cell (the default) reads as a small solid square on a slightly
    // denser well; pass-through reads as hollow with a dotted track-tinted
    // border and a soft dot, so the two states are legible without the tooltip
    // (web parity: .sq-cell-empty vs .sq-cell-empty.pass).
    g.setColour(juce::Colours::black.withAlpha(pass ? 0.10f : 0.28f));
    g.fillRoundedRectangle(rf, 10.0f);
    if (!pass) {
        g.setColour(juce::Colours::white.withAlpha(0.02f));
        g.drawRoundedRectangle(rf.reduced(0.5f), 9.5f, 1.0f); // inset hairline
    }

    juce::Path outline;
    outline.addRoundedRectangle(rf, 10.0f);
    juce::PathStrokeType stroke(1.0f);
    float dashLengths[] = { pass ? 1.0f : 3.0f, pass ? 2.0f : 3.0f };
    juce::Path dashed;
    stroke.createDashedStroke(dashed, outline, dashLengths, 2);
    g.setColour(pass ? tc.withAlpha(0.28f) : juce::Colours::white.withAlpha(0.14f));
    g.fillPath(dashed);

    const auto centre = cellR[s][t].toFloat().getCentre();
    if (pass) {
        // hollow dot (◦) — a passed-through track rides its previous clip.
        g.setColour(tc.interpolatedWith(juce::Colour(0xff333a48), 0.40f));
        g.drawEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(centre), 1.2f);
    } else {
        // solid square (■) — an empty cell stops its track on scene launch.
        g.setColour(juce::Colour(0xff4a5266));
        g.fillRect(juce::Rectangle<float>(7.0f, 7.0f).withCentre(centre));
    }

    if (hoverCellS_ == s && hoverCellT_ == t) {
        // + add chip (web .sq-cell-add): opens the device focused on this cell
        // so CREATE CLIP lands exactly here.
        g.setColour(tc.withAlpha(0.75f));
        g.fillPath(iconPlus(juce::Rectangle<float>(9.0f, 9.0f)
            .withCentre({ rf.getRight() - 12.0f, rf.getY() + 12.0f })));
    }
}

void SceneGridView::paintFilledCell(juce::Graphics& g, int s, int t) {
    const auto& cond = proc.conductor();
    const auto& tracks = cond.session().tracks;
    const auto& sc = cond.session().scenes[(size_t)s];
    const auto& clip = sc.clips[(size_t)t];
    const juce::Colour tc { tracks[(size_t)t].color };

    const bool live = cond.ownerOf(t) == s;
    const bool queued = cond.queueOf(t) == s;
    const bool stopping = cellStopping(s, t);
    // Port of ClipCell's `muted` (SceneRow.tsx): a live cell dims/shows MUTED
    // whenever the track isn't fully audible -- its own mute, another
    // track's solo, or its owning scene's mute -- not just a scene mute.
    const bool muted = live && !cellAudible(s, t);

    auto full = cellR[s][t];
    auto rf = full.toFloat().reduced(0.5f);
    if (live && !stopping) {
        g.setGradientFill(juce::ColourGradient(tc.withAlpha(0.09f), rf.getX(), rf.getY(),
                                               juce::Colours::transparentBlack, rf.getX(), rf.getY() + rf.getHeight() * 0.46f, false));
        g.fillRoundedRectangle(rf, 10.0f);
    }
    g.setGradientFill(juce::ColourGradient(col::panelHi, rf.getX(), rf.getY(),
                                           col::panelLo, rf.getX(), rf.getBottom(), false));
    g.fillRoundedRectangle(rf, 10.0f);
    // A playing clip should be unmistakable at a glance: a brighter track-color
    // border plus a soft, gently-breathing outer glow (web parity: .sq-cell.live
    // border 85% + sq-live-pulse). Stopping cells keep the plain border so the
    // amber stop ring reads instead.
    if (live && !stopping) {
        const float pulse = 0.5f + 0.5f * qpulse(); // 0.6..1.0-ish, slow breathe
        for (int ring = 2; ring >= 1; --ring) {
            g.setColour(tc.withAlpha(0.10f * pulse / (float)ring));
            g.drawRoundedRectangle(rf.expanded((float)ring * 1.5f), 11.0f, 1.5f);
        }
    }
    g.setColour(live ? tc.withAlpha(0.85f) : juce::Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(rf, 10.0f, live ? 1.5f : 1.0f);

    const float bodyAlpha = muted ? 0.32f : live ? 1.0f : 0.72f;

    // Cell internals (web ClipCell, cell-relative): head row y 9..24, steps
    // preview at (10, 31, w-20, 20), progress bar at (10, 57, w-20, 4).
    auto head = juce::Rectangle<int>(full.getX() + 9, full.getY() + 9, full.getWidth() - 18, 15);

    // eq / idle icon
    auto iconArea = head.removeFromLeft(16);
    if (live && !stopping) {
        const int step = proc.trackStep[t].load();
        const int phase = step >= 0 ? step % 4 : 0;
        static const int heights[4][3] = { {6, 10, 8}, {4, 7, 5}, {6, 4, 10}, {8, 10, 6} };
        const auto& h = heights[(size_t)phase];
        int x = iconArea.getX();
        for (int i = 0; i < 3; ++i) {
            g.setColour(tc.withAlpha(bodyAlpha));
            g.fillRect(juce::Rectangle<int>(x, iconArea.getBottom() - h[(size_t)i], 3, h[(size_t)i]));
            x += 4;
        }
    } else if (stopping) {
        g.setColour(col::acB.withAlpha(0.65f + stopPulse() * 0.35f));
        g.fillPath(iconStop(iconArea.toFloat().withSizeKeepingCentre(8.0f, 8.0f)));
    } else {
        g.setColour(juce::Colour(0xff4a5266).withAlpha(bodyAlpha));
        g.fillPath(iconPlay(iconArea.toFloat().withSizeKeepingCentre(7.0f, 9.0f)));
    }
    head.removeFromLeft(6);

    // "{bars}B" chip on the right
    auto chip = head.removeFromRight(24);
    g.setColour(col::textDim.withAlpha(bodyAlpha));
    g.setFont(monoFont(7.0f));
    g.drawText(juce::String(clip.bars) + "B", chip, juce::Justification::centredRight);
    head.removeFromRight(4);

    g.setColour((live ? tc : col::acN).withAlpha(bodyAlpha));
    g.setFont(monoFontMedium(9.5f));
    g.drawText(juce::String(clip.name), head, juce::Justification::centredLeft);

    auto stepsArea = juce::Rectangle<int>(full.getX() + 10, full.getY() + 31, full.getWidth() - 20, 20);
    if (!clip.bytes.empty()) {
        auto steps = fable::sqPreviewSteps(tracks[(size_t)t].machine, clip.bytes.data());
        const float bw = static_cast<float>(stepsArea.getWidth()) / static_cast<float>(fable::SQ_STEPS_PER_BAR);
        for (int i = 0; i < fable::SQ_STEPS_PER_BAR; ++i) {
            const auto& sb = steps[(size_t)i];
            const float bh = (float)juce::jlimit(2, 20, sb.h);
            juce::Rectangle<float> bar(static_cast<float>(stepsArea.getX()) + static_cast<float>(i) * bw + 1.0f,
                                        static_cast<float>(stepsArea.getBottom()) - bh,
                                        juce::jmax(1.0f, bw - 2.0f), bh);
            g.setColour(sb.on ? tc.withAlpha(0.6f * bodyAlpha) : juce::Colours::white.withAlpha(0.08f * bodyAlpha));
            g.fillRoundedRectangle(bar, 1.0f);
        }
    }

    auto progress = juce::Rectangle<int>(full.getX() + 10, full.getY() + 57, full.getWidth() - 20, 4);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.fillRoundedRectangle(progress.toFloat(), 1.5f);
    if (live && clip.bars > 0) {
        const int bar = proc.trackBar[t].load();
        const int step = proc.trackStep[t].load();
        if (bar >= 0 && step >= 0) {
            const int totalSteps = clip.bars * fable::SQ_STEPS_PER_BAR;
            const int pos = ((bar * fable::SQ_STEPS_PER_BAR + step) % totalSteps + totalSteps) % totalSteps;
            const float frac = juce::jlimit(0.0f, 1.0f, (float)pos / (float)totalSteps);
            auto lit = progress.withWidth((int)(static_cast<float>(progress.getWidth()) * frac));
            // Brighter than the plain track color + a soft glow so the live
            // sweep stands out (web parity: .sq-cell-progress div).
            g.setColour(tc.withAlpha(0.35f));
            g.fillRoundedRectangle(lit.toFloat().expanded(0.0f, 1.5f), 2.0f);
            g.setColour(tc.interpolatedWith(juce::Colours::white, 0.12f));
            g.fillRoundedRectangle(lit.toFloat(), 1.5f);
        }
    }

    // edit / delete chips — hover-revealed, like the web's .sq-cell-tools.
    if (hoverCellS_ == s && hoverCellT_ == t) {
        g.setColour(tc.withAlpha(0.90f));
        g.fillPath(iconPencil(editGlyph[s][t].toFloat().withSizeKeepingCentre(9.0f, 9.0f)));
        g.setColour(col::textDim);
        g.fillPath(iconTrash(trashGlyph[s][t].toFloat().withSizeKeepingCentre(8.0f, 9.0f)));
    }

    if (queued) {
        g.setColour(tc.withAlpha(qpulse()));
        g.drawRoundedRectangle(rf, 10.0f, 1.4f);
    }
    if (stopping) {
        const float pulse = stopPulse();
        const auto inset = rf.reduced(1.0f + pulse * 4.0f);
        juce::Path outline, dashed;
        outline.addRoundedRectangle(inset, juce::jmax(4.0f, 9.0f - pulse * 4.0f));
        const float dashes[] = { 5.0f, 3.0f };
        juce::PathStrokeType(1.6f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(col::acB.withAlpha(0.45f + pulse * 0.5f));
        g.fillPath(dashed);
        g.setFont(monoFont(7.0f, true));
        g.drawText("STOPPING", full.reduced(9, 7).removeFromBottom(12),
                   juce::Justification::centredRight);
    }
    if (muted) {
        g.setColour(col::acB);
        g.setFont(monoFont(6.5f));
        drawSpaced(g, "MUTED", { full.getRight() - 60, full.getY() + 5, 40, 10 }, 1.4f, juce::Justification::right);
    }
}

void SceneGridView::paintFocusStrip(juce::Graphics& g) {
    // Back chip (web: .sq-launcher-back). The cards below sit straight on the
    // chassis, exactly as they do in session mode — no panel wrapper.
    auto rf = backChipR.toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff141824), rf.getX(), rf.getY(),
                                           juce::Colour(0xff0c0f16), rf.getX(), rf.getBottom(), false));
    g.fillRoundedRectangle(rf, 10.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(rf.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(juce::Colour(0xffcfd6e4));
    g.setFont(dispFont(9.0f));
    drawSpaced(g, "< SESSION", backChipR.reduced(12, 0), 1.8f);

    paintRail(g);
}

// Playhead through the focused track's clip, from the device's own step/bar
// acks (proc.trackStep/trackBar — the web's pos[track]) rather than a
// free-running animation, so the fill cannot drift from what is sounding.
float SceneGridView::railProgress(int scene) const {
    if (focusTrack_ < 0 || focusTrack_ >= kTracks) return -1.0f;
    if (proc.conductor().ownerOf(focusTrack_) != scene) return -1.0f;
    const int step = proc.trackStep[focusTrack_].load();
    const int bar = proc.trackBar[focusTrack_].load();
    if (step < 0) return -1.0f;
    const auto& scenes = proc.conductor().session().scenes;
    if (scene < 0 || scene >= (int)scenes.size() || !scenes[(size_t)scene].hasClip[(size_t)focusTrack_])
        return -1.0f;
    const int bars = juce::jmax(1, scenes[(size_t)scene].clips[(size_t)focusTrack_].bars);
    const float done = (float)(juce::jmax(0, bar) * fable::SQ_STEPS_PER_BAR + step + 1);
    return juce::jlimit(0.0f, 1.0f, done / (float)(bars * fable::SQ_STEPS_PER_BAR));
}

void SceneGridView::paintRail(juce::Graphics& g) {
    for (int s = 0; s < kScenes; ++s) {
        // The same card the session grid draws — one painter, so the two
        // views cannot drift.
        paintSceneCard(g, s);

        auto r = sceneCardR[s].toFloat();
        // The retargeted scene is ringed (web: .sq-scene-card.active).
        if (s == singleRowScene_) {
            g.setColour(juce::Colour(0xffcfd6e4));
            g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.4f);
        }

        // .sq-scene-progress: the focused track's clip playhead, along the
        // bottom edge of the card that owns it.
        const float progress = railProgress(s);
        if (progress >= 0.0f) {
            g.setColour(juce::Colour(0xff4dff9e));
            g.fillRect(juce::Rectangle<float>(r.getX() + 1.0f, r.getBottom() - 3.0f,
                                              (r.getWidth() - 2.0f) * progress, 2.0f));
        }
    }
}

} // namespace fui
