#include "MasterFxView.h"
#include "../../ui/Theme.h"

namespace fui {

MasterLimiterView::MasterLimiterView(MasterFxUiModel& model)
    : model_(model), power_(model.parameters(), "fx.limiter.on", Accent::N),
      ceiling_(model.parameters(), "fx.limiter.ceiling", Knob::Sm, Accent::N) {
    addAndMakeVisible(power_);
    addAndMakeVisible(ceiling_);
    ceiling_.setLabelText("CEILING");
    startTimerHz(30);
}

void MasterLimiterView::resized() {
    auto r = getLocalBounds().reduced(12, 10);
    auto head = r.removeFromTop(24);
    power_.setBounds(head.removeFromLeft(18).withSizeKeepingCentre(14, 14));
    r.removeFromTop(6);
    plot_ = r.removeFromTop(juce::jmax(72, r.getHeight() - 86));
    readouts_ = r.removeFromTop(24);
    ceiling_.setBounds(r.withSizeKeepingCentre(72, 64));
}

void MasterLimiterView::timerCallback() {
    history_.push_back(model_.limiterTelemetry());
    while (history_.size() > 90)
        history_.pop_front();
    repaint();
}

void MasterLimiterView::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    g.setColour(col::text); g.setFont(dispFont(12));
    g.drawText("LIMITER", 34, 10, 130, 24, juce::Justification::centredLeft);
    g.setColour(col::textDim); g.setFont(monoFont(8));
    g.drawText(power_.isOn() ? "LINKED STEREO · LIVE PEAK" : "BYPASS", getWidth() - 220, 10, 208, 24,
               juce::Justification::centredRight);

    drawDisplayBox(g, plot_.toFloat());
    const float left = (float)plot_.getX() + 28, right = (float)plot_.getRight() - 12;
    const float top = (float)plot_.getY() + 14, bottom = (float)plot_.getBottom() - 24;
    const auto y = [top, bottom](float db) { return top + (1.0f - juce::jlimit(0.0f, 1.0f, (db + 36.0f) / 36.0f)) * (bottom - top); };
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f }) {
        g.setColour(col::acN.withAlpha(.12f)); g.drawLine(left, y(db), right, y(db), .8f);
        g.setColour(col::acN); g.setFont(monoFont(8));
        g.drawText(juce::String((int)db), (int)left - 24, (int)y(db) - 7, 18, 14, juce::Justification::right);
    }
    auto source = model_.parameters();
    float ceilingDb = -1.0f;
    if (auto* p = source.parameter("fx.limiter.ceiling"))
        ceilingDb = p->convertFrom0to1(p->getValue());
    g.setColour(col::text.withAlpha(power_.isOn() ? .8f : .25f));
    for (float x = left; x < right; x += 7)
        g.drawLine(x, y(ceilingDb), juce::jmin(x + 3.0f, right), y(ceilingDb), .8f);
    g.setColour(col::acN); g.drawText("CEIL", (int)right - 32, (int)y(ceilingDb) - 14, 32, 12,
                                      juce::Justification::right);
    if (history_.size() > 1) {
        const auto x = [this, left, right](size_t i) {
            return right - (float)(history_.size() - 1 - i) / 89.0f * (right - left);
        };
        for (int signal = 0; signal < 2; ++signal) {
            juce::Path path;
            for (size_t i = 0; i < history_.size(); ++i) {
                const auto pt = juce::Point<float>(x(i), y(history_[i][(size_t)signal]));
                if (i == 0) path.startNewSubPath(pt); else path.lineTo(pt);
            }
            g.setColour(signal == 0 ? col::acN.withAlpha(.5f) : col::text);
            g.strokePath(path, juce::PathStrokeType(signal == 0 ? 1.0f : 1.5f));
        }
    }
    const auto values = history_.empty() ? std::array<float, 3>{ -90.0f, -90.0f, 0.0f } : history_.back();
    g.setColour(col::text); g.setFont(monoFont(8));
    const juce::String readings[] = { "IN  " + juce::String(values[0], 1) + " dB",
                                      "OUT  " + juce::String(values[1], 1) + " dB",
                                      "GR  " + juce::String(power_.isOn() ? values[2] : 0.0f, 1) + " dB" };
    for (int i = 0; i < 3; ++i)
        g.drawText(readings[i], readouts_.withX(readouts_.getX() + i * readouts_.getWidth() / 3)
                      .withWidth(readouts_.getWidth() / 3), juce::Justification::centred);
    g.setColour(col::acN); g.setFont(monoFont(8));
    g.drawText("POST COMP · CEILING AFTER LEVEL MATCH", getLocalBounds().withTrimmedTop(getHeight() - 20),
               juce::Justification::centred);
}

MasterFxView::MasterFxView(SeqAudioProcessor& proc)
    : model_(proc), eq_(model_, FxModuleView::Eq, false), ott_(model_, FxModuleView::Ott, false),
      comp_(model_, FxModuleView::Comp, false), limiter_(model_) {
    for (auto* c : { static_cast<juce::Component*>(&eq_), static_cast<juce::Component*>(&ott_),
                     static_cast<juce::Component*>(&comp_), static_cast<juce::Component*>(&limiter_) })
        addAndMakeVisible(c);
}

void MasterFxView::paint(juce::Graphics& g) {
    g.setColour(col::text); g.setFont(dispFont(12));
    drawSpaced(g, "MASTER FX", { 2, 0, 180, 24 }, 2.0f);
    g.setColour(col::textDim); g.setFont(monoFont(8));
    drawSpaced(g, "POST-FADER  ›  EQ  ›  OTT  ›  COMP  ›  LIMITER", { 190, 0, getWidth() - 192, 24 }, 1.0f,
               juce::Justification::right);
}

void MasterFxView::resized() {
    auto r = getLocalBounds(); r.removeFromTop(28);
    constexpr int gap = 10;
    auto top = r.removeFromTop((r.getHeight() - gap) * 3 / 5);
    const int w = (top.getWidth() - 2 * gap) / 3;
    eq_.setBounds(top.removeFromLeft(w)); top.removeFromLeft(gap);
    ott_.setBounds(top.removeFromLeft(w)); top.removeFromLeft(gap);
    comp_.setBounds(top);
    r.removeFromTop(gap);
    limiter_.setBounds(r.withSizeKeepingCentre(440, r.getHeight()));
}

} // namespace fui
