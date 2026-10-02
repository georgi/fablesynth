#include "DrumFxRack.h"
#include "../dsp/DrumEngine.h"
#include <cmath>

// Web layout (src/drum/drum.css), rack-relative px:
//   #dr-fxrack panel  1424 x 131, .dr-fx-panel padding 8px.
//   .fx-rack modules share the row with a 10px gap.
//   .fx-group padding 8px 9px 7px, radius 8, head min-height 17px + 4px gap.
namespace fui {

// ===================== Group =====================
DrumFxRack::Group::Group(DrumUiModel& p, const juce::String& prefix, const char* fx, const char* t,
                         std::initializer_list<const char*> knobIds)
    : title(t), power(p.parameters(), prefix + "fx." + fx + ".on", Accent::N) {
    for (const char* k : knobIds)
        knobs.add(new Knob(p.parameters(), prefix + "fx." + fx + "." + k, Knob::Sm, Accent::N));
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
DrumFxRack::DrumFxRack(DrumUiModel& p, bool routingOnly) : proc(p), routingOnly_(routingOnly) {
    proc.selectionChanges().addChangeListener(this);
    if (!routingOnly_) {
        auto styleScope = [this](juce::TextButton& button) {
            button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
            button.setColour(juce::TextButton::buttonOnColourId, col::acA.withAlpha(0.22f));
            button.setColour(juce::TextButton::textColourOffId, col::textDim);
            button.setColour(juce::TextButton::textColourOnId, col::acA);
            button.setClickingTogglesState(true);
            addAndMakeVisible(button);
        };
        styleScope(padFxButton_); styleScope(groupFxButton_);
        padFxButton_.setToggleState(true, juce::dontSendNotification);
        padFxButton_.onClick = [this] { groupMode_ = false; padFxButton_.setToggleState(true, juce::dontSendNotification); groupFxButton_.setToggleState(false, juce::dontSendNotification); rebuild(); };
        groupFxButton_.onClick = [this] { groupMode_ = true; groupFxButton_.setToggleState(true, juce::dontSendNotification); padFxButton_.setToggleState(false, juce::dontSendNotification); rebuild(); };
    }
    rebuild();
}

DrumFxRack::~DrumFxRack() {
    proc.selectionChanges().removeChangeListener(this);
}

void DrumFxRack::changeListenerCallback(juce::ChangeBroadcaster*) {
    rebuild();
}

void DrumFxRack::rebuild() {
    groups.clear();
    outSelector.reset();
    if (routingOnly_) {
        const auto id = "pad" + juce::String(proc.selectedPad()) + ".out";
        outSelector = std::make_unique<Stepper>(proc.parameters(), id, Accent::A);
        addAndMakeVisible(*outSelector);
        resized();
        repaint();
        return;
    }
    struct Def { const char* fx; const char* title; std::initializer_list<const char*> k; };
    const Def defs[] = {
        {"drive",  "DRIVE",  {"amt", "mix"}},
        {"comp",   "COMP",   {"thr", "gain"}},
        {"chorus", "CHORUS", {"rate", "depth", "mix"}},
        {"delay",  "DELAY",  {"time", "fb", "mix"}},
        {"reverb", "REVERB", {"size", "mix"}},
        {"ott",    "OTT",    {"depth", "time", "up", "down"}},
    };
    const juce::String prefix = groupMode_ ? "" : "pad" + juce::String(proc.selectedPad()) + ".";
    for (const auto& d : defs) {
        auto* m = groups.add(new Group(proc, prefix, d.fx, d.title, d.k));
        addAndMakeVisible(m->power);
        for (auto* k : m->knobs) addAndMakeVisible(*k);
    }
    resized();
    repaint();
}

void DrumFxRack::resized() {
    if (routingOnly_) {
        auto row = getLocalBounds().reduced(12, 8);
        padTitleArea = row.removeFromLeft(142);
        row.removeFromLeft(8);
        if (outSelector)
            outSelector->setBounds(row.withSizeKeepingCentre(row.getWidth(), 24));
        return;
    }
    auto r = getLocalBounds().reduced(8);        // .dr-fx-panel padding
    auto scope = r.removeFromTop(16);
    padFxButton_.setBounds(scope.removeFromLeft(58)); scope.removeFromLeft(3);
    groupFxButton_.setBounds(scope.removeFromLeft(66));
    r.removeFromTop(2);
    const int gap = 10, count = groups.size();
    const float cw = static_cast<float>(r.getWidth() - gap * (count - 1)) / static_cast<float>(count);
    for (int i = 0; i < groups.size(); ++i)
        groups[i]->layout({ (int)std::round(static_cast<float>(r.getX())
                                             + static_cast<float>(i) * (cw + static_cast<float>(gap))), r.getY(),
                            (int)std::round(cw), r.getHeight() });
}

void DrumFxRack::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    if (routingOnly_) { paintOutSelector(g); return; }
    g.setColour(col::acA);
    g.setFont(dispFont(8.0f));
    drawSpaced(g, groupMode_ ? "DRUM GROUP FX" : "PAD " + juce::String(proc.selectedPad() + 1).paddedLeft('0', 2) + " FX",
               { 145, 2, 120, 12 }, 1.4f);
    for (auto* m : groups) m->paintGroup(g);
}

void DrumFxRack::paintOutSelector(juce::Graphics& g) {
    auto labels = padTitleArea;
    auto title = labels.removeFromLeft(38);
    g.setColour(col::text);
    g.setFont(dispFont(8.0f));
    drawSpaced(g, "OUT", title, 1.4f);
    g.setColour(col::textDim);
    g.setFont(monoFont(6.5f));
    drawSpaced(g, "PAD " + juce::String(proc.selectedPad() + 1).paddedLeft('0', 2)
                      + " " + proc.padName(proc.selectedPad()),
               labels, 0.5f, juce::Justification::centredLeft);
}

} // namespace fui
