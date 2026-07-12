#include "SeqHeader.h"
#include "../../ui/Controls.h"

#include <array>
#include <cmath>

namespace fui {

// The SQ-4 host-param resolver for fui controls (Task 9's SeqEditor.cpp
// installed this as a placeholder; SeqHeader now owns the header, so it owns
// the install too) — same pattern as bassInfoLookup/drumInfoLookup.
namespace {
const fable::ParamInfo* seqInfoLookup(const std::string& pid) {
    for (const auto& d : fable::seqParamInfo())
        if (d.pid == pid) return &d;
    return nullptr;
}
const bool g_seqResolverInstalled = [] {
    setParamInfoResolver(&seqInfoLookup);
    return true;
}();
} // namespace

// The JUCE default font has no reliable glyph coverage for the web's
// ▶ / ❚❚ / ■ / ◂ / ▸ symbols (and the headless snapshot test renders with
// whatever fonts the CI box has) — ASCII stand-ins, same call BassHeader made
// for the middle dot in its voice-mode line.
namespace {
constexpr const char* kPlayGlyph = "PLAY", *kPauseGlyph = "PAUSE", *kStopGlyph = "STOP";
constexpr const char* kPrevGlyph = "<", *kNextGlyph = ">";
} // namespace

SeqHeader::SeqHeader(SeqAudioProcessor& p) : proc(p) { startTimerHz(30); }

// ---- actions (also the test handles) ---------------------------------------

void SeqHeader::playClick() { proc.setPaused(!proc.paused()); repaint(); }
void SeqHeader::stopAllClick() { proc.conductor().stopAll(); }

void SeqHeader::quantStep(int d) {
    proc.conductor().cycleQuant(d);
    // Mirror the new value into the "quant" APVTS param so hosts can see it.
    // v1 stays one-directional (header -> conductor -> APVTS): host-driven
    // quant automation is not wired back into the conductor (Task 10 brief).
    if (auto* param = proc.apvts.getParameter("quant")) {
        const int idx = (int)proc.conductor().quant();
        param->setValueNotifyingHost(param->convertTo0to1((float)idx));
    }
    repaint();
}

// ---- value sources -----------------------------------------------------------

float SeqHeader::swingValue() const { return (float)proc.conductor().swing(); }

juce::RangedAudioParameter* SeqHeader::volParam() const {
    return dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter("master"));
}
float SeqHeader::volValue() const {
    auto* p = volParam();
    return p ? p->getValue() : 0.75f;
}

// ---- mouse -------------------------------------------------------------------

void SeqHeader::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (playBtn.contains(pos))            playClick();
    else if (stopBtn.contains(pos))       stopAllClick();
    else if (quantPrevBtn.contains(pos))  quantStep(-1);
    else if (quantNextBtn.contains(pos))  quantStep(+1);
    else if (swingKnob.contains(pos))     { dragging_ = Drag::Swing; lastY_ = e.position.y; }
    else if (volKnob.contains(pos)) {
        dragging_ = Drag::Vol;
        lastY_ = e.position.y;
        if (auto* p = volParam()) p->beginChangeGesture();
    }
}

void SeqHeader::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ == Drag::None) return;
    const float dy = lastY_ - e.position.y;
    lastY_ = e.position.y;
    const float delta = dy * (e.mods.isShiftDown() ? 0.0008f : 0.005f);
    if (dragging_ == Drag::Swing) {
        proc.conductor().setSwing(juce::jlimit(0.0f, 1.0f, swingValue() + delta));
    } else if (dragging_ == Drag::Vol) {
        if (auto* p = volParam())
            p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + delta));
    }
    repaint();
}

void SeqHeader::mouseUp(const juce::MouseEvent&) {
    if (dragging_ == Drag::Vol) { if (auto* p = volParam()) p->endChangeGesture(); }
    dragging_ = Drag::None;
}

void SeqHeader::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (swingKnob.contains(pos)) {
        proc.conductor().setSwing(0.0);
    } else if (volKnob.contains(pos)) {
        if (auto* p = volParam()) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->getDefaultValue());
            p->endChangeGesture();
        }
    }
    repaint();
}

