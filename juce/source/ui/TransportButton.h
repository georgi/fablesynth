#pragma once
#include "Theme.h"
#include "VisualTimer.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace fui {

// Standalone header play/stop — port of the web .ns-/.bl-/.dr-transport button
// that sits in each instrument header. It covers the sequencer and the arp,
// which share one transport. Hosted editors leave the transport to SQ-4.
class TransportButton final : public juce::Component, private fui::VisualTimer {
public:
    TransportButton(std::function<bool()> isPlaying, std::function<void(bool)> setPlaying,
                    juce::Colour well)
        : isPlaying_(std::move(isPlaying)), setPlaying_(std::move(setPlaying)),
          well_(well) {
        setTitle("Play sequencer");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        startVisualTimer(*this, 30);
    }

    void paint(juce::Graphics& g) override {
        const bool playing = isPlaying_();
        const auto accent = accentA();   // the editor's theme accent
        const auto tb = getLocalBounds().toFloat();
        g.setColour(well_);
        g.fillRoundedRectangle(tb, 6.0f);
        if (playing) {
            g.setColour(accent.withAlpha(0.1f));
            g.fillRoundedRectangle(tb, 6.0f);
        }
        g.setColour(accent.withAlpha(playing || isMouseOver() ? 0.9f : 0.55f));
        g.drawRoundedRectangle(tb.reduced(0.5f), 6.0f, 1.0f);
        g.setColour(accent);
        if (playing) {                               // stop square
            g.fillRect(juce::Rectangle<float>(8.0f, 8.0f).withCentre(tb.getCentre()));
        } else {                                     // play triangle
            juce::Path p;
            p.addTriangle(tb.getCentreX() - 3.5f, tb.getCentreY() - 5.0f,
                          tb.getCentreX() - 3.5f, tb.getCentreY() + 5.0f,
                          tb.getCentreX() + 5.5f, tb.getCentreY());
            g.fillPath(p);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (!getLocalBounds().contains(e.getPosition())) return;
        setPlaying_(!isPlaying_());
        sync();
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    void timerCallback() override { sync(); }
    void sync() {
        const bool playing = isPlaying_();
        if (playing == shown_) return;
        shown_ = playing;
        setTitle(playing ? "Stop sequencer" : "Play sequencer");
        repaint();
    }

    std::function<bool()> isPlaying_;
    std::function<void(bool)> setPlaying_;
    juce::Colour well_;
    bool shown_ = false;
};

} // namespace fui
