#include "PluginEditor.h"

// ---- Rack ----
Rack::Rack(juce::AudioProcessorValueTreeState& s, FableAudioProcessor& p)
    : topBar(s, p),
      oscA(s, p, 0, "oscA", fui::Accent::A, "OSC A"),
      oscB(s, p, 1, "oscB", fui::Accent::B, "OSC B"),
      util(s),
      filter(s),
      env1(s, "env1", "AMP ENV", juce::Colour(0xffe8edf7), fui::Accent::N),
      env2(s, "env2", "MOD ENV", juce::Colour(0xffb18cff), fui::Accent::F),
      lfos(s, [proc = &p] { return (double)proc->getHostBpm(); }),
      matrix(s),
      fx(s) {
    addAndMakeVisible(topBar);
    addAndMakeVisible(oscA); addAndMakeVisible(oscB); addAndMakeVisible(util);
    addAndMakeVisible(filter); addAndMakeVisible(env1); addAndMakeVisible(env2);
    addAndMakeVisible(lfos); addAndMakeVisible(matrix); addAndMakeVisible(fx);
    oscA.onEditTable = [this](int osc) { if (onEditTable) onEditTable(osc); };
    oscB.onEditTable = [this](int osc) { if (onEditTable) onEditTable(osc); };
}

juce::Rectangle<int> Rack::colArea(int c0, int span, int y, int h) const {
    const int padX = 14, gap = 9;
    const float colUnit = (LW - padX * 2 - 11 * gap) / 12.0f;
    int x = (int)std::round(padX + c0 * (colUnit + gap));
    int w = (int)std::round(span * colUnit + (span - 1) * gap);
    return { x, y, w, h };
}

void Rack::resized() {
    const int padX = 14, padY = 12, gap = 9;
    const int topH = 70, row1 = 250, row2 = 430, row3 = 250;
    topBar.setBounds(padX, padY, LW - padX * 2, topH);
    const int y1 = padY + topH + gap;
    const int y2 = y1 + row1 + gap;
    const int y3 = y2 + row2 + gap;

    oscA.setBounds(colArea(0, 5, y1, row1));
    oscB.setBounds(colArea(5, 5, y1, row1));
    util.setBounds(colArea(10, 2, y1, row1));

    filter.setBounds(colArea(0, 4, y2, row2));
    env1.setBounds(colArea(4, 2, y2, row2));
    env2.setBounds(colArea(6, 2, y2, row2));
    lfos.setBounds(colArea(8, 4, y2, row2));

    matrix.setBounds(colArea(0, 4, y3, row3));
    fx.setBounds(colArea(4, 8, y3, row3));
}

// ---- Editor ----
FableAudioProcessorEditor::FableAudioProcessorEditor(FableAudioProcessor& p)
    : juce::AudioProcessorEditor(p), rack(p.apvts, p), wtEditor(p) {
    setLookAndFeel(&lnf);
    addAndMakeVisible(rack);
    rack.setBounds(0, 0, Rack::LW, Rack::LH);

    // The editor overlay sits above the (scaled) rack and fills the window.
    addChildComponent(wtEditor);
    rack.onEditTable = [this](int osc) { wtEditor.openFor(osc); };

    setResizable(true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)Rack::LW / Rack::LH);
    setResizeLimits(840, (int)(840 * (double)Rack::LH / Rack::LW), 2100, (int)(2100 * (double)Rack::LH / Rack::LW));
    setSize(1200, (int)(1200 * (double)Rack::LH / Rack::LW));
}

FableAudioProcessorEditor::~FableAudioProcessorEditor() { setLookAndFeel(nullptr); }

void FableAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(fui::col::bg);
    // subtle top radial glow, like the web background
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11141d), getWidth() * 0.5f, -120.0f,
                                           fui::col::bg, getWidth() * 0.5f, getHeight() * 0.6f, true));
    g.fillRect(getLocalBounds());
}

void FableAudioProcessorEditor::resized() {
    const float sc = juce::jmin(getWidth() / (float)Rack::LW, getHeight() / (float)Rack::LH);
    const float dx = (getWidth() - Rack::LW * sc) * 0.5f;
    const float dy = (getHeight() - Rack::LH * sc) * 0.5f;
    rack.setTransform(juce::AffineTransform::scale(sc).translated(dx, dy));
    wtEditor.setBounds(getLocalBounds());
}
