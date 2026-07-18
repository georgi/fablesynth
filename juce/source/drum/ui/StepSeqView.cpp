#include "StepSeqView.h"
#include "../dsp/DrumPatches.h"
#include <cmath>

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
    startTimerHz(10);
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
    g.setFont(monoFont(9.0f));
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
    g.setFont(monoFont(9.0f));
    drawSpaced(g, patchName, patchNameArea.reduced(8, 0), 0.8f,
               juce::Justification::centred);
}

// ---- StepSeqView geometry ----------------------------------------------------
// drum.css: .dr-stepseq padding 9px 12px 11px; head 28px + 10px margin;
// .step-row: 16 x 45px steps, 5px gaps, extra 8px after steps 4/8/12.

static constexpr int kPadX = 12, kHeadY = 9, kHeadH = 28, kTitleW = 88;
static constexpr int kNameW = 100;               // .dr-stepseq-target slot after the title
static constexpr int kRowY = 47, kStepH = 45;
static constexpr float kStepGap = 5.0f, kGroupGap = 8.0f;

StepSeqView::StepSeqView(DrumUiModel& p) : proc(p) {
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);   // decision 6/7: verbs on keyPressed, modifier combos only
    startTimerHz(30);
}

#ifndef FABLE_HOSTED_UI
StepSeqView::StepSeqView(DrumAudioProcessor& p)
    : ownedModel(makeStandaloneDrumUiModel(p)), proc(*ownedModel) {
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}
#endif

juce::Rectangle<int> StepSeqView::transportBounds() const {
    return { kPadX, kHeadY, 34, 28 };            // .dr-transport 34x28
}

juce::Rectangle<int> StepSeqView::patternBounds(int i) const {
    const int x0 = kPadX + 34 + 8 + kTitleW + 8   // transport, gap, title, gap
                 + kNameW + 8;                    // editing-pad name, gap
    return { x0 + i * (26 + 4), kHeadY + 2, 26, 24 }; // .dr-pattern 26x24, gap 4
}

juce::Rectangle<int> StepSeqView::sequenceLengthBounds() const {
    return { patternBounds(3).getRight() + 10, kHeadY, 170, 30 };
}

juce::Rectangle<int> StepSeqView::randButtonBounds() const {
    return { sequenceLengthBounds().getRight() + 8, kHeadY + 2, 44, 24 };
}

juce::Rectangle<int> StepSeqView::stepBounds(int step) const {
    const auto row = getLocalBounds().reduced(kPadX, 0).withY(kRowY).withHeight(kStepH);
    const float w = ((float)row.getWidth() - 15.0f * kStepGap - 3.0f * kGroupGap) / 16.0f;
    float x = (float)row.getX();
    for (int i = 0; i < step; ++i) {
        x += w + kStepGap;
        if (i % 4 == 3) x += kGroupGap;          // bar-group breathing room
    }
    return juce::Rectangle<float>(x, (float)row.getY(), w, (float)kStepH).toNearestInt();
}

// ---- store handlers ----------------------------------------------------------

void StepSeqView::toggleStep(int step) {
    const int pat = proc.editPattern(), pad = proc.selectedPad();
    const auto v = (uint8_t)((proc.step(pat, pad, step) + 1) % 3);
    proc.setStep(pat, pad, step, v);             // seq.ts cycleStep
    repaint();
}

void StepSeqView::patternClick(int i) {
    proc.setEditPattern(i);
    repaint();
}

void StepSeqView::setSequenceLength(int bars) {
    bars = juce::jlimit(1, fable::DR_NPATTERNS, bars);
    std::vector<int> sequence;
    for (int bar = 0; bar < bars; ++bar) sequence.push_back(bar);
    proc.setChain(std::move(sequence));
    repaint();
}

// RAND button: rewrite the selected pad's row in the current edit pattern
// only — other pads' lanes are untouched (web store.randomizePad).
void StepSeqView::randomizePad(std::function<float()> rng) {
    pushHistoryEntry();
    const int pad = proc.selectedPad(), pat = proc.editPattern();
    if (!rng) rng = [] { return juce::Random::getSystemRandom().nextFloat(); };
    applyPatternBuffer(pat, fable::randomizeLane(buildPatternBuffer(pat), padLayout(pad), 0, rng));
    repaint();
}

