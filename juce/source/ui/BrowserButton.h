#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// Flat button for the preset browser drawer and its BROWSE entry points. Header
// only, so the hosted SQ-4 build can compile a hidden entry point without the
// browser itself.
namespace fui {

inline juce::Path browserHeartPath(juce::Rectangle<float> r) {
    juce::Path p;
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    p.startNewSubPath(x + w * 0.5f, y + h);
    p.cubicTo(x - w * 0.2f, y + h * 0.5f, x + w * 0.1f, y - h * 0.2f, x + w * 0.5f, y + h * 0.28f);
    p.cubicTo(x + w * 0.9f, y - h * 0.2f, x + w * 1.2f, y + h * 0.5f, x + w * 0.5f, y + h);
    p.closeSubPath();
    return p;
}

class BrowserButton : public juce::Button {
public:
    enum Style { Chip, Fav, Action, Primary, Vary, Browse, Nav };
    BrowserButton(const juce::String& text, Style s) : juce::Button(text), style_(s) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void paintButton(juce::Graphics&, bool over, bool down) override;
    // A host that re-themes the drawer (SQ-4 track colour) sets this; default is the machine accent.
    void setAccent(juce::Colour c) { accent_ = c; repaint(); }
private:
    juce::Colour accentColour() const { return accent_.isTransparent() ? accentA() : accent_; }
    Style style_;
    juce::Colour accent_ = juce::Colours::transparentBlack;
};

inline void BrowserButton::paintButton(juce::Graphics& g, bool over, bool /*down*/) {
    const auto r = getLocalBounds().toFloat();
    const bool on = getToggleState(), en = isEnabled();
    const float radius = (style_ == Chip || style_ == Fav) ? 4.0f : 6.0f;
    juce::Colour fill = juce::Colours::transparentBlack, border = juce::Colours::white.withAlpha(0.14f), text = col::textHint;
    switch (style_) {
        case Chip:
            if (on) { fill = border = accentColour(); text = juce::Colours::black; }
            else if (over) text = col::text;
            break;
        case Fav:
            if (on) { fill = border = col::acB; text = juce::Colours::black; }
            else if (over) text = col::text;
            break;
        case Action:
            if (on) { border = text = col::acB; }
            else if (over && en) { text = col::text; border = col::textDim; }
            break;
        case Primary: fill = border = accentColour(); text = juce::Colours::black; break;
        case Nav: // leaves the browser: the navigation accent
            text = navAccent(); border = over && en ? navAccent() : navAccent().withAlpha(0.45f);
            break;
        case Vary: text = col::acF; border = col::acF.withAlpha(0.5f); break;
        case Browse:
            fill = juce::Colour(0xff11141c); border = col::line; text = col::text;
            if (on) border = text = accentColour();
            else if (over) border = accentColour();
            break;
    }
    const float alpha = en ? 1.0f : 0.35f;
    if (!fill.isTransparent()) { g.setColour(fill.withMultipliedAlpha(alpha)); g.fillRoundedRectangle(r, radius); }
    g.setColour(border.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    g.setColour(text.withMultipliedAlpha(alpha));

    auto area = getLocalBounds();
    if (style_ == Fav) {   // heart glyph + label
        const auto heart = juce::Rectangle<float>(0, 0, 9, 8).withCentre({ (float)area.getX() + 13.0f, r.getCentreY() });
        g.fillPath(browserHeartPath(heart));
        area.removeFromLeft(21);
        g.setFont(monoFont(11.0f));
        g.drawText(getButtonText(), area.withTrimmedRight(6), juce::Justification::centredLeft);
    } else if (style_ == Chip) {
        g.setFont(monoFont(11.0f));
        g.drawText(getButtonText(), area, juce::Justification::centred);
    } else {
        g.setFont(monoFont(10.0f));
        drawSpaced(g, getButtonText(), area, 1.2f, juce::Justification::centred);
    }
}

} // namespace fui
