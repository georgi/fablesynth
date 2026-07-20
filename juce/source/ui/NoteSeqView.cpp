#include "NoteSeqView.h"
#include <cmath>

namespace fui {

using fable::NoteSeqStep;

static const char* const kPatNames[fable::SEQ_NPATTERNS] = { "1", "2", "3", "4" };

// Shared step-editing layout (StepEditOps.h / src/shared/seqEdit.ts WT1_LAYOUT):
// 3 packed bytes/step, 16 steps/pattern, whole-pattern block = 48 bytes.
static const fable::StepLayout kNoteLayout {
    fable::SEQ_STEP_STRIDE, fable::SEQ_STEPS,
    fable::SEQ_STEPS * fable::SEQ_STEP_STRIDE, 0
};
// noteseq.ts EMPTY_STEP: duration 1 (flags 1<<2), note 0, oct 0 (byte 1).
static const std::vector<uint8_t> kEmptyStep { (uint8_t)(1 << 2), 0, 1 };
static constexpr int kDragThreshold = 4; // px, matches SequenceLengthControl.tsx
static constexpr int kMaxNote = fable::SEQ_NOTE_LANES - 1; // top lane (paste clamp)

// ---- geometry (index.css .ns-*, rack-relative px) -----------------------------
// panel padding 8px 12px 10px; head 24px + 10px margin; body: legend 36px +
// 8px gap, 16 columns with 5px gaps, then the clock column (border-left,
// 6px padding). Column: 143px of lanes (12 cells, 1px gaps), oct 5+20,
// acc 4+14, step number 4+10.

static constexpr int kPadX = 12, kHeadY = 8, kHeadH = 24;
static constexpr int kBodyY = kHeadY + kHeadH + 10;
static constexpr int kLegendW = 36, kLegendGap = 8;
static constexpr int kLanesH = 143;
static constexpr float kColGap = 5.0f;
static constexpr int kOctY = kLanesH + 5, kOctH = 20;
static constexpr int kAccY = kOctY + kOctH + 4, kAccH = 14;
static constexpr int kNumY = kAccY + kAccH + 4;
static constexpr int kClockW = 60, kClockGap = 6;

NoteSeqView::NoteSeqView(WtUiModel& m)
    : model(m),
      root_(m.parameters(), "seq.root", Accent::A) {
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(root_);
    // params.ts fmtNote: seq.root shows a note name (48 -> C3), not the number.
    root_.nameProvider = [](int v) {
        static const char* const names[12] =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String(names[((v % 12) + 12) % 12])
             + juce::String((int)std::floor(v / 12.0) - 1);
    };
    startTimerHz(30);
}

void NoteSeqView::resized() {
    // .ns-clock: ROOT stays with pitch editing; timing lives in the top bar.
    const int x = getWidth() - kPadX - kClockW;
    root_.setBounds(x, kBodyY, kClockW, 18);
}

juce::Rectangle<int> NoteSeqView::transportBounds() const {
    return { kPadX, kHeadY, 34, kHeadH };             // .ns-transport 34x24
}

juce::Rectangle<int> NoteSeqView::patternBounds(int i) const {
    const int x0 = kPadX + 34 + 12 + 96 + 12;         // transport, gap, title, gap
    return { x0 + i * (26 + 5), kHeadY, 26, kHeadH }; // .ns-pattern 26x24, gap 5
}

juce::Rectangle<int> NoteSeqView::sequenceLengthBounds() const {
    return { patternBounds(3).getRight() + 12, kHeadY - 3, 170, 30 };
}

juce::Rectangle<int> NoteSeqView::randBounds() const {
    return { sequenceLengthBounds().getRight() + 12, kHeadY, 52, kHeadH };
}

juce::Rectangle<int> NoteSeqView::colBounds(int step) const {
    const int gx = kPadX + kLegendW + kLegendGap;
    const int gw = getWidth() - kPadX - kClockW - kClockGap - kLegendGap - gx;
    const float w = ((float)gw - 15.0f * kColGap) / 16.0f;
    const float x = static_cast<float>(gx) + static_cast<float>(step) * (w + kColGap);
    return juce::Rectangle<float>(x, (float)kBodyY, w, (float)(kNumY + 10)).toNearestInt();
}

juce::Rectangle<int> NoteSeqView::cellBounds(int step, int note) const {
    const auto c = colBounds(step);
    // 12 lanes over kLanesH with 1px gaps, note 11 on top (web lane order)
    const float ch = (kLanesH - 11.0f) / 12.0f;
    const int r = fable::SEQ_NOTE_LANES - 1 - note;
    const float y = static_cast<float>(c.getY()) + static_cast<float>(r) * (ch + 1.0f);
    return juce::Rectangle<float>((float)c.getX(), y, (float)c.getWidth(), ch).toNearestInt();
}

juce::Rectangle<int> NoteSeqView::octBounds(int step) const {
    const auto c = colBounds(step);
    return { c.getX(), c.getY() + kOctY, c.getWidth(), kOctH };
}

juce::Rectangle<int> NoteSeqView::accBounds(int step) const {
    const auto c = colBounds(step);
    return { c.getX(), c.getY() + kAccY, c.getWidth(), kAccH };
}

// .ns-step-num — the selection strip: Shift-click/drag sweeps the step range,
// a plain click inside the current selection starts a content drag-move.
juce::Rectangle<int> NoteSeqView::stepNumBounds(int step) const {
    const auto c = colBounds(step);
    return c.withY(c.getY() + kNumY).withHeight(10);
}

// Inverse of colBounds' x geometry — which column a drag's pointer x lands
// in, clamped to the grid (drags can wander past its edges).
int NoteSeqView::stepAtX(int x) const {
    const int gx = kPadX + kLegendW + kLegendGap;
    const int gw = getWidth() - kPadX - kClockW - kClockGap - kLegendGap - gx;
    const float w = ((float)gw - 15.0f * kColGap) / 16.0f;
    if (w <= 0.0f) return 0;
    const float rel = (float)(x - gx) / (w + kColGap);
    return juce::jlimit(0, fable::SEQ_STEPS - 1, (int)std::floor(rel));
}

// Inverse of cellBounds' lane math: note 11 sits on top, C at the bottom.
// Clamped to the lane range since drags wander past the grid edges.
int NoteSeqView::noteAtY(int y) const {
    const float ch = (kLanesH - 11.0f) / 12.0f;
    const float rel = (float)(y - kBodyY) / (ch + 1.0f);
    const int row = juce::jlimit(0, fable::SEQ_NOTE_LANES - 1, (int)std::floor(rel));
    return fable::SEQ_NOTE_LANES - 1 - row;
}

// The whole 16×12 note-lane area (excludes the oct / acc / step-number rows).
juce::Rectangle<int> NoteSeqView::gridBounds() const {
    const auto a = colBounds(0), b = colBounds(fable::SEQ_STEPS - 1);
    return { a.getX(), kBodyY, b.getRight() - a.getX(), kLanesH };
}

// Floating CUT · COPY · DUP · DEL · ✕ toolbar centered over the selected
// columns (SeqSelectionMenu.tsx), drawn over the top lanes while a rect exists.
juce::Rectangle<int> NoteSeqView::selMenuBounds() const {
    constexpr int bw = 34, gap = 2, n = 5, h = 16;
    constexpr int w = n * bw + (n - 1) * gap;
    const auto nrm = fable::rectNorm(rect_);
    const auto lo = colBounds(juce::jlimit(0, fable::SEQ_STEPS - 1, nrm.stepLo));
    const auto hi = colBounds(juce::jlimit(0, fable::SEQ_STEPS - 1, nrm.stepHi));
    const int cx = (lo.getX() + hi.getRight()) / 2;
    const auto grid = gridBounds();
    const int x = juce::jlimit(grid.getX(), juce::jmax(grid.getX(), grid.getRight() - w), cx - w / 2);
    return { x, kBodyY + 1, w, h };
}

juce::Rectangle<int> NoteSeqView::selMenuButton(int i) const {
    constexpr int bw = 34, gap = 2;
    const auto m = selMenuBounds();
    return { m.getX() + i * (bw + gap), m.getY(), bw, m.getHeight() };
}

// Is (step, note) inside the live sweep rect (while dragging) or the committed
// selection? Mirrors SeqPanel.tsx's inRect over rectNorm(pending ?? rectSel).
bool NoteSeqView::inRect(int step, int note) const {
    if (sweeping_) {
        const auto n = fable::rectNorm(pending_);
        return step >= n.stepLo && step <= n.stepHi && note >= n.noteLo && note <= n.noteHi;
    }
    if (!hasRect_) return false;
    const auto n = fable::rectNorm(rect_);
    return step >= n.stepLo && step <= n.stepHi && note >= n.noteLo && note <= n.noteHi;
}

// The origin step of a note grabbable at (step, note): the cell itself if lit,
// else a longer note whose painted body covers this column (useSeqNoteDrag's
// long-note-body grab). Returns -1 if no note is grabbable here.
int NoteSeqView::grabNoteAt(int step, int note) const {
    const int pat = model.editPattern();
    const auto here = model.sequenceStep(pat, step);
    if (here.on && here.note == note) return step;
    for (int c = step - 1; c >= 0; --c) {
        const auto v = model.sequenceStep(pat, c);
        if (v.on && v.note == note && c + v.duration > step) return c;
    }
    return -1;
}

juce::Rectangle<int> NoteSeqView::resizeBounds(int step) const {
    const auto st = model.sequenceStep(model.editPattern(), step);
    if (!st.on) return {};
    auto cell = cellBounds(step, st.note);
    const int w = colBounds(step).getWidth() * st.duration + (int)std::round(kColGap * (float)(st.duration - 1));
    const int gridRight = getWidth() - kPadX - kClockW - kClockGap;
    cell.setWidth(juce::jmax(1, juce::jmin(w, gridRight - cell.getX())));
    return cell.removeFromRight(6).expanded(2, 2);
}

// ---- store handlers ----------------------------------------------------------

void NoteSeqView::toggleCell(int step, int note) {
    const int pat = model.editPattern();
    NoteSeqStep cur = model.sequenceStep(pat, step);
    if (cur.on && cur.note == note) {              // tap again = rest
        cur.on = false; cur.acc = false; cur.duration = 1;
    } else {
        cur.on = true; cur.note = note;
    }
    model.setSequenceStep(pat, step, cur);
    repaint();
}

void NoteSeqView::cycleStepOct(int step) {
    const int pat = model.editPattern();
    NoteSeqStep cur = model.sequenceStep(pat, step);
    cur.oct = cur.oct >= fable::SEQ_OCT_MAX ? fable::SEQ_OCT_MIN : cur.oct + 1; // noteseq.ts cycleOct
    model.setSequenceStep(pat, step, cur);
    repaint();
}

void NoteSeqView::toggleStepAcc(int step) {
    const int pat = model.editPattern();
    NoteSeqStep cur = model.sequenceStep(pat, step);
    if (!cur.on) return;
    cur.acc = !cur.acc;
    model.setSequenceStep(pat, step, cur);
    repaint();
}

void NoteSeqView::resizeStep(int step, int duration) {
    auto cur = model.sequenceStep(model.editPattern(), step);
    if (!cur.on) return;
    const int bars = model.capabilities().hosted ? model.clipBars() : (int)model.chain().size();
    const int absolute = model.editPattern() * fable::SEQ_STEPS + step;
    int limit = juce::jmin(fable::SEQ_MAX_NOTE_STEPS, bars * fable::SEQ_STEPS - absolute);
    for (int a = absolute + 1; a < bars * fable::SEQ_STEPS; ++a) {
        const auto other = model.sequenceStep(a / fable::SEQ_STEPS, a % fable::SEQ_STEPS);
        if (other.on && other.note == cur.note && other.oct == cur.oct) { limit = a - absolute; break; }
    }
    cur.duration = juce::jlimit(1, limit, duration);
    model.setSequenceStep(model.editPattern(), step, cur);
    repaint();
}

// noteseq.ts randomPattern: a sparse minor line with occasional octave throws,
// accents and varied note lengths — melodic flavor rather than BL-1's acid crawl.
void NoteSeqView::randomize() {
    static const int pool[] = { 0, 0, 2, 3, 5, 7, 10 };
    const int pat = model.editPattern();
    for (int i = 0; i < fable::SEQ_STEPS; ++i) {
        NoteSeqStep s;
        s.on = rng_.nextFloat() < 0.62f;
        s.note = pool[rng_.nextInt(7)];
        s.oct = rng_.nextFloat() < 0.18f ? (rng_.nextFloat() < 0.5f ? -1 : 1) : 0;
        s.acc = s.on && rng_.nextFloat() < 0.22f;
        s.duration = 1 + rng_.nextInt(4);   // 1..4 steps (noteseq.ts randomPattern)
        model.setSequenceStep(pat, i, s);
    }
    repaint();
}

void NoteSeqView::patternClick(int i) {
    model.setEditPattern(i);
    repaint();
}

void NoteSeqView::setSequenceLength(int bars) {
    bars = juce::jlimit(1, fable::SEQ_NPATTERNS, bars);
    std::vector<int> sequence;
    for (int bar = 0; bar < bars; ++bar) sequence.push_back(bar);
    model.setChain(std::move(sequence));
    repaint();
}

// ---- step-range edit verbs (editing-concept.md decision 6) --------------------
// StepEditOps.h is JUCE-free and knows nothing of WtUiModel, so every verb
// round-trips the whole 4-pattern block: read all steps into a packed buffer,
// run the pure op, write every step back through model.setSequenceStep (the
// model's single mutation chokepoint — mirrors the web's _setPatterns).

std::vector<uint8_t> NoteSeqView::snapshotAllPatterns() const {
    std::vector<uint8_t> buf((size_t)(fable::SEQ_NPATTERNS * kNoteLayout.patternSize), 0);
    for (int p = 0; p < fable::SEQ_NPATTERNS; ++p)
        for (int s = 0; s < fable::SEQ_STEPS; ++s)
            fable::setNoteSeqStep(buf.data(), p, s, model.sequenceStep(p, s));
    return buf;
}

void NoteSeqView::applyAllPatterns(const std::vector<uint8_t>& buf) {
    for (int p = 0; p < fable::SEQ_NPATTERNS; ++p)
        for (int s = 0; s < fable::SEQ_STEPS; ++s)
            model.setSequenceStep(p, s, fable::getNoteSeqStep(buf.data(), p, s));
    repaint();
}

NoteSeqView::SeqSnapshot NoteSeqView::snapshot() const {
    return { snapshotAllPatterns(), model.chain() };
}

void NoteSeqView::restore(const SeqSnapshot& snap) {
    model.setChain(snap.chain);
    applyAllPatterns(snap.patterns);
}

void NoteSeqView::setSelection(const fable::RectSel& r) {
    rect_ = r;
    hasRect_ = true;
    const auto n = fable::rectNorm(r);
    hasLastCell_ = true; lastCellStep_ = n.stepLo; lastCellNote_ = n.noteHi;
    repaint();
}

void NoteSeqView::selectAllCells() {
    setSelection({ 0, fable::SEQ_STEPS - 1, 0, fable::SEQ_NOTE_LANES - 1 });
}

void NoteSeqView::clearSelection() { hasRect_ = false; repaint(); }

void NoteSeqView::toggleAt(int step, int note) {
    toggleCell(step, note);
    hasLastCell_ = true; lastCellStep_ = step; lastCellNote_ = note;
}

// Cmd-C / menu COPY: capture the rect (only lit, in-band cells) or — with no
// selection — the whole edit pattern. Refreshes the Cmd-V clipboard.
void NoteSeqView::copySel() {
    const auto buf = snapshotAllPatterns();
    const int pat = model.editPattern();
    clip_.valid = true;
    if (hasRect_) {
        clip_.isPattern = false;
        clip_.rect = fable::copyRect(buf, kNoteLayout, pat, rect_);
    } else {
        clip_.isPattern = true;
        clip_.pattern = fable::copyPattern(buf, kNoteLayout, pat);
    }
}

// Cmd-X: copy, then clear the rect's in-band lit cells in the same undo entry.
// With no rect it behaves like copy (nothing to clear).
void NoteSeqView::cutSel() {
    copySel();
    if (!hasRect_) return;
    const auto before = snapshot();
    auto next = fable::clearRect(before.patterns, kNoteLayout, model.editPattern(), rect_, kEmptyStep);
    history_.push(before);
    applyAllPatterns(next);
}

// Cmd-V: paste the clipboard at the paste anchor — the current rect's top-left,
// else the last cell touched, else the timeline start (useSeqEditKeys.ts).
void NoteSeqView::pasteSel() {
    if (!clip_.valid) return;
    const auto before = snapshot();
    const int pat = model.editPattern();
    std::vector<uint8_t> next;
    if (clip_.isPattern) {
        next = fable::pastePattern(before.patterns, kNoteLayout, pat, clip_.pattern);
        history_.push(before);
        applyAllPatterns(next);
        return;
    }
    int atStep, atNote;
    if (hasRect_)            { const auto n = fable::rectNorm(rect_); atStep = n.stepLo; atNote = n.noteHi; }
    else if (hasLastCell_)   { atStep = lastCellStep_; atNote = lastCellNote_; }
    else                     { atStep = 0; atNote = clip_.rect.noteHi; }
    const int dNote = atNote - clip_.rect.noteHi;
    next = fable::pasteRect(before.patterns, kNoteLayout, pat, atStep, dNote, clip_.rect, kMaxNote);
    history_.push(before);
    applyAllPatterns(next);
    // Re-select the pasted rectangle (SeqPanel re-anchors rectSel post-paste).
    rect_ = { atStep, atStep + clip_.rect.wSteps - 1,
              clip_.rect.noteLo + dNote, clip_.rect.noteHi + dNote };
    hasRect_ = true;
}

// Cmd-D / menu DUP: with a rect, copy it immediately to its right (same pitch
// band; a full no-op if that lands past the pattern end). With none, duplicate
// the edit bar onto the next — the classic "duplicate bar" gesture.
void NoteSeqView::duplicateSel() {
    const int pat = model.editPattern();
    if (hasRect_) {
        const auto n = fable::rectNorm(rect_);
        if (n.stepHi + 1 >= fable::SEQ_STEPS) return;
        const auto before = snapshot();
        const auto data = fable::copyRect(before.patterns, kNoteLayout, pat, rect_);
        auto next = fable::pasteRect(before.patterns, kNoteLayout, pat, n.stepHi + 1, 0, data, kMaxNote);
        history_.push(before);
        applyAllPatterns(next);
        const int w = n.stepHi - n.stepLo + 1;
        rect_ = { n.stepHi + 1, n.stepHi + w, n.noteLo, n.noteHi }; hasRect_ = true;
    } else {
        const auto before = snapshot();
        const int nextPat = juce::jmin(fable::SEQ_NPATTERNS - 1, pat + 1);
        const auto data = fable::copyPattern(before.patterns, kNoteLayout, pat);
        history_.push(before);
        applyAllPatterns(fable::pastePattern(before.patterns, kNoteLayout, nextPat, data));
        if ((int)model.chain().size() <= nextPat) setSequenceLength(nextPat + 1);
        model.setEditPattern(nextPat);
    }
    repaint();
}

// Delete / menu DEL: clear the same in-band lit cells CUT would have captured.
void NoteSeqView::deleteSel() {
    if (!hasRect_) return;
    const auto before = snapshot();
    auto next = fable::clearRect(before.patterns, kNoteLayout, model.editPattern(), rect_, kEmptyStep);
    history_.push(before);
    applyAllPatterns(next);
}

void NoteSeqView::undoEdit() {
    const auto current = snapshot();
    SeqSnapshot restored;
    if (history_.undo(current, restored)) restore(restored);
}

void NoteSeqView::redoEdit() {
    const auto current = snapshot();
    SeqSnapshot restored;
    if (history_.redo(current, restored)) restore(restored);
}

// Note drag release (useSeqNoteDrag): move one note, transposing pitch and
// shifting time, committed once. Alt copies. Selects the dropped cell.
void NoteSeqView::commitNoteMove(int srcStep, int srcNote, int destStep, int destNote, bool copy) {
    destStep = juce::jlimit(0, fable::SEQ_STEPS - 1, destStep);
    destNote = juce::jlimit(0, fable::SEQ_NOTE_LANES - 1, destNote);
    if (srcStep == destStep && srcNote == destNote) return;
    const auto before = snapshot();
    const fable::RectSel one { srcStep, srcStep, srcNote, srcNote };
    fable::RectMoveOpts opts; opts.copy = copy; opts.emptyStep = kEmptyStep; opts.maxNote = kMaxNote;
    auto next = fable::moveRect(before.patterns, kNoteLayout, model.editPattern(), one,
                                destStep - srcStep, destNote - srcNote, opts);
    history_.push(before);
    applyAllPatterns(next);
    rect_ = { destStep, destStep, destNote, destNote }; hasRect_ = true;
    hasLastCell_ = true; lastCellStep_ = destStep; lastCellNote_ = destNote;
}

// Block move release (drag from inside the rect): shift the whole selection in
// step and note. The delta is clamped so the block stays on the grid (the web
// review's overshoot-drag desync fix). Alt copies.
void NoteSeqView::commitBlockMove(int dStep, int dNote, bool copy) {
    if (!hasRect_) return;
    const auto n = fable::rectNorm(rect_);
    dStep = juce::jlimit(-n.stepLo, (fable::SEQ_STEPS - 1) - n.stepHi, dStep);
    dNote = juce::jlimit(-n.noteLo, (fable::SEQ_NOTE_LANES - 1) - n.noteHi, dNote);
    if (dStep == 0 && dNote == 0) return; // no movement: full no-op, no history
    const auto before = snapshot();
    fable::RectMoveOpts opts; opts.copy = copy; opts.emptyStep = kEmptyStep; opts.maxNote = kMaxNote;
    auto next = fable::moveRect(before.patterns, kNoteLayout, model.editPattern(), rect_, dStep, dNote, opts);
    history_.push(before);
    applyAllPatterns(next);
    rect_ = { n.stepLo + dStep, n.stepHi + dStep, n.noteLo + dNote, n.noteHi + dNote }; hasRect_ = true;
}

// Menu CUT / COPY: pick the selection up. The captured cells trail the cursor
// as ghost notes until the next click drops them (useSeqGhostPaste). A cancelled
// CUT mutates nothing — the source is only cleared at drop time.
void NoteSeqView::beginGhostPaste(bool cut) {
    if (!hasRect_) return;
    const auto buf = snapshotAllPatterns();
    ghostData_ = fable::copyRect(buf, kNoteLayout, model.editPattern(), rect_);
    if (ghostData_.cells.empty()) return;
    clip_.valid = true; clip_.isPattern = false; clip_.rect = ghostData_; // keep Cmd-V in sync
    ghost_ = true; ghostCut_ = cut; ghostSrc_ = rect_; ghostHasHover_ = false;
    hasRect_ = false; // the menu closes
    repaint();
}

// Drop the carried ghost at (atStep, atNote): top-left anchored, transposed so
// the rect's top note lands on the hovered lane. A CUT clears its source in the
// same undo entry as the paste.
void NoteSeqView::dropGhost(int atStep, int atNote) {
    if (!ghost_) return;
    const auto before = snapshot();
    const int pat = model.editPattern();
    const int dNote = atNote - ghostData_.noteHi;
    std::vector<uint8_t> base = ghostCut_
        ? fable::clearRect(before.patterns, kNoteLayout, pat, ghostSrc_, kEmptyStep)
        : before.patterns;
    auto next = fable::pasteRect(base, kNoteLayout, pat, atStep, dNote, ghostData_, kMaxNote);
    history_.push(before);
    applyAllPatterns(next);
    rect_ = { atStep, atStep + ghostData_.wSteps - 1,
              ghostData_.noteLo + dNote, ghostData_.noteHi + dNote };
    hasRect_ = true;
    ghost_ = false; ghostHasHover_ = false;
    repaint();
}

// Bar-chip drag release — store.movePattern: swap two patterns wholesale, or
// (Alt) copy one over the other.
void NoteSeqView::movePattern(int from, int to, bool copy) {
    const int a = juce::jlimit(0, fable::SEQ_NPATTERNS - 1, from);
    const int b = juce::jlimit(0, fable::SEQ_NPATTERNS - 1, to);
    if (a == b) return;
    const auto before = snapshot();
    const auto dataA = fable::copyPattern(before.patterns, kNoteLayout, a);
    std::vector<uint8_t> next;
    if (copy) {
        next = fable::pastePattern(before.patterns, kNoteLayout, b, dataA);
    } else {
        const auto dataB = fable::copyPattern(before.patterns, kNoteLayout, b);
        next = fable::pastePattern(fable::pastePattern(before.patterns, kNoteLayout, a, dataB),
                                    kNoteLayout, b, dataA);
    }
    history_.push(before);
    applyAllPatterns(next);
}

void NoteSeqView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    const auto caps = model.capabilities();
    if (!model.hasTargetClip()) {
        if (randBounds().contains(pos)) model.createTargetClip();
        return;
    }

