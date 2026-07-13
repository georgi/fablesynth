#include "PluginEditor.h"

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