// ---- decision 6: layout / buffer helpers ------------------------------------
// Every verb below works over a flat pattern-major buffer (DR_NPADS*DR_STEPS
// bytes for one edit pattern) built/flushed through proc.step()/setStep(), so
// no assumption is made about how the host (standalone engine vs. SQ-4 hosted
// clip bytes) actually stores the data — mirrors StepEditOps.h's own
// JUCE-free, storage-agnostic contract.
fable::StepLayout StepSeqView::padLayout(int pad) const {
    fable::StepLayout l;
    l.stride = 1;
    l.stepsPerPattern = fable::DR_STEPS;
    l.patternSize = fable::DR_NPADS * fable::DR_STEPS;
    l.laneOffset = pad * fable::DR_STEPS;
    return l;
}

fable::StepBytes StepSeqView::buildPatternBuffer(int pat) const {
    fable::StepBytes buf((size_t)(fable::DR_NPADS * fable::DR_STEPS), (uint8_t)0);
    for (int pad = 0; pad < fable::DR_NPADS; ++pad)
        for (int s = 0; s < fable::DR_STEPS; ++s)
            buf[(size_t)(pad * fable::DR_STEPS + s)] = proc.step(pat, pad, s);
    return buf;
}

void StepSeqView::applyPatternBuffer(int pat, const fable::StepBytes& buf) {
    for (int pad = 0; pad < fable::DR_NPADS; ++pad)
        for (int s = 0; s < fable::DR_STEPS; ++s) {
            const uint8_t v = buf[(size_t)(pad * fable::DR_STEPS + s)];
            if (proc.step(pat, pad, s) != v) proc.setStep(pat, pad, s, v);
        }
}

fable::StepBytes StepSeqView::shiftPatternBuffer(const fable::StepBytes& basePattern, int pad,
                                                  int from, int to, int dest, bool copy) const {
    fable::ShiftOpts opts;
    opts.copy = copy;
    return fable::shiftRange(basePattern, padLayout(pad), 0, from, to, dest, opts);
}

DrStepSnapshot StepSeqView::captureSnapshot() const {
    DrStepSnapshot s;
    s.steps.resize((size_t)(fable::DR_NPATTERNS * fable::DR_NPADS * fable::DR_STEPS));
    for (int pat = 0; pat < fable::DR_NPATTERNS; ++pat)
        for (int pad = 0; pad < fable::DR_NPADS; ++pad)
            for (int st = 0; st < fable::DR_STEPS; ++st)
                s.steps[(size_t)((pat * fable::DR_NPADS + pad) * fable::DR_STEPS + st)] =
                    proc.step(pat, pad, st);
    s.chain = proc.chain();
    return s;
}

void StepSeqView::restoreSnapshot(const DrStepSnapshot& s) {
    // Chain first: hosted DR-1's setStep drops writes for patterns beyond the
    // current clip bar count, so restoring across a sequence-length shrink
    // must grow the clip before the bar's steps are written back (matches
    // NoteSeqView::restore's chain-then-steps order).
    if (proc.chain() != s.chain) proc.setChain(s.chain);
    for (int pat = 0; pat < fable::DR_NPATTERNS; ++pat)
        for (int pad = 0; pad < fable::DR_NPADS; ++pad)
            for (int st = 0; st < fable::DR_STEPS; ++st) {
                const uint8_t v = s.steps[(size_t)((pat * fable::DR_NPADS + pad) * fable::DR_STEPS + st)];
                if (proc.step(pat, pad, st) != v) proc.setStep(pat, pad, st, v);
            }
    repaint();
}

void StepSeqView::pushHistoryEntry() { history_.push(captureSnapshot()); }

bool StepSeqView::stepInSelection(int step) const {
    if (!sel_.active) return false;
    if (sel_.selectAll) return true;         // whole visible row (Cmd-A)
    return step >= sel_.from && step <= sel_.to;
}

// ---- decision 6: selection -------------------------------------------------

