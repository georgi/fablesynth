#include "Controls.h"
#include "Format.h"
#include <cmath>

namespace fui {

static constexpr double A0 = -135.0, A1 = 135.0; // knob sweep (degrees, 0 = up)
static float clamp01(float x) { return juce::jlimit(0.0f, 1.0f, x); }

// ======================= Knob =======================
Knob::Knob(juce::AudioProcessorValueTreeState& s, const juce::String& paramId,
           Size sz, Accent ac, bool showLbl)
    : apvts(s), id(paramId), accent(accentColour(ac)), size(sz), showLabel(showLbl) {
    param = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
    const auto& info = fable::paramInfo()[fable::idFromString(id.toStdString())];
    bipolar = info.min < 0;
    midNorm = fable::valueToNorm(info, 0.0f);
    label = juce::String(info.label).toUpperCase();
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    startTimerHz(30);
}

void Knob::timerCallback() { if (norm() != lastNorm) { lastNorm = norm(); repaint(); } }

void Knob::nudge(float d) {
    if (!param) return;
    param->setValueNotifyingHost(clamp01(param->getValue() + d));
    repaint();
}

void Knob::mouseDown(const juce::MouseEvent& e) {
    if (param) param->beginChangeGesture();
    lastY = e.position.y;
}
void Knob::mouseDrag(const juce::MouseEvent& e) {
    float dy = lastY - e.position.y;
    lastY = e.position.y;
    nudge(dy * (e.mods.isShiftDown() ? 0.0008f : 0.005f));
}
void Knob::mouseUp(const juce::MouseEvent&) { if (param) param->endChangeGesture(); }
void Knob::mouseDoubleClick(const juce::MouseEvent&) {
    if (param) param->setValueNotifyingHost(param->getDefaultValue());
}
void Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    nudge((w.deltaY > 0 ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.005f : 0.03f));
}

void Knob::paint(juce::Graphics& g) {
    const float avail = (float)juce::jmin(getWidth(), getHeight() - (showLabel ? 13 : 0));
    const float dia = juce::jmin(avail, (float)svgPx(size));
    const float cx = getWidth() * 0.5f, cy = dia * 0.5f + 1.0f;
    const float scale = dia / 80.0f;
    const float bodyR = 26 * scale, arcR = 33 * scale, ptrLen = 23 * scale;
    const float n = clamp01(norm());

    auto degToRad = [](double d) { return (float)(d * juce::MathConstants<double>::pi / 180.0); };

    // body
    g.setColour(col::knobBody);
    g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawEllipse(cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2, 1.5f * scale);

    // track
    juce::Path track;
    track.addCentredArc(cx, cy, arcR, arcR, 0, degToRad(A0), degToRad(A1), true);
    g.setColour(juce::Colours::white.withAlpha(0.09f));
    g.strokePath(track, juce::PathStrokeType(5 * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    double deg = A0 + (A1 - A0) * n;
    double from = bipolar ? A0 + (A1 - A0) * midNorm : A0;
    if (std::abs(deg - from) > 0.01) {
        juce::Path arc;
        arc.addCentredArc(cx, cy, arcR, arcR, 0, degToRad(from), degToRad(deg), true);
        g.setColour(accent.withAlpha(0.35f));
        g.strokePath(arc, juce::PathStrokeType(8 * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent);
        g.strokePath(arc, juce::PathStrokeType(5 * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // pointer
    float rad = degToRad(deg);
    juce::Point<float> tip(cx + ptrLen * std::sin(rad), cy - ptrLen * std::cos(rad));
    g.setColour(col::ptr);
    g.drawLine({ {cx, cy}, tip }, 4 * scale);

    // label / value
    if (showLabel) {
        auto labelArea = juce::Rectangle<int>(0, (int)(dia) - 1, getWidth(), 12);
        g.setFont(monoFont(8.0f));
        if (dragging) {
            g.setColour(col::text);
            g.drawText(formatParam(id, param ? param->convertFrom0to1(n) : 0.0f), labelArea,
                       juce::Justification::centred);
        } else {
            g.setColour(col::textDim);
            drawSpaced(g, label, labelArea, 1.2f, juce::Justification::centred);
        }
    }
}

// ======================= Stepper =======================
Stepper::Stepper(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, Accent ac)
    : apvts(s), id(paramId), accent(accentColour(ac)) {
    choice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id));
    auto styleBtn = [this](juce::TextButton& b, int dir) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b.setColour(juce::TextButton::textColourOffId, col::textDim);
        b.onClick = [this, dir] { step(dir); };
        addAndMakeVisible(b);
    };
    styleBtn(prev, -1);
    styleBtn(next, +1);
    startTimerHz(20);
}
int Stepper::choiceCount() const {
    if (countProvider) return juce::jmax(1, countProvider());
    return choice ? choice->choices.size() : 1;
}
juce::String Stepper::displayName() const {
    if (!choice) return "-";
    if (nameProvider) return nameProvider(choice->getIndex());
    return choice->getCurrentChoiceName();
}
void Stepper::timerCallback() { if (choice && choice->getIndex() != lastIndex) { lastIndex = choice->getIndex(); repaint(); } }
void Stepper::step(int d) {
    if (!choice) return;
    int n = choiceCount();
    int idx = ((choice->getIndex() % n) + d + n) % n;
    choice->setValueNotifyingHost(choice->convertTo0to1((float)idx));
    repaint();
}
void Stepper::resized() {
    auto r = getLocalBounds();
    prev.setBounds(r.removeFromLeft(18));
    next.setBounds(r.removeFromRight(18));
}
void Stepper::paint(juce::Graphics& g) {
    auto r = getLocalBounds().reduced(20, 0);
    g.setColour(juce::Colour(0xff0a0d13));
    g.fillRoundedRectangle(r.toFloat(), 4.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.0f, 1.0f);
    g.setColour(accent);
    g.setFont(monoFont(10.0f));
    drawSpaced(g, displayName(), r, 0.8f, juce::Justification::centred);
}

// ======================= PowerButton =======================
PowerButton::PowerButton(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, Accent ac)
    : apvts(s), id(paramId), accent(accentColour(ac)) {
    param = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    startTimerHz(20);
}
bool PowerButton::isOn() const { return param && param->getValue() > 0.5f; }
void PowerButton::timerCallback() { if (isOn() != last) { last = isOn(); repaint(); } }
void PowerButton::mouseDown(const juce::MouseEvent&) {
    if (param) param->setValueNotifyingHost(isOn() ? 0.0f : 1.0f);
    repaint();
}
void PowerButton::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat().withSizeKeepingCentre(15, 15);
    if (isOn()) {
        g.setColour(accent.withAlpha(0.5f));
        g.fillEllipse(b.expanded(3));
        g.setGradientFill(juce::ColourGradient(accent.brighter(0.5f), b.getCentreX(), b.getY(),
                                               accent.darker(0.4f), b.getCentreX(), b.getBottom(), false));
        g.fillEllipse(b);
        g.setColour(accent.withAlpha(0.6f));
        g.drawEllipse(b, 1.0f);
    } else {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a3140), b.getCentreX(), b.getY(),
                                               juce::Colour(0xff12151d), b.getCentreX(), b.getBottom(), false));
        g.fillEllipse(b);
        g.setColour(juce::Colours::white.withAlpha(0.14f));
        g.drawEllipse(b, 1.0f);
    }
}

