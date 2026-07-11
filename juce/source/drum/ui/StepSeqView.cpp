#include "StepSeqView.h"
#include "../dsp/DrumPatches.h"
#include <cmath>

namespace fui {

static const char* const kPatNames[fable::DR_NPATTERNS] = { "A", "B", "C", "D" };

// ---- SelBarView -------------------------------------------------------------

SelBarView::SelBarView(DrumAudioProcessor& p) : proc(p) {
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
    const int sel = proc.getSelectedPad();
    const int prog = proc.getCurrentProgram();
    auto name = proc.getPadName(sel);
    if (sel != lastSel_ || prog != lastProgram_ || name != lastName_) {
        // Web store: selectPad and loadKitByValue both clear patchValue — a
        // new pad or a freshly loaded kit isn't a known patch.
        if (sel != lastSel_ || prog != lastProgram_) patchIndex_ = -1;
        lastSel_ = sel;
        lastProgram_ = prog;
        lastName_ = name;
        repaint();
    }
}

void SelBarView::stepPatch(int dir) {
    const int n = (int)fable::factoryPatches().size();
    if (n == 0) return;
    patchIndex_ = patchIndex_ < 0 ? (dir > 0 ? 0 : n - 1)
                                  : ((patchIndex_ + dir) % n + n) % n;
    proc.applyFactoryPatch(patchIndex_);
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

    const int sel = proc.getSelectedPad();
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
    drawSpaced(g, proc.getPadName(sel), row.removeFromLeft(170), 0.9f);

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
static constexpr int kRowY = 47, kStepH = 45;
static constexpr float kStepGap = 5.0f, kGroupGap = 8.0f;

StepSeqView::StepSeqView(DrumAudioProcessor& p) : proc(p) {
    setInterceptsMouseClicks(true, false);
    startTimerHz(30);
}

juce::Rectangle<int> StepSeqView::transportBounds() const {
    return { kPadX, kHeadY, 34, 28 };            // .dr-transport 34x28
}

juce::Rectangle<int> StepSeqView::patternBounds(int i) const {
    const int x0 = kPadX + 34 + 8 + kTitleW + 8; // transport, gap, title, gap
    return { x0 + i * (26 + 4), kHeadY + 2, 26, 24 }; // .dr-pattern 26x24, gap 4
}

juce::Rectangle<int> StepSeqView::chainToggleBounds() const {
    return { patternBounds(3).getRight() + 10, kHeadY + 2, 52, 24 }; // ml 2 + gap 8
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
    const int pat = proc.getEditPattern(), pad = proc.getSelectedPad();
    const auto v = (uint8_t)((proc.getStep(pat, pad, step) + 1) % 3);
    proc.setStep(pat, pad, step, v);             // seq.ts cycleStep
    repaint();
}

void StepSeqView::patternClick(int i) {
    if (!chaining_) {                            // store.setEditPattern
        proc.setEditPattern(i);
        proc.setChain({ i });
    } else {                                     // store.chainClick while chaining
        std::vector<int> chain;
        if (!chainFresh_) chain = proc.getChain();
        chain.push_back(i);
        chainFresh_ = false;
        proc.setEditPattern(i);
        proc.setChain(std::move(chain));
    }
    repaint();
}

void StepSeqView::setChaining(bool on) {
    if (on) {
        chaining_ = true;
        chainFresh_ = true;                      // first click will replace
    } else {                                     // commit (store.setChaining off)
        chaining_ = false;
        chainFresh_ = false;
        auto chain = proc.getChain();
        if (chain.empty()) chain.push_back(proc.getEditPattern());
        proc.setChain(std::move(chain));
    }
    repaint();
}

void StepSeqView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (transportBounds().contains(pos)) {       // play/stop
        proc.setSeqPlaying(!proc.isSeqPlaying());
        repaint();
        return;
    }
    for (int i = 0; i < fable::DR_NPATTERNS; ++i)
        if (patternBounds(i).contains(pos)) { patternClick(i); return; }
    if (chainToggleBounds().contains(pos)) { setChaining(!chaining_); return; }
    for (int s = 0; s < fable::DR_STEPS; ++s)
        if (stepBounds(s).contains(pos)) { toggleStep(s); return; }
}

// ---- animation ----------------------------------------------------------------

// Repaint only when something visible changed: transport/playhead, edit
// pattern, chain, selected pad (the step row shows its lane) or its name,
// or the pattern cells themselves (kit program loads rewrite them).
void StepSeqView::timerCallback() {
    juce::uint32 sig = 17;
    auto mix = [&sig](int v) { sig = sig * 31u + (juce::uint32)(v + 2); };
    const bool playing = proc.isSeqPlaying();
    const int edit = proc.getEditPattern(), sel = proc.getSelectedPad();
    mix(playing ? 1 : 0);
    mix(playing ? proc.getCurrentStep() : -1);
    mix(proc.getCurrentPattern());
    mix(edit); mix(sel); mix(chaining_ ? 1 : 0);
    const auto& chain = proc.getChain();
    mix((int)chain.size());
    for (int p : chain) mix(p);
    for (int s = 0; s < fable::DR_STEPS; ++s) mix(proc.getStep(edit, sel, s));
    mix(proc.getPadName(sel).hashCode());
    if (sig != lastSig_) { lastSig_ = sig; repaint(); }
}

// ---- paint ----------------------------------------------------------------------

// .dr-seq-btn (pattern + CHAIN buttons): 5px radius, #0d1017, dim mono text;
// active = cyan border/text over a faint cyan wash.
static void drawSeqBtn(juce::Graphics& g, juce::Rectangle<int> b, const juce::String& text,
                       bool active, float tracking) {
    const auto bf = b.toFloat();
    g.setColour(juce::Colour(0xff0d1017));
    g.fillRoundedRectangle(bf, 5.0f);
    if (active) {
        g.setColour(col::acA.withAlpha(0.07f));
        g.fillRoundedRectangle(bf, 5.0f);
    }
    g.setColour(active ? col::acA : col::line);
    g.drawRoundedRectangle(bf.reduced(0.5f), 5.0f, 1.0f);
    g.setColour(active ? col::acA : col::textDim);
    g.setFont(monoFont(8.0f));
    drawSpaced(g, text, b, tracking, juce::Justification::centred);
}

void StepSeqView::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    const bool playing = proc.isSeqPlaying();
    const int edit = proc.getEditPattern();
    const int sel = proc.getSelectedPad();

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

