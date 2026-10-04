#pragma once
#include "Controls.h"
#include "Theme.h"
#include "../dsp/LabFx.h"

#include <array>
#include <cmath>
#include <memory>

namespace fui {

// LAB page: the five experimental stages in signal order (see dsp/LabFx.h).
// Each card draws an animated, parameter-driven picture of what its stage
// does; nothing here reads audio, so the pictures never claim live data.
class LabModule final : public juce::Component, private juce::Timer {
public:
    enum Kind { Crush, Reso, Shift, Spray, Glitch };

    LabModule(ParameterSource source, Kind kind, juce::String scope = {}) : source_(std::move(source)), kind_(kind), scope_(std::move(scope)),
        power_(source_, prefix() + ".on", accent()) {
        addAndMakeVisible(power_);
        const auto knob = [this](const char* field) {
            knobs_.add(std::make_unique<Knob>(source_, prefix() + "." + field, Knob::Sm, accent()));
            addAndMakeVisible(knobs_.getLast());
        };
        const auto stepper = [this](const char* field) {
            stepper_ = std::make_unique<Stepper>(source_, prefix() + "." + field, accent());
            addAndMakeVisible(*stepper_);
        };
        switch (kind_) {
            case Crush:  knob("bits"); knob("rate"); knob("chaos"); knob("mix"); break;
            case Reso:   stepper("chord"); knob("note"); knob("decay"); knob("mix"); break;
            case Shift:  knob("hz"); knob("fb"); knob("spread"); knob("mix"); break;
            case Spray:  knob("pitch"); knob("density"); knob("scatter"); knob("mix"); break;
            case Glitch: stepper("div"); knob("chance"); knob("drift"); knob("mix"); break;
        }
        startTimerHz(30);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(12, 10);
        auto head = r.removeFromTop(24);
        power_.setBounds(head.removeFromLeft(18).withSizeKeepingCentre(18, 18));
        r.removeFromTop(22); // tagline
        auto knobs = r.removeFromBottom(78);
        if (stepper_) { stepper_->setBounds(r.removeFromBottom(26)); r.removeFromBottom(8); }
        r.removeFromBottom(8);
        plot_ = r;
        const int w = knobs.getWidth() / std::max(1, knobs_.size());
        for (auto* k : knobs_) k->setBounds(knobs.removeFromLeft(w).withSizeKeepingCentre(w, 78));
    }

