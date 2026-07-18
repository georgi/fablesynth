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
    // Lighter than textDim, reserved for tiny hint/legend copy that must still
    // clear ~4.5:1 contrast on the panel background at 7-9px sizes (--text-hint).
    const juce::Colour textHint {0xff93a0b8};
    const juce::Colour acA      {0xff4de8ff}; // osc A  (cyan)
    const juce::Colour acB      {0xffffa14d}; // osc B  (amber)
    const juce::Colour acF      {0xffb18cff}; // filter (violet)
    const juce::Colour acN      {0xff9fb4d8}; // neutral
    const juce::Colour display  {0xff07090e}; // scope / view boxes
    const juce::Colour knobBody {0xff161b25};
    const juce::Colour ptr      {0xffe8edf7};
}

enum class Accent { A, B, F, N };

// Overridable primary accent — the web keys everything off --ac-a so one CSS
// override re-themes a whole machine (BL-1 sets acid green). A binary that
// wants a different accent A sets this from a static initialiser (same
// pattern as Controls' setParamInfoResolver); WT-1/DR-1 never touch it, so
// their cyan stays byte-identical.
inline juce::Colour& accentA() { static juce::Colour c = col::acA; return c; }

inline juce::Colour accentColour(Accent a) {
    switch (a) { case Accent::A: return accentA(); case Accent::B: return col::acB;
                 case Accent::F: return col::acF; case Accent::N: return col::acN; }
    return col::acN;
}

// Tint per MOD_SOURCES index — single home for the mod-source palette (mirrors
// the web's SOURCE_COLORS in params.ts). Index 0 (the "—" no-source) is
// transparent; LFO 1 cyan, LFO 2 amber, MOD ENV violet, VELO/NOTE slate.
// Out-of-range indices fall back to transparent.
inline juce::Colour modSourceColour(int srcIndex) {
    switch (srcIndex) {
        case 1:  return juce::Colour(0xff4de8ff);
        case 2:  return juce::Colour(0xffffa14d);
        case 3:  return juce::Colour(0xffb18cff);
        case 4:  return juce::Colour(0xff9fb4d8);
        case 5:  return juce::Colour(0xff9fb4d8);
        default: return juce::Colours::transparentBlack;
    }
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
        total += juce::GlyphArrangement::getStringWidth(f, s.substring(i, i + 1)) + tracking;
    total -= tracking;
    float x = (float)area.getX();
    const float areaWidth = (float)area.getWidth();
    if (just.testFlags(juce::Justification::horizontallyCentred)) x += (areaWidth - total) * 0.5f;
    else if (just.testFlags(juce::Justification::right))          x += areaWidth - total;
    for (int i = 0; i < s.length(); ++i) {
        auto ch = s.substring(i, i + 1);
        g.drawText(ch, juce::Rectangle<float>(x, (float)area.getY(), 40.0f, (float)area.getHeight()).toNearestInt(),
                   juce::Justification::centredLeft, false);
        x += juce::GlyphArrangement::getStringWidth(f, ch) + tracking;
    }
}

// Embedded typefaces (FableFonts binary data, visual-parity spec §1): the
// web app's Michroma display face and IBM Plex Mono 400/500/600. Guarded by
// __has_include so a target that doesn't link FableFonts still compiles and
// falls back to the old system-font stand-ins.
#if __has_include("FableFonts.h")
} // namespace fui  (BinaryData header must live outside our namespace)
#include "FableFonts.h"
namespace fui {
namespace detail {
inline juce::Typeface::Ptr embedded(const void* data, int size) {
    return juce::Typeface::createSystemTypefaceFor(data, (size_t) size);
}
inline juce::Typeface::Ptr michroma()      { static auto t = embedded(fablefonts::MichromaRegular_ttf,     fablefonts::MichromaRegular_ttfSize);     return t; }
inline juce::Typeface::Ptr plexRegular()   { static auto t = embedded(fablefonts::IBMPlexMonoRegular_ttf,  fablefonts::IBMPlexMonoRegular_ttfSize);  return t; }
inline juce::Typeface::Ptr plexMedium()    { static auto t = embedded(fablefonts::IBMPlexMonoMedium_ttf,   fablefonts::IBMPlexMonoMedium_ttfSize);   return t; }
inline juce::Typeface::Ptr plexSemiBold()  { static auto t = embedded(fablefonts::IBMPlexMonoSemiBold_ttf, fablefonts::IBMPlexMonoSemiBold_ttfSize); return t; }
} // namespace detail
#else
namespace detail {
inline juce::Typeface::Ptr michroma()     { return nullptr; }
inline juce::Typeface::Ptr plexRegular()  { return nullptr; }
inline juce::Typeface::Ptr plexMedium()   { return nullptr; }
inline juce::Typeface::Ptr plexSemiBold() { return nullptr; }
} // namespace detail
#endif

inline juce::Font monoFont(float h, bool bold = false) {
    if (auto tf = bold ? detail::plexSemiBold() : detail::plexRegular())
        return juce::Font(juce::FontOptions(tf).withHeight(h));
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h,
                                        bold ? juce::Font::bold : juce::Font::plain));
}
inline juce::Font monoFontMedium(float h) {
    if (auto tf = detail::plexMedium())
        return juce::Font(juce::FontOptions(tf).withHeight(h));
    return monoFont(h);
}
inline juce::Font dispFont(float h) {
    if (auto tf = detail::michroma())
        return juce::Font(juce::FontOptions(tf).withHeight(h));
    return juce::Font(juce::FontOptions(h, juce::Font::bold));
}

} // namespace fui
