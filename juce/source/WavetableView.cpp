#include "WavetableView.h"

WavetableView::WavetableView(FableAudioProcessor& p, int oscIndex, juce::Colour acc)
    : proc(p), osc(oscIndex), accent(acc) {
    label = oscIndex == 0 ? "OSC A" : "OSC B";
    startTimerHz(30); // animation cadence (matches the web rAF throttle)
}

int WavetableView::tableIndex() const {
    auto* v = proc.apvts.getRawParameterValue(osc == 0 ? "oscA.table" : "oscB.table");
    return v ? (int)v->load() : 0;
}

float WavetableView::knobPos() const {
    auto* v = proc.apvts.getRawParameterValue(osc == 0 ? "oscA.pos" : "oscB.pos");
    return v ? v->load() : 0.0f;
}

void WavetableView::timerCallback() {
    // Repaint only when the shown frame or the selected table actually moves
    // (same throttle as the web view) — keeps the UI cheap when nothing changes.
    float mp = proc.getVizPos(osc);
    float show = mp >= 0 ? mp : knobPos();
    int idx = tableIndex();
    if (idx == lastTable && std::abs(show - lastShown) < 0.004f) return;
    lastShown = show;
    lastTable = idx;
    repaint();
}

void WavetableView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();

    // panel backdrop
    g.setColour(juce::Colour(0xff0d1016));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(juce::Colour(0xff1c222c));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    const auto& tables = proc.getTables();
    const int idx = tableIndex();
    if (idx < 0 || idx >= (int)tables.size()) return;
    const auto& t = tables[idx];
    const int frames = t.frames;
    if (frames < 1 || t.viz.empty()) return;
    const float* viz = t.viz.data();
    const int N = (int)t.viz.size() / frames;

    const float mp = proc.getVizPos(osc);
    const float show = mp >= 0 ? mp : knobPos();

    // perspective layout (mirrors the web canvas math)
    const float depthX = w * 0.22f, depthY = h * 0.42f;
    const float waveW = w * 0.68f, waveAmp = h * 0.17f;
    const float x0 = w * 0.06f, y0 = h * 0.78f;
    const float posF = show * (frames - 1);

    for (int f = frames - 1; f >= 0; --f) {
        const float d = (float)f / (frames - 1);
        const float ox = x0 + d * depthX;
        const float oy = y0 - d * depthY;
        const float near = juce::jmax(0.0f, 1.0f - std::abs(f - posF));

        juce::Path path;
        for (int i = 0; i < N; ++i) {
            const float x = ox + (i / (float)(N - 1)) * waveW;
            const float y = oy - viz[f * N + i] * waveAmp;
            if (i == 0) path.startNewSubPath(x, y);
            else        path.lineTo(x, y);
        }

        if (near > 0.02f) {
            // bloom pass approximates the canvas shadowBlur, then the bright line
            g.setColour(accent.withAlpha(near * 0.22f));
            g.strokePath(path, juce::PathStrokeType(3.0f + near * 5.0f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(accent.withAlpha(0.25f + near * 0.75f));
            g.strokePath(path, juce::PathStrokeType(1.0f + near * 1.4f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        } else {
            g.setColour(juce::Colour(0xff8893a8).withAlpha(0.16f + d * 0.10f));
            g.strokePath(path, juce::PathStrokeType(1.0f));
        }
    }

    // header label + table name
    g.setColour(accent.withAlpha(0.85f));
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(label, 8, 6, 80, 14, juce::Justification::left);
    g.setColour(juce::Colour(0xff8893a8));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(juce::String(t.name), (int)w - 88, 6, 80, 14, juce::Justification::right);
}