void StepSeqView::extendSelection(int step) {
    step = juce::jlimit(0, fable::DR_STEPS - 1, step);
    if (!sel_.active || sel_.selectAll) {
        sel_.active = true;
        sel_.selectAll = false;
        selAnchor_ = step;
        sel_.from = sel_.to = step;
    } else {
        sel_.from = juce::jmin(selAnchor_, step);
        sel_.to = juce::jmax(selAnchor_, step);
    }
    repaint();
}

void StepSeqView::selectAllPattern() {
    sel_.active = true;
    sel_.selectAll = true;
    sel_.from = 0;
    sel_.to = fable::DR_STEPS - 1;
    repaint();
}

void StepSeqView::clearSelection() {
    sel_ = {};
    repaint();
}

// ---- decision 6: verbs ------------------------------------------------------
// Copy/cut default to the whole edit pattern (all pads) when nothing is
// selected; a range selection scopes to the selected pad's row; Cmd-A scopes
// to the whole edit pattern too (selectAll), matching the web contract.

void StepSeqView::copySelection() {
    const int pad = proc.selectedPad(), pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    if (sel_.active && !sel_.selectAll) {
        clipboard_ = { true, false, fable::copyRange(buf, padLayout(pad), 0, sel_.from, sel_.to) };
    } else {
        clipboard_ = { true, true, fable::copyPattern(buf, padLayout(pad), 0) };
    }
}

void StepSeqView::cutSelection() {
    copySelection();
    pushHistoryEntry();
    const int pad = proc.selectedPad(), pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    if (sel_.active && !sel_.selectAll) {
        buf = fable::clearRange(buf, padLayout(pad), 0, sel_.from, sel_.to);
    } else {
        std::fill(buf.begin(), buf.end(), (uint8_t)0);
    }
    applyPatternBuffer(pat, buf);
    repaint();
}

void StepSeqView::deleteSelection() {
    if (!sel_.active) return;                // Delete only acts on an explicit selection
    pushHistoryEntry();
    const int pad = proc.selectedPad(), pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    if (sel_.selectAll) std::fill(buf.begin(), buf.end(), (uint8_t)0);
    else buf = fable::clearRange(buf, padLayout(pad), 0, sel_.from, sel_.to);
    applyPatternBuffer(pat, buf);
    repaint();
}

void StepSeqView::pasteSelection() {
    if (!clipboard_.valid) return;
    pushHistoryEntry();
    const int pad = proc.selectedPad(), pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    const auto layout = padLayout(pad);
    if (clipboard_.wholePattern) {
        buf = fable::pastePattern(buf, layout, 0, clipboard_.data);
    } else {
        // "at selection start if a selection exists, else step 0" (web contract).
        const int at = (sel_.active && !sel_.selectAll) ? sel_.from : 0;
        buf = fable::pasteRange(buf, layout, 0, at, clipboard_.data);
    }
    applyPatternBuffer(pat, buf);
    repaint();
}

void StepSeqView::duplicatePattern() {
    const int edit = proc.editPattern();
    const int target = edit + 1;
    if (target >= fable::DR_NPATTERNS) return;   // no pattern slot past bar 4
    pushHistoryEntry();
    const int curBars = proc.capabilities().hosted ? proc.clipBars() : (int)proc.chain().size();
    if (target + 1 > curBars) setSequenceLength(target + 1); // extend up to 4
    applyPatternBuffer(target, buildPatternBuffer(edit));
    repaint();
}

void StepSeqView::duplicateSelection() {
    if (!sel_.active || sel_.selectAll) { duplicatePattern(); return; }
    pushHistoryEntry();
    const int pad = proc.selectedPad(), pat = proc.editPattern();
    auto buf = buildPatternBuffer(pat);
    const auto layout = padLayout(pad);
    const auto data = fable::copyRange(buf, layout, 0, sel_.from, sel_.to);
    const int span = sel_.to - sel_.from;
    const int dest = sel_.to + 1;                // immediately after the range (clamped by pasteRange)
    buf = fable::pasteRange(buf, layout, 0, dest, data);
    applyPatternBuffer(pat, buf);
    if (dest < fable::DR_STEPS) {
        sel_.from = dest;
        sel_.to = juce::jmin(fable::DR_STEPS - 1, dest + span);
    }
    repaint();
}