    // Carrying a ghost: the next click drops it (or, outside the grid, cancels).
    if (ghost_) {
        if (gridBounds().contains(pos)) dropGhost(stepAtX(pos.x), noteAtY(pos.y));
        else cancelGesture();
        return;
    }

    // Floating selection menu (drawn over the top lanes while a rect exists).
    if (hasRect_) {
        for (int i = 0; i < 5; ++i) if (selMenuButton(i).contains(pos)) {
            switch (i) {
                case 0: beginGhostPaste(true);  break;  // CUT
                case 1: beginGhostPaste(false); break;  // COPY
                case 2: duplicateSel();         break;  // DUP
                case 3: deleteSel();            break;  // DEL
                default: clearSelection();      break;  // ✕
            }
            repaint();
            return;
        }
    }

    if (caps.ownsTransport && transportBounds().contains(pos)) {       // play/stop
        model.setSequencerPlaying(!model.sequencerPlaying());
        repaint();
        return;
    }
    for (int i = 0; i < juce::jmin(fable::SEQ_NPATTERNS, model.clipBars()); ++i)
        if (patternBounds(i).contains(pos)) {
            // Bar-chip drag (decision 6): standalone only, mirroring
            // SequenceLengthControl.tsx's onMovePattern prop being omitted
            // in hosted mode. A plain click still selects the bar.
            if (caps.hosted) { patternClick(i); return; }
            barDragFrom_ = i; barDragging_ = false; barDropTarget_ = -1;
            return;
        }
    if (caps.supportsPatternChain && sequenceLengthBounds().contains(pos)) {
        const auto length = sequenceLengthBounds();
        const int bars = caps.hosted ? model.clipBars() : (int)model.chain().size();
        if (pos.x < length.getX() + 38) setSequenceLength(bars - 1);
        else if (pos.x >= length.getRight() - 38) setSequenceLength(bars + 1);
        return;
    }
    if (randBounds().contains(pos)) { randomize(); return; }
    for (int s = 0; s < fable::SEQ_STEPS; ++s) if (resizeBounds(s).contains(pos)) {
        resizeStep_ = s; resizeStartDuration_ = model.sequenceStep(model.editPattern(), s).duration; return;
    }

