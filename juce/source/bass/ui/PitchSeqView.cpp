#include "PitchSeqView.h"
#include "../../dsp/PatternPresetCatalog.h"
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
static constexpr int kBodyY0 = kHeadY + kHeadH + 10;  // body top without the POLY strip
static constexpr int kLegendW = 36, kLegendGap = 8;
static constexpr int kMinLanesH = 136, kBottomPad = 11;
static constexpr float kColGap = 5.0f;
// The ACC/SLD toggle lanes were cramped 14px targets; the web made them taller
// (.bl-acc-btn/.bl-sld-btn height 28) so they're easier to hit and read. The
// fixed JUCE grid can't double them, so they grow to 22px each by reclaiming a
// few px from the tall note lanes (a ~5% note-lane trim for a ~57% taller
// toggle target).
// The note lanes take every pixel the view has left after the head and the
// rows below them, so the grid fills its page. Offsets are column-relative.
static constexpr int kOctH = 20, kAccH = 22, kSldH = 22;
static constexpr int kRowsBelowLanes = 5 + kOctH + 4 + kAccH + 4 + kSldH + 4 + 10;

PitchSeqView::PitchSeqView(BassUiModel& p) : proc(p) {
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);
    // Standalone only: a hosted clip takes its notes and length from SQ-4.
    setupPatternPresetCombo(patternPreset_, fable::bassPatternPresets(),
                            [this](const fable::NotatedPreset& preset) { loadPatternPreset(preset); });
    addChildComponent(patternPreset_);
    // Melodic POLY (SequencePoly.tsx) sits between the head and the grid; its
    // edits share the note undo history.
    addChildComponent(poly_);
    addChildComponent(hScroll_);
    hScroll_.addListener(this);
    hScroll_.setAutoHide(false);
    poly_.onChange = [this](const SeqPolySettings& next) { pushHistory(); proc.setSequencePoly(next); repaint(); };
    poly_.onUndo = [this] { undoEdit(); };
    poly_.onRedo = [this] { redoEdit(); };
    poly_.onOpenChanged = [this] { resized(); repaint(); };
    startVisualTimer(*this, fui::kAnimationHz);
}

void PitchSeqView::resized() {
    patternPreset_.setVisible(!proc.capabilities().hosted);
    patternPreset_.setBounds(randBounds().getRight() + 8, kHeadY + 2, 148, 24);
    poly_.setVisible(proc.supportsSequencePoly());
    poly_.setBounds(kPadX, kBodyY0 - 4, getWidth() - 2 * kPadX, poly_.preferredHeight());
    // Sideways scroll once the timeline is wider than the grid area.
    const int gx = kPadX + kLegendW + kLegendGap, view = gridViewWidth(), content = timelineWidth();
    hScroll_.setBounds(gx, getHeight() - kBottomPad + 1, view, 8);
    hScroll_.setRangeLimits(0, juce::jmax(view, content), juce::dontSendNotification);
    hScroll_.setCurrentRange(scrollX_, view, juce::dontSendNotification);
    hScroll_.setVisible(content > view);
    setScrollX(scrollX_);
}

// Timeline column of the playhead: the playing bar's slot in the chain.
int PitchSeqView::cursorColumn() const {
    if (!proc.sequencerPlaying()) return -1;
    const int pat = proc.currentPattern(), step = proc.currentStep();
    if (step < 0 || step >= fable::BL_STEPS) return -1;
    const auto chain = timelineChain();
    for (int b = 0; b < (int)chain.size(); ++b)
        if (chain[(size_t)b] == pat) return b * fable::BL_STEPS + step;
    return -1;
}

int PitchSeqView::bodyY() const {
    return kBodyY0 + (poly_.isVisible() ? poly_.preferredHeight() + 2 : 0);
}

// Note history snapshots carry the clip's POLY settings as a 6-byte tail
// (marker + fields) so a POLY edit is one undo entry like a note edit.
std::vector<uint8_t> PitchSeqView::historySnapshot() const {
    auto bytes = proc.patternBytes();
    if (proc.supportsSequencePoly()) {
        const auto r = proc.sequencePoly();
        bytes.insert(bytes.end(), { (uint8_t)0xa5, (uint8_t)r.enabled, (uint8_t)r.fit,
                                    (uint8_t)r.steps, (uint8_t)r.rotation, (uint8_t)r.cycleBeats });
    }
    return bytes;
}

void PitchSeqView::restoreHistory(std::vector<uint8_t> bytes) {
    const size_t n = proc.patternBytes().size();
    if (bytes.size() == n + 6 && bytes[n] == 0xa5) {
        SeqPolySettings r;
        r.enabled = bytes[n + 1] != 0; r.fit = bytes[n + 2] != 0;
        r.steps = bytes[n + 3]; r.rotation = bytes[n + 4]; r.cycleBeats = bytes[n + 5];
        bytes.resize(n);
        proc.setSequencePoly(r);
    }
    proc.setPatternBytes(std::move(bytes));
}

// Replaces every bar and the length with a catalog line; the patch stays.
void PitchSeqView::loadPatternPreset(const fable::NotatedPreset& preset) {
    pushHistory();
    for (int pat = 0; pat < fable::BL_NPATTERNS; ++pat) {
        std::array<fable::NotatedStep, 16> bar {};
        if (pat < (int)preset.bars.size()) bar = fable::parseNotatedBar(preset.bars[(size_t)pat]);
        for (int s = 0; s < fable::BL_STEPS; ++s) {
            const auto& n = bar[(size_t)s];
            BassSeqStep step;
            step.on = n.on; step.note = n.note; step.oct = n.oct;
            step.acc = n.acc; step.slide = n.slide; step.duration = n.duration;
            proc.setSequenceStep(pat, s, step);
        }
    }
    std::vector<int> chain;
    for (int i = 0; i < (int)preset.bars.size(); ++i) chain.push_back(i);
    proc.setChain(std::move(chain));
    proc.setEditPattern(0);
    clearSelection();
    repaint();
}

