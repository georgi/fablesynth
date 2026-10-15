#pragma once
#include "VisualTimer.h"
#include "../dsp/AnalysisTap.h"
#include "../dsp/automix/AutoEq.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// Live third-octave spectrum for the analyser displays (EQ panels, KIT MIX,
// ASSIST); port of src/components/panels/useLiveSpectrum.ts. Each update
// reads the newest 8192 frames of a tap on the message thread, smooths the
// band powers over ~250 ms and exposes the levels in dB relative to the
// total. Silence (block RMS < -60 dB) or a missing tap clears it.
namespace fui {

class LiveSpectrum {
public:
    static constexpr int kFftSize = 8192;
    static constexpr int kHz = 30;

    // Returns true when the drawn state changed (always while active).
    bool update(const fable::ProgramTap* tap, double sampleRate, double nowMs) {
        namespace am = fable::automix;
        const bool was = active_;
        time_.resize((size_t)kFftSize);
        spec_.resize((size_t)kFftSize / 2);
        if (!tap || !tap->readLatest(time_.data(), nullptr, kFftSize)
            || am::rmsDb(time_.data(), kFftSize) < am::SILENCE_DB) {
            clear();
            return was;
        }
        am::analyserSpectrumDb(time_.data(), kFftSize, spec_.data());
        am::Bands bands{};
        am::addBandPower(bands, spec_.data(), kFftSize / 2, sampleRate > 0 ? sampleRate : 48000, kFftSize);
        const double k = last_ > 0 ? 1 - std::exp(-(nowMs - last_) / 250) : 1;
        last_ = nowMs;
        for (size_t i = 0; i < bands.size(); ++i) smooth_[i] += (bands[i] - smooth_[i]) * k;
        active_ = true;
        return true;
    }
    void clear() { smooth_.fill(0); active_ = false; last_ = 0; }
    bool active() const { return active_; }

    // Levels in dB relative to the total power (empty while inactive).
    std::vector<double> levels() const {
        namespace am = fable::automix;
        if (!active_) return {};
        double total = 0;
        for (double p : smooth_) total += p;
        if (total <= 0) total = 1e-20;
        std::vector<double> out;
        for (double p : smooth_) out.push_back(am::powerDb(p / total));
        return out;
    }

    // EQ-analyser view: +4.5 dB/octave tilt around 1 kHz, centred on the median of 100 Hz-8 kHz.
    static std::vector<double> tilted(std::vector<double> levels) {
        namespace am = fable::automix;
        std::vector<double> mid;
        for (size_t i = 0; i < levels.size(); ++i) {
            levels[i] += 4.5 * std::log2(am::BAND_HZ[i] / 1000);
            if (am::BAND_HZ[i] >= 100 && am::BAND_HZ[i] <= 8000) mid.push_back(levels[i]);
        }
        std::sort(mid.begin(), mid.end());
        const double centre = mid.empty() ? 0 : mid[mid.size() >> 1];
        for (auto& l : levels) l -= centre;
        return levels;
    }

    // Filled path through (X(band), Y(level)), closed down to `bottom`.
    template <class FX, class FY>
    static juce::Path path(const std::vector<double>& levels, FX X, FY Y, float bottom) {
        namespace am = fable::automix;
        juce::Path p;
        if (levels.empty()) return p;
        for (size_t i = 0; i < levels.size(); ++i) {
            const float x = (float)X(am::BAND_HZ[i]), y = (float)Y(levels[i]);
            if (i) p.lineTo(x, y); else p.startNewSubPath(x, y);
        }
        p.lineTo((float)X(am::BAND_HZ[levels.size() - 1]), bottom);
        p.lineTo((float)X(am::BAND_HZ[0]), bottom);
        p.closeSubPath();
        return p;
    }

    // The web's subtle filled area: text colour at ~8-9 % fill, ~32-35 % stroke.
    static void draw(juce::Graphics& g, const juce::Path& p, juce::Colour c, float fill = .085f, float stroke = .33f) {
        if (p.isEmpty()) return;
        g.setColour(c.withAlpha(fill));
        g.fillPath(p);
        g.setColour(c.withAlpha(stroke));
        g.strokePath(p, juce::PathStrokeType(1));
    }

private:
    fable::automix::Bands smooth_{};
    bool active_ = false;
    double last_ = 0;
    std::vector<float> time_, spec_;
};

// Calls `tick` on the message thread at `hz` while `owner` is visible, and
// `hidden` when it stops being visible.
class VisualTick : private VisualTimer {
public:
    VisualTick(juce::Component& owner, int hz, std::function<void()> tick, std::function<void()> hidden = {})
        : tick_(std::move(tick)), hidden_(std::move(hidden)) { startVisualTimer(owner, hz); }
private:
    void timerCallback() override { if (tick_) tick_(); }
    void visualVisibilityChanged(bool visible) override { if (!visible && hidden_) hidden_(); }
    std::function<void()> tick_, hidden_;
};

} // namespace fui