    // ---- note lanes: Shift-sweep / block-move / note-drag / deferred toggle ----
    // Priority mirrors SeqPanel.tsx's onPointerDown ladder. The toggle is
    // deferred to mouseUp so a gesture that never moves still toggles the cell.
    if (gridBounds().contains(pos)) {
        const int step = stepAtX(pos.x), note = noteAtY(pos.y);
        downStep_ = step; downNote_ = note;
        if (e.mods.isShiftDown()) {                              // 1) rectangle sweep
            sweeping_ = true; pending_ = { step, step, note, note };
            repaint();
            return;
        }
        if (hasRect_ && inRect(step, note)) {                    // 2) block move
            moveArmed_ = true; moving_ = false;
            moveOriginStep_ = step; moveOriginNote_ = note;
            moveHoverStep_ = step; moveHoverNote_ = note;
            return;
        }
        const int origin = grabNoteAt(step, note);              // 3) note drag
        if (origin >= 0) {
            noteDragArmed_ = true; noteDragActive_ = false;
            ndSrcStep_ = origin; ndSrcNote_ = note; ndGrabStep_ = step;
            ndOverStep_ = step; ndOverNote_ = note;
            return;
        }
        return;                                                 // 4) empty cell: toggle on up
    }

    // oct / accent rows (the step-number strip below is a plain label now).
    for (int s = 0; s < fable::SEQ_STEPS; ++s) {
        if (!colBounds(s).contains(pos)) continue;
        if (octBounds(s).contains(pos)) { cycleStepOct(s); return; }
        if (accBounds(s).contains(pos)) { toggleStepAcc(s); return; }
        return;
    }
}

