#include "PitchSeqView.h"
#include "../dsp/BassPatches.h"
#include <cmath>

namespace fui {

using fable::BassSeqStep;

static const char* const kPatNames[fable::BL_NPATTERNS] = { "1", "2", "3", "4" };

// Decision-6 (step-range editing): BL-1's packed byte layout for
// StepEditOps.h — stride 3 (flags/note+slide/oct), 16 steps, one lane
// (laneOffset 0, patternSize == stepsPerPattern*stride). The empty-step
// bytes match makeEmptyBassPatterns()/BassSeqStep{}: duration 1, oct 0 — so
// clearRange never produces a step JUCE reads back as anything but a rest.
static fable::StepLayout seqLayout() {
    return { fable::BL_STEP_STRIDE, fable::BL_STEPS, fable::BL_STEPS * fable::BL_STEP_STRIDE, 0 };
}
static const fable::StepBytes kEmptyStep{ (uint8_t)(1 << 2), 0, 1 };
static constexpr int kMaxNote = fable::BL_NOTE_LANES - 1; // top lane (paste clamp)

// ---- geometry (bass.css .bl-seq-*, rack-relative px) --------------------------
// panel padding 9px 12px 11px; head 28px + 10px margin; body: legend 36px +
// 8px gap, then 16 columns with 5px gaps. Column: 143px of lanes (12 cells,
// 1px gaps), oct 5+20, acc 4+14, sld 4+14, step number 4+10.

static constexpr int kPadX = 12, kHeadY = 9, kHeadH = 28;
static constexpr int kBodyY = kHeadY + kHeadH + 10;
static constexpr int kLegendW = 36, kLegendGap = 8;
static constexpr int kLanesH = 136;
static constexpr float kColGap = 5.0f;
// The ACC/SLD toggle lanes were cramped 14px targets; the web made them taller
// (.bl-acc-btn/.bl-sld-btn height 28) so they're easier to hit and read. The
// fixed JUCE grid can't double them, so they grow to 22px each by reclaiming a
// few px from the tall note lanes (a ~5% note-lane trim for a ~57% taller
// toggle target).
static constexpr int kOctY = kLanesH + 5, kOctH = 20;
static constexpr int kAccY = kOctY + kOctH + 4, kAccH = 22;
static constexpr int kSldY = kAccY + kAccH + 4, kSldH = 22;
static constexpr int kNumY = kSldY + kSldH + 4;

PitchSeqView::PitchSeqView(BassUiModel& p) : proc(p) {
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

juce::Rectangle<int> PitchSeqView::transportBounds() const {
    return { kPadX, kHeadY + 2, 34, 24 };            // .bl-transport 34x24
}

juce::Rectangle<int> PitchSeqView::patternBounds(int i) const {
    const int x0 = kPadX + 34 + 12 + 96 + 12;        // transport, gap, title, gap
    return { x0 + i * (26 + 5), kHeadY + 2, 26, 24 }; // .bl-pattern 26x24, gap 5
}

juce::Rectangle<int> PitchSeqView::sequenceLengthBounds() const {
    return { patternBounds(3).getRight() + 12, kHeadY, 170, 30 };
}

juce::Rectangle<int> PitchSeqView::randBounds() const {
    return { sequenceLengthBounds().getRight() + 12, kHeadY + 2, 52, 24 };
}

juce::Rectangle<int> PitchSeqView::colBounds(int step) const {
    const int gx = kPadX + kLegendW + kLegendGap;
    const int gw = getWidth() - kPadX - gx;
    const float w = ((float)gw - 15.0f * kColGap) / 16.0f;
    const float x = static_cast<float>(gx) + static_cast<float>(step) * (w + kColGap);
    return juce::Rectangle<float>(x, (float)kBodyY, w, (float)(kNumY + 10)).toNearestInt();
}

juce::Rectangle<int> PitchSeqView::cellBounds(int step, int note) const {
    const auto c = colBounds(step);
    // 12 lanes over kLanesH with 1px gaps, note 11 on top (web lane order)
    const float ch = (kLanesH - 11.0f) / 12.0f;
    const int r = fable::BL_NOTE_LANES - 1 - note;
    const float y = static_cast<float>(c.getY()) + static_cast<float>(r) * (ch + 1.0f);
    return juce::Rectangle<float>((float)c.getX(), y, (float)c.getWidth(), ch).toNearestInt();
}

juce::Rectangle<int> PitchSeqView::octBounds(int step) const {
    const auto c = colBounds(step);
    return { c.getX(), c.getY() + kOctY, c.getWidth(), kOctH };
}

juce::Rectangle<int> PitchSeqView::accBounds(int step) const {
    const auto c = colBounds(step);
    return { c.getX(), c.getY() + kAccY, c.getWidth(), kAccH };
}

juce::Rectangle<int> PitchSeqView::slideBounds(int step) const {
    const auto c = colBounds(step);
    return { c.getX(), c.getY() + kSldY, c.getWidth(), kSldH };
}

juce::Rectangle<int> PitchSeqView::resizeBounds(int step) const {
    const auto st = proc.sequenceStep(proc.editPattern(), step);
    if (!st.on) return {};
    auto cell = cellBounds(step, st.note);
    const int w = colBounds(step).getWidth() * st.duration + (int)std::round(kColGap * (float)(st.duration - 1));
    const int gridRight = getWidth() - kPadX;
    cell.setWidth(juce::jmax(1, juce::jmin(w, gridRight - cell.getX())));
    return cell.removeFromRight(6).expanded(2, 2);
}

// ---- store handlers ----------------------------------------------------------

void PitchSeqView::toggleCell(int step, int note) {
    const int pat = proc.editPattern();
    BassSeqStep cur = proc.sequenceStep(pat, step);
    if (cur.on && cur.note == note) {              // tap again = rest
        cur.on = false; cur.acc = false; cur.slide = false; cur.duration = 1;
    } else {
        cur.on = true; cur.note = note;
    }
    proc.setSequenceStep(pat, step, cur);
    repaint();
}

void PitchSeqView::cycleStepOct(int step) {
    const int pat = proc.editPattern();
    BassSeqStep cur = proc.sequenceStep(pat, step);
    cur.oct = cur.oct >= fable::BL_OCT_MAX ? fable::BL_OCT_MIN : cur.oct + 1; // seq.ts cycleOct
    proc.setSequenceStep(pat, step, cur);
    repaint();
}

void PitchSeqView::toggleStepAcc(int step) {
    const int pat = proc.editPattern();
    BassSeqStep cur = proc.sequenceStep(pat, step);
    if (!cur.on) return;
    cur.acc = !cur.acc;
    proc.setSequenceStep(pat, step, cur);
    repaint();
}

void PitchSeqView::toggleStepSlide(int step) {
    const int pat = proc.editPattern();
    BassSeqStep cur = proc.sequenceStep(pat, step);
    if (!cur.on) return;
    cur.slide = !cur.slide;
    proc.setSequenceStep(pat, step, cur);
    repaint();
}

void PitchSeqView::resizeStep(int step, int duration) {
    auto cur = proc.sequenceStep(proc.editPattern(), step);
    if (!cur.on) return;
    const int bars = proc.capabilities().hosted ? proc.clipBars() : (int)proc.chain().size();
    const int absolute = proc.editPattern() * fable::BL_STEPS + step;
    int limit = juce::jmin(63, bars * fable::BL_STEPS - absolute);
    for (int a = absolute + 1; a < bars * fable::BL_STEPS; ++a) {
        const auto other = proc.sequenceStep(a / fable::BL_STEPS, a % fable::BL_STEPS);
        if (other.on && other.note == cur.note && other.oct == cur.oct) { limit = a - absolute; break; }
    }
    cur.duration = juce::jlimit(1, limit, duration);
    proc.setSequenceStep(proc.editPattern(), step, cur);
    repaint();
}

// seq.ts randomPattern: sparse minor-pentatonic line with occasional octave
// throws, accents and slides.
void PitchSeqView::randomize() {
    static const int pool[] = { 0, 0, 0, 3, 5, 7, 10 };
    const int pat = proc.editPattern();
    for (int i = 0; i < fable::BL_STEPS; ++i) {
        BassSeqStep s;
        s.on = rng_.nextFloat() < 0.6f;
        s.note = pool[rng_.nextInt(7)];
        s.oct = rng_.nextFloat() < 0.2f ? (rng_.nextFloat() < 0.5f ? -1 : 1) : 0;
        s.acc = s.on && rng_.nextFloat() < 0.25f;
        s.slide = s.on && i > 0 && rng_.nextFloat() < 0.22f;
        s.duration = 1 + rng_.nextInt(4);
        proc.setSequenceStep(pat, i, s);
    }
    repaint();
}

void PitchSeqView::patternClick(int i) {
    proc.setEditPattern(i);
    repaint();
}

void PitchSeqView::setSequenceLength(int bars) {
    bars = juce::jlimit(1, fable::BL_NPATTERNS, bars);
    std::vector<int> sequence;
    for (int bar = 0; bar < bars; ++bar) sequence.push_back(bar);
    proc.setChain(std::move(sequence));
    repaint();
}

int PitchSeqView::stepAt(int x) const {
    const int gx = kPadX + kLegendW + kLegendGap;
    const int gw = getWidth() - kPadX - gx;
    const float w = ((float)gw - 15.0f * kColGap) / 16.0f;
    if (w + kColGap <= 0.0f) return 0;
    const int step = (int)std::floor(((float)x - (float)gx) / (w + kColGap));
    return juce::jlimit(0, fable::BL_STEPS - 1, step);
}

int PitchSeqView::patternIndexAt(juce::Point<int> p) const {
    for (int i = 0; i < fable::BL_NPATTERNS; ++i)
        if (patternBounds(i).contains(p)) return i;
    return -1;
}

// Inverse of cellBounds' lane math: note 11 sits on top, C at the bottom.
int PitchSeqView::noteAt(int y) const {
    const float ch = (kLanesH - 11.0f) / 12.0f;
    const float rel = (float)(y - kBodyY) / (ch + 1.0f);
    const int row = juce::jlimit(0, fable::BL_NOTE_LANES - 1, (int)std::floor(rel));
    return fable::BL_NOTE_LANES - 1 - row;
}

// The 16×12 note-lane area (excludes the oct / acc / slide / step rows).
juce::Rectangle<int> PitchSeqView::gridBounds() const {
    const auto a = colBounds(0), b = colBounds(fable::BL_STEPS - 1);
    return { a.getX(), kBodyY, b.getRight() - a.getX(), kLanesH };
}

// Floating CUT · COPY · DUP · DEL · ✕ toolbar over the selected columns.
juce::Rectangle<int> PitchSeqView::selMenuBounds() const {
    constexpr int bw = 34, gap = 2, n = 5, h = 16;
    constexpr int w = n * bw + (n - 1) * gap;
    const auto nrm = fable::rectNorm(rect_);
    const auto lo = colBounds(juce::jlimit(0, fable::BL_STEPS - 1, nrm.stepLo));
    const auto hi = colBounds(juce::jlimit(0, fable::BL_STEPS - 1, nrm.stepHi));
    const int cx = (lo.getX() + hi.getRight()) / 2;
    const auto grid = gridBounds();
    const int x = juce::jlimit(grid.getX(), juce::jmax(grid.getX(), grid.getRight() - w), cx - w / 2);
    return { x, kBodyY + 1, w, h };
}

juce::Rectangle<int> PitchSeqView::selMenuButton(int i) const {
    constexpr int bw = 34, gap = 2;
    const auto m = selMenuBounds();
    return { m.getX() + i * (bw + gap), m.getY(), bw, m.getHeight() };
}

bool PitchSeqView::inRect(int step, int note) const {
    if (sweeping_) {
        const auto n = fable::rectNorm(pending_);
        return step >= n.stepLo && step <= n.stepHi && note >= n.noteLo && note <= n.noteHi;
    }
    if (!hasRect_) return false;
    const auto n = fable::rectNorm(rect_);
    return step >= n.stepLo && step <= n.stepHi && note >= n.noteLo && note <= n.noteHi;
}

// Origin step of a note grabbable at (step, note): the lit cell itself, else a
// longer note whose painted body covers this column.
int PitchSeqView::grabNoteAt(int step, int note) const {
    const int pat = proc.editPattern();
    const auto here = proc.sequenceStep(pat, step);
    if (here.on && here.note == note) return step;
    for (int c = step - 1; c >= 0; --c) {
        const auto v = proc.sequenceStep(pat, c);
        if (v.on && v.note == note && c + v.duration > step) return c;
    }
    return -1;
}

// ---- 2-D rectangle editing (seqEdit.ts port via StepEditOps.h) --------------

void PitchSeqView::setSelection(const fable::RectSel& r) {
    rect_ = r;
    hasRect_ = true;
    const auto n = fable::rectNorm(r);
    hasLastCell_ = true; lastCellStep_ = n.stepLo; lastCellNote_ = n.noteHi;
    repaint();
}

void PitchSeqView::selectAllCells() {
    setSelection({ 0, fable::BL_STEPS - 1, 0, fable::BL_NOTE_LANES - 1 });
}

void PitchSeqView::clearSelection() { hasRect_ = false; repaint(); }

void PitchSeqView::toggleAt(int step, int note) {
    toggleCell(step, note);
    hasLastCell_ = true; lastCellStep_ = step; lastCellNote_ = note;
}

void PitchSeqView::copySel() {
    const int pat = proc.editPattern();
    clip_.valid = true;
    if (hasRect_) {
        clip_.isPattern = false;
        clip_.rect = fable::copyRect(proc.patternBytes(), seqLayout(), pat, rect_);
    } else {
        clip_.isPattern = true;
        clip_.pattern = fable::copyPattern(proc.patternBytes(), seqLayout(), pat);
    }
}

void PitchSeqView::cutSel() {
    copySel();
    if (!hasRect_) return;
    pushHistory();
    proc.setPatternBytes(fable::clearRect(proc.patternBytes(), seqLayout(), proc.editPattern(), rect_, kEmptyStep));
    repaint();
}

void PitchSeqView::pasteSel() {
    if (!clip_.valid) return;
    const int pat = proc.editPattern();
    if (clip_.isPattern) {
        pushHistory();
        proc.setPatternBytes(fable::pastePattern(proc.patternBytes(), seqLayout(), pat, clip_.pattern));
        repaint();
        return;
    }
    int atStep, atNote;
    if (hasRect_)          { const auto n = fable::rectNorm(rect_); atStep = n.stepLo; atNote = n.noteHi; }
    else if (hasLastCell_) { atStep = lastCellStep_; atNote = lastCellNote_; }
    else                   { atStep = 0; atNote = clip_.rect.noteHi; }
    const int dNote = atNote - clip_.rect.noteHi;
    pushHistory();
    proc.setPatternBytes(fable::pasteRect(proc.patternBytes(), seqLayout(), pat, atStep, dNote, clip_.rect, kMaxNote));
    rect_ = { atStep, atStep + clip_.rect.wSteps - 1, clip_.rect.noteLo + dNote, clip_.rect.noteHi + dNote };
    hasRect_ = true;
    repaint();
}

void PitchSeqView::duplicateSel() {
    const int pat = proc.editPattern();
    if (hasRect_) {
        const auto n = fable::rectNorm(rect_);
        if (n.stepHi + 1 >= fable::BL_STEPS) return;
        const auto data = fable::copyRect(proc.patternBytes(), seqLayout(), pat, rect_);
        pushHistory();
        proc.setPatternBytes(fable::pasteRect(proc.patternBytes(), seqLayout(), pat, n.stepHi + 1, 0, data, kMaxNote));
        const int w = n.stepHi - n.stepLo + 1;
        rect_ = { n.stepHi + 1, n.stepHi + w, n.noteLo, n.noteHi }; hasRect_ = true;
        repaint();
        return;
    }
    // No selection: the classic "duplicate bar" gesture — copy the whole edit
    // pattern to editPattern+1, extending sequence length if that bar isn't
    // played yet.
    const int target = pat + 1;
    if (target >= fable::BL_NPATTERNS) return;
    const int bars = proc.capabilities().hosted ? proc.clipBars() : (int)proc.chain().size();
    if (target >= bars) {
        std::vector<int> next;
        for (int i = 0; i <= target; ++i) next.push_back(i);
        proc.setChain(std::move(next));
    }
    pushHistory();
    const auto data = fable::copyPattern(proc.patternBytes(), seqLayout(), pat);
    proc.setPatternBytes(fable::pastePattern(proc.patternBytes(), seqLayout(), target, data));
    proc.setEditPattern(target);
    repaint();
}

void PitchSeqView::deleteSel() {
    if (!hasRect_) return;
    pushHistory();
    proc.setPatternBytes(fable::clearRect(proc.patternBytes(), seqLayout(), proc.editPattern(), rect_, kEmptyStep));
    repaint();
}

void PitchSeqView::undoEdit() {
    fable::StepBytes restored;
    if (history_.undo(proc.patternBytes(), restored)) { proc.setPatternBytes(restored); repaint(); }
}

void PitchSeqView::redoEdit() {
    fable::StepBytes restored;
    if (history_.redo(proc.patternBytes(), restored)) { proc.setPatternBytes(restored); repaint(); }
}

void PitchSeqView::commitNoteMove(int srcStep, int srcNote, int destStep, int destNote, bool copy) {
    destStep = juce::jlimit(0, fable::BL_STEPS - 1, destStep);
    destNote = juce::jlimit(0, fable::BL_NOTE_LANES - 1, destNote);
    if (srcStep == destStep && srcNote == destNote) return;
    const fable::RectSel one { srcStep, srcStep, srcNote, srcNote };
    fable::RectMoveOpts opts; opts.copy = copy; opts.emptyStep = kEmptyStep; opts.maxNote = kMaxNote;
    pushHistory();
    proc.setPatternBytes(fable::moveRect(proc.patternBytes(), seqLayout(), proc.editPattern(), one,
                                         destStep - srcStep, destNote - srcNote, opts));
    rect_ = { destStep, destStep, destNote, destNote }; hasRect_ = true;
    hasLastCell_ = true; lastCellStep_ = destStep; lastCellNote_ = destNote;
    repaint();
}

void PitchSeqView::commitBlockMove(int dStep, int dNote, bool copy) {
    if (!hasRect_) return;
    const auto n = fable::rectNorm(rect_);
    dStep = juce::jlimit(-n.stepLo, (fable::BL_STEPS - 1) - n.stepHi, dStep);
    dNote = juce::jlimit(-n.noteLo, (fable::BL_NOTE_LANES - 1) - n.noteHi, dNote);
    if (dStep == 0 && dNote == 0) return;
    fable::RectMoveOpts opts; opts.copy = copy; opts.emptyStep = kEmptyStep; opts.maxNote = kMaxNote;
    pushHistory();
    proc.setPatternBytes(fable::moveRect(proc.patternBytes(), seqLayout(), proc.editPattern(), rect_, dStep, dNote, opts));
    rect_ = { n.stepLo + dStep, n.stepHi + dStep, n.noteLo + dNote, n.noteHi + dNote }; hasRect_ = true;
    repaint();
}

void PitchSeqView::beginGhostPaste(bool cut) {
    if (!hasRect_) return;
    ghostData_ = fable::copyRect(proc.patternBytes(), seqLayout(), proc.editPattern(), rect_);
    if (ghostData_.cells.empty()) return;
    clip_.valid = true; clip_.isPattern = false; clip_.rect = ghostData_;
    ghost_ = true; ghostCut_ = cut; ghostSrc_ = rect_; ghostHasHover_ = false;
    hasRect_ = false;
    repaint();
}

void PitchSeqView::dropGhost(int atStep, int atNote) {
    if (!ghost_) return;
    const int pat = proc.editPattern();
    const int dNote = atNote - ghostData_.noteHi;
    pushHistory();
    std::vector<uint8_t> base = ghostCut_
        ? fable::clearRect(proc.patternBytes(), seqLayout(), pat, ghostSrc_, kEmptyStep)
        : proc.patternBytes();
    proc.setPatternBytes(fable::pasteRect(base, seqLayout(), pat, atStep, dNote, ghostData_, kMaxNote));
    rect_ = { atStep, atStep + ghostData_.wSteps - 1, ghostData_.noteLo + dNote, ghostData_.noteHi + dNote };
    hasRect_ = true;
    ghost_ = false; ghostHasHover_ = false;
    repaint();
}

void PitchSeqView::moveBar(int fromPattern, int toPattern, bool copy) {
    const int bars = proc.capabilities().hosted ? proc.clipBars() : fable::BL_NPATTERNS;
    if (fromPattern < 0 || toPattern < 0 || fromPattern >= bars || toPattern >= bars
        || fromPattern == toPattern) return;
    pushHistory();
    const auto bytes = proc.patternBytes();
    const auto a = fable::copyPattern(bytes, seqLayout(), fromPattern);
    auto next = fable::pastePattern(bytes, seqLayout(), toPattern, a);
    if (!copy) {
        const auto b = fable::copyPattern(bytes, seqLayout(), toPattern);
        next = fable::pastePattern(next, seqLayout(), fromPattern, b);
    }
    proc.setPatternBytes(next);
    repaint();
}

void PitchSeqView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();

    if (ghost_) {                                 // carrying a ghost: drop or cancel
        if (gridBounds().contains(pos)) dropGhost(stepAt(pos.x), noteAt(pos.y));
        else cancelGesture();
        return;
    }
    if (hasRect_) {                               // floating selection menu
        for (int i = 0; i < 5; ++i) if (selMenuButton(i).contains(pos)) {
            switch (i) {
                case 0: beginGhostPaste(true);  break;
                case 1: beginGhostPaste(false); break;
                case 2: duplicateSel();         break;
                case 3: deleteSel();            break;
                default: clearSelection();      break;
            }
            repaint();
            return;
        }
    }

    if (transportBounds().contains(pos)) {        // play/stop
        proc.setSequencerPlaying(!proc.sequencerPlaying());
        repaint();
        return;
    }
    // Bar chip: plain click selects the edit pattern (mouseUp); past the drag
    // threshold it becomes a bar-chip drag (move, Alt = copy).
    for (int i = 0; i < fable::BL_NPATTERNS; ++i)
        if (patternBounds(i).contains(pos)) {
            barDragFrom_ = i; barDragStarted_ = false; barDragHover_ = -1;
            return;
        }
    if (proc.capabilities().supportsPatternChain && sequenceLengthBounds().contains(pos)) {
        const auto length = sequenceLengthBounds();
        const int bars = proc.capabilities().hosted ? proc.clipBars() : (int)proc.chain().size();
        if (pos.x < length.getX() + 38) setSequenceLength(bars - 1);
        else if (pos.x >= length.getRight() - 38) setSequenceLength(bars + 1);
        return;
    }
    if (randBounds().contains(pos)) { randomize(); return; }
    for (int s = 0; s < fable::BL_STEPS; ++s) if (resizeBounds(s).contains(pos)) {
        resizeStep_ = s; resizeStartDuration_ = proc.sequenceStep(proc.editPattern(), s).duration; return;
    }

    // ---- note lanes: Shift-sweep / block-move / note-drag / deferred toggle ----
    if (gridBounds().contains(pos)) {
        const int step = stepAt(pos.x), note = noteAt(pos.y);
        downStep_ = step; downNote_ = note;
        if (e.mods.isShiftDown()) {
            sweeping_ = true; pending_ = { step, step, note, note };
            repaint();
            return;
        }
        if (hasRect_ && inRect(step, note)) {
            moveArmed_ = true; moving_ = false;
            moveOriginStep_ = step; moveOriginNote_ = note;
            moveHoverStep_ = step; moveHoverNote_ = note;
            return;
        }
        const int origin = grabNoteAt(step, note);
        if (origin >= 0) {
            noteDragArmed_ = true; noteDragActive_ = false;
            ndSrcStep_ = origin; ndSrcNote_ = note; ndGrabStep_ = step;
            ndOverStep_ = step; ndOverNote_ = note;
            return;
        }
        return;
    }

    // oct / accent / slide rows (the step-number strip below is a plain label).
    for (int s = 0; s < fable::BL_STEPS; ++s) {
        if (!colBounds(s).contains(pos)) continue;
        if (octBounds(s).contains(pos))   { cycleStepOct(s); return; }
        if (accBounds(s).contains(pos))   { toggleStepAcc(s); return; }
        if (slideBounds(s).contains(pos)) { toggleStepSlide(s); return; }
        return;
    }
}

void PitchSeqView::mouseDrag(const juce::MouseEvent& e) {
    if (resizeStep_ >= 0) {
        const int delta = (int)std::round(e.getDistanceFromDragStartX() / (double)juce::jmax(1, colBounds(resizeStep_).getWidth()));
        resizeStep(resizeStep_, resizeStartDuration_ + delta);
        return;
    }
    const auto p = e.getPosition();
    if (sweeping_) {
        pending_.stepTo = stepAt(p.x); pending_.noteTo = noteAt(p.y);
        repaint();
        return;
    }
    if (moveArmed_) {
        moveHoverStep_ = stepAt(p.x); moveHoverNote_ = noteAt(p.y);
        if (moveHoverStep_ != moveOriginStep_ || moveHoverNote_ != moveOriginNote_) moving_ = true;
        repaint();
        return;
    }
    if (noteDragArmed_) {
        ndOverStep_ = stepAt(p.x); ndOverNote_ = noteAt(p.y);
        if (ndOverStep_ != ndGrabStep_ || ndOverNote_ != ndSrcNote_) noteDragActive_ = true;
        repaint();
        return;
    }
    if (barDragFrom_ >= 0) {
        if (!barDragStarted_ && e.getDistanceFromDragStart() > 4) barDragStarted_ = true;
        if (barDragStarted_) {
            const int hover = patternIndexAt(e.getPosition());
            if (hover != barDragHover_) { barDragHover_ = hover; repaint(); }
        }
        return;
    }
}

void PitchSeqView::mouseUp(const juce::MouseEvent& e) {
    if (resizeStep_ >= 0) { resizeStep_ = -1; return; }
    if (sweeping_) {
        sweeping_ = false;
        setSelection(pending_);
        return;
    }
    if (moveArmed_) {
        moveArmed_ = false;
        if (moving_) {
            moving_ = false;
            commitBlockMove(moveHoverStep_ - moveOriginStep_, moveHoverNote_ - moveOriginNote_, e.mods.isAltDown());
        } else if (downStep_ >= 0) {
            toggleAt(downStep_, downNote_);
        }
        downStep_ = downNote_ = -1;
        return;
    }
    if (noteDragArmed_) {
        noteDragArmed_ = false;
        if (noteDragActive_) {
            noteDragActive_ = false;
            const int offset = ndGrabStep_ - ndSrcStep_;
            const int dest = juce::jmax(0, ndOverStep_ - offset);
            commitNoteMove(ndSrcStep_, ndSrcNote_, dest, ndOverNote_, e.mods.isAltDown());
        } else if (downStep_ >= 0) {
            toggleAt(downStep_, downNote_);
        }
        downStep_ = downNote_ = -1;
        return;
    }
    if (barDragFrom_ >= 0) {
        if (barDragStarted_) {
            const int target = patternIndexAt(e.getPosition());
            if (target >= 0 && target != barDragFrom_) moveBar(barDragFrom_, target, e.mods.isAltDown());
        } else {
            patternClick(barDragFrom_);
        }
        barDragFrom_ = -1; barDragStarted_ = false; barDragHover_ = -1;
        repaint();
        return;
    }
    if (downStep_ >= 0) {                          // plain click on an empty cell
        toggleAt(downStep_, downNote_);
        downStep_ = downNote_ = -1;
    }
}

void PitchSeqView::mouseMove(const juce::MouseEvent& e) {
    if (!ghost_) return;
    const auto pos = e.getPosition();
    ghostHasHover_ = gridBounds().contains(pos);
    if (ghostHasHover_) { ghostHoverStep_ = stepAt(pos.x); ghostHoverNote_ = noteAt(pos.y); }
    repaint();
}

void PitchSeqView::cancelResize() { if (resizeStep_ >= 0) resizeStep(resizeStep_, resizeStartDuration_); resizeStep_ = -1; }

void PitchSeqView::cancelGesture() {
    sweeping_ = false;
    moveArmed_ = moving_ = false;
    noteDragArmed_ = noteDragActive_ = false;
    ghost_ = false; ghostHasHover_ = false;
    barDragFrom_ = -1; barDragStarted_ = false; barDragHover_ = -1;
    downStep_ = downNote_ = -1;
    repaint();
}

bool PitchSeqView::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        cancelResize();
        cancelGesture();
        clearSelection();
        return true;
    }
    const auto mods = k.getModifiers();
    if (mods.isCommandDown()) {
        switch (k.getKeyCode()) {
            case 'A': selectAllCells(); return true;
            case 'C': copySel();        return true;
            case 'X': cutSel();         return true;
            case 'V': pasteSel();       return true;
            case 'D': duplicateSel();   return true;
            case 'Z': mods.isShiftDown() ? redoEdit() : undoEdit(); return true;
            default: break;
        }
    }
    if (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey) {
        deleteSel();
        return true;
    }
    return false;
}