int PitchSeqView::lanesH() const {
    return juce::jmax(kMinLanesH, getHeight() - bodyY() - kRowsBelowLanes - kBottomPad);
}
int PitchSeqView::octY() const { return lanesH() + 5; }
int PitchSeqView::accY() const { return octY() + kOctH + 4; }
int PitchSeqView::sldY() const { return accY() + kAccH + 4; }
int PitchSeqView::numY() const { return sldY() + kSldH + 4; }

juce::Rectangle<int> PitchSeqView::patternBounds(int i) const {
    const int x0 = kPadX + 96 + 12;                  // title, gap
    return { x0 + i * (26 + 5), kHeadY + 2, 26, 24 }; // .bl-pattern 26x24, gap 5
}

juce::Rectangle<int> PitchSeqView::sequenceLengthBounds() const {
    return { patternBounds(3).getRight() + 12, kHeadY, 170, 30 };
}

juce::Rectangle<int> PitchSeqView::randBounds() const {
    return { sequenceLengthBounds().getRight() + 12, kHeadY + 2, 52, 24 };
}

// ---- timeline (PitchSeq.tsx): every bar of the chain (hosted: the clip's
// bars) side by side; steps are absolute, bar b plays pattern chain[b]. ----
int PitchSeqView::timelineBars() const {
    const int bars = proc.capabilities().hosted ? proc.clipBars() : (int)proc.chain().size();
    return juce::jlimit(1, fable::BL_NPATTERNS, bars);
}
int PitchSeqView::timelineSteps() const { return timelineBars() * fable::BL_STEPS; }
std::vector<int> PitchSeqView::timelineChain() const {
    std::vector<int> chain;
    if (proc.capabilities().hosted) for (int b = 0; b < timelineBars(); ++b) chain.push_back(b);
    else { chain = proc.chain(); chain.resize((size_t)timelineBars(), 0); }
    return chain;
}
int PitchSeqView::patternOfAbs(int abs) const {
    const auto chain = timelineChain();
    return chain[(size_t)juce::jlimit(0, (int)chain.size() - 1, abs / fable::BL_STEPS)];
}
BassSeqStep PitchSeqView::stepAbs(int abs) const {
    return proc.sequenceStep(patternOfAbs(abs), abs % fable::BL_STEPS);
}
void PitchSeqView::setStepAbs(int abs, const BassSeqStep& st) {
    proc.setSequenceStep(patternOfAbs(abs), abs % fable::BL_STEPS, st);
}

// Columns share the grid width; below the web's 32 px column pitch
// (.bl-seq-grid min-width: steps * 32px) the timeline scrolls sideways.
static constexpr float kMinColW = 27.0f;
float PitchSeqView::colWidth() const {
    const int gw = getWidth() - kPadX - (kPadX + kLegendW + kLegendGap);
    const int n = timelineSteps();
    return juce::jmax(kMinColW, ((float)gw - (float)(n - 1) * kColGap) / (float)n);
}
int PitchSeqView::timelineWidth() const {
    const int n = timelineSteps();
    return (int)std::ceil(colWidth() * (float)n + kColGap * (float)(n - 1));
}
int PitchSeqView::gridViewWidth() const { return getWidth() - kPadX - (kPadX + kLegendW + kLegendGap); }

juce::Rectangle<int> PitchSeqView::colBounds(int step) const {
    const int gx = kPadX + kLegendW + kLegendGap;
    const float w = colWidth();
    const float x = static_cast<float>(gx - scrollX_) + static_cast<float>(step) * (w + kColGap);
    return juce::Rectangle<float>(x, (float)bodyY(), w, (float)(numY() + 10)).toNearestInt();
}

void PitchSeqView::scrollToStep(int abs) {
    const auto c = colBounds(abs);
    const int gx = kPadX + kLegendW + kLegendGap, right = gx + gridViewWidth();
    if (c.getX() < gx) setScrollX(scrollX_ - (gx - c.getX()));
    else if (c.getRight() > right) setScrollX(scrollX_ + (c.getRight() - right));
}

void PitchSeqView::setScrollX(int x) {
    const int maxScroll = juce::jmax(0, timelineWidth() - gridViewWidth());
    x = juce::jlimit(0, maxScroll, x);
    if (x != scrollX_) { scrollX_ = x; repaint(); }
    hScroll_.setCurrentRangeStart(scrollX_, juce::dontSendNotification);
}

void PitchSeqView::scrollBarMoved(juce::ScrollBar*, double start) { setScrollX((int)start); }

void PitchSeqView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& d) {
    if (timelineWidth() <= gridViewWidth()) { Component::mouseWheelMove(e, d); return; }
    const float delta = std::abs(d.deltaX) > std::abs(d.deltaY) ? d.deltaX : d.deltaY;
    setScrollX(scrollX_ - (int)std::round(delta * 240.0f));
}

juce::Rectangle<int> PitchSeqView::cellBounds(int step, int note) const {
    const auto c = colBounds(step);
    // 12 lanes over lanesH() with 1px gaps, note 11 on top (web lane order)
    const float ch = (lanesH() - 11.0f) / 12.0f;
    const int r = fable::BL_NOTE_LANES - 1 - note;
    const float y = static_cast<float>(c.getY()) + static_cast<float>(r) * (ch + 1.0f);
    return juce::Rectangle<float>((float)c.getX(), y, (float)c.getWidth(), ch).toNearestInt();
}

juce::Rectangle<int> PitchSeqView::octBounds(int step) const {
    const auto c = colBounds(step);
    return { c.getX(), c.getY() + octY(), c.getWidth(), kOctH };
}

juce::Rectangle<int> PitchSeqView::accBounds(int step) const {
    const auto c = colBounds(step);
    return { c.getX(), c.getY() + accY(), c.getWidth(), kAccH };
}

