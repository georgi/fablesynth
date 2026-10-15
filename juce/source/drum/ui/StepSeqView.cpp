#include "StepSeqView.h"
#include "../dsp/DrumKits.h"
#include "../dsp/DrumPatches.h"
#include <cmath>
#include <limits>

namespace fui {

static const char* const kPatNames[fable::DR_NPATTERNS] = { "1", "2", "3", "4" };

// ---- SelBarView -------------------------------------------------------------

SelBarView::SelBarView(DrumUiModel& p) : proc(p) {
    lastPatchContextRevision_ = proc.patchContextRevision();
    setInterceptsMouseClicks(false, true);    // bar is display-only; buttons live
    auto styleBtn = [this](juce::TextButton& b, int dir) {   // kit-stepper styling
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b.setColour(juce::TextButton::textColourOffId, col::text);
        b.onClick = [this, dir] { stepPatch(dir); };
        addAndMakeVisible(b);
    };
    styleBtn(prevBtn, -1);
    styleBtn(nextBtn, +1);
    browseBtn_.onClick = [this] { if (onBrowse) onBrowse(); };
    addAndMakeVisible(browseBtn_);
    startVisualTimer(*this, 10);
}

void SelBarView::timerCallback() {
    const int sel = proc.selectedPad();
    const int prog = proc.currentProgram();
    const auto patchRevision = proc.patchContextRevision();
    auto name = proc.padName(sel);
    if (sel != lastSel_ || prog != lastProgram_ || patchRevision != lastPatchContextRevision_
        || name != lastName_) {
        // The processor revision also catches same-pad selection, reloading the
        // current kit, and host-state restores whose visible indices don't change.
        if (patchRevision != lastPatchContextRevision_) patchIndex_ = -1;
        lastSel_ = sel;
        lastProgram_ = prog;
        lastPatchContextRevision_ = patchRevision;
        lastName_ = name;
        repaint();
    }
}

void SelBarView::stepPatch(int dir) {
    const int n = (int)fable::factoryPatches().size();
    if (n == 0) return;
    patchIndex_ = patchIndex_ < 0 ? (dir > 0 ? 0 : n - 1)
                                  : ((patchIndex_ + dir) % n + n) % n;
    proc.applyFactoryPadPatch(patchIndex_);
    repaint();
}

void SelBarView::resized() {
    // Right-aligned patch stepper: [PATCH 40] 4 [prev 22] 4 [name 104] 4 [next 22]
    constexpr int w = 40 + 4 + 22 + 4 + 104 + 4 + 22;
    auto strip = getLocalBounds().reduced(11, 0).removeFromRight(w)
                     .withSizeKeepingCentre(w, 20);
    browseBtn_.setBounds(strip.getX() - 14 - 66, strip.getY(), 66, 20);
    strip.removeFromLeft(40 + 4);   // "PATCH" mini head drawn in paint()
    prevBtn.setBounds(strip.removeFromLeft(22));
    strip.removeFromLeft(4);
    nextBtn.setBounds(strip.removeFromRight(22));
    strip.removeFromRight(4);
    patchNameArea = strip;
}

void SelBarView::paint(juce::Graphics& g) {
    // .dr-selbar: 7px radius, soft vertical gradient, hairline + top highlight
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff181c26).withAlpha(0.82f), r.getX(), r.getY(),
        juce::Colour(0xff0a0d13).withAlpha(0.88f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour(juce::Colours::white.withAlpha(0.035f));
    g.drawHorizontalLine((int)r.getY() + 1, r.getX() + 7.0f, r.getRight() - 7.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);

    const int sel = proc.selectedPad();
    auto row = getLocalBounds().reduced(11, 5);   // padding 5px 11px

    // .dr-led.dr-led-a: 8px cyan LED with halo and a specular hotspot
    const juce::Rectangle<float> led((float)row.getX(), r.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour(col::acA.withAlpha(0.4f));
    g.fillEllipse(led.expanded(2.5f));
    g.setColour(col::acA);
    g.fillEllipse(led);
    g.setColour(juce::Colour(0xffe8fcff));
    g.fillEllipse(led.reduced(2.6f).translated(-0.9f, -0.9f));
    row.removeFromLeft(8 + 9);                    // led + flex gap 9

    // .dr-mini-head "PAD NN"
    g.setColour(col::text);
    g.setFont(dispFont(8.0f));
    drawSpaced(g, "PAD " + juce::String(sel + 1).paddedLeft('0', 2),
               row.removeFromLeft(58), 1.3f);
    row.removeFromLeft(9);

    // .dr-sel-name (cyan)
    g.setColour(col::acA);
    g.setFont(seqFont(9.0f));
    drawSpaced(g, proc.padName(sel), row.removeFromLeft(170), 0.9f);

    // Patch stepper (web .dr-patchbar): "PATCH" mini head left of the prev
    // button, current factory patch name in a dark well between the buttons.
    g.setColour(col::text);
    g.setFont(dispFont(8.0f));
    drawSpaced(g, "PATCH",
               { prevBtn.getX() - 44, patchNameArea.getY(), 40, patchNameArea.getHeight() },
               1.3f, juce::Justification::centredRight);
    g.setColour(juce::Colour(0xff0c0f16));
    g.fillRoundedRectangle(patchNameArea.toFloat(), 6.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(patchNameArea.toFloat().reduced(0.5f), 6.0f, 1.0f);
    const auto& bank = fable::factoryPatches();
    const juce::String patchName =
        (patchIndex_ >= 0 && patchIndex_ < (int)bank.size())
            ? juce::String(bank[(size_t)patchIndex_].name) : juce::String::fromUTF8("\xe2\x80\x94");
    g.setColour(col::acA);
    g.setFont(seqFont(9.0f));
    drawSpaced(g, patchName, patchNameArea.reduced(8, 0), 0.8f,
               juce::Justification::centred);
}

// ---- StepSeqView geometry ----------------------------------------------------
// drum.css: .dr-stepseq padding 9px 12px 11px; head 28px + 10px margin;
// .step-row: 16 x 45px steps, 5px gaps, extra 8px after steps 4/8/12.

static constexpr int kPadX = 12, kHeadY = 9, kHeadH = 28, kTitleW = 88;
static constexpr int kNameW = 100;               // .dr-stepseq-target slot after the title
static constexpr int kRowY = 47;
static constexpr float kGroupGap = 8.0f;

// All 16 pad lanes at once (drum.css .dr-lanes), lane 0 = pad 15 at the top so
// the stack reads like the pad grid's bottom-left origin. Flat BL-1-style cells
// — the whole cell is the hit target, no inset pill leaving dead space.
static constexpr int kLaneMaxH = 42, kLaneGap = 1, kLaneNameW = 120, kLaneNameGap = 7;
static constexpr int kRowBottomPad = 11;
static constexpr int kTimingHeight = 64;   // .dr-timing-panel one-row height
static constexpr int kInspectorGap = 10;
static constexpr float kStepGap = 2.0f;

StepSeqView::StepSeqView(DrumUiModel& p) : proc(p) {
    setInterceptsMouseClicks(true, true);
    setupPoly();
    setWantsKeyboardFocus(true);   // verbs on keyPressed, modifier combos only
    startVisualTimer(*this, fui::kAnimationHz);
}

#ifndef FABLE_HOSTED_UI
StepSeqView::StepSeqView(DrumAudioProcessor& p)
    : ownedModel(makeStandaloneDrumUiModel(p)), proc(*ownedModel) {
    setInterceptsMouseClicks(true, true);
    setupPoly();
    setWantsKeyboardFocus(true);
    startVisualTimer(*this, fui::kAnimationHz);
}
#endif

void StepSeqView::setupPoly() {
    patternPreset_.setTextWhenNothingSelected("PATTERN PRESET");
    patternPreset_.setTooltip("Load a drum pattern preset without changing the kit or tempo");
    patternPreset_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0d1017));
    patternPreset_.setColour(juce::ComboBox::textColourId, col::textDim);
    patternPreset_.setColour(juce::ComboBox::outlineColourId, col::line);
    const auto& patternPresets = fable::factoryPatternPresets();
    for (int i = 0; i < (int)patternPresets.size(); ++i)
        patternPreset_.addItem(juce::String(patternPresets[(size_t)i].name), i + 1);
    patternPreset_.setEnabled(proc.hasTargetClip());
    patternPreset_.onChange = [this] {
        const int index = patternPreset_.getSelectedId() - 1;
        if (index < 0 || !proc.hasTargetClip()) return;
        pushHistoryEntry();
        if (proc.loadPatternPreset(index)) {
            proc.setEditPattern(0);
            clearSelection();
            repaint();
        }
    };
    addAndMakeVisible(patternPreset_);
    patternsBtn_.setTooltip("Browse the pattern library by genre and song section");
    patternsBtn_.onClick = [this] { if (onPatterns) onPatterns(); };
    addAndMakeVisible(patternsBtn_);
    if (!proc.supportsPoly()) return;
    // The web timing panel is always present (StepSeq.tsx .dr-inspector-grid);
    // a plain step click both toggles the step and makes it the timing target.
    addAndMakeVisible(timingPanel_);
    timingPanel_.setInterceptsMouseClicks(false, true);
    for (auto* number : { &laneDelay_, &stepDelay_ }) {
        number->minimum = -50;
        number->setValue(0, 50);
        timingPanel_.addAndMakeVisible(*number);
        // One press-and-hold of a stepper is one undo entry (SequenceNumberInput gesture).
        number->onBegin = [this] { if (!timingGesture_) { timingGesture_ = true; timingGestureSaved_ = false; } };
        number->onEnd = [this] { timingGesture_ = timingGestureSaved_ = false; };
    }
    laneDelay_.onChange = [this](int v) { if (!refreshingTiming_) setMicroDelay(v, false); };
    stepDelay_.onChange = [this](int v) { if (!refreshingTiming_) setMicroDelay(v, true); };
    for (auto* label : { &laneDelayLabel_, &stepDelayLabel_ }) {
        timingPanel_.addAndMakeVisible(*label);
        label->setFont(seqFont(9, true));
        label->setBorderSize({});
        label->setColour(juce::Label::textColourId, juce::Colour(0xff9ca9bd));
    }
    polyPanel_ = std::make_unique<PolyLanePanel>(proc);
    addAndMakeVisible(*polyPanel_); addAndMakeVisible(laneScroll_);
    polyPanel_->beforeEdit = [this] { pushHistoryEntry(); };
    polyPanel_->onUndo = [this] { undo(); }; polyPanel_->onRedo = [this] { redo(); };
    polyPanel_->onLayoutChange = [this] { resized(); repaint(); };
    laneScroll_.addListener(this);
    refreshTiming();
}
int StepSeqView::gridSpaceBottom() const {
    if (!polyPanel_) return getHeight() - kRowBottomPad;
    return juce::jmin(timingPanel_.getY(), polyPanel_->getY()) - kInspectorGap;
}
// When the lanes scroll, the visible grid ends on a whole lane, not mid-row.
int StepSeqView::gridBottom() const {
    const int space = gridSpaceBottom() - kRowY, pitch = laneHeight() + kLaneGap;
    const int total = pitch * fable::DR_NPADS - kLaneGap;
    if (total <= space) return gridSpaceBottom();
    return kRowY + juce::jmax(1, (space + kLaneGap) / pitch) * pitch - kLaneGap;
}
// .dr-timing-panel: 8 px padding, 8 px gaps, columns .7fr | 1fr | 1fr | .7fr;
// narrow panels (< 520 px, the web's 730 px breakpoint) fold into a 2 × 2 grid.
juce::Rectangle<int> StepSeqView::timingColumn(int i) const {
    const auto panel = timingPanel_.getBounds().reduced(8);
    if (panel.getWidth() < 504) {
        const int w = (panel.getWidth() - 8) / 2, h = (panel.getHeight() - 8) / 2;
        return { panel.getX() + (i % 2) * (w + 8), panel.getY() + (i / 2) * (h + 8), w, h };
    }
    const float unit = (float)(panel.getWidth() - 24) / 3.4f;
    const float widths[4] = { unit * .7f, unit, unit, unit * .7f };
    float x = (float)panel.getX();
    for (int c = 0; c < i; ++c) x += widths[c] + 8.0f;
    return juce::Rectangle<float>(x, (float)panel.getY(), widths[i], (float)panel.getHeight()).toNearestInt();
}
void StepSeqView::resized() {
    if (!polyPanel_) return;
    patternPreset_.setBounds(randButtonBounds().getRight() + 8, kHeadY + 2, 148, 24);
    patternsBtn_.setBounds(patternPreset_.getRight() + 6, kHeadY + 2, 84, 24);
    // .dr-inspector-grid: timing and POLY share one row when both halves are
    // roomy, otherwise they stack (timing above POLY), full width.
    const int panelWidth = juce::jmax(0, getWidth() - 24);
    const int half = (panelWidth - 8) / 2;
    const bool sideBySide = half >= 600;
    if (sideBySide) {
        const int h = juce::jmax(kTimingHeight, polyPanel_->preferredHeight(panelWidth - half - 8));
        timingPanel_.setBounds(12, getHeight() - h - 4, half, h);
        polyPanel_->setBounds(12 + half + 8, getHeight() - h - 4, panelWidth - half - 8, h);
    } else {
        const int h = polyPanel_->preferredHeight(panelWidth);
        polyPanel_->setBounds(12, getHeight() - h - 4, panelWidth, h);
        const int timingHeight = panelWidth < 520 ? 2 * kTimingHeight - 8 : kTimingHeight;
        timingPanel_.setBounds(12, polyPanel_->getY() - kInspectorGap - timingHeight, panelWidth, timingHeight);
    }
    // Control columns: a 9 px caption over a 30 px number well, vertically centred.
    for (int i : { 1, 2 }) {
        auto column = timingColumn(i).translated(-timingPanel_.getX(), -timingPanel_.getY());
        column = column.withSizeKeepingCentre(column.getWidth(), 48);
        auto& label = i == 1 ? laneDelayLabel_ : stepDelayLabel_;
        auto& number = i == 1 ? laneDelay_ : stepDelay_;
        label.setBounds(column.removeFromTop(14));
        column.removeFromTop(4);
        number.setBounds(column.withWidth(juce::jmin(column.getWidth(), 150)));
    }
    const int visible = juce::jmax(24, gridBottom() - kRowY);
    const int total = laneHeight() * fable::DR_NPADS
                    + kLaneGap * (fable::DR_NPADS - 1);
    laneScroll_.setBounds(getWidth() - 12, kRowY, 12, visible);
    laneScroll_.setRangeLimits(0, total);
    laneScroll_.setCurrentRange(juce::jlimit(0, juce::jmax(0, total - visible), laneOffset_), visible);
    laneOffset_ = (int)laneScroll_.getCurrentRangeStart();
    laneScroll_.setVisible(total > visible);
}
void StepSeqView::refreshTiming() {
    if (!polyPanel_) return;
    const auto s = proc.sequence(); const auto& lane = s.rhythm.lanes[(size_t)proc.selectedPad()];
    refreshingTiming_ = true;
    laneDelayLabel_.setText("LANE OFFSET", juce::dontSendNotification);
    stepDelayLabel_.setText("STEP " + juce::String(timingStep_ + 1).paddedLeft('0', 2), juce::dontSendNotification);
    laneDelay_.setValue(lane.delayMs, 50);
    stepDelay_.setValue(lane.stepDelayMs[(size_t)(proc.editPattern() * 16 + timingStep_)], 50);
    refreshingTiming_ = false;
}
void StepSeqView::setMicroDelay(int value, bool step) {
    auto s = proc.sequence(); const auto pad = (size_t)proc.selectedPad();
    auto& lane = s.rhythm.lanes[pad];
    auto& target = step ? lane.stepDelayMs[(size_t)(proc.editPattern() * 16 + timingStep_)] : lane.delayMs;
    if (target == value) return;
    if (!timingGesture_ || !timingGestureSaved_) { pushHistoryEntry(); timingGestureSaved_ = true; }
    target = std::clamp(value, -50, 50); lane.micro = true;
    s.hasRhythm = true; s.configuredLanes |= (uint16_t)(1u << pad);
    proc.commitSequence(s); refreshTiming(); repaint();
}

void StepSeqView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& d) {
    laneScroll_.setCurrentRangeStart(laneScroll_.getCurrentRangeStart() - d.deltaY * 160);
}