    void paint(juce::Graphics& g) override {
        drawPanel(g, getLocalBounds().toFloat(), 9);
        g.setFont(dispFont(12)); g.setColour(col::text);
        g.drawText(title(), 34, 10, 160, 24, juce::Justification::centredLeft);
        g.setFont(monoFont(8)); g.setColour(col::acN);
        g.drawText(power_.isOn() ? caption() : juce::String("BYPASS"), getWidth() - 140, 10, 128, 24,
                   juce::Justification::centredRight);
        g.setColour(col::textHint);
        g.drawText(tagline(), 12, 34, getWidth() - 24, 18, juce::Justification::centredLeft);
        drawDisplayBox(g, plot_.toFloat());
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(plot_.reduced(1));
        g.addTransform(juce::AffineTransform::translation((float)plot_.getX(), (float)plot_.getY()));
        g.setOpacity(power_.isOn() ? 1.0f : 0.35f);
        const float w = (float)plot_.getWidth(), h = (float)plot_.getHeight();
        switch (kind_) {
            case Crush: drawCrush(g, w, h); break;
            case Reso: drawReso(g, w, h); break;
            case Shift: drawShift(g, w, h); break;
            case Spray: drawSpray(g, w, h); break;
            case Glitch: drawGlitch(g, w, h); break;
        }
    }

private:
    juce::String prefix() const {
        static const char* const ids[] = {"fx.crush", "fx.reso", "fx.shift", "fx.spray", "fx.glitch"};
        return scope_ + ids[kind_];
    }
    juce::String title() const {
        static const char* const names[] = {"CRUSH", "RESO", "SHIFT", "SPRAY", "GLITCH"};
        return names[kind_];
    }
    juce::String tagline() const {
        static const char* const lines[] = {
            "BIT + RATE DECIMATOR, JITTERED CLOCK",
            "FOUR COMBS RING AT A CHORD",
            "FREQUENCY SHIFT, SPIRALLING 1/16 ECHOES",
            "PITCHED GRAIN CLOUD, SCATTERED + REVERSED",
            "TEMPO-SYNCED BEAT REPEAT WITH TAPE DRIFT",
        };
        return lines[kind_];
    }
    Accent accent() const {
        static const Accent a[] = {Accent::B, Accent::F, Accent::A, Accent::N, Accent::B};
        return a[kind_];
    }
    juce::Colour ink() const {
        static const juce::uint32 c[] = {0xffff6b5d, 0xffb18cff, 0xff4de8ff, 0xff9dffb0, 0xffffa14d};
        return juce::Colour(c[kind_]);
    }
    float value(const char* field) const {
        if (auto* p = source_.parameter(prefix() + "." + field)) return p->convertFrom0to1(p->getValue());
        return 0;
    }
    static juce::String noteName(int n) {
        static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return juce::String(names[((n % 12) + 12) % 12]) + juce::String((int)std::floor(n / 12.0) - 1);
    }
    juce::String caption() const {
        switch (kind_) {
            case Crush: return juce::String(value("bits"), 1) + " BIT";
            case Reso: return noteName((int)std::lround(value("note")));
            case Shift: { const int hz = (int)std::lround(value("hz")); return (hz > 0 ? "+" : "") + juce::String(hz) + " HZ"; }
            case Spray: { const int st = (int)std::lround(value("pitch")); return (st > 0 ? "+" : "") + juce::String(st) + " ST"; }
            case Glitch: return juce::String((int)std::lround(value("chance") * 100)) + "% CHANCE";
        }
        return {};
    }
    // Deterministic hash noise in [0, 1) so frames are stable between repaints.
    static float hash(int i, int salt = 0) {
        uint32_t x = (uint32_t)i * 0x9e3779b1u ^ (uint32_t)salt * 0x85ebca6bu;
        x ^= x >> 15; x *= 0x2c1b3c6du; x ^= x >> 12; x *= 0x297a2d39u; x ^= x >> 15;
        return (float)(x >> 8) * (1.0f / 16777216.0f);
    }
    void grid(juce::Graphics& g, float w, float h) const {
        g.setColour(col::line);
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine((int)(h * (float)i / 4), 0, w);
        for (int i = 1; i < 8; ++i) g.drawVerticalLine((int)(w * (float)i / 8), 0, h);
    }

    void drawCrush(juce::Graphics& g, float w, float h) const {
        grid(g, w, h);
        const float mid = h * 0.5f, amp = h * 0.38f;
        const float steps = std::exp2(value("bits") - 1);
        const float rateNorm = std::log(value("rate") / 200.0f) / std::log(240.0f); // 0 at 200 Hz, 1 at 48 kHz
        const int holds = juce::jlimit(4, 160, (int)(4 + rateNorm * rateNorm * 156));
        const float chaos = value("chaos");
        juce::Path smooth, stairs;
        for (int x = 0; x <= (int)w; x += 2) {
            const float y = mid - amp * std::sin(phase_ * 2 + (float)x / w * 6.2832f * 2);
            if (x == 0) smooth.startNewSubPath(0, y); else smooth.lineTo((float)x, y);
        }
        g.setColour(ink().withAlpha(0.22f)); g.strokePath(smooth, juce::PathStrokeType(1.5f));
        const int frame = (int)(phase_ * 8);
        float x = 0; bool first = true;
        for (int i = 0; x < w; ++i) {
            const float width = w / (float)holds * (1 + chaos * 1.6f * hash(i, frame));
            const float v = std::sin(phase_ * 2 + x / w * 6.2832f * 2);
            float q = std::round(v * steps) / steps;
            if (chaos > 0 && hash(i, frame + 99) < chaos * 0.15f) q += (hash(i, 7) < 0.5f ? -1 : 1) / steps;
            const float y = mid - amp * juce::jlimit(-1.0f, 1.0f, q);
            if (first) { stairs.startNewSubPath(0, y); first = false; } else stairs.lineTo(x, y);
            stairs.lineTo(std::min(w, x + width), y);
            x += width;
        }
        g.setColour(ink()); g.strokePath(stairs, juce::PathStrokeType(2.0f));
    }