void NoteSeqView::mouseDrag(const juce::MouseEvent& e) {
    if (resizeStep_ >= 0) {
        const int delta = (int)std::round(e.getDistanceFromDragStartX() / (double)juce::jmax(1, colBounds(resizeStep_).getWidth()));
        resizeStep(resizeStep_, resizeStartDuration_ + delta);
        return;
    }
    const auto p = e.position.roundToInt();
    if (sweeping_) {
        pending_.stepTo = stepAtX(p.x); pending_.noteTo = noteAtY(p.y);
        repaint();
        return;
    }
    if (moveArmed_) {
        moveHoverStep_ = stepAtX(p.x); moveHoverNote_ = noteAtY(p.y);
        if (moveHoverStep_ != moveOriginStep_ || moveHoverNote_ != moveOriginNote_) moving_ = true;
        repaint();
        return;
    }
    if (noteDragArmed_) {
        ndOverStep_ = stepAtX(p.x); ndOverNote_ = noteAtY(p.y);
        if (ndOverStep_ != ndGrabStep_ || ndOverNote_ != ndSrcNote_) noteDragActive_ = true;
        repaint();
        return;
    }
    if (barDragFrom_ >= 0) {
        if (!barDragging_) {
            if (e.getDistanceFromDragStart() < kDragThreshold) return;
            barDragging_ = true;
        }
        int target = -1;
        for (int i = 0; i < juce::jmin(fable::SEQ_NPATTERNS, model.clipBars()); ++i)
            if (patternBounds(i).contains(e.getPosition())) { target = i; break; }
        barDropTarget_ = (target != barDragFrom_) ? target : -1;
        repaint();
        return;
    }
}