juce::Rectangle<int> StepSeqView::patternBounds(int i) const {
    const int x0 = kPadX + kTitleW + 8            // title, gap
                 + kNameW + 8;                    // editing-pad name, gap
    return { x0 + i * (26 + 4), kHeadY + 2, 26, 24 }; // .dr-pattern 26x24, gap 4
}

juce::Rectangle<int> StepSeqView::sequenceLengthBounds() const {
    return { patternBounds(3).getRight() + 10, kHeadY, 170, 30 };
}

juce::Rectangle<int> StepSeqView::randButtonBounds() const {
    return { sequenceLengthBounds().getRight() + 8, kHeadY + 2, 44, 24 };
}

// Lane 0 is the topmost row and holds the highest pad.
int StepSeqView::laneOfPad(int pad) { return fable::DR_NPADS - 1 - pad; }
int StepSeqView::padOfLane(int lane) { return fable::DR_NPADS - 1 - lane; }

// Lanes divide whatever height the panel was given, so the view still reads
// correctly when a host scales the rack down. In its dedicated workspace the
// lane stack can use the full vertical canvas. Below the web's 24 px minimum
// cell height the lanes stop shrinking and the stack scrolls instead.
int StepSeqView::laneHeight() const {
    const int space = gridSpaceBottom() - kRowY;
    const int usable = space - (fable::DR_NPADS - 1) * kLaneGap;
    if (usable / fable::DR_NPADS >= 24) return juce::jmin(kLaneMaxH, usable / fable::DR_NPADS);
    // Scrolling: as many 24 px (.dr-lanes .step min-height) rows as fit, stretched to fill the space.
    const int rows = juce::jmax(1, (space + kLaneGap) / (24 + kLaneGap));
    return juce::jlimit(24, kLaneMaxH, (space + kLaneGap) / rows - kLaneGap);
}