// ---- layout --------------------------------------------------------------

void SeqHeader::resized() {
    auto r = getLocalBounds().reduced(16, 8); // content row, ~1392 x 50

    logoArea = r.removeFromLeft(190);
    r.removeFromLeft(20);

    playBtn = r.removeFromLeft(56).withSizeKeepingCentre(56, 32);
    r.removeFromLeft(6);
    stopBtn = r.removeFromLeft(52).withSizeKeepingCentre(52, 32);
    r.removeFromLeft(20);

    quantTagArea = r.removeFromLeft(40).withSizeKeepingCentre(40, 16);
    r.removeFromLeft(4);
    quantPrevBtn = r.removeFromLeft(18).withSizeKeepingCentre(18, 18);
    r.removeFromLeft(4);
    quantValArea = r.removeFromLeft(56).withSizeKeepingCentre(56, 20);
    r.removeFromLeft(4);
    quantNextBtn = r.removeFromLeft(18).withSizeKeepingCentre(18, 18);
    r.removeFromLeft(20);

    auto clockArea = r.removeFromLeft(130);
    beatsArea = clockArea.removeFromTop(clockArea.getHeight() / 2);
    clockLineArea = clockArea;

    auto knobsArea = r.removeFromRight(90).withSizeKeepingCentre(90, 44);
    swingKnob = knobsArea.removeFromLeft(45).withSizeKeepingCentre(40, 44);
    volKnob = knobsArea.withSizeKeepingCentre(40, 44);
    r.removeFromRight(14);
    scopeArea = r.removeFromRight(190).withSizeKeepingCentre(190, 46);
}

// ---- paint -----------------------------------------------------------------

void SeqHeader::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    // logo: FABLE (white) SEQ (amber) SQ-4 (dim tag) -- web .sq-logo
    auto lb = logoArea;
    g.setFont(dispFont(15.0f));
    g.setColour(col::text);
    drawSpaced(g, "FABLE", lb.removeFromLeft(60), 1.2f);
    g.setColour(col::acB);
    drawSpaced(g, "SEQ", lb.removeFromLeft(46), 1.2f);
    lb.removeFromLeft(10);
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    drawSpaced(g, "SQ-4", lb, 2.5f);

    paintButtons(g);
    paintQuant(g);
    paintClock(g);
    paintScope(g);

    paintKnob(g, swingKnob, "SWING", swingValue());
    paintKnob(g, volKnob, "VOL", volValue());
}

void SeqHeader::paintButtons(juce::Graphics& g) {
    const bool playing = !proc.paused();
    auto drawBtn = [&](juce::Rectangle<int> r, const juce::String& txt, bool on) {
        auto rf = r.toFloat();
        g.setColour(on ? juce::Colour(0xff0e3120) : juce::Colour(0xff11141c));
        g.fillRoundedRectangle(rf, 7.0f);
        g.setColour(on ? accentA().withAlpha(0.55f) : col::line);
        g.drawRoundedRectangle(rf.reduced(0.5f), 7.0f, 1.0f);
        g.setColour(on ? accentA() : col::text);
        g.setFont(monoFont(10.0f));
        g.drawText(txt, r, juce::Justification::centred);
    };
    drawBtn(playBtn, playing ? kPauseGlyph : kPlayGlyph, playing);
    drawBtn(stopBtn, kStopGlyph, false);
}

void SeqHeader::paintQuant(juce::Graphics& g) {
    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    drawSpaced(g, "QUANT", quantTagArea, 1.6f);

    auto drawStep = [&](juce::Rectangle<int> r, const char* txt) {
        g.setColour(juce::Colour(0xff11141c));
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
        g.setColour(col::line);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.0f, 1.0f);
        g.setColour(col::textDim);
        g.setFont(monoFont(9.0f));
        g.drawText(txt, r, juce::Justification::centred);
    };
    drawStep(quantPrevBtn, kPrevGlyph);
    drawStep(quantNextBtn, kNextGlyph);

    const auto q = proc.conductor().quant();
    const char* label = q == fable::Quant::Bar ? "1 BAR" : q == fable::Quant::Quarter ? "1/4" : "OFF";
    g.setColour(juce::Colour(0xff0a0d13));
    g.fillRoundedRectangle(quantValArea.toFloat(), 4.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(quantValArea.toFloat().reduced(0.5f), 4.0f, 1.0f);
    g.setColour(col::acB);
    g.setFont(monoFont(10.0f));
    g.drawText(label, quantValArea, juce::Justification::centred);
}

