#include "DrumEditor.h"

// Web layout, measured from the running DR-1 app (src/drum/drum.css,
// #drum-rack at its 1460px max-width; rack-relative px):
//   rack              1460 x 880   (padding 14px 18px 22px)
//   header            (18,  14) 1424 x 80
//   dr-main           (18, 103) 1424 x 501 = left 352px column + right, 9px gap
//     #dr-pads        (18, 103)  352 x 369
//     #dr-padstrip    (18, 481)  352 x 119
//     #dr-selbar      (379,103) 1063 x 31
//     #dr-oscrow      (379,143) 1063 x 243
//     #dr-editrow     (379,395) 1063 x 209
//   #dr-stepseq       (18, 613) 1424 x 105
//   #dr-fxrack        (18, 727) 1424 x 131

// ---- DrumRack ----
void DrumRack::Placeholder::paint(juce::Graphics& g) {
    fui::drawPanel(g, getLocalBounds().toFloat());
    g.setColour(fui::col::textDim);
    g.setFont(fui::monoFont(9.0f));
    fui::drawSpaced(g, label, getLocalBounds(), 2.0f, juce::Justification::centred);
}

DrumRack::DrumRack(DrumAudioProcessor& p) : header(p), pads(p), padStrip(p) {
    addAndMakeVisible(header);
    addAndMakeVisible(pads);
    addAndMakeVisible(padStrip);
    for (auto* c : { &selBar, &oscRow, &editRow, &stepSeq, &fxRack })
        addAndMakeVisible(*c);
}

void DrumRack::resized() {
    header.setBounds(18, 14, 1424, 80);
    pads.setBounds(18, 103, 352, 369);
    padStrip.setBounds(18, 481, 352, 119);
    selBar.setBounds(379, 103, 1063, 31);
    oscRow.setBounds(379, 143, 1063, 243);
    editRow.setBounds(379, 395, 1063, 209);
    stepSeq.setBounds(18, 613, 1424, 105);
    fxRack.setBounds(18, 727, 1424, 131);
}

// ---- DrumEditor ----
DrumEditor::DrumEditor(DrumAudioProcessor& p)
    : juce::AudioProcessorEditor(p), rack(p) {
    setLookAndFeel(&lnf);
    setWantsKeyboardFocus(true); // QWERTY pad map (PadGrid key-listens on us)
    addAndMakeVisible(rack);
    rack.setBounds(0, 0, DrumRack::LW, DrumRack::LH);

    setResizable(true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)DrumRack::LW / DrumRack::LH);
    setResizeLimits(840, (int)(840 * (double)DrumRack::LH / DrumRack::LW),
                    2100, (int)(2100 * (double)DrumRack::LH / DrumRack::LW));
    setSize(1200, (int)(1200 * (double)DrumRack::LH / DrumRack::LW));
}

DrumEditor::~DrumEditor() { setLookAndFeel(nullptr); }

void DrumEditor::paint(juce::Graphics& g) {
    g.fillAll(fui::col::bg);
    // subtle top radial glow, like the web background
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11141d), getWidth() * 0.5f, -120.0f,
                                           fui::col::bg, getWidth() * 0.5f, getHeight() * 0.6f, true));
    g.fillRect(getLocalBounds());
}

void DrumEditor::resized() {
    const float sc = juce::jmin(getWidth() / (float)DrumRack::LW, getHeight() / (float)DrumRack::LH);
    const float dx = (getWidth() - DrumRack::LW * sc) * 0.5f;
    const float dy = (getHeight() - DrumRack::LH * sc) * 0.5f;
    rack.setTransform(juce::AffineTransform::scale(sc).translated(dx, dy));
}