    void drawReso(juce::Graphics& g, float w, float h) const {
        grid(g, w, h);
        const auto& chord = fable::labResoChords()[(size_t)juce::jlimit(0, 5, (int)std::lround(value("chord")))];
        const int note = (int)std::lround(value("note"));
        const float decay = value("decay");
        const auto xOf = [w](double hz) { return (float)(std::log(hz / 30.0) / std::log(12000.0 / 30.0)) * w; };
        for (size_t i = 0; i < chord.size(); ++i) {
            const double f0 = 440.0 * std::exp2((note + chord[i] - 69) / 12.0);
            const auto c = ink().withRotatedHue(0.06f * (float)i);
            for (int k = 1; k <= 24; ++k) {
                const double hz = f0 * k; if (hz > 12000) break;
                const float x = xOf(hz);
                const float shimmer = 0.75f + 0.25f * std::sin(phase_ * (3 + (float)i) + (float)k);
                const float height = h * 0.82f * std::pow(0.82f, (float)(k - 1)) * shimmer;
                const float width = 1.0f + 5.0f * (1 - decay);
                g.setColour(c.withAlpha(0.18f + 0.6f * decay * std::pow(0.9f, (float)k)));
                g.fillRect(x - width * 0.5f, h - height, width, height);
            }
            g.setColour(c); g.setFont(monoFont(8));
            g.drawText(noteName(note + chord[i]), (int)xOf(f0) + 3, 4 + 11 * (int)i, 40, 11, juce::Justification::centredLeft);
        }
    }