void NoteSeqView::mouseUp(const juce::MouseEvent& e) {
    if (resizeStep_ >= 0) { resizeStep_ = -1; return; }
    if (sweeping_) {                       // commit the swept rectangle
        sweeping_ = false;
        setSelection(pending_);
        return;
    }
    if (moveArmed_) {
        moveArmed_ = false;
        if (moving_) {
            moving_ = false;
            commitBlockMove(moveHoverStep_ - moveOriginStep_, moveHoverNote_ - moveOriginNote_, e.mods.isAltDown());
        } else if (downStep_ >= 0) {       // plain click inside the rect: toggle
            toggleAt(downStep_, downNote_);
        }
        downStep_ = downNote_ = -1;
        return;
    }
    if (noteDragArmed_) {
        noteDragArmed_ = false;
        if (noteDragActive_) {
            noteDragActive_ = false;
            const int offset = ndGrabStep_ - ndSrcStep_;                 // long-note body grab
            const int dest = juce::jmax(0, ndOverStep_ - offset);
            commitNoteMove(ndSrcStep_, ndSrcNote_, dest, ndOverNote_, e.mods.isAltDown());
        } else if (downStep_ >= 0) {       // plain tap on a lit cell: toggle it off
            toggleAt(downStep_, downNote_);
        }
        downStep_ = downNote_ = -1;
        return;
    }
    if (barDragFrom_ >= 0) {
        if (barDragging_) {
            if (barDropTarget_ >= 0) movePattern(barDragFrom_, barDropTarget_, e.mods.isAltDown());
        } else {
            patternClick(barDragFrom_);
        }
        barDragFrom_ = -1; barDragging_ = false; barDropTarget_ = -1;
        repaint();
        return;
    }
    if (downStep_ >= 0) {                   // plain click on an empty cell: toggle on
        toggleAt(downStep_, downNote_);
        downStep_ = downNote_ = -1;
    }
}

// Ghost paste follows the pointer even with no button down (useSeqGhostPaste's
// pointermove). Any grid cell is a drop target.
void NoteSeqView::mouseMove(const juce::MouseEvent& e) {
    if (!ghost_) return;
    const auto pos = e.getPosition();
    ghostHasHover_ = gridBounds().contains(pos);
    if (ghostHasHover_) { ghostHoverStep_ = stepAtX(pos.x); ghostHoverNote_ = noteAtY(pos.y); }
    repaint();
}

void NoteSeqView::cancelResize() { if (resizeStep_ >= 0) resizeStep(resizeStep_, resizeStartDuration_); resizeStep_ = -1; }

// Esc: drop any in-flight sweep / drag / ghost uncommitted (SeqPanel.tsx's
// Escape handlers). A cancelled CUT ghost mutates nothing, since its source is
// only cleared at drop time. Selection itself is cleared separately by Esc.
void NoteSeqView::cancelGesture() {
    sweeping_ = false;
    moveArmed_ = moving_ = false;
    noteDragArmed_ = noteDragActive_ = false;
    ghost_ = false; ghostHasHover_ = false;
    barDragFrom_ = -1; barDragging_ = false; barDropTarget_ = -1;
    downStep_ = downNote_ = -1;
    repaint();
}