juce::Rectangle<int> PitchSeqView::slideBounds(int step) const {
    const auto c = colBounds(step);
    return { c.getX(), c.getY() + sldY(), c.getWidth(), kSldH };
}

juce::Rectangle<int> PitchSeqView::resizeBounds(int step) const {
    const auto st = stepAbs(step);
    if (!st.on) return {};
    auto cell = cellBounds(step, st.note);
    const int len = juce::jmin((int)st.duration, timelineSteps() - step);
    const int w = cellBounds(step + len - 1, st.note).getRight() - cell.getX();
    cell.setWidth(juce::jmax(1, w));
    return cell.removeFromRight(6).expanded(2, 2);
}

// ---- store handlers ----------------------------------------------------------

void PitchSeqView::toggleCell(int step, int note) {
    if (step < 0 || step >= timelineSteps()) return;
    BassSeqStep cur = stepAbs(step);
    if (cur.on && cur.note == note) {              // tap again = rest
        cur.on = false; cur.acc = false; cur.slide = false; cur.duration = 1;
    } else {
        cur.on = true; cur.note = note;
    }
    setStepAbs(step, cur);
    repaint();
}

void PitchSeqView::cycleStepOct(int step) {
    if (step < 0 || step >= timelineSteps()) return;
    BassSeqStep cur = stepAbs(step);
    cur.oct = cur.oct >= fable::BL_OCT_MAX ? fable::BL_OCT_MIN : cur.oct + 1; // seq.ts cycleOct
    setStepAbs(step, cur);
    repaint();
}

void PitchSeqView::toggleStepAcc(int step) {
    if (step < 0 || step >= timelineSteps()) return;
    BassSeqStep cur = stepAbs(step);
    if (!cur.on) return;
    cur.acc = !cur.acc;
    setStepAbs(step, cur);
    repaint();
}

void PitchSeqView::toggleStepSlide(int step) {
    if (step < 0 || step >= timelineSteps()) return;
    BassSeqStep cur = stepAbs(step);
    if (!cur.on) return;
    cur.slide = !cur.slide;
    setStepAbs(step, cur);
    repaint();
}