    void drawShift(juce::Graphics& g, float w, float h) const {
        grid(g, w, h);
        const float hz = value("hz"), fb = value("fb"), spread = value("spread");
        const float mid = h * 0.5f;
        const float travel = std::fmod(phase_ * 0.6f, 1.0f);
        // One dot per audible echo (until it falls below -28 dB); each echo is
        // shifted by another SHIFT Hz, so the trace climbs or falls.
        int echoes = 1;
        for (float amp = fb; echoes < 16 && amp > 0.04f; amp *= fb) ++echoes;
        const auto trace = [&](float hzStep, juce::Colour c) {
            juce::Path p; float amp = 1;
            for (int k = 0; k < echoes; ++k, amp *= fb) {
                const float x = 18 + (w - 36) * (float)k / (float)std::max(1, echoes - 1);
                const float off = (float)k * hzStep;
                const float y = mid - (off < 0 ? -1.0f : 1.0f) * h * 0.44f * (1 - std::exp(-std::abs(off) / 600.0f));
                if (k == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
                const float r = 3.0f + 7.0f * amp;
                const bool lit = std::abs((float)k / (float)std::max(1, echoes - 1) - travel) < 0.06f;
                g.setColour(c.withAlpha(lit ? 1.0f : 0.3f + 0.6f * amp));
                g.fillEllipse(x - r, y - r, r * 2, r * 2);
            }
            g.setColour(c.withAlpha(0.6f)); g.strokePath(p, juce::PathStrokeType(1.5f));
        };
        g.setColour(col::line.brighter(0.4f)); g.drawHorizontalLine((int)mid, 0, w);
        trace(hz * (1 - 2 * spread), col::acB);
        trace(hz, ink());
        g.setColour(col::textHint); g.setFont(monoFont(8));
        g.drawText("L", 4, 2, 20, 12, juce::Justification::centredLeft);
        g.setColour(col::acB); g.drawText("R", 16, 2, 20, 12, juce::Justification::centredLeft);
        g.drawText("TIME (1/16) >", (int)w - 90, (int)h - 14, 86, 12, juce::Justification::centredRight);
    }

    void drawSpray(juce::Graphics& g, float w, float h) const {
        grid(g, w, h);
        const float pitch = value("pitch"), density = value("density"), scatter = value("scatter");
        const float mid = h * 0.5f - pitch / 24.0f * h * 0.4f;
        g.setColour(col::acN.withAlpha(0.35f));
        g.drawHorizontalLine((int)(h * 0.5f), 0, w);
        const int count = juce::jlimit(3, 80, (int)(density * 2));
        const float t = phase_ * 0.35f;
        for (int i = 0; i < count; ++i) {
            const float life = std::fmod(t + hash(i, 1), 1.0f);
            const float x = w * (1.0f - std::fmod(hash(i, 2) * scatter + life * 0.35f + (1 - scatter) * 0.6f, 1.0f));
            const float y = mid + (hash(i, 3) - 0.5f) * h * (0.08f + 0.5f * scatter);
            const float a = std::sin(life * 3.14159f);
            const float r = 2 + 6 * a;
            const bool reverse = hash(i, 4) < scatter * 0.35f;
            g.setColour(ink().withAlpha(0.15f + 0.75f * a));
            if (reverse) g.drawEllipse(x - r, y - r, r * 2, r * 2, 1.4f);
            else g.fillEllipse(x - r, y - r, r * 2, r * 2);
        }
    }

    void drawGlitch(juce::Graphics& g, float w, float h) const {
        const int div = juce::jlimit(0, 4, (int)std::lround(value("div")));
        const float chance = value("chance"), drift = value("drift");
        const int periods = 4;
        const float pw = w / periods;
        const int epoch = (int)(phase_ * 0.5f);
        const float play = std::fmod(phase_ * 0.5f, 1.0f) * w;
        const double sliceBeats = fable::labGlitchBeats(div);
        const int repeats = (int)std::lround(2.0 / sliceBeats);
        for (int p = 0; p < periods; ++p) {
            const float x0 = pw * (float)p;
            g.setColour(col::line); g.drawVerticalLine((int)x0, 0, h);
            if (hash(p, epoch) >= chance) {
                g.setColour(col::acN.withAlpha(0.18f));
                g.fillRect(x0 + 2, h * 0.35f, pw - 4, h * 0.3f);
                continue;
            }
            const float sw = pw / (float)repeats;
            for (int k = 0; k < repeats; ++k) {
                const float rate = (float)juce::jlimit(0.25, 4.0, std::exp2(drift * 0.25 * k));
                const float bh = juce::jlimit(6.0f, h - 8, h * 0.3f * rate);
                const float y = h * 0.5f - bh * 0.5f;
                g.setColour(ink().withAlpha(0.35f + 0.6f * std::pow(0.93f, (float)k)));
                g.fillRect(x0 + sw * (float)k + 1, y, std::max(1.0f, sw - 2), bh);
            }
        }
        g.setColour(col::text.withAlpha(0.7f)); g.drawVerticalLine((int)play, 0, h);
        g.setColour(col::textHint); g.setFont(monoFont(8));
        g.drawText("1/2 BAR WINDOWS", 4, 2, 120, 12, juce::Justification::centredLeft);
    }

    void timerCallback() override {
        if (!isShowing()) return;
        phase_ += power_.isOn() ? 1.0f / 30.0f : 0.0f;
        repaint(plot_);
        if (power_.isOn() != lastOn_) { lastOn_ = power_.isOn(); repaint(); }
    }

    ParameterSource source_;
    Kind kind_;
    juce::String scope_;
    PowerButton power_;
    juce::OwnedArray<Knob> knobs_;
    std::unique_ptr<Stepper> stepper_;
    juce::Rectangle<int> plot_;
    float phase_ = 0;
    bool lastOn_ = false;
};

class LabPanel final : public juce::Component {
public:
    explicit LabPanel(ParameterSource source, juce::String scope = {}) {
        for (int k = 0; k < 5; ++k) {
            modules_[(size_t)k] = std::make_unique<LabModule>(source, (LabModule::Kind)k, scope);
            addAndMakeVisible(*modules_[(size_t)k]);
        }
    }
    void resized() override {
        auto r = getLocalBounds();
        const int gap = 9, w = (r.getWidth() - gap * 4) / 5;
        for (auto& m : modules_) { m->setBounds(r.removeFromLeft(w)); r.removeFromLeft(gap); }
    }
private:
    std::array<std::unique_ptr<LabModule>, 5> modules_;
};

} // namespace fui