juce::Rectangle<int> StepSeqView::laneBounds(int pad) const {
    const int h = laneHeight();
    return { kPadX, kRowY + laneOfPad(pad) * (h + kLaneGap) - laneOffset_,
             juce::jmax(0, getWidth() - 2 * kPadX - 12), h };
}

juce::Rectangle<int> StepSeqView::laneNameBounds(int pad) const {
    return laneBounds(pad).withWidth(kLaneNameW);
}

juce::Rectangle<int> StepSeqView::stepBounds(int pad, int step) const {
    const auto sequence = proc.sequence();
    return stepBoundsForLane(pad, step, sequence.hasRhythm ? &sequence.rhythm.lanes[(size_t)pad] : nullptr);
}

juce::Rectangle<int> StepSeqView::stepBoundsForLane(int pad, int step, const fable::DrumLaneRhythm* rhythm) const {
    const auto lane = laneBounds(pad).withTrimmedLeft(kLaneNameW + kLaneNameGap);
    if (rhythm && rhythm->enabled && rhythm->mode == fable::DrumRhythmMode::fit && rhythm->sourceBar == proc.editPattern()) {
        if (step >= rhythm->steps) return {}; // retained source notes are outside this cycle
        const int slot = (step + rhythm->rotation) % rhythm->steps;
        const float pitch = (float)lane.getWidth() / (float)rhythm->steps;
        const float width = juce::jmin(pitch - 2.0f, juce::jmax(24.0f, (float)lane.getWidth() / (float)(rhythm->cycleBeats * 4) - 2.0f));
        return juce::Rectangle<float>((float)lane.getX() + (float)slot * pitch, (float)lane.getY(), width, (float)lane.getHeight()).toNearestInt();
    }
    const float w = ((float)lane.getWidth() - 15.0f * kStepGap - 3.0f * kGroupGap) / 16.0f;
    float x = (float)lane.getX();
    for (int i = 0; i < step; ++i) {
        x += w + kStepGap;
        if (i % 4 == 3) x += kGroupGap;          // bar-group breathing room
    }
    return juce::Rectangle<float>(x, (float)lane.getY(), w, (float)lane.getHeight()).toNearestInt();
}

// The single-argument form addresses the selected pad's lane, so every verb,
// drag and test written against it keeps working unchanged.
juce::Rectangle<int> StepSeqView::stepBounds(int step) const {
    return stepBounds(proc.selectedPad(), step);
}

// The union of every lane × step cell (excludes the lane-name selectors).
juce::Rectangle<int> StepSeqView::gridBounds() const {
    const auto top = laneBounds(padOfLane(0)).withTrimmedLeft(kLaneNameW + kLaneNameGap);
    const auto bottom = laneBounds(padOfLane(fable::DR_NPADS - 1)).withTrimmedLeft(kLaneNameW + kLaneNameGap);
    return top.getUnion(bottom);
}

// Floating CUT · COPY · DUP · DEL · ✕ toolbar centered over the selected step
// columns (SeqSelectionMenu.tsx), drawn over the top lanes while a rect exists.
juce::Rectangle<int> StepSeqView::selMenuBounds() const {
    constexpr int bw = 46, gap = 3, n = 5, h = 22;   // web .seq-selmenu size (WT-1/BL-1 parity)
    constexpr int w = n * bw + (n - 1) * gap;
    const auto nrm = fable::padRectNorm(rect_);
    const int topPad = padOfLane(0);
    const auto lo = stepBoundsForLane(topPad, juce::jlimit(0, fable::DR_STEPS - 1, nrm.stepLo), nullptr);
    const auto hi = stepBoundsForLane(topPad, juce::jlimit(0, fable::DR_STEPS - 1, nrm.stepHi), nullptr);
    const int cx = (lo.getX() + hi.getRight()) / 2;
    const auto grid = gridBounds();
    const int x = juce::jlimit(grid.getX(), juce::jmax(grid.getX(), grid.getRight() - w), cx - w / 2);
    return { x, juce::jmax(kRowY, grid.getY()) + 4, w, h };
}

juce::Rectangle<int> StepSeqView::selMenuButton(int i) const {
    constexpr int bw = 46, gap = 3;
    const auto m = selMenuBounds();
    return { m.getX() + i * (bw + gap), m.getY(), bw, m.getHeight() };
}

bool StepSeqView::inRect(int step, int pad) const {
    if (sweeping_) {
        const auto n = fable::padRectNorm(pending_);
        return step >= n.stepLo && step <= n.stepHi && pad >= n.padLo && pad <= n.padHi;
    }
    if (!hasRect_) return false;
    const auto n = fable::padRectNorm(rect_);
    return step >= n.stepLo && step <= n.stepHi && pad >= n.padLo && pad <= n.padHi;
}

bool StepSeqView::cellAt(juce::Point<int> pos, int& pad, int& step) const {
    const auto sequence = proc.sequence();
    for (int p = 0; p < fable::DR_NPADS; ++p)
        for (int s = 0; s < fable::DR_STEPS; ++s)
            if (stepBoundsForLane(p, s, sequence.hasRhythm ? &sequence.rhythm.lanes[(size_t)p] : nullptr).contains(pos)) { pad = p; step = s; return true; }
    return false;
}

// Snap a point to the nearest lane / column (drag tracking may run between the
// exact cells, past the group gaps, or off the panel edge).
void StepSeqView::cellClamp(juce::Point<int> pos, int& pad, int& step) const {
    pad = 0; int bestDy = std::numeric_limits<int>::max();
    for (int p = 0; p < fable::DR_NPADS; ++p) {
        const int dy = std::abs(pos.y - laneBounds(p).getCentreY());
        if (dy < bestDy) { bestDy = dy; pad = p; }
    }
    const auto sequence = proc.sequence();
    step = 0; int bestDx = std::numeric_limits<int>::max();
    for (int s = 0; s < fable::DR_STEPS; ++s) {
        const auto cell = stepBoundsForLane(pad, s, sequence.hasRhythm ? &sequence.rhythm.lanes[(size_t)pad] : nullptr);
        if (cell.isEmpty()) continue;
        const int dx = std::abs(pos.x - cell.getCentreX());
        if (dx < bestDx) { bestDx = dx; step = s; }
    }
}

// ---- store handlers ----------------------------------------------------------

void StepSeqView::toggleStep(int pad, int step) {
    const int pat = proc.editPattern();
    pushHistoryEntry();
    const auto v = (uint8_t)((proc.step(pat, pad, step) + 1) % 3);
    auto state = captureSnapshot(); state.steps[(size_t)((pat * 16 + pad) * 16 + step)] = v;
    proc.commitSequence(state);
    hasLastCell_ = true; lastCellStep_ = step; lastCellPad_ = pad;
    repaint();
}

void StepSeqView::toggleStep(int step) { toggleStep(proc.selectedPad(), step); }

void StepSeqView::patternClick(int i) {
    proc.setEditPattern(i);
    repaint();
}

void StepSeqView::setSequenceLength(int bars) {
    bars = juce::jlimit(1, fable::DR_NPATTERNS, bars);
    std::vector<int> sequence;
    for (int bar = 0; bar < bars; ++bar) sequence.push_back(bar);
    if (proc.chain() == sequence) return;
    pushHistoryEntry();
    auto state = captureSnapshot(); state.chain = std::move(sequence);
    proc.commitSequence(state);
    repaint();
}