// NoteLengthHandle: a note may run across bar lines up to the timeline end,
// and stops before the next note of the same pitch.
void PitchSeqView::resizeStep(int step, int duration) {
    if (step < 0 || step >= timelineSteps()) return;
    auto cur = stepAbs(step);
    if (!cur.on) return;
    const int total = timelineSteps();
    int limit = juce::jmin(63, total - step);
    for (int a = step + 1; a < total; ++a) {
        const auto other = stepAbs(a);
        if (other.on && other.note == cur.note && other.oct == cur.oct) { limit = a - step; break; }
    }
    cur.duration = juce::jlimit(1, limit, duration);
    setStepAbs(step, cur);
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

// Bar chip: choose the bar RAND / Cmd-D / pattern paste act on, and bring
// its first played column into view.
void PitchSeqView::patternClick(int i) {
    proc.setEditPattern(i);
    const auto chain = timelineChain();
    for (int b = 0; b < (int)chain.size(); ++b)
        if (chain[(size_t)b] == i) {           // the bar's first column at the left edge
            setScrollX(scrollX_ + colBounds(b * fable::BL_STEPS).getX() - (kPadX + kLegendW + kLegendGap));
            break;
        }
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
    const float w = colWidth();
    if (w + kColGap <= 0.0f) return 0;
    const int step = (int)std::floor(((float)(x + scrollX_) - (float)gx) / (w + kColGap));
    return juce::jlimit(0, timelineSteps() - 1, step);
}

int PitchSeqView::patternIndexAt(juce::Point<int> p) const {
    for (int i = 0; i < fable::BL_NPATTERNS; ++i)
        if (patternBounds(i).contains(p)) return i;
    return -1;
}

// Inverse of cellBounds' lane math: note 11 sits on top, C at the bottom.
int PitchSeqView::noteAt(int y) const {
    const float ch = (lanesH() - 11.0f) / 12.0f;
    const float rel = (float)(y - bodyY()) / (ch + 1.0f);
    const int row = juce::jlimit(0, fable::BL_NOTE_LANES - 1, (int)std::floor(rel));
    return fable::BL_NOTE_LANES - 1 - row;
}

// The 16×12 note-lane area (excludes the oct / acc / slide / step rows).
// The visible part of the note-lane timeline (excludes the oct / acc /
// slide / step rows and any columns scrolled out of view).
juce::Rectangle<int> PitchSeqView::gridBounds() const {
    const int gx = kPadX + kLegendW + kLegendGap;
    return { gx, bodyY(), juce::jmin(gridViewWidth(), timelineWidth()), lanesH() };
}

// Floating CUT · COPY · DUP · DEL · ✕ toolbar over the selected columns.
juce::Rectangle<int> PitchSeqView::selMenuBounds() const {
    constexpr int bw = 46, gap = 3, n = 5, h = 22;
    constexpr int w = n * bw + (n - 1) * gap;
    const auto nrm = fable::rectNorm(rect_);
    const auto lo = colBounds(juce::jlimit(0, timelineSteps() - 1, nrm.stepLo));
    const auto hi = colBounds(juce::jlimit(0, timelineSteps() - 1, nrm.stepHi));
    const int cx = (lo.getX() + hi.getRight()) / 2;
    const auto grid = gridBounds();
    const int x = juce::jlimit(grid.getX(), juce::jmax(grid.getX(), grid.getRight() - w), cx - w / 2);
    return { x, bodyY() + 1, w, h };
}

juce::Rectangle<int> PitchSeqView::selMenuButton(int i) const {
    constexpr int bw = 46, gap = 3;
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
// PitchSeq.tsx noteHead: searches back within the cell's own bar only.
int PitchSeqView::grabNoteAt(int step, int note) const {
    const auto here = stepAbs(step);
    if (here.on && here.note == note) return step;
    const int barStart = step - step % fable::BL_STEPS;
    for (int c = step - 1; c >= barStart; --c) {
        const auto v = stepAbs(c);
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
    setSelection({ 0, timelineSteps() - 1, 0, fable::BL_NOTE_LANES - 1 });
}

void PitchSeqView::clearSelection() { hasRect_ = false; repaint(); }

void PitchSeqView::toggleAt(int step, int note) {
    toggleCell(step, note);
    hasLastCell_ = true; lastCellStep_ = step; lastCellNote_ = note;
}

// Verbs mirror src/bass/store.ts: a rectangle works over the timeline
// (chain-aware, may span bars); without one, copy/cut/paste act on the
// edit bar's whole pattern.
void PitchSeqView::copySel() {
    const int pat = proc.editPattern();
    clip_.valid = true;
    if (hasRect_) {
        clip_.isPattern = false;
        clip_.rect = fable::copyRectChain(proc.patternBytes(), seqLayout(), timelineChain(), rect_);
    } else {
        clip_.isPattern = true;
        clip_.pattern = fable::copyPattern(proc.patternBytes(), seqLayout(), pat);
    }
}

void PitchSeqView::cutSel() {
    copySel();
    pushHistory();
    if (hasRect_)
        proc.setPatternBytes(fable::clearRectChain(proc.patternBytes(), seqLayout(), timelineChain(), rect_, kEmptyStep));
    else
        proc.setPatternBytes(fable::clearRange(proc.patternBytes(), seqLayout(), proc.editPattern(), 0,
                                               fable::BL_STEPS - 1, kEmptyStep));
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
    proc.setPatternBytes(fable::pasteRectChain(proc.patternBytes(), seqLayout(), timelineChain(), atStep, dNote, clip_.rect, kMaxNote));
    rect_ = { atStep, atStep + clip_.rect.wSteps - 1, clip_.rect.noteLo + dNote, clip_.rect.noteHi + dNote };
    hasRect_ = true;
    repaint();
}

void PitchSeqView::duplicateSel() {
    const int pat = proc.editPattern();
    if (hasRect_) {
        const auto n = fable::rectNorm(rect_);
        if (n.stepHi + 1 >= timelineSteps()) return;
        const auto chain = timelineChain();
        const auto data = fable::copyRectChain(proc.patternBytes(), seqLayout(), chain, rect_);
        pushHistory();
        proc.setPatternBytes(fable::pasteRectChain(proc.patternBytes(), seqLayout(), chain, n.stepHi + 1, 0, data, kMaxNote));
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
    proc.setPatternBytes(fable::clearRectChain(proc.patternBytes(), seqLayout(), timelineChain(), rect_, kEmptyStep));
    repaint();
}

void PitchSeqView::undoEdit() {
    fable::StepBytes restored;
    if (history_.undo(historySnapshot(), restored)) { restoreHistory(std::move(restored)); repaint(); }
}

void PitchSeqView::redoEdit() {
    fable::StepBytes restored;
    if (history_.redo(historySnapshot(), restored)) { restoreHistory(std::move(restored)); repaint(); }
}

// useSeqNoteDrag: a note moves within its own bar (same pattern only).
void PitchSeqView::commitNoteMove(int srcStep, int srcNote, int destStep, int destNote, bool copy) {
    const int barStart = srcStep - srcStep % fable::BL_STEPS;
    destStep = juce::jlimit(barStart, barStart + fable::BL_STEPS - 1, destStep);
    destNote = juce::jlimit(0, fable::BL_NOTE_LANES - 1, destNote);
    if (srcStep == destStep && srcNote == destNote) return;
    const fable::RectSel one { srcStep, srcStep, srcNote, srcNote };
    fable::RectMoveOpts opts; opts.copy = copy; opts.emptyStep = kEmptyStep; opts.maxNote = kMaxNote;
    pushHistory();
    proc.setPatternBytes(fable::moveRectChain(proc.patternBytes(), seqLayout(), timelineChain(), one,
                                              destStep - srcStep, destNote - srcNote, opts));
    rect_ = { destStep, destStep, destNote, destNote }; hasRect_ = true;
    hasLastCell_ = true; lastCellStep_ = destStep; lastCellNote_ = destNote;
    repaint();
}

void PitchSeqView::commitBlockMove(int dStep, int dNote, bool copy) {
    if (!hasRect_) return;
    const auto n = fable::rectNorm(rect_);
    dStep = juce::jlimit(-n.stepLo, (timelineSteps() - 1) - n.stepHi, dStep);
    dNote = juce::jlimit(-n.noteLo, (fable::BL_NOTE_LANES - 1) - n.noteHi, dNote);
    if (dStep == 0 && dNote == 0) return;
    fable::RectMoveOpts opts; opts.copy = copy; opts.emptyStep = kEmptyStep; opts.maxNote = kMaxNote;
    pushHistory();
    proc.setPatternBytes(fable::moveRectChain(proc.patternBytes(), seqLayout(), timelineChain(), rect_, dStep, dNote, opts));
    rect_ = { n.stepLo + dStep, n.stepHi + dStep, n.noteLo + dNote, n.noteHi + dNote }; hasRect_ = true;
    repaint();
}

void PitchSeqView::beginGhostPaste(bool cut) {
    if (!hasRect_) return;
    ghostData_ = fable::copyRectChain(proc.patternBytes(), seqLayout(), timelineChain(), rect_);
    if (ghostData_.cells.empty()) return;
    clip_.valid = true; clip_.isPattern = false; clip_.rect = ghostData_;
    ghost_ = true; ghostCut_ = cut; ghostSrc_ = rect_; ghostHasHover_ = false;
    hasRect_ = false;
    repaint();
}

void PitchSeqView::dropGhost(int atStep, int atNote) {
    if (!ghost_) return;
    const auto chain = timelineChain();
    const int dNote = atNote - ghostData_.noteHi;
    pushHistory();
    std::vector<uint8_t> base = ghostCut_
        ? fable::clearRectChain(proc.patternBytes(), seqLayout(), chain, ghostSrc_, kEmptyStep)
        : proc.patternBytes();
    proc.setPatternBytes(fable::pasteRectChain(base, seqLayout(), chain, atStep, dNote, ghostData_, kMaxNote));
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
    for (int s = 0; s < timelineSteps(); ++s) if (gridBounds().contains(pos) && resizeBounds(s).contains(pos)) {
        resizeStep_ = s; resizeStartDuration_ = stepAbs(s).duration; return;
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
            dragStartPos_ = dragCurPos_ = e.position;
            return;
        }
        const int origin = grabNoteAt(step, note);
        if (origin >= 0) {
            noteDragArmed_ = true; noteDragActive_ = false;
            ndSrcStep_ = origin; ndSrcNote_ = note; ndGrabStep_ = step;
            ndOverStep_ = step; ndOverNote_ = note;
            dragStartPos_ = dragCurPos_ = e.position;
            return;
        }
        drawingNote_ = true; drawDuration_ = 1;
        repaint();
        return;
    }

    // oct / accent / slide rows (the step-number strip below is a plain label).
    const int gx = kPadX + kLegendW + kLegendGap;
    if (pos.x < gx || pos.x >= gx + gridViewWidth()) return;
    for (int s = 0; s < timelineSteps(); ++s) {
        if (!colBounds(s).contains(pos)) continue;
        if (octBounds(s).contains(pos))   { cycleStepOct(s); return; }
        if (accBounds(s).contains(pos))   { toggleStepAcc(s); return; }
        if (slideBounds(s).contains(pos)) { toggleStepSlide(s); return; }
        return;
    }
}

void PitchSeqView::mouseDrag(const juce::MouseEvent& e) {
    if (drawingNote_) {
        drawDuration_ = juce::jmax(1, stepAt(e.getPosition().x) - downStep_ + 1);
        repaint();
        return;
    }
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
        dragCurPos_ = e.position;
        moveHoverStep_ = stepAt(p.x); moveHoverNote_ = noteAt(p.y);
        if (moveHoverStep_ != moveOriginStep_ || moveHoverNote_ != moveOriginNote_) moving_ = true;
        repaint();
        return;
    }
    if (noteDragArmed_) {
        dragCurPos_ = e.position;
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
    if (drawingNote_) {
        mouseDrag(e);
        drawingNote_ = false;
        pushHistory();
        auto note = stepAbs(downStep_);
        note.on = true; note.note = downNote_;
        note.duration = juce::jlimit(1, juce::jmin(63, timelineSteps() - downStep_), drawDuration_);
        setStepAbs(downStep_, note);
        hasLastCell_ = true; lastCellStep_ = downStep_; lastCellNote_ = downNote_;
        downStep_ = downNote_ = -1;
        repaint();
        return;
    }
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
        } else if (downStep_ >= 0) {       // plain click inside the rect
            const int head = grabNoteAt(downStep_, downNote_);
            if (head >= 0) setSelection({ head, head, downNote_, downNote_ }); // click a note = select it
            else toggleAt(downStep_, downNote_);                               // empty cell: make a note
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
        } else if (downStep_ >= 0) {       // plain tap on a note: select it (head or body)
            setSelection({ ndSrcStep_, ndSrcStep_, ndSrcNote_, ndSrcNote_ });
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
    drawingNote_ = false;
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
        drawingNote_ = false;
        moveArmed_ = moving_ = false; noteDragArmed_ = noteDragActive_ = false;
        ghost_ = false; ghostHasHover_ = false;
        barDragFrom_ = -1; barDragStarted_ = false; barDragHover_ = -1;
        downStep_ = downNote_ = -1;
        hasLastCell_ = false;
        clip_ = {};
    }
    if (poly_.isVisible() != proc.supportsSequencePoly() || timelineSteps() != lastTimelineSteps_) {
        lastTimelineSteps_ = timelineSteps();
        resized(); repaint();
    }
    if (proc.supportsSequencePoly()) poly_.setSettings(proc.sequencePoly(), srcId);


    const auto key = contentKey();
    const bool playing = proc.sequencerPlaying();
    // The cursor sits on the timeline column of the playing bar and step.
    const int curPat = playing ? proc.currentPattern() : -1;    // matches the painted bar chip
    const int curStep = cursorColumn();

    if (key != lastContentKey_) {
        lastContentKey_ = key;
        lastCursorStep_ = curStep;
        lastCursorPattern_ = curPat;
        repaint();
        return;
    }
    if (curStep == lastCursorStep_ && curPat == lastCursorPattern_) return;

    // Only the playhead moved: repaint the old and the new cursor column. The
    // cursor is a 3px glow ring outside colBounds, so pad generously.
    static constexpr int kCursorPad = 8;
    juce::Rectangle<int> dirty;
    if (lastCursorStep_ >= 0) dirty = dirty.getUnion(colBounds(lastCursorStep_).expanded(kCursorPad));
    if (curStep >= 0) dirty = dirty.getUnion(colBounds(curStep).expanded(kCursorPad));
    // The playing bar also lights its chip in the header row.
    if (curPat != lastCursorPattern_)
        dirty = dirty.getUnion(patternBounds(0)
                                   .getUnion(patternBounds(fable::BL_NPATTERNS - 1))
                                   .expanded(kCursorPad));
    lastCursorStep_ = curStep;
    lastCursorPattern_ = curPat;
    if (!dirty.isEmpty()) repaint(dirty);
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

juce::MemoryBlock PitchSeqView::contentKey() const {
    juce::MemoryOutputStream out;
    auto add = [&](int v) { out.writeInt(v); };
    add(getWidth()); add(getHeight());
    add(proc.sequencerPlaying()); add(proc.editPattern()); add(proc.clipBars());
    add(proc.capabilities().hosted); add(proc.capabilities().supportsPatternChain);
    const auto& chain = proc.chain(); add((int)chain.size()); for (int v : chain) add(v);
    add(timelineSteps()); add(scrollX_);
    for (int i = 0; i < timelineSteps(); ++i) {
        const auto st = stepAbs(i);
        add(st.on); add(st.note); add(st.oct); add(st.acc); add(st.slide); add(st.duration);
    }
    auto rect = [&](const fable::RectSel& r) {
        const auto n = fable::rectNorm(r);
        add(n.stepLo); add(n.stepHi); add(n.noteLo); add(n.noteHi);
    };
    add(hasRect_); rect(rect_); add(sweeping_); rect(pending_);
    add(drawingNote_); add(downStep_); add(downNote_); add(drawDuration_);
    add(noteDragActive_); add(ndSrcStep_); add(ndSrcNote_); add(ndGrabStep_); add(ndOverStep_); add(ndOverNote_);
    out.writeFloat(dragStartPos_.x); out.writeFloat(dragStartPos_.y);
    out.writeFloat(dragCurPos_.x); out.writeFloat(dragCurPos_.y);
    add(noteDragActive_ && juce::ModifierKeys::getCurrentModifiers().isAltDown());
    add(moving_); add(moveOriginStep_); add(moveOriginNote_); add(moveHoverStep_); add(moveHoverNote_);
    add(ghost_); add(ghostCut_); add(ghostHasHover_); rect(ghostSrc_);
    add(ghostHoverStep_); add(ghostHoverNote_); add(ghostData_.noteHi);
    add((int)ghostData_.cells.size());
    for (const auto& cell : ghostData_.cells) { add(cell.dStep); add(cell.bytes[1]); }
    add(barDragFrom_); add(barDragStarted_); add(barDragHover_);
    const auto poly = proc.supportsSequencePoly() ? proc.sequencePoly() : SeqPolySettings{};
    add(poly_.isVisible()); add(poly_.isOpen()); add(poly.enabled); add(poly.steps);
    return out.getMemoryBlock();
}

void PitchSeqView::paint(juce::Graphics& g) {
    const float physicalScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float scale = std::isfinite(physicalScale) && physicalScale > 0 ? physicalScale : 1.0f;
    // Hosted editors use fractional zoom and parent origins. JUCE exposes
    // raster scale here, but not the subpixel device origin needed to cache
    // that raster phase faithfully, so preserve the original renderer there.
    bool aligned = scale == std::floor(scale);
    for (juce::Component* component = this; aligned && component != nullptr; component = component->getParentComponent()) {
        const auto transform = component->getTransform();
        aligned = transform.mat00 == std::floor(transform.mat00)
            && transform.mat11 == std::floor(transform.mat11)
            && transform.mat01 == 0.0f && transform.mat10 == 0.0f
            && transform.mat02 == std::floor(transform.mat02)
            && transform.mat12 == std::floor(transform.mat12);
    }
    if (!aligned) {
        paintArtwork(g, true);
        return;
    }
    const int w = (int)std::ceil(getWidth() * scale), h = (int)std::ceil(getHeight() * scale);
    if (w <= 0 || h <= 0) return;
    const auto key = contentKey();
    const bool rebuild = artwork_.getWidth() != w || artwork_.getHeight() != h
        || artworkScale_ != scale || artworkKey_ != key;
    if (rebuild) {
        artworkScale_ = scale; artworkKey_ = key;
        artwork_ = juce::Image(juce::Image::ARGB, w, h, true);
        juce::Graphics ag(artwork_); ag.addTransform(juce::AffineTransform::scale(scale));
        paintArtwork(ag, false);
        panelBackdrop_ = juce::Image(juce::Image::ARGB, w, h, true);
        juce::Graphics pg(panelBackdrop_); pg.addTransform(juce::AffineTransform::scale(scale));
        drawPanel(pg, getLocalBounds().toFloat());
        ++artworkBuilds_;
    }
    const bool playing = proc.sequencerPlaying();
    const std::array<int, 2> cursor { playing ? proc.currentPattern() : -1,
                                      playing ? proc.currentStep() : -1 };
    if (rebuild || cursor != composedCursor_) {
        composedCursor_ = cursor;
        composed_ = artwork_.createCopy();
        juce::RectangleList<int> live;
        if (playing) {
            if (cursor[0] >= 0 && cursor[0] < 4) live.add(patternBounds(cursor[0]).expanded(2));
            if (const int col = cursorColumn(); col >= 0) live.add(colBounds(col).expanded(8));
        }
        if (!live.isEmpty()) {
            juce::RectangleList<int> pixels;
            for (const auto& r : live) {
                const auto physical = r.toFloat() * scale;
                pixels.add(physical.getSmallestIntegerContainer());
            }
            // Reconstruct each dirty region from transparent pixels. Replaying
            // translucent panel/shadow artwork over the base would double it.
            for (const auto& r : pixels) composed_.clear(r, juce::Colours::transparentBlack);
            juce::Graphics og(composed_);
            og.reduceClipRegion(pixels);
            og.setColour(juce::Colours::white);
            og.drawImageAt(panelBackdrop_, 0, 0);
            og.addTransform(juce::AffineTransform::scale(scale));
            paintArtwork(og, true, false);
        }
        ++overlayBuilds_;
    }
    juce::Graphics::ScopedSaveState save(g);
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.setColour(juce::Colours::white);
    g.drawImageTransformed(composed_, juce::AffineTransform::scale(1.0f / scale));
}

void PitchSeqView::paintArtwork(juce::Graphics& g, bool cursors, bool panel) {
    if (panel) drawPanel(g, getLocalBounds().toFloat());
    const juce::Colour green = accentA();
    // .bl-seq-section carries a faint green border + glow
    g.setColour(green.withAlpha(0.16f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 12.0f, 1.0f);

    const bool playing = proc.sequencerPlaying();
    const int edit = proc.editPattern();

    // ---- head: title ----
    g.setColour(green);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, "PITCH SEQ",
               { kPadX, kHeadY, 96, kHeadH }, 2.2f);

    // ---- head: bar buttons + sequence length + RAND ----
    const int bars = proc.capabilities().hosted ? proc.clipBars()
                                                : juce::jlimit(1, fable::BL_NPATTERNS, (int)proc.chain().size());
    const int currentBar = playing && cursors ? proc.currentPattern() : -1;
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
    if (barDragFrom_ >= 0 && barDragStarted_ && barDragHover_ >= 0) {
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawRoundedRectangle(patternBounds(barDragHover_).toFloat().expanded(2.0f), 6.0f, 2.0f);
    }

    // ---- head: hint, right-aligned (.bl-seq-hint) ----
    g.setColour(col::textHint);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "DRAG EMPTY = DRAW - EDGE = LENGTH",
               { getWidth() - kPadX - 320, kHeadY, 320, kHeadH }, 0.9f,
               juce::Justification::right);

    // ---- legend column (.bl-seq-legend) ----
    {
        const juce::Rectangle<int> legend(kPadX, bodyY(), kLegendW, numY() + 10);
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        g.drawText("B",    legend.withHeight(10).translated(0, 2), juce::Justification::centredRight);
        g.drawText("NOTE", legend.withHeight(10).translated(0, (lanesH() - 10) / 2), juce::Justification::centredRight);
        g.drawText("C",    legend.withHeight(10).translated(0, lanesH() - 12), juce::Justification::centredRight);
        g.drawText("OCT",  legend.withHeight(kOctH).translated(0, octY()), juce::Justification::centredRight);
        g.setColour(col::acB);
        g.drawText("ACC",  legend.withHeight(kAccH).translated(0, accY()), juce::Justification::centredRight);
        g.setColour(green);
        g.drawText("SLD",  legend.withHeight(kSldH).translated(0, sldY()), juce::Justification::centredRight);
    }

    // ---- step columns: the whole timeline, clipped to the scrolled view ----
    const int total = timelineSteps();
    const int cursorCol = cursors && playing ? cursorColumn() : -1;
    std::vector<BassSeqStep> steps((size_t)total);
    for (int s = 0; s < total; ++s) steps[(size_t)s] = stepAbs(s);
    const int gridX = kPadX + kLegendW + kLegendGap;
    juce::Graphics::ScopedSaveState columnClip(g);
    g.reduceClipRegion(gridX - 3, 0, gridViewWidth() + 6, getHeight());

    for (int s = 0; s < total; ++s) {
        const BassSeqStep& st = steps[(size_t)s];
        if (!g.clipRegionIntersects(colBounds(s).expanded(4, 4))) continue;
        // .bl-seq-col.bar-start::before: a green rule ahead of each new bar.
        if (s > 0 && s % fable::BL_STEPS == 0) {
            const auto c = colBounds(s);
            const float x = (float)c.getX() - kColGap * 0.5f - 0.5f;
            g.setColour(juce::Colour(0x804dff9e));
            g.fillRect(x, (float)c.getY() - 2.0f, 1.0f, (float)c.getHeight() - 8.0f);
        }

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
        g.drawText(s % fable::BL_STEPS == 0 ? "BAR " + juce::String(s / fable::BL_STEPS + 1)
                                            : juce::String(s % fable::BL_STEPS + 1),
                   c.withY(c.getY() + numY()).withHeight(10).expanded(6, 0),
                   juce::Justification::centredTop, false);

        // playhead cursor (.bl-step-cursor)
        if (cursorCol == s) {
            const auto cf = juce::Rectangle<float>((float)c.getX() - 2, (float)c.getY() - 2,
                                                   (float)c.getWidth() + 4, (float)(sldY() + kSldH) + 4);
            g.setColour(green.withAlpha(0.85f));
            g.drawRoundedRectangle(cf, 6.0f, 1.0f);
            g.setColour(green.withAlpha(0.18f));
            g.drawRoundedRectangle(cf.expanded(1.5f), 7.0f, 3.0f);
        }
    }

    // Duration blocks share the WT-1 piano-roll affordance; slide remains a
    // separate per-step control and is never modified by resizing.
    for (int s = 0; s < total; ++s) {
        const auto& st = steps[(size_t)s];
        if (!st.on) continue;
        const auto start = cellBounds(s, st.note);
        const int last = juce::jmin(total - 1, s + (int)st.duration - 1);
        auto block = start.withRight(cellBounds(last, st.note).getRight()).reduced(1, 1);
        const auto bf = block.toFloat();
        g.setColour(green.withAlpha(0.78f));
        g.fillRoundedRectangle(bf, 3.0f);
        g.setColour(green.withAlpha(0.95f));
        g.drawRoundedRectangle(bf.reduced(0.5f), 3.0f, 1.0f);
        g.fillRect(block.removeFromRight(4).toFloat().reduced(0.0f, 2.0f));
    }

    // ---- slide connectors, drawn INTO a step from its predecessor ----
    // (seq.ts slidesInto: cur.on && cur.slide && prev.on)
    const float laneH = lanesH() / (float)fable::BL_NOTE_LANES;
    auto yOf = [&](const BassSeqStep& st) {
        return static_cast<float>(bodyY())
             + (static_cast<float>(fable::BL_NOTE_LANES - 1 - st.note) + 0.5f) * laneH
             - static_cast<float>(st.oct) * 4.0f;
    };
    g.setColour(green.withAlpha(0.75f));
    for (int i = 1; i < total; ++i) {
        const auto& prev = steps[(size_t)i - 1];
        const auto& cur = steps[(size_t)i];
        if (!(cur.on && cur.slide && prev.on)) continue;
        const auto c0 = colBounds(i - 1), c1 = colBounds(i);
        g.drawLine((float)c0.getCentreX(), yOf(prev), (float)c1.getCentreX(), yOf(cur), 1.4f);
    }

    // ---- melodic POLY: lanes outside the loop (bar 1, first N cells) fade
    // to 45 % (.sequence-poly-outside .bl-seq-lanes) ----
    if (proc.supportsSequencePoly()) {
        const auto poly = proc.sequencePoly();
        if (poly.enabled)
            for (int s = 0; s < total; ++s) {
                if (patternOfAbs(s) == 0 && s % fable::BL_STEPS < poly.steps) continue;
                const auto c = colBounds(s);
                g.setColour(col::panelLo.withAlpha(0.55f));
                g.fillRect(c.getX() - 1, bodyY() - 1, c.getWidth() + 2, lanesH() + 2);
            }
    }

    // ---- 2-D selection / drag / ghost overlays (PitchSeq.tsx .bl-sel-rect) ----
    auto rectPixels = [&](const fable::RectNorm& n) {
        const int sLo = juce::jlimit(0, total - 1, n.stepLo);
        const int sHi = juce::jlimit(0, total - 1, n.stepHi);
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

    if (drawingNote_) {
        auto r = cellBounds(downStep_, downNote_).toFloat();
        r.setRight(cellBounds(downStep_ + drawDuration_ - 1, downNote_).toFloat().getRight());
        g.setColour(green.withAlpha(0.45f));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(green.withAlpha(0.9f));
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.2f);
    }
    if (noteDragActive_) {
        // The dragged note previews at its landing spot with its real length,
        // and the note it left behind is dimmed (.ns-note-preview / .drag-src).
        const int dur = juce::jmax(1, (int)stepAbs(ndSrcStep_).duration);
        const auto noteRect = [this, total](int step, int note, int len) {
            auto r = cellBounds(step, note).toFloat();
            r.setRight(cellBounds(juce::jmin(total - 1, step + len - 1), note).toFloat().getRight());
            return r;
        };
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(noteRect(ndSrcStep_, ndSrcNote_, dur), 2.0f);
        const int offset = ndGrabStep_ - ndSrcStep_;
        const int barStart = ndSrcStep_ - ndSrcStep_ % fable::BL_STEPS;
        const int dest = juce::jlimit(barStart, barStart + fable::BL_STEPS - 1, ndOverStep_ - offset);
        // Snap target: a faint outline where the note lands on release.
        const auto snap = noteRect(dest, ndOverNote_, juce::jmin(dur, total - dest));
        g.setColour(green.withAlpha(0.35f));
        g.drawRoundedRectangle(snap.reduced(0.5f), 3.0f, 1.0f);
        // The note body itself rides the pointer at pixel resolution.
        const auto over = noteRect(ndSrcStep_, ndSrcNote_, dur)
                              .translated(dragCurPos_.x - dragStartPos_.x, dragCurPos_.y - dragStartPos_.y);
        g.setColour(green.withAlpha(0.45f));
        g.fillRoundedRectangle(over, 3.0f);
        g.setColour(green.withAlpha(0.9f));
        if (juce::ModifierKeys::getCurrentModifiers().isAltDown()) {
            const float dashes[] = { 3.0f, 2.0f };
            juce::Path pth; pth.addRoundedRectangle(over.reduced(0.5f), 3.0f);
            juce::Path dashed; juce::PathStrokeType(1.2f).createDashedStroke(dashed, pth, dashes, 2);
            g.strokePath(dashed, juce::PathStrokeType(1.2f));
        } else {
            g.drawRoundedRectangle(over.reduced(0.5f), 3.0f, 1.2f);
        }
    }

    if (moving_) {
        const auto n = fable::rectNorm(rect_);
        const int dStep = juce::jlimit(-n.stepLo, (total - 1) - n.stepHi, moveHoverStep_ - moveOriginStep_);
        const int dNote = juce::jlimit(-n.noteLo, (fable::BL_NOTE_LANES - 1) - n.noteHi, moveHoverNote_ - moveOriginNote_);
        // Snap target: a faint outline where the block lands on release.
        const auto r = rectPixels({ n.stepLo + dStep, n.stepHi + dStep, n.noteLo + dNote, n.noteHi + dNote });
        g.setColour(green.withAlpha(0.35f));
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
        // The block itself rides the pointer at pixel resolution.
        const auto free = rectPixels({ n.stepLo, n.stepHi, n.noteLo, n.noteHi })
                              .translated(dragCurPos_.x - dragStartPos_.x, dragCurPos_.y - dragStartPos_.y);
        g.setColour(green.withAlpha(0.10f));
        g.fillRoundedRectangle(free, 3.0f);
        g.setColour(green.withAlpha(0.9f));
        g.drawRoundedRectangle(free.reduced(0.5f), 3.0f, 1.4f);
    }

    if (ghost_) {
        if (ghostCut_) {
            const auto sn = fable::rectNorm(ghostSrc_);
            for (const auto& cCell : ghostData_.cells) {
                const int s = sn.stepLo + cCell.dStep, note = cCell.bytes[1] & 0x7f;
                if (s < 0 || s >= total) continue;
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillRoundedRectangle(cellBounds(s, note).toFloat(), 2.0f);
            }
        }
        if (ghostHasHover_) {
            const int dNote = ghostHoverNote_ - ghostData_.noteHi;
            for (const auto& cCell : ghostData_.cells) {
                const int s = ghostHoverStep_ + cCell.dStep;
                const int note = (cCell.bytes[1] & 0x7f) + dNote;
                if (s < 0 || s >= total || note < 0 || note > kMaxNote) continue;
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
        // .seq-selmenu backdrop: dark rounded panel with a faint accent border
        const auto mb = selMenuBounds().expanded(4, 3).toFloat();
        g.setColour(juce::Colour(0xea0a0d13));
        g.fillRoundedRectangle(mb, 6.0f);
        g.setColour(accentA().withAlpha(0.45f));
        g.drawRoundedRectangle(mb.reduced(0.5f), 6.0f, 1.0f);
        for (int i = 0; i < 5; ++i)
            drawSeqBtn(g, selMenuButton(i), kMenu[i], false, 0.4f, false, 10.0f);
    }

}

} // namespace fui
