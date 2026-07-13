#include "DrumFxRack.h"
#include "../dsp/DrumEngine.h"
#include <cmath>

// Web layout (src/drum/drum.css), rack-relative px:
//   #dr-fxrack panel  1424 x 131, .dr-fx-panel padding 8px.
//   .fx-rack grid: repeat(5, 1fr) 190px, gap 10px.
//   .fx-group padding 8px 9px 7px, radius 8, head min-height 17px + 4px gap.
//   .out-head margin-bottom 6px; .out-route rows 13px min-height, 2px gap,
//   grid 7px 38px 1fr with 5px gaps.
namespace fui {

// ===================== Group =====================
DrumFxRack::Group::Group(DrumUiModel& p, const char* fx, const char* t,
                         std::initializer_list<const char*> knobIds)
    : title(t), power(p.parameters(), juce::String("fx.") + fx + ".on", Accent::N) {
    for (const char* k : knobIds)
        knobs.add(new Knob(p.parameters(), juce::String("fx.") + fx + "." + k, Knob::Sm, Accent::N));
}

void DrumFxRack::Group::layout(juce::Rectangle<int> r) {
    bounds = r;
    auto inner = r;
    inner.removeFromLeft(9);  inner.removeFromRight(9);
    inner.removeFromTop(8);   inner.removeFromBottom(7);
    auto head = inner.removeFromTop(17);
    power.setBounds(head.removeFromLeft(13).withSizeKeepingCentre(13, 13));
    head.removeFromLeft(7);
    titleArea = head;
    inner.removeFromTop(4);
    // .fx-knobs: even columns, knobs aligned to the top (align-items flex-start)
    const int n = knobs.size();
    if (n == 0) return;
    const float cw = (float)inner.getWidth() / (float)n;
    const int kh = juce::jmin(inner.getHeight(), Knob::svgPx(Knob::Sm) + 13); // dia + label
    for (int i = 0; i < n; ++i)
        knobs[i]->setBounds((int)std::round(static_cast<float>(inner.getX()) + static_cast<float>(i) * cw), inner.getY(),
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

void DrumFxRack::Group::paintGroup(juce::Graphics& g) {
    drawGroupBox(g, bounds);
    g.setColour(col::text);
    g.setFont(dispFont(8.0f));
    drawSpaced(g, title, titleArea, 1.4f); // 0.18em tracking at 8px
}

// ===================== DrumFxRack =====================
DrumFxRack::DrumFxRack(DrumUiModel& p) : proc(p) {
    struct Def { const char* fx; const char* title; std::initializer_list<const char*> k; };
    const Def defs[] = {
        {"drive",  "DRIVE",  {"amt", "mix"}},
        {"comp",   "COMP",   {"thr", "gain"}},
        {"chorus", "CHORUS", {"rate", "depth", "mix"}},
        {"delay",  "DELAY",  {"time", "fb", "mix"}},
        {"reverb", "REVERB", {"size", "mix"}},
    };
    for (const auto& d : defs) {
        auto* m = groups.add(new Group(p, d.fx, d.title, d.k));
        addAndMakeVisible(m->power);
        for (auto* k : m->knobs) addAndMakeVisible(*k);
    }
    lastSig = routeSignature();
    startTimerHz(1); // OUT panel reflects live pad.out routing + renames
}

void DrumFxRack::resized() {
    auto r = getLocalBounds().reduced(8);        // .dr-fx-panel padding
    const int gap = 10, outW = 190;
    const float cw = static_cast<float>(r.getWidth() - outW - gap * 5) / 5.0f;
    for (int i = 0; i < groups.size(); ++i)
        groups[i]->layout({ (int)std::round(static_cast<float>(r.getX())
                                             + static_cast<float>(i) * (cw + static_cast<float>(gap))), r.getY(),
                            (int)std::round(cw), r.getHeight() });
    outBounds = { r.getRight() - outW, r.getY(), outW, r.getHeight() };
}

juce::String DrumFxRack::routeSignature() const {
    juce::String sig;
    for (int i = 0; i < fable::DR_NPADS; ++i) {
        auto* v = proc.parameters().parameter("pad" + juce::String(i) + ".out");
        sig << (v ? (int)std::lround(v->convertFrom0to1(v->getValue())) : 0) << ':' << proc.padName(i) << ';';
    }
    return sig;
}

void DrumFxRack::timerCallback() {
    auto sig = routeSignature();
    if (sig == lastSig) return;
    lastSig = sig;
    repaint(outBounds);
}

void DrumFxRack::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    for (auto* m : groups) m->paintGroup(g);
    paintOutPanel(g);
}

void DrumFxRack::paintOutPanel(juce::Graphics& g) {
    drawGroupBox(g, outBounds);
    auto inner = outBounds;
    inner.removeFromLeft(9);  inner.removeFromRight(9);
    inner.removeFromTop(8);   inner.removeFromBottom(7);

    // .out-head: OUT title left, routing hint right (space-between).
    auto head = inner.removeFromTop(17);
    g.setColour(col::text);
    g.setFont(dispFont(8.0f));
    drawSpaced(g, "OUT", head, 1.4f);
    g.setColour(col::textDim);
    g.setFont(monoFont(6.5f));
    drawSpaced(g, juce::String::fromUTF8("FX \xe2\x86\x92 MAIN ONLY"), head, 0.6f,
               juce::Justification::right);
    inner.removeFromTop(6);

    // Pad -> output assignments (params are live; the timer diffs for repaint).
    std::array<juce::StringArray, 5> assigned;
    for (int i = 0; i < fable::DR_NPADS; ++i) {
        auto* v = proc.parameters().parameter("pad" + juce::String(i) + ".out");
        const int out = juce::jlimit(0, 4, v ? (int)std::lround(v->convertFrom0to1(v->getValue())) : 0);
        assigned[(size_t)out].add(proc.padName(i));
    }

    for (int o = 0; o < 5; ++o) {
        auto row = inner.removeFromTop(13);
        inner.removeFromTop(2);
        auto dotCell  = row.removeFromLeft(7);  row.removeFromLeft(5);
        auto nameCell = row.removeFromLeft(38); row.removeFromLeft(5);

        const bool isMain = o == 0;
        const bool isAssigned = !isMain && !assigned[(size_t)o].isEmpty();
        const auto dot = dotCell.withSizeKeepingCentre(6, 6).toFloat();
        if (isMain || isAssigned) {
            const auto ac = isMain ? col::acA : col::acB;
            g.setColour(ac.withAlpha(0.35f));       // glow halo
            g.fillEllipse(dot.expanded(2.0f));
            g.setColour(ac);
        } else {
            g.setColour(juce::Colour(0xff232936));
        }
        g.fillEllipse(dot);

        g.setColour(isAssigned ? col::acB : isMain ? col::text : col::textDim);
        g.setFont(monoFont(7.0f, true));
        drawSpaced(g, juce::String(fable::OUT_NAMES[(size_t)o]), nameCell, 0.5f);

        const int n = assigned[(size_t)o].size();
        const juce::String val = isMain ? juce::String(n) + (n == 1 ? " PAD" : " PADS")
                                 : n > 0 ? assigned[(size_t)o].joinIntoString(", ")
                                         : juce::String::fromUTF8("\xe2\x80\x94");
        g.setColour(isAssigned ? col::acB : col::textDim);
        g.setFont(monoFont(6.5f));
        g.drawText(val, row, juce::Justification::centredRight, true); // ellipsis
    }
}

} // namespace fui
