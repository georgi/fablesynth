#include "BassEditor.h"

// Web layout, measured from the running BL-1 app (src/bass/bass.css,
// #bass-rack at its 1460px max-width; rack-relative px):
//   rack              1460 x 931   (padding 14px 18px 22px)
//   header            (18,  14) 1424 x 80
//   #bl-editrow       (18, 103) 1424 x 243  cols 1.5fr 0.62fr 1.15fr 1.25fr, 9px gaps
//     OSC (18,464) SUB (491,192) FILTER (692,355) ENV (1056,386)
//   #bl-modrow        (18, 355) 1424 x 140  cols 290px 250px 1fr, 9px gaps
//     LFO (18,290) ACCENT (317,250) KEYS (576,866)
//   #bl-seq           (18, 504) 1424 x 276
//   #bl-fxrack        (18, 789) 1424 x 120

// ---- BassRack ----
BassRack::BassRack(BassAudioProcessor& p)
    : header(p), osc(p), sub(p), filter(p), env(p), lfo(p), accent(p), keys(p),
      seq(p), fxRack(p) {
    for (auto* c : std::initializer_list<juce::Component*>{
             &header, &osc, &sub, &filter, &env, &lfo, &accent, &keys, &seq, &fxRack })
        addAndMakeVisible(*c);
}

void BassRack::resized() {
    header.setBounds(18, 14, 1424, 80);
    osc.setBounds(18, 103, 464, 243);
    sub.setBounds(491, 103, 192, 243);
    filter.setBounds(692, 103, 355, 243);
    env.setBounds(1056, 103, 386, 243);
    lfo.setBounds(18, 355, 290, 140);
    accent.setBounds(317, 355, 250, 140);
    keys.setBounds(576, 355, 866, 140);
    seq.setBounds(18, 504, 1424, 276);
    fxRack.setBounds(18, 789, 1424, 120);
}

// ---- BassEditor ----
BassEditor::BassEditor(BassAudioProcessor& p)
    : juce::AudioProcessorEditor(p), rack(p) {
    setLookAndFeel(&lnf);
    addAndMakeVisible(rack);
    rack.setBounds(0, 0, BassRack::LW, BassRack::LH);

    setResizable(true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)BassRack::LW / BassRack::LH);
    setResizeLimits(840, (int)(840 * (double)BassRack::LH / BassRack::LW),
                    2100, (int)(2100 * (double)BassRack::LH / BassRack::LW));
    setSize(1200, (int)(1200 * (double)BassRack::LH / BassRack::LW));
}

BassEditor::~BassEditor() { setLookAndFeel(nullptr); }

void BassEditor::paint(juce::Graphics& g) {
    g.fillAll(fui::col::bg);
    // subtle top radial glow, like the web background
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11141d), getWidth() * 0.5f, -120.0f,
                                           fui::col::bg, getWidth() * 0.5f, getHeight() * 0.6f, true));
    g.fillRect(getLocalBounds());
}

void BassEditor::resized() {
    const float sc = juce::jmin(getWidth() / (float)BassRack::LW, getHeight() / (float)BassRack::LH);
    const float dx = (getWidth() - BassRack::LW * sc) * 0.5f;
    const float dy = (getHeight() - BassRack::LH * sc) * 0.5f;
    rack.setTransform(juce::AffineTransform::scale(sc).translated(dx, dy));
}