    // ---- head: pattern buttons + CHAIN toggle ----
    for (int i = 0; i < fable::DR_NPATTERNS; ++i)
        drawSeqBtn(g, patternBounds(i), kPatNames[i], edit == i, 0.0f);
    drawSeqBtn(g, chainToggleBounds(), "CHAIN", chaining_, 0.96f);

    // ---- head: chain readout ("CHAIN A→B" — ASCII arrow) ----
    juce::String readout("CHAIN ");
    const auto& chain = proc.getChain();
    for (size_t k = 0; k < chain.size(); ++k) {
        if (k > 0) readout << "->";
        const int p = chain[k];
        readout << (p >= 0 && p < fable::DR_NPATTERNS ? kPatNames[p] : "?");
    }
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, readout,
               { chainToggleBounds().getRight() + 8, kHeadY, 180, kHeadH }, 0.7f);

    // ---- head: EDITING <pad name> + tap hint, right-aligned ----
    auto right = getLocalBounds().withY(kHeadY).withHeight(kHeadH)
                     .withTrimmedRight(kPadX).withTrimmedLeft(560);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    // web "TAP STEP · ON → ACCENT → OFF" — ASCII substitutes (mono font glyphs)
    drawSpaced(g, "TAP STEP - ON -> ACCENT -> OFF", right.removeFromRight(180), 0.9f,
               juce::Justification::right);
    right.removeFromRight(7);
    const auto name = proc.getPadName(sel);
    const auto nameFont = monoFont(8.0f);
    const int nameW = juce::jmin(120, (int)std::ceil(nameFont.getStringWidthFloat(name)
                                          + 0.8f * (float)name.length()) + 14);
    const auto nameBox = right.removeFromRight(nameW).withSizeKeepingCentre(nameW, 17);
    g.setColour(juce::Colour(0xff0a0d13));                  // .dr-step-editing strong
    g.fillRoundedRectangle(nameBox.toFloat(), 4.0f);
    g.setColour(col::acA.withAlpha(0.18f));
    g.drawRoundedRectangle(nameBox.toFloat().reduced(0.5f), 4.0f, 1.0f);
    g.setColour(col::acA);
    g.setFont(nameFont);
    drawSpaced(g, name, nameBox, 0.8f, juce::Justification::centred);
    right.removeFromRight(7);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "EDITING", right, 0.9f, juce::Justification::right);

    // ---- step row ----
    const int curStep = proc.getCurrentStep(), curPat = proc.getCurrentPattern();
    for (int s = 0; s < fable::DR_STEPS; ++s) {
        const auto b = stepBounds(s).toFloat();
        const int v = proc.getStep(edit, sel, s);
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

        // .step-accent: 2px bar at top 5, inset 5 (amber when accented)
        const juce::Rectangle<float> ab(b.getX() + 5.0f, b.getY() + 5.0f,
                                        b.getWidth() - 10.0f, 2.0f);
        if (v == 2) {
            g.setColour(col::acB.withAlpha(0.55f));         // glow
            g.fillRoundedRectangle(ab.expanded(1.5f), 3.0f);
            g.setColour(col::acB);
        } else {
            g.setColour(col::acB.withAlpha(0.12f));
        }
        g.fillRoundedRectangle(ab, 2.0f);

        // .step-fill: inset 5, top 10, bottom 12 (cyan when on)
        const juce::Rectangle<float> fb(b.getX() + 5.0f, b.getY() + 10.0f,
                                        b.getWidth() - 10.0f, b.getHeight() - 22.0f);
        if (v >= 1) {
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
    }
}

} // namespace fui
