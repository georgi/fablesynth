#pragma once
// Melodic sequence POLY strip for the WT-1 / BL-1 sequencers — port of
// src/components/SequencePoly.tsx (+ sequencePoly.css): a "POLY <summary>"
// disclosure; open, one row of ON/OFF · MODE · STEPS · (CYCLE) · ROTATE ·
// undo/redo and the explanation. Painted and hit-tested directly; the owning
// view supplies the settings, commits edits and owns the undo history.

#include "DeviceUiModel.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <limits>

namespace fui {

class SequencePolyStrip : public juce::Component {
public:
    static constexpr int kClosedH = 32, kOpenH = 66;
    enum Hit { none, disclosure, power, grid, fit, stepsDown, stepsUp, cycle1, cycle2,
               rotateDown, rotateUp, undo, redo };

    std::function<void(const SeqPolySettings&)> onChange;
    std::function<void()> onUndo, onRedo, onOpenChanged;

    int preferredHeight() const { return open_ ? kOpenH : kClosedH; }
    bool isOpen() const { return open_; }
    void setOpen(bool open) {
        if (open == open_) return;
        open_ = open; repaint();
        if (onOpenChanged) onOpenChanged();
    }
    const SeqPolySettings& settings() const { return value_; }

    // Web re-sync rules: a new clip opens the strip iff its POLY is on; POLY
    // turning on opens it; a manual collapse otherwise stays.
    void setSettings(const SeqPolySettings& next, int clipKey) {
        const bool clipChanged = clipKey != clipKey_;
        const bool turnedOn = next.enabled && !value_.enabled;
        clipKey_ = clipKey;
        if (next != value_) { value_ = next; repaint(); }
        if (clipChanged) setOpen(next.enabled);
        else if (turnedOn) setOpen(true);
    }

    juce::String summary() const {
        if (!value_.enabled) return "OFF";
        return value_.fit ? "FIT " + juce::String(value_.steps) + "/" + juce::String(value_.cycleBeats / 4) + "B"
                          : "GRID " + juce::String(value_.steps);
    }

    // Geometry (local px), public for the host tests.
    juce::Rectangle<int> bounds(Hit h) const {
        const int y = 34, rh = 26;
        switch (h) {
            case disclosure: return { 0, 0, 170, 26 };
            case power:      return { 0, y + 2, 44, rh - 4 };
            case grid:       return { 86, y, 44, rh };
            case fit:        return { 130, y, 40, rh };
            case stepsDown:  return { 226, y, 24, rh };
            case stepsUp:    return { 278, y, 24, rh };
            case cycle1:     return { 362, y, 50, rh };
            case cycle2:     return { 412, y, 56, rh };
            case rotateDown: return { rotateX() + 52, y, 24, rh };
            case rotateUp:   return { rotateX() + 104, y, 24, rh };
            case undo:       return { rotateX() + 144, y, 28, rh };
            case redo:       return { rotateX() + 176, y, 28, rh };
            case none:       break;
        }
        return {};
    }

    Hit hitAt(juce::Point<int> p) const {
        if (bounds(disclosure).contains(p)) return disclosure;
        if (!open_) return none;
        for (Hit h : { power, grid, fit, stepsDown, stepsUp, rotateDown, rotateUp, undo, redo })
            if (bounds(h).contains(p)) return h;
        if (value_.fit) for (Hit h : { cycle1, cycle2 }) if (bounds(h).contains(p)) return h;
        return none;
    }

    // store.updateSequenceRhythm: merge, keep rotation inside the new length.
    void press(Hit h) {
        auto next = value_;
        switch (h) {
            case disclosure: setOpen(!open_); return;
            case undo: if (onUndo) onUndo(); return;
            case redo: if (onRedo) onRedo(); return;
            case power: next.enabled = !next.enabled; break;
            case grid: next.fit = false; break;
            case fit: next.fit = true; break;
            case stepsDown: next.steps = juce::jmax(1, next.steps - 1); break;
            case stepsUp: next.steps = juce::jmin(16, next.steps + 1); break;
            case cycle1: next.cycleBeats = 4; break;
            case cycle2: next.cycleBeats = 8; break;
            case rotateDown: next.rotation = next.rotation <= 0 ? next.steps - 1 : next.rotation - 1; break;
            case rotateUp: next.rotation = next.rotation >= next.steps - 1 ? 0 : next.rotation + 1; break;
            case none: return;
        }
        next.rotation %= next.steps;
        if (next == value_) return;
        value_ = next; repaint();
        if (onChange) onChange(next);
    }

    void mouseDown(const juce::MouseEvent& e) override { press(hitAt(e.getPosition())); }

