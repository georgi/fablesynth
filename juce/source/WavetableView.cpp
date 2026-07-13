#include "WavetableView.h"
#include "PluginProcessor.h"

WavetableView::WavetableView(fui::WtUiModel& m, int oscIndex, juce::Colour acc)
    : model(m), osc(oscIndex), accent(acc) {
    label = oscIndex == 0 ? "OSC A" : "OSC B";
    startTimerHz(30); // animation cadence (matches the web rAF throttle)
}

WavetableView::WavetableView(FableAudioProcessor& p, int oscIndex, juce::Colour acc)
    : ownedModel(std::make_unique<StandaloneWtUiModel>(p)), model(*ownedModel),
      osc(oscIndex), accent(acc) {
    label = oscIndex == 0 ? "OSC A" : "OSC B";
    startTimerHz(30);
}

int WavetableView::tableIndex() const {
    auto* p = model.parameters().parameter(osc == 0 ? "oscA.table" : "oscB.table");
    return p ? (int)p->convertFrom0to1(p->getValue()) : 0;
}

float WavetableView::knobPos() const {
    auto* p = model.parameters().parameter(osc == 0 ? "oscA.pos" : "oscB.pos");
    return p ? p->convertFrom0to1(p->getValue()) : 0.0f;
}

void WavetableView::timerCallback() {
    // Repaint only when the shown frame or the selected table actually moves
    // (same throttle as the web view) — keeps the UI cheap when nothing changes.
    float mp = model.vizPosition(osc);
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

    const int idx = tableIndex();
    const auto* tp = model.tableAt(idx);
    if (!tp) return; // empty user slot -> nothing to draw
    const auto& t = *tp;
    const int frames = t.frames;
    if (frames < 1 || t.viz.empty()) return;
    const float* viz = t.viz.data();
    const int N = (int)t.viz.size() / frames;

    const float mp = model.vizPosition(osc);
    const float show = mp >= 0 ? mp : knobPos();

    // perspective layout (mirrors the web canvas math)
    const float depthX = w * 0.22f, depthY = h * 0.42f;
    const float waveW = w * 0.68f, waveAmp = h * 0.17f;
    const float x0 = w * 0.06f, y0 = h * 0.78f;

    const auto buildPath = [&](int f) {
        // Single-frame tables (drawn / single-cycle import) have frames == 1;
        // guard the depth ratio so it doesn't become 0/0 (NaN coords -> blank).
        const float d = frames > 1 ? (float)f / (frames - 1) : 0.0f;
        const float ox = x0 + d * depthX;
        const float oy = y0 - d * depthY;

        juce::Path path;
        for (int i = 0; i < N; ++i) {
            const float x = ox + (i / (float)(N - 1)) * waveW;
            const float y = oy - viz[f * N + i] * waveAmp;
            if (i == 0) path.startNewSubPath(x, y);
            else        path.lineTo(x, y);
        }
        return path;
    };

    const int cw = getWidth(), ch = getHeight();
    const int gen = model.tablesGeneration();
    const bool cacheValid = cacheTable == idx && cacheGen == gen && cacheW == cw && cacheH == ch;
    if (!cacheValid) {
        farCache = (cw > 0 && ch > 0) ? juce::Image(juce::Image::ARGB, cw * 2, ch * 2, true)
                                      : juce::Image();
        if (farCache.isValid()) {
            juce::Graphics cg(farCache);
            cg.addTransform(juce::AffineTransform::scale(2.0f));
            for (int f = frames - 1; f >= 0; --f) {
                const float d = frames > 1 ? (float)f / (frames - 1) : 0.0f;
                cg.setColour(juce::Colour(0xff8893a8).withAlpha(0.16f + d * 0.10f));
                cg.strokePath(buildPath(f), juce::PathStrokeType(1.0f));
            }
        }
        cacheTable = idx;
        cacheGen = gen;
        cacheW = cw;
        cacheH = ch;
    }

    if (farCache.isValid())
        g.drawImage(farCache, getLocalBounds().toFloat());

    const float posF = show * (frames - 1);
    // The near frame keeps its cached grey hairline underneath; the glow pass hides it.
    for (int f = frames - 1; f >= 0; --f) {
        const float near = juce::jmax(0.0f, 1.0f - std::abs(f - posF));
        if (near <= 0.02f) continue;

        auto path = buildPath(f);
        // bloom pass approximates the canvas shadowBlur, then the bright line
        g.setColour(accent.withAlpha(near * 0.22f));
        g.strokePath(path, juce::PathStrokeType(3.0f + near * 5.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent.withAlpha(0.25f + near * 0.75f));
        g.strokePath(path, juce::PathStrokeType(1.0f + near * 1.4f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