// RAND button: rewrite the selected pad's row in the current edit pattern
// only — other pads' lanes are untouched (web store.randomizePad).
void StepSeqView::randomizePad(std::function<float()> rng) {
    pushHistoryEntry();
    const int pad = proc.selectedPad(), pat = proc.editPattern();
    if (!rng) rng = [] { return juce::Random::getSystemRandom().nextFloat(); };
    // randomizeLane rewrites the lane at laneOffset; give it a per-pad layout.
    fable::StepLayout lane = gridLayout();
    lane.laneOffset = pad * fable::DR_STEPS * 2;
    applyPatternBuffer(pat, fable::randomizeLane(buildPatternBuffer(pat), lane, 0, rng));
    repaint();
}

// ---- buffer / snapshot helpers ----------------------------------------------
// Every verb works over a flat single-pattern buffer (DR_NPADS*DR_STEPS bytes)
// built/flushed through proc.step()/setStep(), so nothing assumes how the host
// (standalone engine vs. SQ-4 hosted clip) stores the data — mirrors
// StepEditOps.h's JUCE-free, storage-agnostic contract. The pad-rect verbs use
// this grid layout (no laneOffset; they compute the per-pad offset themselves).
fable::StepLayout StepSeqView::gridLayout() const {
    fable::StepLayout l;
    l.stride = 2;
    l.stepsPerPattern = fable::DR_STEPS;
    l.patternSize = fable::DR_NPADS * fable::DR_STEPS * 2;
    l.laneOffset = 0;
    return l;
}

fable::StepBytes StepSeqView::buildPatternBuffer(int pat) const {
    const auto state = proc.sequence();
    fable::StepBytes buf(512, 0);
    for (int pad = 0; pad < 16; ++pad) for (int step = 0; step < 16; ++step) {
        const auto index = (size_t)(pad * 16 + step);
        buf[index * 2] = state.steps[(size_t)pat * 256 + index];
        buf[index * 2 + 1] = (uint8_t)state.rhythm.lanes[(size_t)pad].stepDelayMs[(size_t)(pat * 16 + step)];
    }
    return buf;
}
void StepSeqView::writePatternBuffer(DrumSequence& state, int pat, const fable::StepBytes& buf) {
    for (int pad = 0; pad < 16; ++pad) for (int step = 0; step < 16; ++step) {
        const auto index = (size_t)(pad * 16 + step);
        state.steps[(size_t)pat * 256 + index] = buf[index * 2];
        const int raw = buf[index * 2 + 1], delay = raw > 127 ? raw - 256 : raw;
        auto& lane = state.rhythm.lanes[(size_t)pad];
        if (delay != lane.stepDelayMs[(size_t)(pat * 16 + step)]) {
            lane.stepDelayMs[(size_t)(pat * 16 + step)] = delay;
            lane.micro = true; state.hasRhythm = true; state.configuredLanes |= (uint16_t)(1u << pad);
        }
    }
}
void StepSeqView::applyPatternBuffer(int pat, const fable::StepBytes& buf) {
    auto state = captureSnapshot(); writePatternBuffer(state, pat, buf); proc.commitSequence(state);
}
DrStepSnapshot StepSeqView::captureSnapshot() const { return proc.sequence(); }
void StepSeqView::restoreSnapshot(const DrStepSnapshot& s) { proc.commitSequence(s); repaint(); }

void StepSeqView::pushHistoryEntry() { syncHistoryContext(); history_.push(captureSnapshot()); }

// ---- selection ---------------------------------------------------------------

void StepSeqView::setSelection(const fable::PadRectSel& r) {
    rect_ = r;
    hasRect_ = true;
    const auto n = fable::padRectNorm(r);
    hasLastCell_ = true; lastCellStep_ = n.stepLo; lastCellPad_ = n.padLo;
    repaint();
}

void StepSeqView::selectAllPattern() {
    setSelection({ 0, fable::DR_STEPS - 1, 0, fable::DR_NPADS - 1 });
}

void StepSeqView::clearSelection() { hasRect_ = false; repaint(); }

// ---- verbs -------------------------------------------------------------------
// Copy/cut/paste default to the whole edit pattern (all pads) when nothing is
// selected; a rectangle scopes to its step × pad band (web store parity).

void StepSeqView::copySelection() {
    const int pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    clipboard_.valid = true;
    if (hasRect_) {
        clipboard_.wholePattern = false;
        clipboard_.rect = fable::copyPadRect(buf, gridLayout(), 0, rect_);
    } else {
        clipboard_.wholePattern = true;
        clipboard_.pattern = fable::copyPattern(buf, gridLayout(), 0);
    }
}

void StepSeqView::cutSelection() {
    copySelection();
    pushHistoryEntry();
    const int pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    if (hasRect_) buf = fable::clearPadRect(buf, gridLayout(), 0, rect_);
    else std::fill(buf.begin(), buf.end(), (uint8_t)0);
    applyPatternBuffer(pat, buf);
    repaint();
}

void StepSeqView::deleteSelection() {
    if (!hasRect_) return;                       // Delete only acts on an explicit selection
    pushHistoryEntry();
    const int pat = proc.editPattern();
    applyPatternBuffer(pat, fable::clearPadRect(buildPatternBuffer(pat), gridLayout(), 0, rect_));
    repaint();
}

void StepSeqView::pasteSelection() {
    if (!clipboard_.valid) return;
    pushHistoryEntry();
    const int pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    if (clipboard_.wholePattern) {
        applyPatternBuffer(pat, fable::pastePattern(buf, gridLayout(), 0, clipboard_.pattern));
        repaint();
        return;
    }
    int atStep, atPad;
    if (hasRect_)          { const auto n = fable::padRectNorm(rect_); atStep = n.stepLo; atPad = n.padLo; }
    else if (hasLastCell_) { atStep = lastCellStep_; atPad = lastCellPad_; }
    else                   { atStep = 0; atPad = 0; }
    buf = fable::pastePadRect(buf, gridLayout(), 0, atStep, atPad, clipboard_.rect, fable::DR_NPADS);
    applyPatternBuffer(pat, buf);
    rect_ = { atStep, atStep + clipboard_.rect.wSteps - 1, atPad, atPad + clipboard_.rect.wPads - 1 };
    hasRect_ = true;
    repaint();
}

void StepSeqView::duplicatePattern() {
    const int edit = proc.editPattern();
    const int target = edit + 1;
    if (target >= fable::DR_NPATTERNS) return;   // no pattern slot past bar 4
    pushHistoryEntry();
    const int curBars = proc.capabilities().hosted ? proc.clipBars() : (int)proc.chain().size();
    auto state = captureSnapshot();
    if (target + 1 > curBars) { state.chain.clear(); for (int b = 0; b <= target; ++b) state.chain.push_back(b); }
    const auto data = buildPatternBuffer(edit);
    writePatternBuffer(state, target, data);
    proc.commitSequence(state);
    proc.setEditPattern(target);                 // web: editing follows the copy
    hasRect_ = false;
    repaint();
}

void StepSeqView::duplicateSelection() {
    if (!hasRect_) { duplicatePattern(); return; }
    const auto n = fable::padRectNorm(rect_);
    const int at = n.stepHi + 1;
    if (at >= fable::DR_STEPS) return;           // nothing past the last step — no-op
    pushHistoryEntry();
    const int pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    const auto data = fable::copyPadRect(buf, gridLayout(), 0, rect_);
    buf = fable::pastePadRect(buf, gridLayout(), 0, at, n.padLo, data, fable::DR_NPADS);
    applyPatternBuffer(pat, buf);
    rect_ = { at, at + (n.stepHi - n.stepLo), n.padLo, n.padHi };
    hasRect_ = true;
    repaint();
}

// Block move release (drag from inside the rect): shift the whole rectangle in
// step and pad. The delta is clamped so the block stays on the grid. Alt copies.
void StepSeqView::commitBlockMove(int dStep, int dPad, bool copy) {
    if (!hasRect_) return;
    const auto n = fable::padRectNorm(rect_);
    dStep = juce::jlimit(-n.stepLo, (fable::DR_STEPS - 1) - n.stepHi, dStep);
    dPad = juce::jlimit(-n.padLo, (fable::DR_NPADS - 1) - n.padHi, dPad);
    if (dStep == 0 && dPad == 0) return;         // no movement: full no-op, no history
    pushHistoryEntry();
    const int pat = proc.editPattern();
    fable::PadRectMoveOpts opts; opts.copy = copy;
    applyPatternBuffer(pat, fable::movePadRect(buildPatternBuffer(pat), gridLayout(), 0, rect_,
                                               dStep, dPad, fable::DR_NPADS, opts));
    rect_ = { n.stepLo + dStep, n.stepHi + dStep, n.padLo + dPad, n.padHi + dPad };
    hasRect_ = true;
    repaint();
}

