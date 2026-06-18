#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// Dark styling for the few stock widgets used (combo boxes for the mod-matrix +
// preset selector, and the preset-bar text buttons), matching the web CSS.
namespace fui {

class DarkLNF : public juce::LookAndFeel_V4 {
public:
    DarkLNF() {
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0c0f16));
        setColour(juce::PopupMenu::textColourId, col::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, col::acA.withAlpha(0.18f));
        setColour(juce::PopupMenu::highlightedTextColourId, col::acA);
        setColour(juce::ComboBox::textColourId, col::text);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override {
        auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h);
        g.setColour(juce::Colour(0xff0c0f16));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(box.hasKeyboardFocus(false) ? col::acN : col::line);
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
        // arrow
        juce::Path p;
        float ax = (float)w - 13, ay = h * 0.5f - 2;
        p.addTriangle(ax, ay, ax + 7, ay, ax + 3.5f, ay + 5);
        g.setColour(col::textDim);
        g.fillPath(p);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return monoFont(10.0f); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(9, 0, box.getWidth() - 22, box.getHeight());
        label.setFont(monoFont(10.0f));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff11141c));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(over || down ? col::acA : col::line);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return monoFont(11.0f); }
};

} // namespace fui
