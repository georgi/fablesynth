#include "PitchSeqView.h"
#include <cmath>

namespace fui {

using fable::BassSeqStep;

static const char* const kPatNames[fable::BL_NPATTERNS] = { "A", "B", "C", "D" };

// ---- geometry (bass.css .bl-seq-*, rack-relative px) --------------------------
// panel padding 9px 12px 11px; head 28px + 10px margin; body: legend 36px +
// 8px gap, then 16 columns with 5px gaps. Column: 143px of lanes (12 cells,
// 1px gaps), oct 5+20, acc 4+14, sld 4+14, step number 4+10.

static constexpr int kPadX = 12, kHeadY = 9, kHeadH = 28;
static constexpr int kBodyY = kHeadY + kHeadH + 10;
static constexpr int kLegendW = 36, kLegendGap = 8;
static constexpr int kLanesH = 143;
static constexpr float kColGap = 5.0f;
static constexpr int kOctY = kLanesH + 5, kOctH = 20;
static constexpr int kAccY = kOctY + kOctH + 4, kAccH = 14;
static constexpr int kSldY = kAccY + kAccH + 4, kSldH = 14;
static constexpr int kNumY = kSldY + kSldH + 4;

PitchSeqView::PitchSeqView(BassAudioProcessor& p) : proc(p) {
    setInterceptsMouseClicks(true, false);
    startTimerHz(30);
}

juce::Rectangle<int> PitchSeqView::transportBounds() const {
    return { kPadX, kHeadY + 2, 34, 24 };            // .bl-transport 34x24
}

juce::Rectangle<int> PitchSeqView::patternBounds(int i) const {
    const int x0 = kPadX + 34 + 12 + 96 + 12;        // transport, gap, title, gap
    return { x0 + i * (26 + 5), kHeadY + 2, 26, 24 }; // .bl-pattern 26x24, gap 5
}

juce::Rectangle<int> PitchSeqView::chainToggleBounds() const {
    return { patternBounds(3).getRight() + 12, kHeadY + 2, 108, 24 };
}

juce::Rectangle<int> PitchSeqView::randBounds() const {
    return { chainToggleBounds().getRight() + 12, kHeadY + 2, 52, 24 };
}

juce::Rectangle<int> PitchSeqView::colBounds(int step) const {
    const int gx = kPadX + kLegendW + kLegendGap;
    const int gw = getWidth() - kPadX - gx;
    const float w = ((float)gw - 15.0f * kColGap) / 16.0f;
    const float x = gx + step * (w + kColGap);
    return juce::Rectangle<float>(x, (float)kBodyY, w, (float)(kNumY + 10)).toNearestInt();
}

juce::Rectangle<int> PitchSeqView::cellBounds(int step, int note) const {
    const auto c = colBounds(step);
    // 12 lanes over kLanesH with 1px gaps, note 11 on top (web lane order)
    const float ch = (kLanesH - 11.0f) / 12.0f;
    const int r = fable::BL_NOTE_LANES - 1 - note;
    const float y = c.getY() + r * (ch + 1.0f);
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

// ---- store handlers ----------------------------------------------------------

void PitchSeqView::toggleCell(int step, int note) {
    const int pat = proc.getEditPattern();
    BassSeqStep cur = proc.getSeqStep(pat, step);
    if (cur.on && cur.note == note) {              // tap again = rest
        cur.on = false; cur.acc = false; cur.slide = false;
    } else {
        cur.on = true; cur.note = note;
    }
    proc.setSeqStep(pat, step, cur);
    repaint();
}

void PitchSeqView::cycleStepOct(int step) {
    const int pat = proc.getEditPattern();
    BassSeqStep cur = proc.getSeqStep(pat, step);
    cur.oct = cur.oct >= fable::BL_OCT_MAX ? fable::BL_OCT_MIN : cur.oct + 1; // seq.ts cycleOct
    proc.setSeqStep(pat, step, cur);
    repaint();
}

void PitchSeqView::toggleStepAcc(int step) {
    const int pat = proc.getEditPattern();
    BassSeqStep cur = proc.getSeqStep(pat, step);
    if (!cur.on) return;
    cur.acc = !cur.acc;
    proc.setSeqStep(pat, step, cur);
    repaint();
}

void PitchSeqView::toggleStepSlide(int step) {
    const int pat = proc.getEditPattern();
    BassSeqStep cur = proc.getSeqStep(pat, step);
    if (!cur.on) return;
    cur.slide = !cur.slide;
    proc.setSeqStep(pat, step, cur);
    repaint();
}

// seq.ts randomPattern: sparse minor-pentatonic line with occasional octave
// throws, accents and slides.
void PitchSeqView::randomize() {
    static const int pool[] = { 0, 0, 0, 3, 5, 7, 10 };
    const int pat = proc.getEditPattern();
    for (int i = 0; i < fable::BL_STEPS; ++i) {
        BassSeqStep s;
        s.on = rng_.nextFloat() < 0.6f;
        s.note = pool[rng_.nextInt(7)];
        s.oct = rng_.nextFloat() < 0.2f ? (rng_.nextFloat() < 0.5f ? -1 : 1) : 0;
        s.acc = s.on && rng_.nextFloat() < 0.25f;
        s.slide = s.on && i > 0 && rng_.nextFloat() < 0.22f;
        proc.setSeqStep(pat, i, s);
    }
    repaint();
}

void PitchSeqView::patternClick(int i) {
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

void PitchSeqView::setChaining(bool on) {
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

void PitchSeqView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (transportBounds().contains(pos)) {       // play/stop
        proc.setSeqPlaying(!proc.isSeqPlaying());
        repaint();
        return;
    }
    for (int i = 0; i < fable::BL_NPATTERNS; ++i)
        if (patternBounds(i).contains(pos)) { patternClick(i); return; }
    if (chainToggleBounds().contains(pos)) { setChaining(!chaining_); return; }
    if (randBounds().contains(pos)) { randomize(); return; }
    for (int s = 0; s < fable::BL_STEPS; ++s) {
        if (!colBounds(s).contains(pos)) continue;
        for (int note = 0; note < fable::BL_NOTE_LANES; ++note)
            if (cellBounds(s, note).contains(pos)) { toggleCell(s, note); return; }
        if (octBounds(s).contains(pos))   { cycleStepOct(s); return; }
        if (accBounds(s).contains(pos))   { toggleStepAcc(s); return; }
        if (slideBounds(s).contains(pos)) { toggleStepSlide(s); return; }
        return;
    }
}

// ---- animation ----------------------------------------------------------------

// Repaint only when something visible changed: transport/playhead, edit
// pattern, chain, chaining mode, or the pattern content itself.
void PitchSeqView::timerCallback() {
    juce::uint32 sig = 17;
    auto mix = [&sig](int v) { sig = sig * 31u + (juce::uint32)(v + 2); };
    const bool playing = proc.isSeqPlaying();
    const int edit = proc.getEditPattern();
    mix(playing ? 1 : 0);
    mix(playing ? proc.getCurrentStep() : -1);
    mix(proc.getCurrentPattern());
    mix(edit); mix(chaining_ ? 1 : 0);
    const auto& chain = proc.getChain();
    mix((int)chain.size());
    for (int p : chain) mix(p);
    for (int s = 0; s < fable::BL_STEPS; ++s) {
        const BassSeqStep st = proc.getSeqStep(edit, s);
        mix((st.on ? 1 : 0) | (st.acc ? 2 : 0) | (st.slide ? 4 : 0));
        mix(st.note); mix(st.oct);
    }
    if (sig != lastSig_) { lastSig_ = sig; repaint(); }
}

// ---- paint ----------------------------------------------------------------------

// .bl-seq-btn: 5px radius, #11141c, dim mono text; active = green border/text
// over a dark green wash (#0e3122).
static void drawSeqBtn(juce::Graphics& g, juce::Rectangle<int> b, const juce::String& text,
                       bool active, float tracking) {
    const auto bf = b.toFloat();
    const juce::Colour green = accentA();
    g.setColour(active ? juce::Colour(0xff0e3122) : juce::Colour(0xff11141c));
    g.fillRoundedRectangle(bf, 5.0f);
    g.setColour(active ? green.withAlpha(0.55f) : col::line);
    g.drawRoundedRectangle(bf.reduced(0.5f), 5.0f, 1.0f);
    g.setColour(active ? green : col::textDim);
    g.setFont(monoFont(8.0f));
    drawSpaced(g, text, b, tracking, juce::Justification::centred);
}

void PitchSeqView::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    const juce::Colour green = accentA();
    // .bl-seq-section carries a faint green border + glow
    g.setColour(green.withAlpha(0.16f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 12.0f, 1.0f);

    const bool playing = proc.isSeqPlaying();
    const int edit = proc.getEditPattern();

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

    // ---- head: pattern buttons + CHAIN + RAND ----
    for (int i = 0; i < fable::BL_NPATTERNS; ++i)
        drawSeqBtn(g, patternBounds(i), kPatNames[i], edit == i, 0.0f);
    juce::String chainLabel("CHAIN ");
    const auto& chain = proc.getChain();
    for (size_t k = 0; k < chain.size(); ++k) {
        if (k > 0) chainLabel << ">";
        const int p = chain[k];
        chainLabel << (p >= 0 && p < fable::BL_NPATTERNS ? kPatNames[p] : "?");
    }
    drawSeqBtn(g, chainToggleBounds(), chainLabel, chaining_, 0.5f);
    drawSeqBtn(g, randBounds(), "RAND", false, 0.7f);

    // ---- head: hint, right-aligned (.bl-seq-hint) ----
    g.setColour(col::textDim);
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
    const int curStep = proc.getCurrentStep(), curPat = proc.getCurrentPattern();
    BassSeqStep steps[fable::BL_STEPS];
    for (int s = 0; s < fable::BL_STEPS; ++s) steps[s] = proc.getSeqStep(edit, s);

    for (int s = 0; s < fable::BL_STEPS; ++s) {
        const BassSeqStep& st = steps[s];

        // note lanes (.bl-cell)
        for (int note = 0; note < fable::BL_NOTE_LANES; ++note) {
            const auto b = cellBounds(s, note).toFloat();
            const bool active = st.on && st.note == note;
            if (active) {
                if (st.acc)
                    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffa4ffd0), b.getX(), b.getY(),
                                                           juce::Colour(0xff2fbf76), b.getX(), b.getBottom(), false));
                else
                    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3ddc8c), b.getX(), b.getY(),
                                                           juce::Colour(0xff1a8f57), b.getX(), b.getBottom(), false));
            } else {
                g.setColour(note == 0 ? juce::Colour(0xff0c1016) : juce::Colour(0xff0a0d13));
            }
            g.fillRoundedRectangle(b, 2.0f);
            g.setColour(active ? green.withAlpha(0.5f) : juce::Colours::white.withAlpha(0.045f));
            g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);
        }

        // octave button (.bl-oct-btn)
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

    // ---- slide connectors, drawn INTO a step from its predecessor ----
    // (seq.ts slidesInto: cur.on && cur.slide && prev.on)
    const float laneH = kLanesH / (float)fable::BL_NOTE_LANES;
    auto yOf = [&](const BassSeqStep& st) {
        return kBodyY + (fable::BL_NOTE_LANES - 1 - st.note + 0.5f) * laneH - st.oct * 4.0f;
    };
    g.setColour(green.withAlpha(0.75f));
    for (int i = 1; i < fable::BL_STEPS; ++i) {
        if (!(steps[i].on && steps[i].slide && steps[i - 1].on)) continue;
        const auto c0 = colBounds(i - 1), c1 = colBounds(i);
        g.drawLine((float)c0.getCentreX(), yOf(steps[i - 1]),
                   (float)c1.getCentreX(), yOf(steps[i]), 1.4f);
    }
}

} // namespace fui