// Menu CUT / COPY: pick the selection up. The captured cells trail the cursor
// as ghosts until the next click drops them (useDrumGhostPaste). A cancelled CUT
// mutates nothing — the source is only cleared at drop time.
void StepSeqView::beginGhostPaste(bool cut) {
    if (!hasRect_) return;
    ghostData_ = fable::copyPadRect(buildPatternBuffer(proc.editPattern()), gridLayout(), 0, rect_);
    if (ghostData_.cells.empty()) return;
    clipboard_.valid = true; clipboard_.wholePattern = false; clipboard_.rect = ghostData_; // keep Cmd-V in sync
    ghost_ = true; ghostCut_ = cut; ghostSrc_ = rect_; ghostHasHover_ = false;
    hasRect_ = false;                            // the menu closes
    repaint();
}

// Drop the carried ghost at (atStep, atPad): top-left anchored. A CUT clears its
// source in the same undo entry as the paste.
void StepSeqView::dropGhost(int atStep, int atPad) {
    if (!ghost_) return;
    pushHistoryEntry();
    const int pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    if (ghostCut_) buf = fable::clearPadRect(buf, gridLayout(), 0, ghostSrc_);
    buf = fable::pastePadRect(buf, gridLayout(), 0, atStep, atPad, ghostData_, fable::DR_NPADS);
    applyPatternBuffer(pat, buf);
    rect_ = { atStep, atStep + ghostData_.wSteps - 1, atPad, atPad + ghostData_.wPads - 1 };
    hasRect_ = true;
    ghost_ = false; ghostHasHover_ = false;
    repaint();
}

void StepSeqView::movePattern(int fromBar, int toBar, bool copy) {
    if (fromBar == toBar || fromBar < 0 || toBar < 0
        || fromBar >= fable::DR_NPATTERNS || toBar >= fable::DR_NPATTERNS) return;
    pushHistoryEntry();
    const auto layout = gridLayout();
    const auto a = fable::copyPattern(buildPatternBuffer(fromBar), layout, 0);
    auto state = captureSnapshot();
    writePatternBuffer(state, toBar, a);
    if (!copy) {
        const auto b = buildPatternBuffer(toBar);
        writePatternBuffer(state, fromBar, b);
    }
    proc.commitSequence(state);
    repaint();
}

void StepSeqView::undo() {
    syncHistoryContext();
    DrStepSnapshot restored, current = captureSnapshot();
    if (history_.undo(current, restored)) restoreSnapshot(restored);
}

void StepSeqView::redo() {
    syncHistoryContext();
    DrStepSnapshot restored, current = captureSnapshot();
    if (history_.redo(current, restored)) restoreSnapshot(restored);
}

// ---- mouse: selection, block-move, ghost drop, bar-chip drag ---------------

void StepSeqView::mouseDown(const juce::MouseEvent& e) {
    // Real clicks already auto-grab focus via JUCE's mouse dispatch; headless
    // tests call mouseDown() directly and skip that path, so grab explicitly
    // here, guarded exactly like SeqEditor::enterFocus (no peer => no-op assert).
    if (isShowing() || isOnDesktop()) grabKeyboardFocus();
    const auto pos = e.getPosition();

    // Carrying a ghost: the next click drops it (or, outside the grid, cancels).
    if (ghost_) {
        if (gridBounds().contains(pos)) { int p, s; cellClamp(pos, p, s); dropGhost(s, p); }
        else cancelGesture();
        return;
    }

    // Floating selection menu (drawn over the top lanes while a rect exists).
    if (hasRect_) {
        for (int i = 0; i < 5; ++i) if (selMenuButton(i).contains(pos)) {
            switch (i) {
                case 0: beginGhostPaste(true);  break;  // CUT
                case 1: beginGhostPaste(false); break;  // COPY
                case 2: duplicateSelection();   break;  // DUP
                case 3: deleteSelection();      break;  // DEL
                default: clearSelection();      break;  // ✕
            }
            repaint();
            return;
        }
    }

    for (int i = 0; i < fable::DR_NPATTERNS; ++i)
        if (patternBounds(i).contains(pos)) {
            drag_ = DragState{};
            drag_.kind = DragState::Kind::barChip;
            drag_.startPos = pos;
            drag_.fromBar = drag_.hoverBar = i;
            return;
        }
    if (proc.capabilities().supportsPatternChain && sequenceLengthBounds().contains(pos)) {
        const auto length = sequenceLengthBounds();
        const int bars = proc.capabilities().hosted ? proc.clipBars() : (int)proc.chain().size();
        if (pos.x < length.getX() + 38) setSequenceLength(bars - 1);
        else if (pos.x >= length.getRight() - 38) setSequenceLength(bars + 1);
        return;
    }
    if (!proc.capabilities().hosted && randButtonBounds().contains(pos)) {
        randomizePad();
        return;
    }
    if (pos.y >= gridBottom() || pos.y < kRowY) return;
    for (int pad = 0; pad < fable::DR_NPADS; ++pad)
        if (laneNameBounds(pad).contains(pos)) { // lane name = pad selector
            proc.selectPad(pad);
            repaint();
            return;
        }

    // ---- step cells: Shift-sweep / block-move / deferred toggle ----
    if (gridBounds().contains(pos)) {
        int pad, step;
        if (!cellAt(pos, pad, step)) return;     // between cells (group gap): ignore
        downStep_ = step; downPad_ = pad;
        if (e.mods.isShiftDown()) {              // 1) rectangle sweep
            sweeping_ = true;
            pending_ = { step, step, pad, pad };
            repaint();
            return;
        }
        if (hasRect_ && inRect(step, pad)) {     // 2) block move (stationary release toggles)
            moveArmed_ = true; moving_ = false;
            moveOriginStep_ = step; moveOriginPad_ = pad;
            moveHoverStep_ = step; moveHoverPad_ = pad;
            return;
        }
        // 3) plain press: retarget editing to this lane's pad; toggle on up.
        if (pad != proc.selectedPad()) proc.selectPad(pad);
        return;
    }
}

void StepSeqView::mouseDrag(const juce::MouseEvent& e) {
    const auto pos = e.position.roundToInt();
    if (sweeping_) {
        int pad, step; cellClamp(pos, pad, step);
        pending_.stepTo = step; pending_.padTo = pad;
        repaint();
        return;
    }
    if (moveArmed_) {
        int pad, step; cellClamp(pos, pad, step);
        moveHoverStep_ = step; moveHoverPad_ = pad;
        if (step != moveOriginStep_ || pad != moveOriginPad_) moving_ = true;
        repaint();
        return;
    }
    if (drag_.kind == DragState::Kind::barChip) {
        if (!drag_.crossed) {
            if (e.getDistanceFromDragStart() < 4) return; // ~4px movement threshold
            drag_.crossed = true;
        }
        int hover = -1;
        for (int i = 0; i < fable::DR_NPATTERNS; ++i)
            if (patternBounds(i).contains(pos)) { hover = i; break; }
        if (hover != drag_.hoverBar) { drag_.hoverBar = hover; repaint(); }
        return;
    }
}

void StepSeqView::mouseUp(const juce::MouseEvent& e) {
    if (sweeping_) {                             // commit the swept rectangle
        sweeping_ = false;
        setSelection(pending_);
        return;
    }
    if (moveArmed_) {
        moveArmed_ = false;
        if (moving_) {
            moving_ = false;
            commitBlockMove(moveHoverStep_ - moveOriginStep_, moveHoverPad_ - moveOriginPad_,
                            e.mods.isAltDown());
        } else if (downStep_ >= 0) {             // plain click inside the rect: toggle
            timingStep_ = downStep_;
            toggleStep(downPad_, downStep_);
            refreshTiming();
        }
        downStep_ = downPad_ = -1;
        return;
    }
    if (drag_.kind == DragState::Kind::barChip) {
        if (drag_.crossed && drag_.hoverBar >= 0 && drag_.hoverBar != drag_.fromBar)
            movePattern(drag_.fromBar, drag_.hoverBar, e.mods.isAltDown());
        else if (!drag_.crossed)
            patternClick(drag_.fromBar);
        drag_ = DragState{};
        repaint();
        return;
    }
    if (downStep_ >= 0) {                         // plain click on a cell: toggle
        timingStep_ = downStep_;                  // ...and make it the timing target
        toggleStep(downPad_, downStep_);
        refreshTiming();
        downStep_ = downPad_ = -1;
    }
}

// Ghost paste follows the pointer even with no button down (useDrumGhostPaste's
// pointermove). Any grid cell is a drop target.
void StepSeqView::mouseMove(const juce::MouseEvent& e) {
    if (!ghost_) return;
    const auto pos = e.getPosition();
    ghostHasHover_ = gridBounds().contains(pos);
    if (ghostHasHover_) cellClamp(pos, ghostHoverPad_, ghostHoverStep_);
    repaint();
}