bool NoteSeqView::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        cancelResize();
        cancelGesture();
        clearSelection();
        return true;
    }
    if (!model.hasTargetClip()) return false;
    const auto mods = k.getModifiers();
    if (mods.isCommandDown()) {
        switch (k.getKeyCode()) {
            case (int) 'C': copySel(); return true;
            case (int) 'X': cutSel(); return true;
            case (int) 'V': pasteSel(); return true;
            case (int) 'D': duplicateSel(); return true;
            case (int) 'A': selectAllCells(); return true;
            case (int) 'Z': if (mods.isShiftDown()) redoEdit(); else undoEdit(); return true;
            default: return false;
        }
    }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSel(); return true; }
    return false;
}

// ---- animation ----------------------------------------------------------------

// Repaint only when something visible changed: transport/playhead, edit
// pattern, chain, chaining mode, host sync, or the pattern content itself.
void NoteSeqView::timerCallback() {
    // SQ-4 focus can retarget this same view onto a different hosted clip;
    // stale selection/clipboard/undo history must not carry across (decision
    // 6/7 — the web's cross-clip undo corruption hazard applies identically).
    const int identity = model.clipIdentity();
    if (identity != lastClipIdentity_) {
        lastClipIdentity_ = identity;
        hasRect_ = false; sweeping_ = false;
        moveArmed_ = moving_ = false; noteDragArmed_ = noteDragActive_ = false;
        ghost_ = false; ghostHasHover_ = false;
        barDragFrom_ = -1; barDragging_ = false; barDropTarget_ = -1;
        downStep_ = downNote_ = -1;
        hasLastCell_ = false;
        clip_ = {};
        history_.clear();
    }

    juce::uint32 sig = 17;
    auto mix = [&sig](int v) { sig = sig * 31u + (juce::uint32)(v + 2); };
    const bool playing = model.sequencerPlaying();
    const int edit = model.editPattern();
    mix(playing ? 1 : 0);
    mix(playing ? model.currentStep() : -1);
    mix(model.currentPattern());
    mix(edit);
    mix(model.hostSynced() ? 1 : 0);
    mix((int)std::lround(model.hostBpm()));
    const auto& chain = model.chain();
    mix((int)chain.size());
    for (int p : chain) mix(p);
    for (int s = 0; s < fable::SEQ_STEPS; ++s) {
        const NoteSeqStep st = model.sequenceStep(edit, s);
        mix((st.on ? 1 : 0) | (st.acc ? 2 : 0));
        mix(st.duration);
        mix(st.note); mix(st.oct);
    }
    if (sig != lastSig_) { lastSig_ = sig; repaint(); }
}

// ---- paint ----------------------------------------------------------------------

// .ns-btn: 5px radius, #11141c, dim mono text; active = cyan border/text over
// a dark cyan wash (#0c2a33).
static void drawSeqBtn(juce::Graphics& g, juce::Rectangle<int> b, const juce::String& text,
                       bool active, float tracking, bool current = false, float fontSize = 8.0f) {
    const auto bf = b.toFloat();
    const juce::Colour cyan = accentA();
    g.setColour(current ? cyan.withAlpha(0.20f)
                        : active ? juce::Colour(0xff0c2a33) : juce::Colour(0xff11141c));
    g.fillRoundedRectangle(bf, 5.0f);
    g.setColour(current ? cyan.withAlpha(0.9f) : active ? cyan.withAlpha(0.55f) : col::line);
    g.drawRoundedRectangle(bf.reduced(0.5f), 5.0f, 1.0f);
    if (active) {
        g.setColour(cyan);
        g.fillRect(b.withY(b.getBottom() - 3).withHeight(3).reduced(2, 0));
    }
    g.setColour(active || current ? cyan : col::textDim);
    g.setFont(monoFont(fontSize));
    drawSpaced(g, text, b, tracking, juce::Justification::centred);
}