void StepSeqView::shiftSelection(int destStep, bool copy) {
    if (!sel_.active) return;
    pushHistoryEntry();
    const int pad = proc.selectedPad(), pat = proc.editPattern();
    const int from = sel_.selectAll ? 0 : sel_.from;
    const int to = sel_.selectAll ? fable::DR_STEPS - 1 : sel_.to;
    // Clamp dest so the whole range fits (STEPS - len): shiftRange truncates
    // the overshooting tail on a move — the web review's overshoot-drag fix,
    // mirrored from NoteSeqView::shiftStepSel.
    const int dest = juce::jlimit(0, fable::DR_STEPS - (to - from + 1), destStep);
    auto next = shiftPatternBuffer(buildPatternBuffer(pat), pad, from, to, dest, copy);
    applyPatternBuffer(pat, next);
    sel_.active = true;
    sel_.selectAll = false;
    sel_.from = dest;
    sel_.to = dest + (to - from);
    repaint();
}

void StepSeqView::movePattern(int fromBar, int toBar, bool copy) {
    if (fromBar == toBar || fromBar < 0 || toBar < 0
        || fromBar >= fable::DR_NPATTERNS || toBar >= fable::DR_NPATTERNS) return;
    pushHistoryEntry();
    const auto layout = padLayout(0);          // patternSize is all copyPattern/pastePattern need
    const auto a = fable::copyPattern(buildPatternBuffer(fromBar), layout, 0);
    if (copy) {
        applyPatternBuffer(toBar, a);           // Alt-drag: copy A over B, A unchanged
    } else {
        const auto b = fable::copyPattern(buildPatternBuffer(toBar), layout, 0);
        applyPatternBuffer(toBar, a);           // plain drag: swap A <-> B
        applyPatternBuffer(fromBar, b);
    }
    repaint();
}

void StepSeqView::undo() {
    DrStepSnapshot restored, current = captureSnapshot();
    if (history_.undo(current, restored)) restoreSnapshot(restored);
}

void StepSeqView::redo() {
    DrStepSnapshot restored, current = captureSnapshot();
    if (history_.redo(current, restored)) restoreSnapshot(restored);
}

// ---- mouse: selection, step-range drag-shift, bar-chip drag ---------------