bool StepSeqView::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        // Drop any in-flight gesture first, then a committed selection; only
        // fall through (to PadGrid's stop-sequencer Escape) when fully idle.
        if (ghost_ || sweeping_ || moveArmed_ || drag_.kind != DragState::Kind::none) {
            sweeping_ = false;
            moveArmed_ = moving_ = false;
            ghost_ = false; ghostHasHover_ = false;
            drag_ = DragState{};
            downStep_ = downPad_ = -1;
            repaint();
            return true;
        }
        if (hasRect_) { clearSelection(); return true; }
        return false;
    }
    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && hasRect_) {
        deleteSelection();
        return true;
    }
    const auto mods = k.getModifiers();
    if (!mods.isCommandDown()) return false;      // only modifier combos claimed
    switch (juce::CharacterFunctions::toLowerCase(k.getTextCharacter())) {
        case 'c': copySelection(); return true;
        case 'x': cutSelection(); return true;
        case 'v': pasteSelection(); return true;
        case 'd': duplicateSelection(); return true;
        case 'a': selectAllPattern(); return true;
        case 'z': if (mods.isShiftDown()) redo(); else undo(); return true;
        default: break;
    }
    return false;
}

void StepSeqView::cancelGesture() {
    sweeping_ = false;
    moveArmed_ = moving_ = false;
    ghost_ = false; ghostHasHover_ = false;
    drag_ = DragState{};
    downStep_ = downPad_ = -1;
    repaint();
}

// ---- animation ----------------------------------------------------------------

void StepSeqView::syncHistoryContext() {
    // The hosted clip/pattern source can swap under us (SQ-4 focus switching a
    // HostedDrumModel to another scene) — clear the undo history and any
    // selection so a later undo can never reach back into a different clip.
    const int identity = proc.clipIdentity();
    if (!haveClipIdentity_) { lastClipIdentity_ = identity; haveClipIdentity_ = true; }
    else if (identity != lastClipIdentity_) {
        lastClipIdentity_ = identity;
        history_.clear();
        hasRect_ = false; sweeping_ = false;
        moveArmed_ = moving_ = false;
        ghost_ = false; ghostHasHover_ = false;
    }

}

void StepSeqView::timerCallback() {
    syncHistoryContext();
    patternPreset_.setEnabled(proc.hasTargetClip());
    refreshTiming();
    const auto key = contentKey();
    const bool playing = proc.sequencerPlaying();
    const int edit = proc.editPattern();

    // The cursor only exists while the transport plays the pattern on screen.
    const int curPat = playing ? proc.currentPattern() : -1;    // matches the painted bar chip
    const int curStep = curPat == edit ? proc.currentStep() : -1;

    if (key != lastContentKey_) {
        lastContentKey_ = key;
        lastCursorStep_ = curStep;
        lastCursorPattern_ = curPat;
        repaint();
        return;
    }
    for (int p = 0; p < fable::DR_NPADS; ++p) {
        const int position = proc.lanePosition(p);
        if (position != lastLanePositions_[(size_t)p]) {
            repaint(laneBounds(p)); lastLanePositions_[(size_t)p] = position;
        }
    }
    if (curStep == lastCursorStep_ && curPat == lastCursorPattern_) return;

    // Only the playhead moved: repaint the old and the new cursor column. The
    // playhead rings one cell in every pad lane, so a column spans the whole
    // lane stack; pad generously to cover the 1px rings and rounding.
    static constexpr int kCursorPad = 6;
    auto columnBounds = [this](int step) {
        const auto top = stepBoundsForLane(padOfLane(0), step, nullptr);
        const auto bot = stepBoundsForLane(padOfLane(fable::DR_NPADS - 1), step, nullptr);
        return juce::Rectangle<int>(top.getX(), top.getY(), top.getWidth(),
                                    bot.getBottom() - top.getY()).expanded(kCursorPad);
    };
    juce::Rectangle<int> dirty;
    if (lastCursorStep_ >= 0) dirty = dirty.getUnion(columnBounds(lastCursorStep_));
    if (curStep >= 0) dirty = dirty.getUnion(columnBounds(curStep));
    // The playing bar also lights its chip in the header row.
    if (curPat != lastCursorPattern_)
        dirty = dirty.getUnion(patternBounds(0)
                                   .getUnion(patternBounds(fable::DR_NPATTERNS - 1))
                                   .expanded(kCursorPad));
    lastCursorStep_ = curStep;
    lastCursorPattern_ = curPat;
    if (!dirty.isEmpty()) repaint(dirty);
}

// ---- paint ----------------------------------------------------------------------

// Sequencer header buttons: 5px radius, #0d1017, dim mono text;
// active = cyan border/text over a faint cyan wash.
static void drawSeqBtn(juce::Graphics& g, juce::Rectangle<int> b, const juce::String& text,
                       bool active, float tracking, bool current = false, float fontSize = 8.0f) {
    if (!g.clipRegionIntersects(b)) return;
    const auto bf = b.toFloat();
    g.setColour(juce::Colour(0xff0d1017));
    g.fillRoundedRectangle(bf, 5.0f);
    if (active || current) {
        g.setColour(col::acA.withAlpha(current ? 0.20f : 0.07f));
        g.fillRoundedRectangle(bf, 5.0f);
    }
    g.setColour(current ? col::acA.withAlpha(0.9f) : active ? col::acA : col::line);
    g.drawRoundedRectangle(bf.reduced(0.5f), 5.0f, 1.0f);
    if (active) {
        g.setColour(col::acA);
        g.fillRect(b.withY(b.getBottom() - 3).withHeight(3).reduced(2, 0));
    }
    g.setColour(active || current ? col::acA : col::textDim);
    g.setFont(seqFont(fontSize));
    drawSpaced(g, text, b, tracking, juce::Justification::centred);
}

juce::MemoryBlock StepSeqView::contentKey() const {
    juce::MemoryOutputStream out(24576);
    auto add = [&](int v) { out.writeInt(v); };
    add(getWidth()); add(getHeight()); add(laneOffset_); add(gridBottom());
    add(proc.sequencerPlaying()); add(proc.editPattern()); add(proc.selectedPad());
    add(proc.clipBars()); add(proc.hasTargetClip()); add(proc.clipIdentity());
    add(proc.capabilities().hosted); add(proc.capabilities().supportsPatternChain);
    add(timingStep_);
    const auto seq = proc.sequence();
    add((int)seq.steps.size()); for (auto v : seq.steps) add(v);
    add((int)seq.chain.size()); for (int v : seq.chain) add(v);
    add(seq.hasRhythm);
    for (const auto& lane : seq.rhythm.lanes) {
        add(lane.enabled); add(lane.micro); add(lane.steps); add(lane.rotation);
        add(lane.sourceBar); add((int)lane.mode); add(lane.cycleBeats); add(lane.delayMs);
        for (int v : lane.stepDelayMs) add(v);
    }
    for (int p = 0; p < fable::DR_NPADS; ++p) out.writeString(proc.padName(p));
    auto rect = [&](const fable::PadRectSel& r) {
        const auto n = fable::padRectNorm(r);
        add(n.stepLo); add(n.stepHi); add(n.padLo); add(n.padHi);
    };
    add(hasRect_); rect(rect_); add(sweeping_); rect(pending_);
    add(moving_); add(moveOriginStep_); add(moveOriginPad_); add(moveHoverStep_); add(moveHoverPad_);
    add(ghost_); add(ghostCut_); add(ghostHasHover_); rect(ghostSrc_);
    add(ghostHoverStep_); add(ghostHoverPad_);
    add((int)ghostData_.cells.size());
    for (const auto& c : ghostData_.cells) { add(c.dStep); add(c.dPad); }
    add((int)drag_.kind); add(drag_.crossed); add(drag_.fromBar); add(drag_.hoverBar);
    return out.getMemoryBlock();
}

