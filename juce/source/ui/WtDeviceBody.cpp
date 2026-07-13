#include "WtDeviceBody.h"

WtDeviceBody::WtDeviceBody(fui::WtUiModel& model,
                           std::function<HostTransport()> transportProvider)
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
    int x = (int)std::round(static_cast<float>(padX) + static_cast<float>(c0) * (colUnit + static_cast<float>(gap)));
    int w = (int)std::round(static_cast<float>(span) * colUnit + static_cast<float>((span - 1) * gap));
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