// ======================= VSlider =======================
VSlider::VSlider(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, Accent ac,
                 std::function<float()> ghostFn)
    : apvts(s), id(paramId), accent(accentColour(ac)), ghost(std::move(ghostFn)) {
    param = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    startTimerHz(30);
}
void VSlider::timerCallback() {
    float n = param ? param->getValue() : 0.0f, gh = ghost ? ghost() : -1.0f;
    if (n != lastNorm || gh != lastGhost) { lastNorm = n; lastGhost = gh; repaint(); }
}
juce::Rectangle<float> VSlider::trackArea() const {
    return getLocalBounds().toFloat().withTrimmedBottom(14)
        .withSizeKeepingCentre(9, (float)getHeight() - 14);
}
void VSlider::moveTo(float y) {
    auto t = trackArea();
    if (param) param->setValueNotifyingHost(clamp01(1.0f - (y - t.getY()) / t.getHeight()));
    repaint();
}
void VSlider::mouseDown(const juce::MouseEvent& e) { if (param) param->beginChangeGesture(); moveTo(e.position.y); }
void VSlider::mouseDrag(const juce::MouseEvent& e) { moveTo(e.position.y); }
void VSlider::mouseUp(const juce::MouseEvent&) { if (param) param->endChangeGesture(); }
void VSlider::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    if (param) { param->setValueNotifyingHost(clamp01(param->getValue() + (w.deltaY > 0 ? 0.03f : -0.03f))); repaint(); }
}
void VSlider::paint(juce::Graphics& g) {
    auto t = trackArea();
    g.setColour(juce::Colour(0xff0a0d13));
    g.fillRoundedRectangle(t, 5.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(t.reduced(0.5f), 5.0f, 1.0f);

    float n = param ? param->getValue() : 0.0f;
    float fillTop = t.getBottom() - n * t.getHeight();
    g.setColour(accent.withAlpha(0.55f));
    g.fillRoundedRectangle({ t.getX(), fillTop, t.getWidth(), t.getBottom() - fillTop }, 4.0f);

    // modulated ghost marker
    float gh = ghost ? ghost() : -1.0f;
    if (gh >= 0) {
        float gy = t.getBottom() - gh * t.getHeight();
        g.setColour(accent);
        g.fillRect(t.getX() - 4, gy - 1, t.getWidth() + 8, 2.0f);
    }

    // handle
    float hy = t.getBottom() - n * t.getHeight();
    juce::Rectangle<float> handle(t.getCentreX() - 11, hy - 4.5f, 22, 9);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2b3344), handle.getCentreX(), handle.getY(),
                                           juce::Colour(0xff161b25), handle.getCentreX(), handle.getBottom(), false));
    g.fillRoundedRectangle(handle, 3.0f);
    g.setColour(accent.withAlpha(0.55f));
    g.drawRoundedRectangle(handle, 3.0f, 1.0f);

    // POS label
    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    drawSpaced(g, "POS", getLocalBounds().removeFromBottom(12), 1.0f, juce::Justification::centred);
}

} // namespace fui