void StepSeqView::paint(juce::Graphics& g) {
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
    std::array<int, 18> cursor;
    const bool playing = proc.sequencerPlaying();
    cursor[0] = playing ? proc.currentPattern() : -1;
    cursor[1] = playing ? proc.currentStep() : -1;
    for (int p = 0; p < 16; ++p) cursor[(size_t)p + 2] = playing ? proc.lanePosition(p) : -1;
    if (rebuild || cursor != composedCursor_) {
        composedCursor_ = cursor;
        composed_ = artwork_.createCopy();
        juce::RectangleList<int> live;
        const int edit = proc.editPattern();
        if (playing) {
            if (cursor[0] >= 0 && cursor[0] < 4) live.add(patternBounds(cursor[0]).expanded(2));
            const auto seq = proc.sequence();
            for (int p = 0; p < 16; ++p) {
                const auto& lane = seq.rhythm.lanes[(size_t)p];
                const bool poly = seq.hasRhythm && lane.enabled;
                const int step = poly ? cursor[(size_t)p + 2] - edit * 16
                                      : (cursor[0] == edit ? cursor[1] : -1);
                if (step >= 0 && step < 16)
                    live.add(stepBoundsForLane(p, step, seq.hasRhythm ? &lane : nullptr));
            }
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

void StepSeqView::paintArtwork(juce::Graphics& g, bool cursors, bool panel) {

    if (panel) drawPanel(g, getLocalBounds().toFloat());

    const bool playing = proc.sequencerPlaying();
    const int edit = proc.editPattern();
    const int sel = proc.selectedPad();

    if (g.clipRegionIntersects({0, 0, getWidth(), kRowY})) {
    // ---- head: title (panel accent a -> cyan) ----
    g.setColour(col::acA);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, "STEP SEQ",
               { kPadX, kHeadY, kTitleW, kHeadH }, 2.2f);

    // ---- head: editing-pad name (.dr-stepseq-target) ----
    g.setColour(col::acA);
    g.setFont(seqFont(9.0f, true));
    drawSpaced(g, proc.padName(sel),
               { kPadX + kTitleW + 8, kHeadY, kNameW, kHeadH }, 1.0f);

    // ---- head: bar buttons + sequence length ----
    const int bars = proc.capabilities().hosted ? proc.clipBars()
                                                : juce::jlimit(1, fable::DR_NPATTERNS, (int)proc.chain().size());
    const int currentBar = cursors && playing ? proc.currentPattern() : -1;
    for (int i = 0; i < fable::DR_NPATTERNS; ++i)
        drawSeqBtn(g, patternBounds(i), kPatNames[i], edit == i, 0.0f,
                   bars > 1 && currentBar == i);
    // Bar-chip drag target highlight while a move/copy drag is in flight.
    if (drag_.kind == DragState::Kind::barChip && drag_.crossed
        && drag_.hoverBar >= 0 && drag_.hoverBar != drag_.fromBar) {
        g.setColour(col::acF.withAlpha(0.7f));
        g.drawRoundedRectangle(patternBounds(drag_.hoverBar).toFloat().reduced(0.5f), 5.0f, 2.0f);
    }
    if (proc.capabilities().supportsPatternChain) {
        auto length = sequenceLengthBounds();
        const auto minus = length.removeFromLeft(38);
        const auto plus = length.removeFromRight(38);
        drawSeqBtn(g, minus, "-", false, 0.0f, false, 15.0f);
        drawSeqBtn(g, length, "LENGTH " + juce::String(bars) + "B", false, 0.25f);
        drawSeqBtn(g, plus, "+", false, 0.0f, false, 15.0f);
    }
    if (!proc.capabilities().hosted)
        drawSeqBtn(g, randButtonBounds(), "RAND", false, 0.9f);

    // ---- head: tap hint, right-aligned (.dr-step-editing) ----
    {
        const int left = patternsBtn_.isVisible() ? patternsBtn_.getRight() + 16 : randButtonBounds().getRight() + 16;
        const auto hint = juce::Rectangle<int>(left, kHeadY, getWidth() - kPadX - left, kHeadH);
        if (hint.getWidth() >= 300) {
            g.setColour(juce::Colour(0xff93a0b8));
            g.setFont(seqFont(7.0f));
            drawSpaced(g, juce::String::fromUTF8(u8"TAP STEP · ON → ACCENT → OFF · SHIFT-DRAG TO SELECT"),
                       hint, 0.9f, juce::Justification::centredRight);
        }
    }
    }

    // ---- MICRO TIMING inspector (.dr-timing-panel) ----
    if (polyPanel_ && g.clipRegionIntersects(timingPanel_.getBounds())) {
        const auto panel = timingPanel_.getBounds().toFloat();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff111722), 0, panel.getY(),
                                               juce::Colour(0xff0c1018), 0, panel.getBottom(), false));
        g.fillRoundedRectangle(panel, 9.0f);
        g.setColour(juce::Colours::white.withAlpha(.035f));
        g.drawHorizontalLine((int)panel.getY() + 1, panel.getX() + 9.0f, panel.getRight() - 9.0f);
        g.setColour(col::line); g.drawRoundedRectangle(panel.reduced(.5f), 9.0f, 1.0f);
        const bool grid2x2 = timingPanel_.getWidth() - 16 < 504;
        // Context: title over the selected pad.
        auto context = timingColumn(0).withSizeKeepingCentre(timingColumn(0).getWidth(), 34);
        g.setColour(col::text); g.setFont(dispFont(10));
        drawSpaced(g, "MICRO TIMING", context.removeFromTop(16), 1.0f);
        context.removeFromTop(4);
        g.setColour(col::acA); g.setFont(seqFont(10, true));
        drawSpaced(g, juce::String(sel + 1).paddedLeft('0', 2) + " " + proc.padName(sel), context, 1.0f);
        // Result: caption over the signed sum of lane + step offsets.
        const auto sequence = proc.sequence();
        const auto& lane = sequence.rhythm.lanes[(size_t)sel];
        const int total = lane.delayMs + lane.stepDelayMs[(size_t)(edit * 16 + timingStep_)];
        auto result = timingColumn(3).withSizeKeepingCentre(timingColumn(3).getWidth(), 36);
        if (!grid2x2) result.removeFromLeft(8);
        g.setColour(juce::Colour(0xff9ca9bd)); g.setFont(seqFont(9, true));
        drawSpaced(g, "COMBINED OFFSET", result.removeFromTop(14), 1.2f);
        result.removeFromTop(5);
        g.setColour(col::acA); g.setFont(seqFont(16, true));
        g.drawText((total > 0 ? "+" : "") + juce::String(total) + " ms", result, juce::Justification::centredLeft);
        if (!grid2x2) {
            g.setColour(col::line);
            const float top = panel.getY() + 8.0f, bottom = panel.getBottom() - 8.0f;
            g.drawVerticalLine(timingColumn(0).getRight(), top, bottom);   // .dr-timing-context border-right
            g.drawVerticalLine(timingColumn(3).getX(), top, bottom);       // .dr-timing-result border-left
        }
    }

    // ---- lanes: every pad at once, flat cells (drum.css .dr-lanes) ----
    g.reduceClipRegion(0, kRowY, getWidth() - 12, juce::jmax(0, gridBottom() - kRowY));
    const auto sequence = proc.sequence();
    // StepSeq.tsx selBox: FIT lanes inside the selected pad range wrap their
    // source steps, so those selections mark each cell instead of one box.
    const bool selVisible = (sweeping_ || hasRect_) && !ghost_;
    const auto selNorm = fable::padRectNorm(sweeping_ ? pending_ : rect_);
    bool perCellSelection = false;
    if (selVisible)
        for (int p = selNorm.padLo; p <= selNorm.padHi && p < fable::DR_NPADS; ++p) {
            const auto& l = sequence.rhythm.lanes[(size_t)juce::jmax(0, p)];
            if (sequence.hasRhythm && l.enabled && l.mode == fable::DrumRhythmMode::fit && l.sourceBar == edit)
                perCellSelection = true;
        }
    const int curStep = proc.currentStep(), curPat = proc.currentPattern();
    for (int pad = 0; pad < fable::DR_NPADS; ++pad) {
        const bool isSel = pad == sel;
        const auto& rhythm = sequence.rhythm.lanes[(size_t)pad];
        const bool poly = sequence.hasRhythm && rhythm.enabled;

        if (g.clipRegionIntersects(laneNameBounds(pad))) {
        // Lane name doubles as the pad selector (.dr-lane-name).
        const auto nb = laneNameBounds(pad).toFloat();
        if (isSel) {
            g.setColour(col::acA.withAlpha(0.1f));
            g.fillRoundedRectangle(nb, 4.0f);
            g.setColour(col::acA.withAlpha(0.35f));
            g.drawRoundedRectangle(nb.reduced(0.5f), 4.0f, 1.0f);
        }
        auto text = nb.toNearestInt().reduced(5, 0);
        g.setColour(isSel ? col::acA : col::textDim);
        g.setFont(seqFont(8.0f));
        g.drawText(juce::String(pad + 1).paddedLeft('0', 2),
                   text.removeFromLeft(16), juce::Justification::centredLeft, false);
        if (poly) {
            auto badge = text.removeFromBottom(12);
            g.setColour(col::acB); g.setFont(seqFont(9));
            g.drawText(rhythm.mode == fable::DrumRhythmMode::grid ? "GRID " + juce::String(rhythm.steps)
                : "FIT " + juce::String(rhythm.steps) + "/" + juce::String(rhythm.cycleBeats / 4) + "B", badge, juce::Justification::centredLeft);
            g.setColour(isSel ? col::acA : col::textDim);
        }
        g.drawText(proc.padName(pad), text, juce::Justification::centredLeft, true);

        }
        const bool fit = poly && rhythm.mode == fable::DrumRhythmMode::fit && rhythm.sourceBar == edit;
        if (fit) {
            const auto timeline = laneBounds(pad).withTrimmedLeft(kLaneNameW + kLaneNameGap);
            for (int beat = 0; beat < rhythm.cycleBeats; ++beat) {
                const float x = (float)timeline.getX() + (float)(beat * timeline.getWidth()) / (float)rhythm.cycleBeats;
                g.setColour(col::textDim.withAlpha(.35f));
                g.drawVerticalLine(juce::roundToInt(x), (float)timeline.getY(), (float)timeline.getBottom());
                if (beat % 4 == 0) {
                    g.setColour(col::textDim); g.setFont(seqFont(8));
                    g.drawText(juce::String(beat / 4 + 1), juce::roundToInt(x) - 9, timeline.getY(), 8, 10, juce::Justification::centredRight);
                }
            }
        }
        const bool hasNotes = [&] {
            const int bar = poly ? rhythm.sourceBar : edit;
            const int count = poly ? rhythm.steps : fable::DR_STEPS;
            for (int i = 0; i < count; ++i) if (proc.step(bar, pad, i) != 0) return true;
            return false;
        }();
        for (int s = 0; s < fable::DR_STEPS; ++s) {
            const auto bounds = stepBoundsForLane(pad, s, sequence.hasRhythm ? &rhythm : nullptr);
            if (bounds.isEmpty() || !g.clipRegionIntersects(bounds)) continue;
            const auto b = bounds.toFloat();
            const int v = proc.step(edit, pad, s);
            const bool cur = cursors && playing && hasNotes && (poly ? proc.lanePosition(pad) == edit * 16 + s && rhythm.sourceBar == edit : curStep == s && curPat == edit);
            const bool accented = v == 2;

            // Ghost source cells dim while carried (CUT only).
            bool cutSrc = false;
            if (ghost_ && ghostCut_) {
                const auto sn = fable::padRectNorm(ghostSrc_);
                for (const auto& c : ghostData_.cells)
                    if (sn.stepLo + c.dStep == s && sn.padLo + c.dPad == pad) { cutSrc = true; break; }
            }

            if (accented) {
                g.setGradientFill(juce::ColourGradient(
                    juce::Colour(0xfffff6ea), b.getX(), b.getY(),
                    col::acB.withAlpha(0.6f), b.getX(), b.getBottom(), false));
            } else if (v == 1) {
                g.setGradientFill(juce::ColourGradient(
                    juce::Colour(0xff77f2ff).withAlpha(0.72f), b.getX(), b.getY(),
                    col::acA.withAlpha(0.3f), b.getX(), b.getBottom(), false));
            } else {
                g.setColour(juce::Colour(0xff0a0d13));
            }
            g.fillRoundedRectangle(b, 2.0f);
            // Keep the cells in place and retain their notes, but distinguish
            // everything that this lane no longer plays from its active loop.
            if (poly && (rhythm.sourceBar != edit || s >= rhythm.steps)) {
                g.setColour(rhythm.mode == fable::DrumRhythmMode::grid
                    ? juce::Colour(0xff0c0f15).withAlpha(v ? .85f : 1.0f)
                    : col::panelLo.withAlpha(.82f));
                g.fillRoundedRectangle(b, 2.0f);
                if (rhythm.mode == fable::DrumRhythmMode::fit) {
                    g.setColour(col::textDim); g.setFont(seqFont(9));
                    g.drawText(juce::String(s + 1), b.toNearestInt(), juce::Justification::centred);
                }
            }
            if (cutSrc) {                        // .step.cut-src { opacity: .35 }
                g.setColour(juce::Colour(0xff0e1219).withAlpha(0.65f));
                g.fillRoundedRectangle(b, 2.0f);
            }

            if (cur) {                           // .step.cur amber playhead ring
                g.setColour(col::acB);
            } else if (fit && s < rhythm.steps) {
                g.setColour(col::acB.withAlpha(.65f));
            } else {
                g.setColour(juce::Colours::white.withAlpha(0.045f));
            }
            g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);

            if (fit) {
                g.setColour(v ? col::panelLo : col::text); g.setFont(seqFont(9));
                g.drawText(juce::String(s + 1), bounds, juce::Justification::centred);
            }
            if (polyPanel_ && pad == sel && s == timingStep_) {   // .step.timing-selected
                g.setColour(col::acA); g.drawRect(bounds, 2);
            }
            const int delay = rhythm.stepDelayMs[(size_t)(edit * 16 + s)];
            if (delay != 0) {
                g.setColour(v ? col::panelLo : col::acA); g.setFont(seqFont(10));
                g.drawText(delay > 0 ? "+" : "-", bounds.reduced(3), juce::Justification::topRight);
            }
            // Per-cell selection ring only where FIT reorders the columns;
            // otherwise one rectangle covers the selection (below).
            if (perCellSelection && inRect(s, pad)) {
                if (v == 0) {
                    g.setColour(col::acF.withAlpha(0.14f));
                    g.fillRoundedRectangle(b, 2.0f);
                }
                g.setColour(col::acF.withAlpha(0.85f));
                g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);
            }
        }
    }

    // ---- selection rectangle (.dr-sel-rect): one translucent bordered box ----
    if (selVisible && !perCellSelection) {
        const auto tl = stepBounds(juce::jlimit(0, fable::DR_NPADS - 1, selNorm.padHi), juce::jlimit(0, fable::DR_STEPS - 1, selNorm.stepLo));
        const auto br = stepBounds(juce::jlimit(0, fable::DR_NPADS - 1, selNorm.padLo), juce::jlimit(0, fable::DR_STEPS - 1, selNorm.stepHi));
        const auto box = tl.getUnion(br).toFloat();
        g.setColour(col::acA.withAlpha(0.14f));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(col::acA.withAlpha(0.85f));
        g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, 1.0f);
    }

    // ---- block-move preview: the rect outlined at its dragged destination ----
    if (moving_) {
        const auto n = fable::padRectNorm(rect_);
        const int dStep = juce::jlimit(-n.stepLo, (fable::DR_STEPS - 1) - n.stepHi, moveHoverStep_ - moveOriginStep_);
        const int dPad = juce::jlimit(-n.padLo, (fable::DR_NPADS - 1) - n.padHi, moveHoverPad_ - moveOriginPad_);
        const auto box = stepBounds(n.padHi + dPad, n.stepLo + dStep)
                             .getUnion(stepBounds(n.padLo + dPad, n.stepHi + dStep)).toFloat();
        g.setColour(col::acA.withAlpha(0.9f));
        g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, 1.2f);
    }

    // ---- ghost cells trailing the cursor (.step.ghost: dashed outline over a 34 % wash) ----
    if (ghost_ && ghostHasHover_) {
        for (const auto& c : ghostData_.cells) {
            const int s = ghostHoverStep_ + c.dStep;
            const int pad = ghostHoverPad_ + c.dPad;
            if (s < 0 || s >= fable::DR_STEPS || pad < 0 || pad >= fable::DR_NPADS) continue;
            const auto b = stepBounds(pad, s).toFloat();
            g.setColour(col::acA.withAlpha(0.34f));
            g.fillRoundedRectangle(b, 2.0f);
            juce::Path outline, dashed;
            outline.addRoundedRectangle(b.reduced(0.5f), 2.0f);
            const float dashes[2] = { 3.0f, 2.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
            g.setColour(col::acA.withAlpha(0.85f));
            g.fillPath(dashed);
        }
    }

    // ---- floating CUT · COPY · DUP · DEL · ✕ toolbar over the selection (.seq-selmenu) ----
    if (hasRect_ && !ghost_) {
        static const char* const kMenu[5] = { "CUT", "COPY", "DUP", "DEL", "X" };
        const auto mb = selMenuBounds().expanded(4, 3).toFloat();
        g.setColour(juce::Colour(0xea0a0d13));
        g.fillRoundedRectangle(mb, 6.0f);
        g.setColour(col::acA.withAlpha(0.45f));
        g.drawRoundedRectangle(mb.reduced(0.5f), 6.0f, 1.0f);
        for (int i = 0; i < 5; ++i)
            drawSeqBtn(g, selMenuButton(i), kMenu[i], false, 0.4f, false, 10.0f);
    }
}

} // namespace fui
