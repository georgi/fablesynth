#include "PluginEditor.h"

// ---- reusable device body ----
WtDeviceBody::WtDeviceBody(fui::WtUiModel& model, std::function<HostTransport()> transportProvider)
    : oscA(model, 0, "oscA", fui::Accent::A, "OSC A"),
      oscB(model, 1, "oscB", fui::Accent::B, "OSC B"),
      util(model.parameters()), filter(model.parameters()),
      env1(model.parameters(), "env1", "AMP ENV", juce::Colour(0xffe8edf7), fui::Accent::N),
      env2(model.parameters(), "env2", "MOD ENV", juce::Colour(0xffb18cff), fui::Accent::F, 3),
      lfos(model.parameters(), transportProvider ? std::move(transportProvider) : [&model] {
          return HostTransport{ model.hostBpm(), 0.0, model.sequencerPlaying() };
      }),
      matrix(model.parameters()), fx(model.parameters()), seq(model) {
    addAndMakeVisible(oscA); addAndMakeVisible(oscB); addAndMakeVisible(util);
    addAndMakeVisible(filter); addAndMakeVisible(env1); addAndMakeVisible(env2);
    addAndMakeVisible(lfos); addAndMakeVisible(matrix); addAndMakeVisible(fx);
    addAndMakeVisible(seq);
    oscA.onEditTable = [this](int osc) { if (onEditTable) onEditTable(osc); };
    oscB.onEditTable = [this](int osc) { if (onEditTable) onEditTable(osc); };
}

juce::Rectangle<int> WtDeviceBody::colArea(int c0, int span, int y, int h) const {
    const int padX = 14, gap = 9;
    const float colUnit = (LW - padX * 2 - 11 * gap) / 12.0f;
    int x = (int)std::round(padX + c0 * (colUnit + gap));
    int w = (int)std::round(span * colUnit + (span - 1) * gap);
    return { x, y, w, h };
}

void WtDeviceBody::resized() {
    const int gap = 9;
    const int row1 = 250, row2 = 430, row3 = 250, row4 = 270;
    const int y1 = 2;
    const int y2 = y1 + row1 + gap;
    const int y3 = y2 + row2 + gap;
    const int y4 = y3 + row3 + gap;
    oscA.setBounds(colArea(0, 5, y1, row1));
    oscB.setBounds(colArea(5, 5, y1, row1));
    util.setBounds(colArea(10, 2, y1, row1));
    filter.setBounds(colArea(0, 4, y2, row2));
    env1.setBounds(colArea(4, 2, y2, row2));
    env2.setBounds(colArea(6, 2, y2, row2));
    lfos.setBounds(colArea(8, 4, y2, row2));
    matrix.setBounds(colArea(0, 4, y3, row3));
    fx.setBounds(colArea(4, 8, y3, row3));
    seq.setBounds(colArea(0, 12, y4, row4));
}

// ---- standalone rack ----
Rack::Rack(fui::WtUiModel& model, juce::AudioProcessorValueTreeState& s, FableAudioProcessor& p)
    : topBar(s, p), body(model, [&p] { return p.getTransport(); }) {
    addAndMakeVisible(topBar);
    addAndMakeVisible(body);
    body.onEditTable = [this](int osc) { if (onEditTable) onEditTable(osc); };
}

void Rack::resized() {
    const int padX = 14, padY = 12, topH = 70, gap = 9;
    topBar.setBounds(padX, padY, LW - padX * 2, topH);
    body.setBounds(0, padY + topH + gap - 2, WtDeviceBody::LW, WtDeviceBody::LH);
}

// ---- Editor ----
FableAudioProcessorEditor::FableAudioProcessorEditor(FableAudioProcessor& p)
    : juce::AudioProcessorEditor(p),
      model(std::make_unique<StandaloneWtUiModel>(p)), rack(*model, p.apvts, p), wtEditor(*model) {
    setLookAndFeel(&lnf);
    addAndMakeVisible(rack);
    rack.setBounds(0, 0, Rack::LW, Rack::LH);
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