void StepSeqView::mouseDown(const juce::MouseEvent& e) {
    // Real clicks already auto-grab focus via JUCE's mouse dispatch; headless
    // tests call mouseDown() directly and skip that path, so grab explicitly
    // here, guarded exactly like SeqEditor::enterFocus (no peer => no-op assert).
    if (isShowing() || isOnDesktop()) grabKeyboardFocus();
    const auto pos = e.getPosition();
    if (transportBounds().contains(pos)) {       // play/stop
        proc.setSequencerPlaying(!proc.sequencerPlaying());
        repaint();
        return;
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
    for (int s = 0; s < fable::DR_STEPS; ++s) {
        if (!stepBounds(s).contains(pos)) continue;
        if (e.mods.isShiftDown()) {              // Shift-click set/extend; never toggles
            selecting_ = true;
            extendSelection(s);
            return;
        }
        if (sel_.active && stepInSelection(s)) {
            // Arm a potential drag-shift; a stationary release still toggles
            // (mouseUp), matching the plain-click semantics this replaces.
            drag_ = DragState{};
            drag_.kind = DragState::Kind::stepShift;
            drag_.startPos = pos;
            drag_.originStep = s;
            drag_.pad = proc.selectedPad();
            drag_.pat = proc.editPattern();
            drag_.selFrom = sel_.selectAll ? 0 : sel_.from;
            drag_.selTo = sel_.selectAll ? fable::DR_STEPS - 1 : sel_.to;
            return;
        }
        toggleStep(s);
        return;
    }
}

void StepSeqView::mouseDrag(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (selecting_) {
        for (int s = 0; s < fable::DR_STEPS; ++s)
            if (stepBounds(s).contains(pos)) { extendSelection(s); break; }
        return;
    }
    if (drag_.kind == DragState::Kind::none) return;
    if (!drag_.crossed) {
        const int dx = pos.x - drag_.startPos.x, dy = pos.y - drag_.startPos.y;
        if (dx * dx + dy * dy < 16) return;      // ~4px movement threshold
        drag_.crossed = true;
        if (drag_.kind == DragState::Kind::stepShift) {
            drag_.preDrag = captureSnapshot();
            drag_.basePattern = buildPatternBuffer(drag_.pat);
        }
    }
    if (drag_.kind == DragState::Kind::barChip) {
        int hover = -1;
        for (int i = 0; i < fable::DR_NPATTERNS; ++i)
            if (patternBounds(i).contains(pos)) { hover = i; break; }
        if (hover != drag_.hoverBar) { drag_.hoverBar = hover; repaint(); }
        return;
    }
    // stepShift: live preview recomputed from the captured pre-drag baseline
    // (never compounded), imitating NoteSeqView's duration-resize idiom.
    int destStep = -1;
    for (int s = 0; s < fable::DR_STEPS; ++s) if (stepBounds(s).contains(pos)) { destStep = s; break; }
    if (destStep < 0) return;
    const int delta = destStep - drag_.originStep;
    const int dest = juce::jlimit(0, fable::DR_STEPS - (drag_.selTo - drag_.selFrom + 1),
                                  drag_.selFrom + delta);
    auto next = shiftPatternBuffer(drag_.basePattern, drag_.pad, drag_.selFrom, drag_.selTo,
                                   dest, e.mods.isAltDown());
    applyPatternBuffer(drag_.pat, next);
    sel_.active = true;
    sel_.selectAll = false;
    sel_.from = dest;
    sel_.to = dest + (drag_.selTo - drag_.selFrom);
    repaint();
}

void StepSeqView::mouseUp(const juce::MouseEvent& e) {
    if (selecting_) { selecting_ = false; return; }
    if (drag_.kind == DragState::Kind::barChip) {
        if (drag_.crossed && drag_.hoverBar >= 0 && drag_.hoverBar != drag_.fromBar)
            movePattern(drag_.fromBar, drag_.hoverBar, e.mods.isAltDown());
        else if (!drag_.crossed)
            patternClick(drag_.fromBar);
        drag_ = DragState{};
        repaint();
        return;
    }
    if (drag_.kind == DragState::Kind::stepShift) {
        if (drag_.crossed) history_.push(std::move(drag_.preDrag)); // one entry per gesture
        else toggleStep(drag_.originStep);
        drag_ = DragState{};
        return;
    }
}

bool StepSeqView::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        if (drag_.kind == DragState::Kind::stepShift && drag_.crossed) {
            restoreSnapshot(drag_.preDrag);      // undo the live preview
            drag_ = DragState{};
            return true;
        }
        if (drag_.kind != DragState::Kind::none) { drag_ = DragState{}; repaint(); return true; }
        if (sel_.active) { clearSelection(); return true; }
        return false;                            // fall through to PadGrid's stop-sequencer Escape
    }
    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && sel_.active) {
        deleteSelection();
        return true;
    }
    const auto mods = k.getModifiers();
    if (!mods.isCommandDown()) return false;      // only modifier combos claimed (decision 7)
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

// ---- animation ----------------------------------------------------------------

// Repaint only when something visible changed: transport/playhead, edit
// pattern, chain, selected pad (the step row shows its lane) or its name,
// or the pattern cells themselves (kit program loads rewrite them).
void StepSeqView::timerCallback() {
    // Decision 6: the hosted clip/pattern source can swap under us (SQ-4
    // focus switching a HostedDrumModel to another scene) — clear the undo
    // history and any selection so a later undo can never reach back into a
    // different clip's content (the web's cross-clip corruption hazard).
    const int identity = proc.clipIdentity();
    if (!haveClipIdentity_) { lastClipIdentity_ = identity; haveClipIdentity_ = true; }
    else if (identity != lastClipIdentity_) {
        lastClipIdentity_ = identity;
        history_.clear();
        clearSelection();
    }

    juce::uint32 sig = 17;
    auto mix = [&sig](int v) { sig = sig * 31u + (juce::uint32)(v + 2); };
    const bool playing = proc.sequencerPlaying();
    const int edit = proc.editPattern(), sel = proc.selectedPad();
    mix(playing ? 1 : 0);
    mix(playing ? proc.currentStep() : -1);
    mix(proc.currentPattern());
    mix(edit); mix(sel);
    const auto& chain = proc.chain();
    mix((int)chain.size());
    for (int p : chain) mix(p);
    for (int s = 0; s < fable::DR_STEPS; ++s) mix(proc.step(edit, sel, s));
    mix(proc.padName(sel).hashCode());
    mix(sel_.active ? (sel_.selectAll ? 1000 : 100 + sel_.from * 20 + sel_.to) : 0);
    mix(drag_.kind == DragState::Kind::barChip ? drag_.hoverBar + 1 : 0);
    if (sig != lastSig_) { lastSig_ = sig; repaint(); }
}

