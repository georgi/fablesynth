#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/Params.h"

// Shared palette + drawing helpers — a 1:1 transcription of the web app's CSS
// custom properties (src/index.css :root) and the machined-dark panel styling.
namespace fui {

namespace col {
    const juce::Colour bg       {0xff06070b};
    const juce::Colour panelHi  {0xff181c26};
    const juce::Colour panelLo  {0xff0d1017};
    const juce::Colour line     {juce::Colours::white.withAlpha(0.07f)};
    const juce::Colour text     {0xffdfe6f3};
    const juce::Colour textDim  {0xff6b768c};
    const juce::Colour acA      {0xff4de8ff}; // osc A  (cyan)
    const juce::Colour acB      {0xffffa14d}; // osc B  (amber)
    const juce::Colour acF      {0xffb18cff}; // filter (violet)
    const juce::Colour acN      {0xff9fb4d8}; // neutral
    const juce::Colour display  {0xff07090e}; // scope / view boxes
    const juce::Colour knobBody {0xff161b25};
    const juce::Colour ptr      {0xffe8edf7};
}

enum class Accent { A, B, F, N };
inline juce::Colour accentColour(Accent a) {
    switch (a) { case Accent::A: return col::acA; case Accent::B: return col::acB;
                 case Accent::F: return col::acF; default: return col::acN; }
}

// Panel: rounded rect, vertical gradient + top inner highlight + drop shadow.
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 12.0f) {
    juce::DropShadow(juce::Colours::black.withAlpha(0.4f), 18, {0, 8}).drawForRectangle(
        g, r.toNearestInt());
    g.setGradientFill(juce::ColourGradient(col::panelHi, r.getX(), r.getY(),
                                           col::panelLo, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, radius);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawHorizontalLine((int)r.getY() + 1, r.getX() + radius, r.getRight() - radius);
    g.setColour(col::line);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

// Inset display box (scope, wavetable, env/filter/lfo views).
inline void drawDisplayBox(juce::Graphics& g, juce::Rectangle<float> r, float radius = 9.0f) {
    g.setColour(col::display);
    g.fillRoundedRectangle(r, radius);
    g.setColour(col::line);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

// Text with manual letter-spacing (the web uses CSS letter-spacing on the
// Michroma display font; JUCE has no native tracking, so we advance per glyph).
inline void drawSpaced(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> area,
                       float tracking, juce::Justification just = juce::Justification::centredLeft) {
    const auto f = g.getCurrentFont();
    float total = 0;
    for (int i = 0; i < s.length(); ++i)
        total += f.getStringWidthFloat(s.substring(i, i + 1)) + tracking;
    total -= tracking;
    float x = (float)area.getX();
    if (just.testFlags(juce::Justification::horizontallyCentred)) x += (area.getWidth() - total) * 0.5f;
    else if (just.testFlags(juce::Justification::right))          x += area.getWidth() - total;
    for (int i = 0; i < s.length(); ++i) {
        auto ch = s.substring(i, i + 1);
        g.drawText(ch, juce::Rectangle<float>(x, (float)area.getY(), 40.0f, (float)area.getHeight()).toNearestInt(),
                   juce::Justification::centredLeft, false);
        x += f.getStringWidthFloat(ch) + tracking;
    }
}

inline juce::Font monoFont(float h, bool bold = false) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h,
                                        bold ? juce::Font::bold : juce::Font::plain));
}
inline juce::Font dispFont(float h) {
    return juce::Font(juce::FontOptions(h, juce::Font::bold)); // stand-in for Michroma
}

} // namespace fui
