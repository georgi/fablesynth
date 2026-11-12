#include "WavetableView.h"
#include "ui/Theme.h"
#include "ui/WarpPreview.h"
#include "dsp/UserTables.h"
#ifndef FABLE_HOSTED_UI
#include "PluginProcessor.h"
#endif

WavetableView::WavetableView(fui::WtUiModel& m, int oscIndex, juce::Colour acc)
    : model(m), osc(oscIndex), accent(acc) {
    label = oscIndex == 0 ? "OSC A" : "OSC B";
    startVisualTimer(*this, fui::kAnimationHz);
}

#ifndef FABLE_HOSTED_UI
WavetableView::WavetableView(FableAudioProcessor& p, int oscIndex, juce::Colour acc)
    : ownedModel(std::make_unique<StandaloneWtUiModel>(p)), model(*ownedModel),
      osc(oscIndex), accent(acc) {
    label = oscIndex == 0 ? "OSC A" : "OSC B";
    startVisualTimer(*this, fui::kAnimationHz);
}
#endif

int WavetableView::tableIndex() const {
    auto* p = model.parameters().parameter(osc == 0 ? "oscA.table" : "oscB.table");
    return p ? (int)model.parameters().effectiveValue(osc == 0 ? "oscA.table" : "oscB.table") : 0;
}

float WavetableView::knobPos() const {
    auto* p = model.parameters().parameter(osc == 0 ? "oscA.pos" : "oscB.pos");
    return p ? model.parameters().effectiveValue(osc == 0 ? "oscA.pos" : "oscB.pos") : 0.0f;
}

WavetableView::Warp WavetableView::warpState() const {
    const juce::String me = osc == 0 ? "oscA" : "oscB", other = osc == 0 ? "oscB" : "oscA";
    const auto& ps = model.parameters();
    auto value = [&](const juce::String& id) { return ps.parameter(id) ? ps.effectiveValue(id) : 0.0f; };
    Warp w;
    w.mode = (int)value(me + ".warp");
    if (w.mode == 0) return w;
    w.amount = value(me + ".warpamt");
    if (w.mode >= fable::WARP_FM) {
        // No modulator (other oscillator off or empty slot): the warp is off.
        if (value(other + ".on") < 0.5f || !model.tableAt((int)value(other + ".table"))) return Warp{};
        w.modTable = (int)value(other + ".table");
        w.modPos = value(other + ".pos");
        const float semis = (value(other + ".oct") - value(me + ".oct")) * 12.0f + value(other + ".semi") - value(me + ".semi")
            + (value(other + ".fine") - value(me + ".fine")) / 100.0f;
        w.ratio = std::pow(2.0f, semis / 12.0f);
    }
    return w;
}

void WavetableView::timerCallback() {
    // Repaint only when the shown frame or the selected table actually moves
    // (same throttle as the web view) — keeps the UI cheap when nothing changes.
    float mp = model.vizPosition(osc);
    float show = mp >= 0 ? mp : knobPos();
    int idx = tableIndex();
    const Warp warp = warpState();
    if (idx == lastTable && std::abs(show - lastShown) < 0.004f && warp == lastWarp) return;
    lastShown = show;
    lastTable = idx;
    lastWarp = warp;
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
        const float d = frames > 1 ? static_cast<float>(f) / static_cast<float>(frames - 1) : 0.0f;
        const float ox = x0 + d * depthX;
        const float oy = y0 - d * depthY;

        juce::Path path;
        for (int i = 0; i < N; ++i) {
            const float x = ox + (static_cast<float>(i) / static_cast<float>(N - 1)) * waveW;
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
                const float d = frames > 1 ? static_cast<float>(f) / static_cast<float>(frames - 1) : 0.0f;
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

    // Periodic linear read of one viz frame at a phase in cycles.
    const auto readFrame = [](const float* data, int count, double q) {
        const double x = (q - std::floor(q)) * count;
        const int i = (int)x % count;
        return (double)data[i] + ((double)data[(i + 1) % count] - data[i]) * (x - std::floor(x));
    };
    // The frames at the play position show the warped waveform.
    const Warp warp = warpState();
    const auto* modTable = warp.modTable >= 0 ? model.tableAt(warp.modTable) : nullptr;
    const int modN = modTable && modTable->frames > 0 ? (int)modTable->viz.size() / modTable->frames : 0;
    const float* modViz = modN > 0
        ? modTable->viz.data() + (size_t)juce::roundToInt(warp.modPos * (float)(modTable->frames - 1)) * (size_t)modN : nullptr;
    const auto buildWarpedPath = [&](int f) {
        const float d = frames > 1 ? static_cast<float>(f) / static_cast<float>(frames - 1) : 0.0f;
        const float ox = x0 + d * depthX, oy = y0 - d * depthY;
        juce::Path path;
        for (int i = 0; i < N; ++i) {
            const double p = (double)i / N;
            const double v = fui::warpSample(warp.mode, warp.amount, p,
                [&](double q) { return readFrame(viz + f * N, N, q); },
                modViz ? readFrame(modViz, modN, p * warp.ratio) : 0.0);
            const float x = ox + (static_cast<float>(i) / static_cast<float>(N - 1)) * waveW;
            const float y = oy - (float)v * waveAmp;
            if (i == 0) path.startNewSubPath(x, y);
            else        path.lineTo(x, y);
        }
        return path;
    };

    const float posF = show * static_cast<float>(frames - 1);
    // The near frame keeps its cached grey hairline underneath; the glow pass hides it.
    for (int f = frames - 1; f >= 0; --f) {
        const float near = juce::jmax(0.0f, 1.0f - std::abs(static_cast<float>(f) - posF));
        if (near <= 0.02f) continue;

        auto path = warp.mode != 0 ? buildWarpedPath(f) : buildPath(f);
        // bloom pass approximates the canvas shadowBlur, then the bright line
        g.setColour(accent.withAlpha(near * 0.22f));
        g.strokePath(path, juce::PathStrokeType(3.0f + near * 5.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent.withAlpha(0.25f + near * 0.75f));
        g.strokePath(path, juce::PathStrokeType(1.0f + near * 1.4f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