// ---- paint ----------------------------------------------------------------------

// Sequencer header buttons: 5px radius, #0d1017, dim mono text;
// active = cyan border/text over a faint cyan wash.
static void drawSeqBtn(juce::Graphics& g, juce::Rectangle<int> b, const juce::String& text,
                       bool active, float tracking, bool current = false, float fontSize = 8.0f) {
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
    g.setFont(monoFont(fontSize));
    drawSpaced(g, text, b, tracking, juce::Justification::centred);
}

void StepSeqView::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    const bool playing = proc.sequencerPlaying();
    const int edit = proc.editPattern();
    const int sel = proc.selectedPad();

    // ---- head: transport ----
    const auto tb = transportBounds().toFloat();
    g.setColour(juce::Colour(0xff11141c));
    g.fillRoundedRectangle(tb, 6.0f);
    if (playing) {                               // .dr-transport.active inner glow
        g.setColour(col::acA.withAlpha(0.1f));
        g.fillRoundedRectangle(tb, 6.0f);
        g.setColour(col::acA);
    } else {
        g.setColour(col::acA.withAlpha(0.55f));
    }
    g.drawRoundedRectangle(tb.reduced(0.5f), 6.0f, 1.0f);
    g.setColour(col::acA);
    if (playing) {                               // stop square
        g.fillRect(juce::Rectangle<float>(8.0f, 8.0f).withCentre(tb.getCentre()));
    } else {                                     // play triangle
        juce::Path p;
        p.addTriangle(tb.getCentreX() - 3.5f, tb.getCentreY() - 5.0f,
                      tb.getCentreX() - 3.5f, tb.getCentreY() + 5.0f,
                      tb.getCentreX() + 5.5f, tb.getCentreY());
        g.fillPath(p);
    }

    // ---- head: title (panel accent a -> cyan) ----
    g.setColour(col::acA);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, "STEP SEQ",
               { transportBounds().getRight() + 8, kHeadY, kTitleW, kHeadH }, 2.2f);

    // ---- head: editing-pad name (.dr-stepseq-target) — tied directly to the
    // STEP SEQ nameplate in the accent color instead of sitting disconnected
    // on the far right.
    g.setColour(col::acA);
    g.setFont(monoFont(9.0f, true));
    drawSpaced(g, proc.padName(sel),
               { transportBounds().getRight() + 8 + kTitleW + 8, kHeadY, kNameW, kHeadH }, 1.0f);

    // ---- head: bar buttons + sequence length ----
    const int bars = proc.capabilities().hosted ? proc.clipBars()
                                                : juce::jlimit(1, fable::DR_NPATTERNS, (int)proc.chain().size());
    const int currentBar = playing ? proc.currentPattern() : -1;
    for (int i = 0; i < fable::DR_NPATTERNS; ++i)
        drawSeqBtn(g, patternBounds(i), kPatNames[i], edit == i, 0.0f,
                   bars > 1 && currentBar == i);
    // Bar-chip drag target: highlight the pattern button under the pointer
    // while a move/copy drag is in flight (decision 6 DnD).
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

    // ---- head: tap hint, right-aligned (the pad name moved next to the
    // title; the web dropped the standalone EDITING label with it) ----
    auto right = getLocalBounds().withY(kHeadY).withHeight(kHeadH)
                     .withTrimmedRight(kPadX).withTrimmedLeft(660);
    g.setColour(col::textHint);
    g.setFont(monoFont(7.0f));
    // web "TAP STEP · ON → ACCENT → OFF · SHIFT-DRAG TO SELECT" — ASCII
    // substitutes (mono font glyphs)
    drawSpaced(g, "TAP STEP - ON -> ACCENT -> OFF - SHIFT-DRAG TO SELECT",
               right.removeFromRight(320), 0.9f, juce::Justification::right);

    // ---- step row ----
    const int curStep = proc.currentStep(), curPat = proc.currentPattern();
    for (int s = 0; s < fable::DR_STEPS; ++s) {
        const auto b = stepBounds(s).toFloat();
        const int v = proc.step(edit, sel, s);
        const bool cur = playing && curStep == s && curPat == edit;

        // .step body: #171b25 -> #0d1017 gradient, 6px radius
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff171b25), b.getX(), b.getY(),
                                               juce::Colour(0xff0d1017), b.getX(), b.getBottom(),
                                               false));
        g.fillRoundedRectangle(b, 6.0f);
        if (cur) {                               // .step.cur amber playhead ring
            g.setColour(col::acB.withAlpha(0.3f));
            g.drawRoundedRectangle(b.expanded(1.0f), 7.0f, 2.0f);
            g.setColour(col::acB);
        } else {
            g.setColour(col::line);
        }
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);

        // .step-accent: an accent must read at a glance, not just on close
        // inspection — a taller, brighter, near-white cap plus a wider glow set
        // it clearly apart from a plain on-step (web parity: .step.accented
        // .step-accent — top 3, height 4, #fff2e0).
        const bool accented = v == 2;
        const juce::Rectangle<float> ab = accented
            ? juce::Rectangle<float>(b.getX() + 5.0f, b.getY() + 3.0f, b.getWidth() - 10.0f, 4.0f)
            : juce::Rectangle<float>(b.getX() + 5.0f, b.getY() + 5.0f, b.getWidth() - 10.0f, 2.0f);
        if (accented) {
            g.setColour(col::acB.withAlpha(0.9f));          // wider glow
            g.fillRoundedRectangle(ab.expanded(2.0f), 3.0f);
            g.setColour(juce::Colour(0xfffff2e0));          // near-white cap
        } else {
            g.setColour(col::acB.withAlpha(0.12f));
        }
        g.fillRoundedRectangle(ab, 2.0f);

        // .step-fill: inset 5, top 10, bottom 12 (cyan when on; a brighter,
        // fully-opaque amber wash when accented so the whole cell — not just the
        // cap — reads as accented, matching .step.accented .step-fill).
        const juce::Rectangle<float> fb(b.getX() + 5.0f, b.getY() + 10.0f,
                                        b.getWidth() - 10.0f, b.getHeight() - 22.0f);
        if (accented) {
            g.setGradientFill(juce::ColourGradient(
                juce::Colour(0xfffff6ea), fb.getX(), fb.getY(),
                col::acB.withAlpha(0.55f), fb.getX(), fb.getBottom(), false));
            g.fillRoundedRectangle(fb, 3.0f);
            g.setColour(juce::Colour(0xfffff2e0).withAlpha(0.55f));
            g.fillRect(fb.withHeight(1.0f));                // brighter inner top highlight
        } else if (v == 1) {
            g.setGradientFill(juce::ColourGradient(
                juce::Colour(0xff77f2ff).withAlpha(0.76f), fb.getX(), fb.getY(),
                col::acA.withAlpha(0.28f), fb.getX(), fb.getBottom(), false));
            g.fillRoundedRectangle(fb, 3.0f);
            g.setColour(juce::Colour(0xffe8fcff).withAlpha(0.34f));
            g.fillRect(fb.withHeight(1.0f));                // inner top highlight
        } else {
            g.setColour(juce::Colour(0xff0a0d13));
            g.fillRoundedRectangle(fb, 3.0f);
        }

        // .step-num centred at the bottom
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        g.drawText(juce::String(s + 1),
                   b.toNearestInt().withTop(b.toNearestInt().getBottom() - 10),
                   juce::Justification::centredTop, false);

        // Decision 6 selection paint: a violet wash + ring distinct from the
        // cyan on-state, the amber accent/playhead, and the bar-chip drag
        // highlight above (col::acF, the theme's third accent).
        if (stepInSelection(s)) {
            g.setColour(col::acF.withAlpha(0.14f));
            g.fillRoundedRectangle(b, 6.0f);
            g.setColour(col::acF.withAlpha(0.85f));
            g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.5f);
        }
    }
}

} // namespace fui
