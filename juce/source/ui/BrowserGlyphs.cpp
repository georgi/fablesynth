#include "BrowserGlyphs.h"
#include <cmath>

namespace fui::glyph {
namespace {

constexpr float kTMax = 4.0f;       // shared time axis of every envelope cell
constexpr int kEnvSteps = 40;

struct Canvas {
    juce::Graphics& g;
    juce::Colour colour;
    bool fill;
    juce::Rectangle<float> r;
    float cell, pad;

    Canvas(juce::Graphics& gr, juce::Rectangle<float> rect, juce::Colour c, bool f)
        : g(gr), colour(c), fill(f), r(rect), cell(rect.getWidth() / 4.0f), pad(f ? 6.0f : 3.0f) {
        if (fill) {
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            for (int k = 1; k < 4; ++k)
                g.fillRect(r.getX() + (float)k * cell, r.getY() + pad, 1.0f, r.getHeight() - 2 * pad);
        }
    }
    juce::Rectangle<float> box(int k) const {
        return { r.getX() + (float)k * cell + pad, r.getY() + pad, cell - 2 * pad, r.getHeight() - 2 * pad };
    }
    float lineWidth() const { return fill ? 1.2f : 1.0f; }
    void stroke(const juce::Path& p, float alpha = 1.0f) {
        g.setColour(colour.withMultipliedAlpha(alpha));
        g.strokePath(p, juce::PathStrokeType(lineWidth(), juce::PathStrokeType::curved));
    }
    // Stroke an open polyline; in fill mode also tint the area under it.
    void area(const juce::Path& open, float baseY, float x0, float x1) {
        stroke(open);
        if (!fill) return;
        juce::Path closed(open);
        closed.lineTo(x1, baseY);
        closed.lineTo(x0, baseY);
        closed.closeSubPath();
        g.setColour(colour.withAlpha(0.14f));
        g.fillPath(closed);
    }
    void flatLine(const juce::Rectangle<float>& b, float y, float alpha = 1.0f) {
        juce::Path p; p.startNewSubPath(b.getX(), y); p.lineTo(b.getRight(), y); stroke(p, alpha);
    }
};

// Response magnitude (linear) of the filter type at r = f / cutoff.
float response(int type, float r, float q) {
    const float den = std::sqrt((1 - r * r) * (1 - r * r) + (r / q) * (r / q));
    switch (type) {
        case 0: return 1 / den;                                                            // LP 12
        case 1: return 1 / (den * std::max(1.0f, std::sqrt((1 - r * r) * (1 - r * r) + r * r * 2))); // LP 24
        case 3: return (r * r) / den;                                                      // HP 12
        case 4: return std::abs(1 - r * r) / den;                                          // NOTCH
        case 5: return 0.35f + 0.65f * std::abs(std::cos(juce::MathConstants<float>::pi * r * 0.5f)); // COMB
        default: return (r / q) / den;                                                     // BP 12, VOWEL
    }
}

void drawFilter(Canvas& c, const juce::Rectangle<float>& b, bool on, int type, float cutoff, float res) {
    if (!on) { c.flatLine(b, b.getY() + b.getHeight() * 0.33f); return; }
    const float q = 0.7f + res * 8.0f;
    juce::Path p;
    for (int i = 0; i <= 48; ++i) {
        const float x = (float)i / 48.0f, f = 20.0f * std::pow(1000.0f, x);
        const float db = 20 * std::log10(std::max(1.0e-4f, response(type, f / cutoff, q)));
        const float y = juce::jlimit(0.0f, 1.0f, (db + 36.0f) / 54.0f);
        const float px = b.getX() + x * b.getWidth(), py = b.getBottom() - y * b.getHeight();
        if (i) p.lineTo(px, py); else p.startNewSubPath(px, py);
    }
    c.area(p, b.getBottom(), b.getX(), b.getRight());
}

// Time axis shared by every envelope cell: log-ish, 0..4 s.
float timeX(float t) { return std::log1p(t / 0.01f) / std::log1p(kTMax / 0.01f); }

// Amp envelope: attack ramp, hold, then a decay that reaches zero at `dec`.
float ampLevel(const AmpEnv& e, float t) {
    if (t < e.att) return t / e.att;
    const float d = t - e.att - e.hold;
    if (d <= 0) return 1;
    return d >= e.dec ? 0.0f : std::pow(1 - d / e.dec, 1 + e.curve * 2);
}

void drawAmp(Canvas& c, const juce::Rectangle<float>& b, const AmpEnv& e) {
    const float end = std::min(kTMax, e.att + e.hold + e.dec);
    juce::Path p;
    float lastX = b.getX();
    for (int i = 0; i <= kEnvSteps; ++i) {
        const float t = (float)i / kEnvSteps * end;
        lastX = b.getX() + timeX(t) * b.getWidth();
        const float y = b.getY() + (1 - ampLevel(e, t)) * b.getHeight();
        if (i) p.lineTo(lastX, y); else p.startNewSubPath(lastX, y);
    }
    c.area(p, b.getBottom(), b.getX(), lastX);
}

} // namespace

void drawWave(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool fill,
              const WaveData& d, const fable::GeneratedTable* table) {
    Canvas c(g, r, colour, fill);

    // wave: two cycles of the oscillator A frame at its position
    auto b = c.box(0);
    if (table && table->frames > 0 && !table->viz.empty()) {
        const int fr = juce::roundToInt(juce::jlimit(0.0f, 1.0f, d.pos) * (float)(table->frames - 1));
        const int off = fr * fable::VIZ_N;
        juce::Path p;
        for (int i = 0; i <= 2 * fable::VIZ_N; i += 2) {
            const float v = table->viz[(size_t)(off + (i % fable::VIZ_N))];
            const float x = b.getX() + (float)i / (2 * fable::VIZ_N) * b.getWidth();
            const float y = b.getCentreY() - v * b.getHeight() * 0.45f;
            if (i) p.lineTo(x, y); else p.startNewSubPath(x, y);
        }
        c.stroke(p);
    }

    drawFilter(c, c.box(1), d.filterType >= 0, d.filterType, d.cutoff, d.res);

    // amp envelope, with the same log-ish time scale as EnvView
    b = c.box(2);
    {
        auto seg = [](float s) { return std::pow(s / 12.0f, 0.4f); };
        const float ta = seg(d.env[0]), td = seg(d.env[1]), tr = seg(d.env[3]), hold = 0.22f;
        const float total = ta + td + tr + hold, s = d.env[2];
        auto X = [&](float v) { return b.getX() + v / total * b.getWidth(); };
        auto Y = [&](float v) { return b.getY() + (1 - v) * b.getHeight(); };
        juce::Path p;
        p.startNewSubPath(X(0), Y(0)); p.lineTo(X(ta), Y(1)); p.lineTo(X(ta + td), Y(s));
        p.lineTo(X(ta + td + hold), Y(s)); p.lineTo(X(total), Y(0));
        c.area(p, b.getBottom(), X(0), X(total));
    }

    // LFO depth: an arc around a faint ring
    b = c.box(3);
    {
        const float cx = b.getCentreX(), cy = b.getCentreY(), rad = std::min(b.getWidth(), b.getHeight()) * 0.42f;
        juce::Path ring; ring.addEllipse(cx - rad, cy - rad, rad * 2, rad * 2);
        c.stroke(ring, 0.3f);
        if (d.lfoDepth > 0) {
            const float end = d.lfoDepth * juce::MathConstants<float>::twoPi;   // 0 = 12 o'clock, clockwise
            juce::Path arc;
            arc.addCentredArc(cx, cy, rad, rad, 0.0f, 0.0f, end, true);
            c.stroke(arc);
            const float dot = fill ? 2.5f : 1.6f;
            g.setColour(colour);
            g.fillEllipse(cx + std::sin(end) * rad - dot, cy - std::cos(end) * rad - dot, dot * 2, dot * 2);
        }
    }
}

void drawPad(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool fill, const PadData& d) {
    Canvas c(g, r, colour, fill);

    // pitch envelope: sweep from the start offset to the pad's own pitch
    auto b = c.box(0);
    const float zeroY = b.getY() + b.getHeight() * 0.78f, span = b.getHeight() * 0.7f;
    c.flatLine(b, zeroY, 0.3f);
    const float amtN = (d.penvAmt > 0 ? 1.0f : d.penvAmt < 0 ? -1.0f : 0.0f) * std::sqrt(std::abs(d.penvAmt) / 48.0f);
    const float dec = std::max(1.0e-4f, d.penvDec);
    const float pitchEnd = std::min(kTMax / 2, d.penvDec * 5);
    {
        juce::Path p;
        for (int i = 0; i <= kEnvSteps; ++i) {
            const float t = (float)i / kEnvSteps * pitchEnd;
            const float x = b.getX() + (std::log1p(t / 0.01f) / std::log1p(pitchEnd / 0.01f)) * b.getWidth();
            const float y = zeroY - amtN * std::exp(-t / dec) * span;
            if (i) p.lineTo(x, y); else p.startNewSubPath(x, y);
        }
        c.stroke(p);
    }

    drawAmp(c, c.box(1), d.amp);
    drawFilter(c, c.box(2), d.filterOn, d.filterType, d.cutoff, d.res);

    // source mix: osc, sample, noise, ring as four bars
    b = c.box(3);
    const float bw = b.getWidth() / 7.0f;
    for (int i = 0; i < 4; ++i) {
        const float x = b.getX() + bw * (0.5f + (float)i * 2), h = std::max(1.0f, std::min(1.0f, d.mix[i]) * b.getHeight());
        juce::Path p; p.startNewSubPath(x, b.getBottom()); p.lineTo(x, b.getBottom() - h);
        c.stroke(p);
    }
}

void drawKit(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool fill, const KitData& d) {
    Canvas c(g, r, colour, fill);
    for (int i = 0; i < 3; ++i) drawAmp(c, c.box(i), d.voices[i]);
    // tom tunings: three vertical ticks, higher = higher pitch (-48..+48 st)
    const auto b = c.box(3);
    c.flatLine(b, b.getBottom(), 0.3f);
    for (int i = 0; i < 3; ++i) {
        const float x = b.getX() + b.getWidth() * (0.2f + (float)i * 0.3f);
        const float h = std::max(2.0f, (d.toms[i] + 48.0f) / 96.0f * b.getHeight());
        juce::Path p; p.startNewSubPath(x, b.getBottom()); p.lineTo(x, b.getBottom() - h);
        c.stroke(p);
    }
}

} // namespace fui::glyph
