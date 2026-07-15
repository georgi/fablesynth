#include "NoteSeqView.h"
#include <cmath>

namespace fui {

using fable::NoteSeqStep;

static const char* const kPatNames[fable::SEQ_NPATTERNS] = { "1", "2", "3", "4" };

// ---- geometry (index.css .ns-*, rack-relative px) -----------------------------
// panel padding 8px 12px 10px; head 24px + 10px margin; body: legend 36px +
// 8px gap, 16 columns with 5px gaps, then the clock column (border-left,
// 6px padding). Column: 143px of lanes (12 cells, 1px gaps), oct 5+20,
// acc 4+14, tie 4+14, step number 4+10.

static constexpr int kPadX = 12, kHeadY = 8, kHeadH = 24;
static constexpr int kBodyY = kHeadY + kHeadH + 10;
static constexpr int kLegendW = 36, kLegendGap = 8;
static constexpr int kLanesH = 143;
static constexpr float kColGap = 5.0f;
static constexpr int kOctY = kLanesH + 5, kOctH = 20;
static constexpr int kAccY = kOctY + kOctH + 4, kAccH = 14;
static constexpr int kTieY = kAccY + kAccH + 4, kTieH = 14;
static constexpr int kNumY = kTieY + kTieH + 4;
static constexpr int kClockW = 60, kClockGap = 6;

NoteSeqView::NoteSeqView(WtUiModel& m)
    : model(m),
      root_(m.parameters(), "seq.root", Accent::A) {
    setInterceptsMouseClicks(true, true);
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

juce::Rectangle<int> NoteSeqView::tieBounds(int step) const {
    const auto c = colBounds(step);
    return { c.getX(), c.getY() + kTieY, c.getWidth(), kTieH };
}

// ---- store handlers ----------------------------------------------------------

void NoteSeqView::toggleCell(int step, int note) {
    const int pat = model.editPattern();
    NoteSeqStep cur = model.sequenceStep(pat, step);
    if (cur.on && cur.note == note) {              // tap again = rest
        cur.on = false; cur.acc = false; cur.tie = false;
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

void NoteSeqView::toggleStepTie(int step) {
    const int pat = model.editPattern();
    NoteSeqStep cur = model.sequenceStep(pat, step);
    if (!cur.on) return;
    cur.tie = !cur.tie;
    model.setSequenceStep(pat, step, cur);
    repaint();
}

// noteseq.ts randomPattern: a sparse minor line with occasional octave throws,
// accents and legato ties — melodic flavor rather than BL-1's acid crawl.
void NoteSeqView::randomize() {
    static const int pool[] = { 0, 0, 2, 3, 5, 7, 10 };
    const int pat = model.editPattern();
    for (int i = 0; i < fable::SEQ_STEPS; ++i) {
        NoteSeqStep s;
        s.on = rng_.nextFloat() < 0.62f;
        s.note = pool[rng_.nextInt(7)];
        s.oct = rng_.nextFloat() < 0.18f ? (rng_.nextFloat() < 0.5f ? -1 : 1) : 0;
        s.acc = s.on && rng_.nextFloat() < 0.22f;
        s.tie = s.on && i > 0 && rng_.nextFloat() < 0.25f;
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

void NoteSeqView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    const auto caps = model.capabilities();
    if (!model.hasTargetClip()) {
        if (randBounds().contains(pos)) model.createTargetClip();
        return;
    }
    if (caps.ownsTransport && transportBounds().contains(pos)) {       // play/stop
        model.setSequencerPlaying(!model.sequencerPlaying());
        repaint();
        return;
    }
    for (int i = 0; i < juce::jmin(fable::SEQ_NPATTERNS, model.clipBars()); ++i)
        if (patternBounds(i).contains(pos)) { patternClick(i); return; }
    if (caps.supportsPatternChain && sequenceLengthBounds().contains(pos)) {
        const auto length = sequenceLengthBounds();
        const int bars = caps.hosted ? model.clipBars() : (int)model.chain().size();
        if (pos.x < length.getX() + 38) setSequenceLength(bars - 1);
        else if (pos.x >= length.getRight() - 38) setSequenceLength(bars + 1);
        return;
    }
    if (randBounds().contains(pos)) { randomize(); return; }
    for (int s = 0; s < fable::SEQ_STEPS; ++s) {
        if (!colBounds(s).contains(pos)) continue;
        for (int note = 0; note < fable::SEQ_NOTE_LANES; ++note)
            if (cellBounds(s, note).contains(pos)) { toggleCell(s, note); return; }
        if (octBounds(s).contains(pos)) { cycleStepOct(s); return; }
        if (accBounds(s).contains(pos)) { toggleStepAcc(s); return; }
        if (tieBounds(s).contains(pos)) { toggleStepTie(s); return; }
        return;
    }
}

// ---- animation ----------------------------------------------------------------

// Repaint only when something visible changed: transport/playhead, edit
// pattern, chain, chaining mode, host sync, or the pattern content itself.
void NoteSeqView::timerCallback() {
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
        mix((st.on ? 1 : 0) | (st.acc ? 2 : 0) | (st.tie ? 4 : 0));
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
    for (int i = 0; i < juce::jmin(fable::SEQ_NPATTERNS, model.clipBars()); ++i)
        drawSeqBtn(g, patternBounds(i), kPatNames[i], edit == i, 0.0f,
                   bars > 1 && currentBar == i);
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
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    // web "TAP LANE = NOTE · TIE HOLDS FROM PREV STEP — GLIDE MAKES IT SLIDE"
    drawSpaced(g, "TAP LANE = NOTE - TIE HOLDS FROM PREV STEP - GLIDE MAKES IT SLIDE",
               { hintRight - 420, kHeadY, 420, kHeadH }, 0.9f,
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
        g.setColour(cyan);
        g.drawText("TIE",  legend.withHeight(kTieH).translated(0, kTieY), juce::Justification::centredRight);
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

        // note lanes (.ns-cell / .ns-cell.on / .ns-cell.on.acc)
        for (int note = 0; note < fable::SEQ_NOTE_LANES; ++note) {
            const auto b = cellBounds(s, note).toFloat();
            const bool active = st.on && st.note == note;
            if (active) {
                if (st.acc)
                    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffaef2ff), b.getX(), b.getY(),
                                                           juce::Colour(0xff2fa8c4), b.getX(), b.getBottom(), false));
                else
                    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3fd0e8), b.getX(), b.getY(),
                                                           juce::Colour(0xff1a7c94), b.getX(), b.getBottom(), false));
            } else {
                g.setColour(note == 0 ? juce::Colour(0xff0c1016) : juce::Colour(0xff0a0d13));
            }
            g.fillRoundedRectangle(b, 2.0f);
            g.setColour(active ? cyan.withAlpha(0.5f) : juce::Colours::white.withAlpha(0.045f));
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

        // accent / tie buttons (.ns-acc-btn / .ns-tie-btn)
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
        drawToggle(tieBounds(s), st.on && st.tie,
                   juce::Colour(0xff7de6ff), juce::Colour(0xff2293b4));

        // step number (.ns-step-num)
        const auto c = colBounds(s);
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        g.drawText(juce::String(s + 1), c.withY(c.getY() + kNumY).withHeight(10),
                   juce::Justification::centredTop, false);

        // playhead cursor (.ns-step-cursor)
        if (playing && curStep == s && curPat == edit) {
            const auto cf = juce::Rectangle<float>((float)c.getX() - 2, (float)c.getY() - 2,
                                                   (float)c.getWidth() + 4, (float)(kTieY + kTieH) + 4);
            g.setColour(cyan.withAlpha(0.85f));
            g.drawRoundedRectangle(cf, 6.0f, 1.0f);
            g.setColour(cyan.withAlpha(0.18f));
            g.drawRoundedRectangle(cf.expanded(1.5f), 7.0f, 3.0f);
        }
    }

    // ---- tie connectors, drawn INTO a step from its predecessor ----
    // (noteseq.ts tiesInto: cur.on && cur.tie && prev.on)
    const float laneH = kLanesH / (float)fable::SEQ_NOTE_LANES;
    auto yOf = [&](const NoteSeqStep& st) {
        return static_cast<float>(kBodyY)
             + (static_cast<float>(fable::SEQ_NOTE_LANES - 1 - st.note) + 0.5f) * laneH
             - static_cast<float>(st.oct) * 4.0f;
    };
    g.setColour(cyan.withAlpha(0.75f));
    for (int i = 1; i < fable::SEQ_STEPS; ++i) {
        if (!fable::seqTiesInto(steps[i - 1], steps[i])) continue;
        const auto c0 = colBounds(i - 1), c1 = colBounds(i);
        g.drawLine((float)c0.getCentreX(), yOf(steps[i - 1]),
                   (float)c1.getCentreX(), yOf(steps[i]), 1.4f);
    }
}

} // namespace fui
