#include "BassFxRack.h"
#include <cmath>

// Web layout (src/bass/bass.css): .fx-rack grid repeat(4, 1fr), gap 10px;
// .fx-group padding 8px 9px 7px, radius 8, head min-height 17px + 4px gap.
namespace fui {

// ===================== Group =====================
BassFxRack::Group::Group(BassAudioProcessor& p, const char* fx, const char* t,
                         const char* n, std::initializer_list<const char*> knobIds)
    : title(t), note(n), power(p.apvts, juce::String("fx.") + fx + ".on", Accent::N) {
    for (const char* k : knobIds)
        knobs.add(new Knob(p.apvts, juce::String("fx.") + fx + "." + k, Knob::Sm, Accent::N));
}

void BassFxRack::Group::layout(juce::Rectangle<int> r) {
    bounds = r;
    auto inner = r;
    inner.removeFromLeft(9);  inner.removeFromRight(9);
    inner.removeFromTop(8);   inner.removeFromBottom(7);
    auto head = inner.removeFromTop(17);
    power.setBounds(head.removeFromLeft(13).withSizeKeepingCentre(13, 13));
    head.removeFromLeft(7);
    titleArea = head;
    inner.removeFromTop(4);
    const int n = knobs.size();
    if (n == 0) return;
    const float cw = (float)inner.getWidth() / (float)n;
    const int kh = juce::jmin(inner.getHeight(), Knob::svgPx(Knob::Sm) + 13); // dia + label
    for (int i = 0; i < n; ++i)
        knobs[i]->setBounds((int)std::round(inner.getX() + i * cw), inner.getY(),
                            (int)std::round(cw), kh);
}

// .fx-group chrome: rgba(24,28,38,.72) -> rgba(9,12,18,.8) fill, line border.
static void drawGroupBox(juce::Graphics& g, juce::Rectangle<int> bounds) {
    const auto r = bounds.toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xb8181c26), r.getX(), r.getY(),
                                           juce::Colour(0xcc090c12), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
}

void BassFxRack::Group::paintGroup(juce::Graphics& g) {
    drawGroupBox(g, bounds);
    auto head = titleArea;
    if (note.isNotEmpty()) {                    // .bl-fx-note, right-aligned dim
        g.setColour(col::textDim);
        g.setFont(monoFont(6.5f));
        drawSpaced(g, note, head.removeFromRight(150), 0.8f, juce::Justification::right);
    }
    g.setColour(col::text);
    g.setFont(dispFont(8.0f));
    drawSpaced(g, title, head, 1.4f); // 0.18em tracking at 8px
}

// ===================== BassFxRack =====================
BassFxRack::BassFxRack(BassAudioProcessor& p) {
    struct Def {
        const char* fx; const char* title; const char* note;
        std::initializer_list<const char*> k;
    };
    const Def defs[] = {
        {"drive",  "DRIVE",  "POST-ACCENT",            {"amt", "mix"}},
        {"chorus", "CHORUS", "",                       {"rate", "depth", "mix"}},
        {"delay",  "DELAY",  "PING-PONG",              {"time", "fb", "mix"}},
        {"reverb", "REVERB", "NO COMP - ACCENTS LIVE", {"size", "mix"}},
    };
    for (const auto& d : defs) {
        auto* m = groups.add(new Group(p, d.fx, d.title, d.note, d.k));
        addAndMakeVisible(m->power);
        for (auto* k : m->knobs) addAndMakeVisible(*k);
    }
}

void BassFxRack::resized() {
    auto r = getLocalBounds().reduced(8);        // .bl-fx-panel padding
    const int gap = 10;
    const float cw = (r.getWidth() - gap * 3) / 4.0f;
    for (int i = 0; i < groups.size(); ++i)
        groups[i]->layout({ (int)std::round(r.getX() + i * (cw + gap)), r.getY(),
                            (int)std::round(cw), r.getHeight() });
}

void BassFxRack::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    for (auto* m : groups) m->paintGroup(g);
}

} // namespace fui
