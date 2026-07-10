#include "DrumEditor.h"

// Web layout, measured from the running DR-1 app (src/drum/drum.css,
// #drum-rack at its 1460px max-width; rack-relative px):
//   rack              1460 x 880   (padding 14px 18px 22px)
//   header            (18,  14) 1424 x 80
//   dr-main           (18, 103) 1424 x 501 = left 352px column + right, 9px gap
//     #dr-pads        (18, 103)  352 x 369
//     #dr-padstrip    (18, 481)  352 x 119
//     #dr-selbar      (379,103) 1063 x 31
//     #dr-oscrow      (379,143) 1063 x 243  cols 1fr 1fr 196px, 9px gaps
//       OSC A (379,143,424) OSC B (812,143,425) NOISE (1246,143,196)
//     #dr-editrow     (379,395) 1063 x 209  cols 1fr 1.15fr 1.15fr 1.3fr, 9px gaps
//       PITCH (379,395,225) AMP (613,395,259) FLT (881,395,259) MOD (1149,395,293)
//   #dr-stepseq       (18, 613) 1424 x 105
//   #dr-fxrack        (18, 727) 1424 x 131

// ---- DrumRack ----
DrumRack::DrumRack(DrumAudioProcessor& p)
    : header(p), pads(p), padStrip(p), oscA(p, 0), oscB(p, 1), noise(p),
      pitchEnv(p), ampEnv(p), filter(p), mod(p), selBar(p), stepSeq(p), fxRack(p) {
    addAndMakeVisible(header);
    addAndMakeVisible(pads);
    addAndMakeVisible(padStrip);
    for (auto* c : std::initializer_list<juce::Component*>{
             &oscA, &oscB, &noise, &pitchEnv, &ampEnv, &filter, &mod,
             &selBar, &stepSeq })
        addAndMakeVisible(*c);
    addAndMakeVisible(fxRack);
}

void DrumRack::resized() {
    header.setBounds(18, 14, 1424, 80);
    pads.setBounds(18, 103, 352, 369);
    padStrip.setBounds(18, 481, 352, 119);
    selBar.setBounds(379, 103, 1063, 31);
    oscA.setBounds(379, 143, 424, 243);
    oscB.setBounds(812, 143, 425, 243);
    noise.setBounds(1246, 143, 196, 243);
    pitchEnv.setBounds(379, 395, 225, 209);
    ampEnv.setBounds(613, 395, 259, 209);
    filter.setBounds(881, 395, 259, 209);
    mod.setBounds(1149, 395, 293, 209);
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