void SeqHeader::paintClock(juce::Graphics& g) {
    const bool playing = !proc.paused();
    const auto pos = proc.conductor().songPos();

    auto r = beatsArea;
    const int dotSize = 8, gap = 5;
    for (int i = 0; i < 4; ++i) {
        auto d = r.removeFromLeft(dotSize).withSizeKeepingCentre(dotSize, dotSize);
        r.removeFromLeft(gap);
        const bool on = playing && pos.beat == i;
        g.setColour(on ? accentA() : juce::Colour(0xff232936));
        g.fillEllipse(d.toFloat());
        if (on) { g.setColour(accentA().withAlpha(0.5f)); g.fillEllipse(d.toFloat().expanded(2.0f)); }
    }

    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    juce::String bar = "BAR " + juce::String(pos.bar).paddedLeft('0', 2);
    juce::String bpm = juce::String((int)std::lround(proc.conductor().session().bpm)) + " BPM";
    g.drawText(bar + " - " + bpm, clockLineArea, juce::Justification::centredLeft);
}

void SeqHeader::paintScope(juce::Graphics& g) {
    drawDisplayBox(g, scopeArea.toFloat());

    constexpr int N = 512;
    std::array<float, N> buf;
    proc.readScope(buf.data(), N);

    const float w = (float)scopeArea.getWidth(), h = (float)scopeArea.getHeight();
    const float x0 = (float)scopeArea.getX(), y0 = (float)scopeArea.getY();
    juce::Path path;
    for (int i = 0; i < N; ++i) {
        const float x = x0 + (i / (float)(N - 1)) * w;
        const float y = y0 + h * 0.5f - buf[(size_t)i] * h * 0.46f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(accentA().withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(1.2f));

    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "SUM", scopeArea.reduced(6, 3).removeFromTop(10), 2.0f,
               juce::Justification::right);
}

void SeqHeader::paintKnob(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, float v) {
    const float d = 22.0f;
    auto circle = juce::Rectangle<float>(0, 0, d, d).withCentre(
        juce::Point<float>((float)r.getCentreX(), (float)r.getY() + d * 0.5f));

    const float a0 = -135.0f, a1 = 135.0f;
    const float deg = a0 + (a1 - a0) * juce::jlimit(0.0f, 1.0f, v);
    // JUCE angles are clockwise from 12 o'clock, which already puts -135/+135
    // at the lower-left/lower-right "knob rest" positions -- no extra offset.
    auto toRad = [](float degv) { return juce::degreesToRadians(degv); };

    g.setColour(juce::Colour(0xff161b25));
    g.fillEllipse(circle);
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawEllipse(circle, 1.5f);

    const float rr = d * 0.5f - 2.0f;
    const juce::Point<float> c = circle.getCentre();
    juce::Path track, arc;
    track.addCentredArc(c.x, c.y, rr, rr, 0.0f, toRad(a0), toRad(a1), true);
    arc.addCentredArc(c.x, c.y, rr, rr, 0.0f, toRad(a0), toRad(deg), true);
    g.setColour(juce::Colours::white.withAlpha(0.09f));
    g.strokePath(track, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accentA());
    g.strokePath(arc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Point<float> tip = c.getPointOnCircumference(rr, toRad(deg));
    g.setColour(col::ptr);
    g.drawLine({ c, tip }, 1.6f);

    auto labelArea = r.withY(r.getY() + (int)d + 2).withHeight(10);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    g.drawText(label, labelArea, juce::Justification::centred);
}

} // namespace fui