// ---- animation ----------------------------------------------------------------

// Repaint only when something visible changed: transport/playhead, edit
// pattern, chain, chaining mode, or the pattern content itself.
void PitchSeqView::timerCallback() {
    // Decision-6: when the hosted clip/pattern source is swapped (SQ-4 focus
    // switching to a different scene), the undo history and selection would
    // otherwise silently apply to the wrong clip — clear both. The sentinel
    // start value means the very first tick never spuriously fires this.
    const int srcId = proc.patternSourceId();
    if (srcId != lastPatternSrc_) {
        lastPatternSrc_ = srcId;
        history_.clear();
        hasRect_ = false; sweeping_ = false;
        moveArmed_ = moving_ = false; noteDragArmed_ = noteDragActive_ = false;
        ghost_ = false; ghostHasHover_ = false;
        barDragFrom_ = -1; barDragStarted_ = false; barDragHover_ = -1;
        downStep_ = downNote_ = -1;
        hasLastCell_ = false;
        clip_ = {};
    }

    juce::uint32 sig = 17;
    auto mix = [&sig](int v) { sig = sig * 31u + (juce::uint32)(v + 2); };
    const bool playing = proc.sequencerPlaying();
    const int edit = proc.editPattern();
    mix(playing ? 1 : 0);
    mix(playing ? proc.currentStep() : -1);
    mix(proc.currentPattern());
    mix(edit);
    const auto& chain = proc.chain();
    mix((int)chain.size());
    for (int p : chain) mix(p);
    for (int s = 0; s < fable::BL_STEPS; ++s) {
        const BassSeqStep st = proc.sequenceStep(edit, s);
        mix((st.on ? 1 : 0) | (st.acc ? 2 : 0) | (st.slide ? 4 : 0));
        mix(st.note); mix(st.oct);
    }
    if (sig != lastSig_) { lastSig_ = sig; repaint(); }
}

