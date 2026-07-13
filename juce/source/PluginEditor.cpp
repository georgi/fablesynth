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
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11141d), width * 0.5f, -120.0f,
                                           fui::col::bg, width * 0.5f, height * 0.6f, true));
    g.fillRect(getLocalBounds());
}

void FableAudioProcessorEditor::resized() {
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    const float rackWidth = static_cast<float>(Rack::LW);
    const float rackHeight = static_cast<float>(Rack::LH);
    const float sc = juce::jmin(width / rackWidth, height / rackHeight);
    const float dx = (width - rackWidth * sc) * 0.5f;
    const float dy = (height - rackHeight * sc) * 0.5f;
    rack.setTransform(juce::AffineTransform::scale(sc).translated(dx, dy));
    wtEditor.setBounds(getLocalBounds());
}
