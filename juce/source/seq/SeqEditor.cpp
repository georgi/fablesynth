#include "SeqEditor.h"
#include "../ui/Controls.h"

// Web layout, measured from the running SQ-4 app (src/seq/seq.css, #seq-rack
// at its 1460px max-width; page grid 218px repeat(4, 1fr), gap 9, padding
// 14/18/22; rack-relative px):
//   rack              1460 x 920
//   header            (18,  14) 1424 x 66
//   track heads       (18,  89) 1424 x 54
//   scene grid        (18, 152) 1424 x 630   (6 rows, 96 tall, 105 step)
//   footer            (18, 782) 1424 x 68
//   hint line          18, 858
// Scene grid columns: scene col (18, 218), then 4 track cols of 292 each,
// 9px gaps: x = 18 + 218 + 9 + i*(292 + 9).

namespace {
// The SQ-4 host-param resolver for fui controls (Task 10's header knobs) —
// same pattern as bassInfoLookup/drumInfoLookup, installed once here since
// SeqHeader.cpp is still a Task 10 placeholder.
const fable::ParamInfo* seqInfoLookup(const std::string& pid) {
    for (const auto& d : fable::seqParamInfo())
        if (d.pid == pid) return &d;
    return nullptr;
}
const bool g_seqResolverInstalled = [] {
    fui::setParamInfoResolver(&seqInfoLookup);
    return true;
}();
} // namespace

// ---- SeqRack ----
SeqRack::SeqRack(SeqAudioProcessor&) {
    for (auto* c : std::initializer_list<juce::Component*>{
             &header, &trackHeads, &sceneGrid, &footer, &hint, &clipEdit })
        addAndMakeVisible(*c);
    clipEdit.setVisible(false); // Task 13: focus-mode overlay, hidden until entered
}

void SeqRack::resized() {
    header.setBounds(18, 14, 1424, 66);
    trackHeads.setBounds(18, 89, 1424, 54);
    sceneGrid.setBounds(18, 152, 1424, 630);
    footer.setBounds(18, 782, 1424, 68);
    hint.setBounds(18, 858, 1424, 20);
    clipEdit.setBounds(0, 0, LW, LH);
}

// ---- SeqEditor ----
SeqEditor::SeqEditor(SeqAudioProcessor& p)
    : juce::AudioProcessorEditor(p), rack(p) {
    setLookAndFeel(&lnf);
    addAndMakeVisible(rack);
    rack.setBounds(0, 0, SeqRack::LW, SeqRack::LH);

    setResizable(true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)SeqRack::LW / SeqRack::LH);
    setResizeLimits(840, (int)(840 * (double)SeqRack::LH / SeqRack::LW),
                    2100, (int)(2100 * (double)SeqRack::LH / SeqRack::LW));
    setSize(1200, (int)(1200 * (double)SeqRack::LH / SeqRack::LW));
}

SeqEditor::~SeqEditor() { setLookAndFeel(nullptr); }

void SeqEditor::paint(juce::Graphics& g) {
    g.fillAll(fui::col::bg);
    // subtle top radial glow, like the web background
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11141d), getWidth() * 0.5f, -120.0f,
                                           fui::col::bg, getWidth() * 0.5f, getHeight() * 0.6f, true));
    g.fillRect(getLocalBounds());
}

void SeqEditor::resized() {
    const float sc = juce::jmin(getWidth() / (float)SeqRack::LW, getHeight() / (float)SeqRack::LH);
    const float dx = (getWidth() - SeqRack::LW * sc) * 0.5f;
    const float dy = (getHeight() - SeqRack::LH * sc) * 0.5f;
    rack.setTransform(juce::AffineTransform::scale(sc).translated(dx, dy));
}
