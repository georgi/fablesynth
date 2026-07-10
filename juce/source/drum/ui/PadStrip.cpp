#include "PadStrip.h"

namespace fui {

PadStrip::PadStrip(DrumAudioProcessor& p) : proc(p) {
    proc.selectionBroadcaster.addChangeListener(this);
    rebuild();
}

PadStrip::~PadStrip() {
    proc.selectionBroadcaster.removeChangeListener(this);
}

void PadStrip::changeListenerCallback(juce::ChangeBroadcaster*) {
    rebuild();   // new pad selected -> rebind all six controls by param id
}

void PadStrip::rebuild() {
    const auto pre = "pad" + juce::String(proc.getSelectedPad()) + ".";
    choke = std::make_unique<Stepper>(proc.apvts, pre + "choke", Accent::A);
    out   = std::make_unique<Stepper>(proc.apvts, pre + "out",   Accent::A);
    addAndMakeVisible(*choke);
    addAndMakeVisible(*out);
    knobs.clear();
    for (const char* id : { "lvl", "pan", "v2l", "v2m" })
        addAndMakeVisible(knobs.add(new Knob(proc.apvts, pre + id, Knob::Sm, Accent::A)));
    resized();
    repaint();   // head shows the pad number
}

void PadStrip::resized() {
    // .panel padding 8px 12px 10px; head row with the steppers pushed right
    // (.padstrip-steppers), then the 4-knob row (.padstrip-knobs).
    auto r = getLocalBounds();
    r.removeFromLeft(12);  r.removeFromRight(12);
    r.removeFromTop(8);    r.removeFromBottom(10);

    headArea = r.removeFromTop(18);
    auto head = headArea;
    if (out) {
        out->setBounds(head.removeFromRight(84).withSizeKeepingCentre(84, 18));
        outLabel = head.removeFromRight(26);
        head.removeFromRight(8);
    }
    if (choke) {
        choke->setBounds(head.removeFromRight(84).withSizeKeepingCentre(84, 18));
        chokeLabel = head.removeFromRight(40);
    }

    r.removeFromTop(6);
    const int cw = r.getWidth() / juce::jmax(1, knobs.size());
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds(juce::Rectangle<int>(r.getX() + i * cw, r.getY(), cw, r.getHeight())
                                .withSizeKeepingCentre(48, juce::jmin(62, r.getHeight())));
}

void PadStrip::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    // h2 "PAD <nn>" (accent a) — the web shows "PAD"; the pad number makes the
    // rebound target explicit since JUCE has no DOM to inspect.
    g.setColour(col::acA);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, "PAD " + juce::String(proc.getSelectedPad() + 1).paddedLeft('0', 2),
               headArea, 2.2f);

    // .st-label captions for the steppers
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "CHOKE", chokeLabel, 1.0f);
    drawSpaced(g, "OUT", outLabel, 1.0f);
}

} // namespace fui