    void paint(juce::Graphics& g) override {
        const auto border = juce::Colour(0x4d8b95a8), face = juce::Colour(0xff151a24);
        const auto chip = [&](juce::Rectangle<int> r, const juce::String& text, bool lit, bool enabled = true) {
            g.setColour(lit ? juce::Colour(0xff17303a) : face);
            g.fillRoundedRectangle(r.toFloat(), 4.0f);
            g.setColour(lit ? col::acA.withAlpha(.6f) : border);
            g.drawRoundedRectangle(r.toFloat().reduced(.5f), 4.0f, 1.0f);
            g.setColour((lit ? col::acA : col::text).withMultipliedAlpha(enabled ? 1.0f : .4f));
            drawSpaced(g, text, r, .6f, juce::Justification::centred);
        };
        const auto legend = [&](int x, const juce::String& text, int w) {
            g.setColour(juce::Colour(0xffa9b3c6)); g.setFont(seqFont(9.0f));
            drawSpaced(g, text, { x, bounds(grid).getY(), w, bounds(grid).getHeight() }, 1.0f);
        };
        g.setFont(seqFont(10.0f));
        // Disclosure: "POLY  <summary>  ▾/▴".
        {
            const auto r = bounds(disclosure);
            g.setColour(face); g.fillRoundedRectangle(r.toFloat(), 4.0f);
            g.setColour(border); g.drawRoundedRectangle(r.toFloat().reduced(.5f), 4.0f, 1.0f);
            auto inner = r.reduced(9, 0);
            g.setColour(col::text); drawSpaced(g, "POLY", inner.removeFromLeft(36), 1.2f);
            inner.removeFromLeft(8);
            const auto arrow = inner.removeFromRight(12);
            g.setColour(value_.enabled ? col::acA : juce::Colour(0xffa9b3c6));
            drawSpaced(g, summary(), inner, 1.2f);
            g.setColour(col::text);
            g.drawText(juce::String::fromUTF8(open_ ? u8"▴" : u8"▾"), arrow, juce::Justification::centred);
        }
        if (!open_) return;
        g.setFont(seqFont(10.0f));
        chip(bounds(power), value_.enabled ? "ON" : "OFF", value_.enabled);
        legend(52, "MODE", 34);
        chip(bounds(grid), "GRID", !value_.fit); chip(bounds(fit), "FIT", value_.fit);
        legend(184, "STEPS", 40);
        stepper(g, bounds(stepsDown), bounds(stepsUp), value_.steps, value_.steps > 1, value_.steps < 16);
        int x = 316;
        if (value_.fit) {
            legend(x, "CYCLE", 40);
            chip(bounds(cycle1), "1 BAR", value_.cycleBeats == 4);
            chip(bounds(cycle2), "2 BARS", value_.cycleBeats == 8);
        }
        x = rotateX();
        legend(x, "ROTATE", 50);
        stepper(g, bounds(rotateDown), bounds(rotateUp), value_.rotation, true, true);
        chip(bounds(undo), juce::String::fromUTF8(u8"↶"), false);
        chip(bounds(redo), juce::String::fromUTF8(u8"↷"), false);
        const int ex = bounds(redo).getRight() + 16;
        g.setColour(juce::Colour(0xffa9b3c6)); g.setFont(seqFont(9.0f));
        drawSpaced(g, juce::String::fromUTF8(u8"BAR 1 · FIRST ") + juce::String(value_.steps) + " CELLS · "
                       + (value_.fit ? juce::String::fromUTF8(u8"EQUAL SLOTS · STRAIGHT") : juce::String::fromUTF8(u8"1/16 GRID · SWING")),
                   { ex, bounds(grid).getY(), juce::jmax(0, getWidth() - ex), bounds(grid).getHeight() }, .4f);
    }

private:
    int rotateX() const { return value_.fit ? 484 : 316; }

    void stepper(juce::Graphics& g, juce::Rectangle<int> down, juce::Rectangle<int> up, int v, bool canDown, bool canUp) {
        const auto border = juce::Colour(0x4d8b95a8);
        const auto well = down.getUnion(up).toFloat();
        g.setColour(juce::Colour(0xff0d1017)); g.fillRoundedRectangle(well, 4.0f);
        g.setColour(border); g.drawRoundedRectangle(well.reduced(.5f), 4.0f, 1.0f);
        g.setFont(seqFont(10.0f));
        g.setColour(col::text.withMultipliedAlpha(canDown ? 1.0f : .4f));
        g.drawText(juce::String::fromUTF8(u8"◂"), down, juce::Justification::centred);
        g.setColour(col::text.withMultipliedAlpha(canUp ? 1.0f : .4f));
        g.drawText(juce::String::fromUTF8(u8"▸"), up, juce::Justification::centred);
        g.setColour(col::acA); g.setFont(seqFont(11.0f, true));
        g.drawText(juce::String(v), juce::Rectangle<int>(down.getRight(), down.getY(), up.getX() - down.getRight(), down.getHeight()),
                   juce::Justification::centred);
    }

    SeqPolySettings value_;
    bool open_ = false;
    int clipKey_ = std::numeric_limits<int>::min();
};

} // namespace fui