// ---- paint ----------------------------------------------------------------------

// .bl-seq-btn: 5px radius, #11141c, dim mono text; active = green border/text
// over a dark green wash (#0e3122).
static void drawSeqBtn(juce::Graphics& g, juce::Rectangle<int> b, const juce::String& text,
                       bool active, float tracking, bool current = false, float fontSize = 8.0f) {
    const auto bf = b.toFloat();
    const juce::Colour green = accentA();
    g.setColour(current ? green.withAlpha(0.20f)
                        : active ? juce::Colour(0xff0e3122) : juce::Colour(0xff11141c));
    g.fillRoundedRectangle(bf, 5.0f);
    g.setColour(current ? green.withAlpha(0.9f) : active ? green.withAlpha(0.55f) : col::line);
    g.drawRoundedRectangle(bf.reduced(0.5f), 5.0f, 1.0f);
    if (active) {
        g.setColour(green);
        g.fillRect(b.withY(b.getBottom() - 3).withHeight(3).reduced(2, 0));
    }
    g.setColour(active || current ? green : col::textDim);
    g.setFont(monoFont(fontSize));
    drawSpaced(g, text, b, tracking, juce::Justification::centred);
}

void PitchSeqView::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    const juce::Colour green = accentA();
    // .bl-seq-section carries a faint green border + glow
    g.setColour(green.withAlpha(0.16f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 12.0f, 1.0f);

    const bool playing = proc.sequencerPlaying();
    const int edit = proc.editPattern();

    // ---- head: transport (.bl-transport — green-tinted well) ----
    const auto tb = transportBounds().toFloat();
    g.setColour(juce::Colour(0xff0e3122));
    g.fillRoundedRectangle(tb, 5.0f);
    g.setColour(green.withAlpha(playing ? 0.9f : 0.55f));
    g.drawRoundedRectangle(tb.reduced(0.5f), 5.0f, 1.0f);
    g.setColour(green);
    if (playing) {                               // stop square
        g.fillRect(juce::Rectangle<float>(8.0f, 8.0f).withCentre(tb.getCentre()));
    } else {                                     // play triangle
        juce::Path p;
        p.addTriangle(tb.getCentreX() - 3.5f, tb.getCentreY() - 5.0f,
                      tb.getCentreX() - 3.5f, tb.getCentreY() + 5.0f,
                      tb.getCentreX() + 5.5f, tb.getCentreY());
        g.fillPath(p);
    }

    // ---- head: title ----
    g.setColour(green);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, "PITCH SEQ",
               { transportBounds().getRight() + 12, kHeadY, 96, kHeadH }, 2.2f);

    // ---- head: bar buttons + sequence length + RAND ----
    const int bars = proc.capabilities().hosted ? proc.clipBars()
                                                : juce::jlimit(1, fable::BL_NPATTERNS, (int)proc.chain().size());
    const int currentBar = playing ? proc.currentPattern() : -1;
    for (int i = 0; i < fable::BL_NPATTERNS; ++i)
        drawSeqBtn(g, patternBounds(i), kPatNames[i], edit == i, 0.0f,
                   bars > 1 && currentBar == i);
    if (proc.capabilities().supportsPatternChain) {
        auto length = sequenceLengthBounds();
        const auto minus = length.removeFromLeft(38);
        const auto plus = length.removeFromRight(38);
        drawSeqBtn(g, minus, "-", false, 0.0f, false, 15.0f);
        drawSeqBtn(g, length, "LENGTH " + juce::String(bars) + "B", false, 0.25f);
        drawSeqBtn(g, plus, "+", false, 0.0f, false, 15.0f);
    }
    drawSeqBtn(g, randBounds(), "RAND", false, 0.7f);

    // ---- head: hint, right-aligned (.bl-seq-hint) ----
    g.setColour(col::textHint);
    g.setFont(monoFont(7.0f));
    // web "TAP LANE = NOTE · SLIDE TIES INTO STEP FROM PREV" — ASCII middle dot
    drawSpaced(g, "TAP LANE = NOTE - SLIDE TIES INTO STEP FROM PREV",
               { getWidth() - kPadX - 320, kHeadY, 320, kHeadH }, 0.9f,
               juce::Justification::right);

    // ---- legend column (.bl-seq-legend) ----
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
        g.setColour(green);
        g.drawText("SLD",  legend.withHeight(kSldH).translated(0, kSldY), juce::Justification::centredRight);
    }

    // ---- step columns ----
    const int curStep = proc.currentStep(), curPat = proc.currentPattern();
    BassSeqStep steps[fable::BL_STEPS];
    for (int s = 0; s < fable::BL_STEPS; ++s) steps[s] = proc.sequenceStep(edit, s);

    for (int s = 0; s < fable::BL_STEPS; ++s) {
        const BassSeqStep& st = steps[s];

        // note lanes (.bl-cell)
        for (int note = 0; note < fable::BL_NOTE_LANES; ++note) {
            const auto b = cellBounds(s, note).toFloat();
            const bool active = st.on && st.note == note;
            if (active) {
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3ddc8c), b.getX(), b.getY(),
                                                       juce::Colour(0xff1a8f57), b.getX(), b.getBottom(), false));
            } else {
                g.setColour(note == 0 ? juce::Colour(0xff0c1016) : juce::Colour(0xff0a0d13));
            }
            g.fillRoundedRectangle(b, 2.0f);
            g.setColour(active ? green.withAlpha(0.5f) : juce::Colours::white.withAlpha(0.045f));
            g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);
        }

        // octave button (.bl-oct-btn) — a nonzero octave is highlighted with a
        // tinted fill + border and bold text so an octave throw is visible at a
        // glance instead of a lone "+1"/"-1" glyph (web parity: .bl-oct-btn.set).
        {
            const auto b = octBounds(s).toFloat();
            const bool set = st.oct != 0;
            g.setColour(set ? juce::Colour(0xff0c0f16).interpolatedWith(col::acN, 0.18f)
                            : juce::Colour(0xff0c0f16));
            g.fillRoundedRectangle(b, 4.0f);
            g.setColour(set ? col::acN.withAlpha(0.55f) : col::line);
            g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
            g.setColour(set ? col::acN : col::textDim);
            g.setFont(monoFont(9.0f, set));
            g.drawText(st.oct == 0 ? "0" : st.oct > 0 ? "+1" : "-1",
                       octBounds(s), juce::Justification::centred);
        }

        // accent / slide buttons (.bl-acc-btn / .bl-sld-btn)
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
        drawToggle(slideBounds(s), st.on && st.slide,
                   juce::Colour(0xff7dffb8), juce::Colour(0xff22a565));

        // step number (.bl-step-num)
        const auto c = colBounds(s);
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        g.drawText(juce::String(s + 1), c.withY(c.getY() + kNumY).withHeight(10),
                   juce::Justification::centredTop, false);

        // playhead cursor (.bl-step-cursor)
        if (playing && curStep == s && curPat == edit) {
            const auto cf = juce::Rectangle<float>((float)c.getX() - 2, (float)c.getY() - 2,
                                                   (float)c.getWidth() + 4, (float)(kSldY + kSldH) + 4);
            g.setColour(green.withAlpha(0.85f));
            g.drawRoundedRectangle(cf, 6.0f, 1.0f);
            g.setColour(green.withAlpha(0.18f));
            g.drawRoundedRectangle(cf.expanded(1.5f), 7.0f, 3.0f);
        }
    }

    // Duration blocks share the WT-1 piano-roll affordance; slide remains a
    // separate per-step control and is never modified by resizing.
    const int gridRight = getWidth() - kPadX;
    for (int s = 0; s < fable::BL_STEPS; ++s) {
        const auto& st = steps[s];
        if (!st.on) continue;
        const auto start = cellBounds(s, st.note);
        const int pitch = colBounds(s).getWidth();
        const int width = pitch * st.duration + (int)std::round(kColGap * (float)(st.duration - 1));
        auto block = start.withWidth(juce::jmax(pitch, juce::jmin(width, gridRight - start.getX()))).reduced(1, 1);
        const auto bf = block.toFloat();
        g.setColour(green.withAlpha(0.78f));
        g.fillRoundedRectangle(bf, 3.0f);
        g.setColour(green.withAlpha(0.95f));
        g.drawRoundedRectangle(bf.reduced(0.5f), 3.0f, 1.0f);
        g.fillRect(block.removeFromRight(4).toFloat().reduced(0.0f, 2.0f));
    }

    // ---- slide connectors, drawn INTO a step from its predecessor ----
    // (seq.ts slidesInto: cur.on && cur.slide && prev.on)
    const float laneH = kLanesH / (float)fable::BL_NOTE_LANES;
    auto yOf = [&](const BassSeqStep& st) {
        return static_cast<float>(kBodyY)
             + (static_cast<float>(fable::BL_NOTE_LANES - 1 - st.note) + 0.5f) * laneH
             - static_cast<float>(st.oct) * 4.0f;
    };
    g.setColour(green.withAlpha(0.75f));
    for (int i = 1; i < fable::BL_STEPS; ++i) {
        if (!(steps[i].on && steps[i].slide && steps[i - 1].on)) continue;
        const auto c0 = colBounds(i - 1), c1 = colBounds(i);
        g.drawLine((float)c0.getCentreX(), yOf(steps[i - 1]),
                   (float)c1.getCentreX(), yOf(steps[i]), 1.4f);
    }

    // ---- 2-D selection / drag / ghost overlays (PitchSeq.tsx .bl-sel-rect) ----
    auto rectPixels = [&](const fable::RectNorm& n) {
        const int sLo = juce::jlimit(0, fable::BL_STEPS - 1, n.stepLo);
        const int sHi = juce::jlimit(0, fable::BL_STEPS - 1, n.stepHi);
        const int nLo = juce::jlimit(0, fable::BL_NOTE_LANES - 1, n.noteLo);
        const int nHi = juce::jlimit(0, fable::BL_NOTE_LANES - 1, n.noteHi);
        const auto tl = cellBounds(sLo, nHi);
        const auto br = cellBounds(sHi, nLo);
        return juce::Rectangle<float>((float)tl.getX(), (float)tl.getY(),
                                      (float)(br.getRight() - tl.getX()),
                                      (float)(br.getBottom() - tl.getY()));
    };

    if (sweeping_ || hasRect_) {
        const auto r = rectPixels(fable::rectNorm(sweeping_ ? pending_ : rect_));
        g.setColour(green.withAlpha(0.14f));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(green.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
    }

    if (noteDragActive_) {
        const auto src = cellBounds(ndSrcStep_, ndSrcNote_).toFloat();
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(src, 2.0f);
        const int offset = ndGrabStep_ - ndSrcStep_;
        const auto over = cellBounds(juce::jmax(0, ndOverStep_ - offset), ndOverNote_).toFloat();
        g.setColour(green);
        if (juce::ModifierKeys::getCurrentModifiers().isAltDown()) {
            const float dashes[] = { 3.0f, 2.0f };
            juce::Path pth; pth.addRoundedRectangle(over.reduced(0.5f), 2.0f);
            juce::Path dashed; juce::PathStrokeType(1.2f).createDashedStroke(dashed, pth, dashes, 2);
            g.strokePath(dashed, juce::PathStrokeType(1.2f));
        } else {
            g.drawRoundedRectangle(over.reduced(0.5f), 2.0f, 1.4f);
        }
    }

    if (moving_) {
        const auto n = fable::rectNorm(rect_);
        const int dStep = juce::jlimit(-n.stepLo, (fable::BL_STEPS - 1) - n.stepHi, moveHoverStep_ - moveOriginStep_);
        const int dNote = juce::jlimit(-n.noteLo, (fable::BL_NOTE_LANES - 1) - n.noteHi, moveHoverNote_ - moveOriginNote_);
        const auto r = rectPixels({ n.stepLo + dStep, n.stepHi + dStep, n.noteLo + dNote, n.noteHi + dNote });
        g.setColour(green.withAlpha(0.10f));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(green.withAlpha(0.9f));
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.4f);
    }

    if (ghost_) {
        if (ghostCut_) {
            const auto sn = fable::rectNorm(ghostSrc_);
            for (const auto& cCell : ghostData_.cells) {
                const int s = sn.stepLo + cCell.dStep, note = cCell.bytes[1] & 0x7f;
                if (s < 0 || s >= fable::BL_STEPS) continue;
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillRoundedRectangle(cellBounds(s, note).toFloat(), 2.0f);
            }
        }
        if (ghostHasHover_) {
            const int dNote = ghostHoverNote_ - ghostData_.noteHi;
            for (const auto& cCell : ghostData_.cells) {
                const int s = ghostHoverStep_ + cCell.dStep;
                const int note = (cCell.bytes[1] & 0x7f) + dNote;
                if (s < 0 || s >= fable::BL_STEPS || note < 0 || note > kMaxNote) continue;
                const auto b = cellBounds(s, note).toFloat();
                g.setColour(green.withAlpha(0.38f));
                g.fillRoundedRectangle(b, 2.0f);
                const float dashes[] = { 3.0f, 2.0f };
                juce::Path pth; pth.addRoundedRectangle(b.reduced(0.5f), 2.0f);
                juce::Path dashed; juce::PathStrokeType(1.0f).createDashedStroke(dashed, pth, dashes, 2);
                g.setColour(green);
                g.strokePath(dashed, juce::PathStrokeType(1.0f));
            }
        }
    }

    if (hasRect_ && !ghost_) {
        static const char* const kMenu[5] = { "CUT", "COPY", "DUP", "DEL", "X" };
        for (int i = 0; i < 5; ++i)
            drawSeqBtn(g, selMenuButton(i), kMenu[i], false, 0.4f, false, 7.0f);
    }

    if (barDragFrom_ >= 0 && barDragStarted_ && barDragHover_ >= 0) {
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawRoundedRectangle(patternBounds(barDragHover_).toFloat().expanded(2.0f), 6.0f, 2.0f);
    }
}

} // namespace fui