void NoteSeqView::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    const juce::Colour cyan = accentA();
    // .ns-section carries a faint cyan border + glow
    g.setColour(cyan.withAlpha(0.16f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 12.0f, 1.0f);

    const bool playing = model.sequencerPlaying();
    const int edit = model.editPattern();
    const auto caps = model.capabilities();

    if (!model.hasTargetClip()) {
        g.setColour(cyan);
        g.setFont(dispFont(10.0f));
        drawSpaced(g, "NOTE SEQ", { kPadX, kHeadY, 110, kHeadH }, 2.2f);
        drawSeqBtn(g, randBounds(), "CREATE CLIP", false, 0.7f);
        return;
    }

    // ---- head: transport (.ns-transport — cyan-tinted well) ----
    if (caps.ownsTransport) {
        const auto tb = transportBounds().toFloat();
        g.setColour(juce::Colour(0xff0c2a33));
        g.fillRoundedRectangle(tb, 5.0f);
        g.setColour(cyan.withAlpha(playing ? 0.9f : 0.55f));
        g.drawRoundedRectangle(tb.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(cyan);
        if (playing) g.fillRect(juce::Rectangle<float>(8.0f, 8.0f).withCentre(tb.getCentre()));
        else {
            juce::Path p;
            p.addTriangle(tb.getCentreX() - 3.5f, tb.getCentreY() - 5.0f,
                          tb.getCentreX() - 3.5f, tb.getCentreY() + 5.0f,
                          tb.getCentreX() + 5.5f, tb.getCentreY());
            g.fillPath(p);
        }
    }

    // ---- head: title ----
    g.setColour(cyan);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, "NOTE SEQ", { caps.ownsTransport ? transportBounds().getRight() + 12 : kPadX,
                                 kHeadY, 96, kHeadH }, 2.2f);

    // ---- head: bar buttons + sequence length + RAND ----
    const int bars = caps.hosted ? model.clipBars()
                                 : juce::jlimit(1, fable::SEQ_NPATTERNS, (int)model.chain().size());
    const int currentBar = playing ? model.currentPattern() : -1;
    for (int i = 0; i < juce::jmin(fable::SEQ_NPATTERNS, model.clipBars()); ++i) {
        drawSeqBtn(g, patternBounds(i), kPatNames[i], edit == i, 0.0f,
                   bars > 1 && currentBar == i);
        // Bar-chip drag (.seq-bar.dragging / .drop-target)
        if (i == barDragFrom_ && barDragging_) {
            g.setColour(cyan.withAlpha(0.35f));
            g.fillRoundedRectangle(patternBounds(i).toFloat(), 5.0f);
        } else if (i == barDropTarget_) {
            g.setColour(cyan);
            g.drawRoundedRectangle(patternBounds(i).toFloat().reduced(1.0f), 5.0f, 2.0f);
        }
    }
    if (caps.supportsPatternChain) {
        auto length = sequenceLengthBounds();
        const auto minus = length.removeFromLeft(38);
        const auto plus = length.removeFromRight(38);
        drawSeqBtn(g, minus, "-", false, 0.0f, false, 15.0f);
        drawSeqBtn(g, length, "LENGTH " + juce::String(bars) + "B", false, 0.25f);
        drawSeqBtn(g, plus, "+", false, 0.0f, false, 15.0f);
    }
    drawSeqBtn(g, randBounds(), "RAND", false, 0.7f);

    // ---- head: SYNC badge + hint, right-aligned (.ns-hint) ----
    int hintRight = getWidth() - kPadX;
    if (model.hostSynced()) {
        // host tempo overrides seq.bpm — surface it like DR-1's SYNC tag
        const juce::String sync = "SYNC " + juce::String((int)std::lround(model.hostBpm()));
        const juce::Rectangle<int> sb{ hintRight - 64, kHeadY + 3, 64, kHeadH - 6 };
        g.setColour(juce::Colour(0xff0c2a33));
        g.fillRoundedRectangle(sb.toFloat(), 4.0f);
        g.setColour(cyan.withAlpha(0.55f));
        g.drawRoundedRectangle(sb.toFloat().reduced(0.5f), 4.0f, 1.0f);
        g.setColour(cyan);
        g.setFont(monoFont(7.0f));
        drawSpaced(g, sync, sb, 0.7f, juce::Justification::centred);
        hintRight = sb.getX() - 10;
    }
    g.setColour(col::textHint);
    g.setFont(monoFont(7.0f));
    // web "TAP LANE = NOTE · CYCLE OCT · ACCENT" (tie retired; note length
    // lives in the duration bits, surfaced by the milestone-3 piano roll)
    drawSpaced(g, "TAP LANE = NOTE - CYCLE OCT - ACCENT",
               { hintRight - 360, kHeadY, 360, kHeadH }, 0.9f,
               juce::Justification::right);

    // ---- legend column (.ns-legend) ----
    {
        const juce::Rectangle<int> legend(kPadX, kBodyY, kLegendW, kNumY + 10);
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        g.drawText("B",    legend.withHeight(10).translated(0, 2), juce::Justification::centredRight);
        g.drawText("NOTE", legend.withHeight(10).translated(0, (kLanesH - 10) / 2), juce::Justification::centredRight);
        g.drawText("C",    legend.withHeight(10).translated(0, kLanesH - 12), juce::Justification::centredRight);
        g.drawText("OCT",  legend.withHeight(kOctH).translated(0, kOctY), juce::Justification::centredRight);
        g.setColour(col::acB);
        g.drawText("ACC",  legend.withHeight(kAccH).translated(0, kAccY), juce::Justification::centredRight);
    }

    // ---- clock column divider (.ns-clock border-left) ----
    {
        const int cx = getWidth() - kPadX - kClockW - kClockGap;
        g.setColour(col::line);
        g.drawVerticalLine(cx, (float)kBodyY, (float)(kBodyY + kNumY + 10));
        // ROOT label under the stepper (the web Stepper renders its own label)
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        g.drawText("ROOT", root_.getX(), root_.getBottom() + 2, root_.getWidth(), 10,
                   juce::Justification::centred);
    }

    // ---- step columns ----
    const int curStep = model.currentStep(), curPat = model.currentPattern();
    NoteSeqStep steps[fable::SEQ_STEPS];
    for (int s = 0; s < fable::SEQ_STEPS; ++s) steps[s] = model.sequenceStep(edit, s);

    for (int s = 0; s < fable::SEQ_STEPS; ++s) {
        const NoteSeqStep& st = steps[s];

        // note lanes (.ns-cell / .ns-cell.on / .ns-cell.on.acc) with the web's
        // piano-style shading (SHARP_LANE in SeqPanel.tsx): the root lane is
        // accent-tinted, natural-degree lanes sit slightly lighter than
        // sharp-degree lanes, so the 12 chromatic rows read like a keyboard.
        static const bool kSharpLane[12] = {false, true, false, true, false, false,
                                            true, false, true, false, true, false};
        for (int note = 0; note < fable::SEQ_NOTE_LANES; ++note) {
            const auto b = cellBounds(s, note).toFloat();
            const bool active = st.on && st.note == note;
            const bool rootLane = note == 0;
            if (active) {
                if (st.acc)
                    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffaef2ff), b.getX(), b.getY(),
                                                           juce::Colour(0xff2fa8c4), b.getX(), b.getBottom(), false));
                else
                    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3fd0e8), b.getX(), b.getY(),
                                                           juce::Colour(0xff1a7c94), b.getX(), b.getBottom(), false));
            } else if (rootLane) {
                g.setColour(juce::Colour(0xff0c0f16).interpolatedWith(cyan, 0.09f));
            } else {
                g.setColour(kSharpLane[note % 12] ? juce::Colour(0xff080a0f) : juce::Colour(0xff0c0f16));
            }
            g.fillRoundedRectangle(b, 2.0f);
            g.setColour(active ? cyan.withAlpha(0.5f)
                       : rootLane ? cyan.withAlpha(0.30f)
                                  : juce::Colours::white.withAlpha(0.045f));
            g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);
        }

        // octave button (.ns-oct-btn)
        {
            const auto b = octBounds(s).toFloat();
            g.setColour(juce::Colour(0xff0c0f16));
            g.fillRoundedRectangle(b, 4.0f);
            g.setColour(col::line);
            g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
            g.setColour(st.oct != 0 ? col::acN : col::textDim);
            g.setFont(monoFont(9.0f));
            g.drawText(st.oct == 0 ? "0" : st.oct > 0 ? "+1" : "-1",
                       octBounds(s), juce::Justification::centred);
        }

        // accent button (.ns-acc-btn)
        auto drawToggle = [&](juce::Rectangle<int> bi, bool on, juce::Colour top, juce::Colour bot) {
            const auto b = bi.toFloat();
            if (on) {
                g.setGradientFill(juce::ColourGradient(top, b.getX(), b.getY(),
                                                       bot, b.getX(), b.getBottom(), false));
                g.fillRoundedRectangle(b, 3.0f);
            } else {
                g.setColour(juce::Colour(0xff0a0d13));
                g.fillRoundedRectangle(b, 3.0f);
            }
            g.setColour(on ? top.withAlpha(0.6f) : col::line);
            g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 1.0f);
        };
        drawToggle(accBounds(s), st.on && st.acc,
                   juce::Colour(0xffffc98d), juce::Colour(0xffcc7a2e));

        // step number (.ns-step-num) — a plain label now; rectangle selection
        // lives on the note lanes, not this strip.
        const auto c = colBounds(s);
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        g.drawText(juce::String(s + 1), c.withY(c.getY() + kNumY).withHeight(10),
                   juce::Justification::centredTop, false);

        // playhead cursor (.ns-step-cursor)
        if (playing && curStep == s && curPat == edit) {
            const auto cf = juce::Rectangle<float>((float)c.getX() - 2, (float)c.getY() - 2,
                                                   (float)c.getWidth() + 4, (float)(kAccY + kAccH) + 4);
            g.setColour(cyan.withAlpha(0.85f));
            g.drawRoundedRectangle(cf, 6.0f, 1.0f);
            g.setColour(cyan.withAlpha(0.18f));
            g.drawRoundedRectangle(cf.expanded(1.5f), 7.0f, 3.0f);
        }
    }

    // Painted piano-roll layer: each trigger owns a horizontal duration block.
    // The final 5px is a bright grab handle for right-edge resizing.
    const int gridRight = getWidth() - kPadX - kClockW - kClockGap;
    for (int s = 0; s < fable::SEQ_STEPS; ++s) {
        const auto& st = steps[s];
        if (!st.on) continue;
        const auto start = cellBounds(s, st.note);
        const int pitch = colBounds(s).getWidth();
        const int width = pitch * st.duration + (int)std::round(kColGap * (float)(st.duration - 1));
        auto block = start.withWidth(juce::jmax(pitch, juce::jmin(width, gridRight - start.getX()))).reduced(1, 1);
        const auto bf = block.toFloat();
        g.setColour((st.acc ? juce::Colour(0xffaef2ff) : cyan).withAlpha(0.78f));
        g.fillRoundedRectangle(bf, 3.0f);
        g.setColour(cyan.withAlpha(0.95f));
        g.drawRoundedRectangle(bf.reduced(0.5f), 3.0f, 1.0f);
        g.fillRect(block.removeFromRight(4).toFloat().reduced(0.0f, 2.0f));
    }

    // ---- selection / drag / ghost overlays (SeqPanel.tsx .ns-sel-rect etc.) ----
    // Given a normalized rect, the covering pixel box: note lanes run top-down
    // with the highest note on top, so noteHi maps to the top-left cell.
    auto rectPixels = [&](const fable::RectNorm& n) {
        const int sLo = juce::jlimit(0, fable::SEQ_STEPS - 1, n.stepLo);
        const int sHi = juce::jlimit(0, fable::SEQ_STEPS - 1, n.stepHi);
        const int nLo = juce::jlimit(0, fable::SEQ_NOTE_LANES - 1, n.noteLo);
        const int nHi = juce::jlimit(0, fable::SEQ_NOTE_LANES - 1, n.noteHi);
        const auto tl = cellBounds(sLo, nHi);
        const auto br = cellBounds(sHi, nLo);
        return juce::Rectangle<float>((float)tl.getX(), (float)tl.getY(),
                                      (float)(br.getRight() - tl.getX()),
                                      (float)(br.getBottom() - tl.getY()));
    };

    // Live sweep (while dragging) or committed selection: one translucent
    // bordered rectangle (.ns-sel-rect).
    if (sweeping_ || hasRect_) {
        const auto r = rectPixels(fable::rectNorm(sweeping_ ? pending_ : rect_));
        g.setColour(cyan.withAlpha(0.14f));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(cyan.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
    }

    // Note-drag feedback: dim the source cell, outline the hovered target
    // (.drag-src / .drag-over[.copy]).
    if (noteDragActive_) {
        const auto src = cellBounds(ndSrcStep_, ndSrcNote_).toFloat();
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(src, 2.0f);
        const int offset = ndGrabStep_ - ndSrcStep_;
        const auto over = cellBounds(juce::jmax(0, ndOverStep_ - offset), ndOverNote_).toFloat();
        g.setColour(cyan);
        if (juce::ModifierKeys::getCurrentModifiers().isAltDown()) {
            const float dashes[] = { 3.0f, 2.0f };
            juce::Path pth; pth.addRoundedRectangle(over.reduced(0.5f), 2.0f);
            juce::Path dashed; juce::PathStrokeType(1.2f).createDashedStroke(dashed, pth, dashes, 2);
            g.strokePath(dashed, juce::PathStrokeType(1.2f));
        } else {
            g.drawRoundedRectangle(over.reduced(0.5f), 2.0f, 1.4f);
        }
    }

    // Block-move preview: the rectangle outlined at its dragged destination.
    if (moving_) {
        const auto n = fable::rectNorm(rect_);
        const int dStep = juce::jlimit(-n.stepLo, (fable::SEQ_STEPS - 1) - n.stepHi, moveHoverStep_ - moveOriginStep_);
        const int dNote = juce::jlimit(-n.noteLo, (fable::SEQ_NOTE_LANES - 1) - n.noteHi, moveHoverNote_ - moveOriginNote_);
        const auto r = rectPixels({ n.stepLo + dStep, n.stepHi + dStep, n.noteLo + dNote, n.noteHi + dNote });
        g.setColour(cyan.withAlpha(0.10f));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(cyan.withAlpha(0.9f));
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.4f);
    }

    // Ghost cells trailing the cursor (.ns-cell.ghost): dashed accent blocks at
    // the carried rect's landing position; the picked-up CUT source dims.
    if (ghost_) {
        if (ghostCut_) {
            const auto sn = fable::rectNorm(ghostSrc_);
            for (const auto& cCell : ghostData_.cells) {
                const int s = sn.stepLo + cCell.dStep, note = cCell.bytes[1] & 0x7f;
                if (s < 0 || s >= fable::SEQ_STEPS) continue;
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillRoundedRectangle(cellBounds(s, note).toFloat(), 2.0f);
            }
        }
        if (ghostHasHover_) {
            const int dNote = ghostHoverNote_ - ghostData_.noteHi;
            for (const auto& cCell : ghostData_.cells) {
                const int s = ghostHoverStep_ + cCell.dStep;
                const int note = (cCell.bytes[1] & 0x7f) + dNote;
                if (s < 0 || s >= fable::SEQ_STEPS || note < 0 || note > kMaxNote) continue;
                const auto b = cellBounds(s, note).toFloat();
                g.setColour(cyan.withAlpha(0.38f));
                g.fillRoundedRectangle(b, 2.0f);
                const float dashes[] = { 3.0f, 2.0f };
                juce::Path pth; pth.addRoundedRectangle(b.reduced(0.5f), 2.0f);
                juce::Path dashed; juce::PathStrokeType(1.0f).createDashedStroke(dashed, pth, dashes, 2);
                g.setColour(cyan);
                g.strokePath(dashed, juce::PathStrokeType(1.0f));
            }
        }
    }

    // Floating CUT · COPY · DUP · DEL · ✕ toolbar over the selection.
    if (hasRect_ && !ghost_) {
        static const char* const kMenu[5] = { "CUT", "COPY", "DUP", "DEL", "X" };
        for (int i = 0; i < 5; ++i)
            drawSeqBtn(g, selMenuButton(i), kMenu[i], false, 0.4f, false, 7.0f);
    }
}

} // namespace fui
