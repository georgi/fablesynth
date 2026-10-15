#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace fui::arp {
// Web arp.css palette: the WT-1 / BL-1 arpeggiator surface.
namespace c {
    const juce::Colour card     {0xff0a0f17};
    const juce::Colour cardLine {juce::Colours::white.withAlpha(.06f)};
    const juce::Colour well     {0xff080c13};
    const juce::Colour field    {0xff0c121c};
    const juce::Colour cell     {0xff0e1520};
    const juce::Colour cellNow  {0xff142230};
    const juce::Colour display  {0xff070b11};
    const juce::Colour label    {0xff8d9db3};
    const juce::Colour hint     {0xff7d8ea4};
    const juce::Colour dim      {0xff56667c};
    const juce::Colour value    {0xffdfe6f3};
    const juce::Colour status   {0xffa2b2c8};
    const juce::Colour accent   {0xffffa14d};
    const juce::Colour help     {0xff5f6f86};
}
inline juce::Font font(float h, bool bold = false) { return fui::seqFont(h, bold); }
inline juce::Colour chordHue(int root, float s, float l) { return juce::Colour::fromHSL(root / 12.0f, s, l, 1.0f); }

// Letter-spaced uppercase label (web letter-spacing .1em).
inline void label(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour colour, float size = 9.0f,
                  bool bold = true, juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(colour); g.setFont(font(size, bold));
    fui::drawSpaced(g, s, r, size * .1f, j);
}
inline int labelWidth(const juce::String& s, float size = 9.0f, bool bold = true) {
    return juce::GlyphArrangement::getStringWidthInt(font(size, bold), s) + (int)std::ceil(s.length() * size * .1f);
}

// A numbered section (web .arp-card): badge, title and a body below.
inline void drawCard(juce::Graphics& g, juce::Rectangle<int> r, int n, const juce::String& title, juce::Colour live, int headHeight) {
    g.setColour(c::card); g.fillRoundedRectangle(r.toFloat(), 8);
    g.setColour(c::cardLine); g.drawRoundedRectangle(r.toFloat().reduced(.5f), 8, 1);
    auto head = r.reduced(12, 0).withTrimmedTop(8).withHeight(headHeight);
    auto badge = head.removeFromLeft(18).withSizeKeepingCentre(18, 18).toFloat();
    g.setColour(live.withAlpha(.5f)); g.drawEllipse(badge.reduced(.5f), 1);
    g.setColour(live); g.setFont(font(9, true)); g.drawText(juce::String(n), badge, juce::Justification::centred);
    head.removeFromLeft(10);
    label(g, title, head, c::value, 10, true);
}

// Web .arp-seg / .ns-btn / .arp-octave buttons.
class Button final : public juce::Button {
public:
    enum class Kind { Segment, Ghost, Step, Key, Plain };
    Button(Kind k = Kind::Ghost, const juce::String& text = {}) : juce::Button(text), kind(k) { setButtonText(text); }
    Kind kind;
    juce::Colour live {fui::col::acA};
    bool sharp = false;  // Key: black key
    bool nav = false;    // Ghost: navigation (a browser exit), in the nav accent
    float textSize = 9.0f;
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat();
        const bool on = getToggleState(), enabled = isEnabled();
        const float alpha = enabled ? 1.0f : .35f;
        juce::Colour text = c::label;
        switch (kind) {
            case Kind::Segment:
                if (on) { g.setColour(live.withAlpha(.12f)); g.fillRoundedRectangle(r, 3); g.setColour(live.withAlpha(.35f)); g.drawRoundedRectangle(r.reduced(.5f), 3, 1); }
                text = on ? live : over ? c::value : c::label;
                break;
            case Kind::Ghost:
                g.setColour(down ? c::cellNow : c::field); g.fillRoundedRectangle(r.reduced(.5f), 4);
                if (nav) g.setColour(over ? fui::navAccent() : fui::navAccent().withAlpha(.45f));
                else g.setColour(on ? live.withAlpha(.53f) : over ? juce::Colours::white.withAlpha(.2f) : juce::Colours::white.withAlpha(.11f));
                g.drawRoundedRectangle(r.reduced(.5f), 4, 1);
                text = nav ? fui::navAccent() : on ? live : c::value;
                break;
            case Kind::Step:
                g.setColour(juce::Colour(0xff111c29)); g.fillRoundedRectangle(r.reduced(.5f), 3);
                g.setColour(juce::Colours::white.withAlpha(over ? .2f : .11f)); g.drawRoundedRectangle(r.reduced(.5f), 3, 1);
                text = c::value;
                break;
            case Kind::Key: {
                const auto fill = on ? live : sharp ? juce::Colour(0xff172231) : juce::Colour(0xffc8d2df);
                g.setColour(fill.withMultipliedAlpha(down ? .85f : 1.0f)); g.fillRoundedRectangle(r.reduced(.5f), 3);
                g.setColour(on ? live : sharp ? juce::Colour(0xff445268) : juce::Colour(0xff8291a4)); g.drawRoundedRectangle(r.reduced(.5f), 3, 1);
                text = on || !sharp ? juce::Colour(0xff111925) : c::value;
                break;
            }
            case Kind::Plain:
                if (over) { g.setColour(live.withAlpha(.08f)); g.fillRect(r); }
                text = over ? live : juce::Colour(0xffb4c6d9);
                break;
        }
        const bool spaced = kind == Kind::Segment || kind == Kind::Ghost;
        g.setColour(text.withMultipliedAlpha(alpha)); g.setFont(font(textSize, kind != Kind::Key && kind != Kind::Plain));
        if (spaced) fui::drawSpaced(g, getButtonText(), getLocalBounds(), textSize * .1f, juce::Justification::centred);
        else g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
    int preferredWidth() const { return labelWidth(getButtonText(), textSize) + (kind == Kind::Segment ? 20 : 22); }
};

// A row of mutually exclusive options (web Segmented).
class Segmented final : public juce::Component {
public:
    struct Option { int id; juce::String text, tooltip; };
    std::function<void(int)> onChange;
    float textSize = 9.0f;
    void setOptions(const std::vector<Option>& options, juce::Colour live) {
        buttons.clear(); ids.clear();
        for (const auto& o : options) {
            auto* b = buttons.add(new Button(Button::Kind::Segment, o.text));
            b->live = live; b->textSize = textSize; b->setTooltip(o.tooltip);
            b->setName(getName() + " " + o.text);
            const int id = o.id;
            b->onClick = [this, id] { setSelectedId(id, juce::sendNotificationSync); };
            addAndMakeVisible(b); ids.push_back(id);
        }
        refreshState(); resized();
    }
    void setSelectedId(int id, juce::NotificationType n) {
        const bool changed = id != selected;
        selected = id; refreshState();
        if (n != juce::dontSendNotification && onChange && (changed || n == juce::sendNotificationSync)) onChange(id);
    }
    int getSelectedId() const { return selected; }
    int preferredWidth() const { int w = 6; for (auto* b : buttons) w += b->preferredWidth() + 2; return w; }
    void resized() override {
        auto r = getLocalBounds().reduced(3);
        const int total = std::max(1, preferredWidth() - 6);
        const float scale = (float)r.getWidth() / (float)total;
        for (auto* b : buttons) b->setBounds(r.removeFromLeft(juce::roundToInt((b->preferredWidth() + 2) * scale)).withTrimmedRight(2));
    }
    void paint(juce::Graphics& g) override {
        g.setColour(c::well); g.fillRoundedRectangle(getLocalBounds().toFloat(), 5);
        g.setColour(juce::Colours::white.withAlpha(.09f)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 5, 1);
    }
    juce::Button* button(int id) { for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return buttons[(int)i]; return nullptr; }
private:
    void refreshState() { for (size_t i = 0; i < ids.size(); ++i) buttons[(int)i]->setToggleState(ids[i] == selected, juce::dontSendNotification); }
    juce::OwnedArray<Button> buttons;
    std::vector<int> ids;
    int selected = -1;
};

// ‹ NAME detail BROWSE › (web .arp-pattern-pick).
class Picker final : public juce::Component {
public:
    std::function<void(int)> onStep;
    std::function<void()> onBrowse;
    Picker(const juce::String& thing, juce::Colour liveColour) : live(liveColour) {
        prev.setName("Previous " + thing); next.setName("Next " + thing); name.setName("Browse " + thing + "s");
        prev.setTooltip("Previous " + thing); next.setTooltip("Next " + thing); name.setTooltip("Browse " + thing + "s");
        prev.setButtonText(juce::String::fromUTF8("‹")); next.setButtonText(juce::String::fromUTF8("›"));
        for (auto* b : {&prev, &next}) { b->live = live; b->textSize = 16; addAndMakeVisible(b); }
        prev.onClick = [this] { if (onStep) onStep(-1); }; next.onClick = [this] { if (onStep) onStep(1); };
        name.onClick = [this] { if (onBrowse) onBrowse(); };
        name.paintFn = [this](juce::Graphics& g, juce::Rectangle<int> r) {
            r = r.reduced(12, 0);
            auto badge = r.removeFromRight(labelWidth("BROWSE", 8) + 14).withSizeKeepingCentre(labelWidth("BROWSE", 8) + 14, 18);
            g.setColour(live.withAlpha(.14f)); g.fillRoundedRectangle(badge.toFloat(), 3);
            label(g, "BROWSE", badge, live, 8, true, juce::Justification::centred);
            r.removeFromRight(8);
            const int strongW = std::min(r.getWidth(), labelWidth(strong, 11));
            label(g, strong, r.removeFromLeft(strongW), juce::Colours::white, 11, true);
            r.removeFromLeft(10);
            if (detail.isNotEmpty()) { g.setColour(live); g.setFont(font(9)); g.drawText(detail, r, juce::Justification::centredLeft, true); }
        };
        addAndMakeVisible(name);
    }
    void setText(const juce::String& s, const juce::String& d) { if (s != strong || d != detail) { strong = s; detail = d; name.repaint(); } }
    void resized() override {
        auto r = getLocalBounds();
        prev.setBounds(r.removeFromLeft(32)); next.setBounds(r.removeFromRight(32)); name.setBounds(r);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff0c1520)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6);
    }
    void paintOverChildren(juce::Graphics& g) override {
        g.setColour(juce::Colours::white.withAlpha(.07f));
        g.fillRect(name.getX(), 1, 1, getHeight() - 2); g.fillRect(name.getRight() - 1, 1, 1, getHeight() - 2);
        g.setColour(live.withAlpha(.35f)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 6, 1);
    }
private:
    struct NameButton final : juce::Button {
        NameButton() : juce::Button({}) {}
        std::function<void(juce::Graphics&, juce::Rectangle<int>)> paintFn;
        void paintButton(juce::Graphics& g, bool over, bool) override {
            if (over) { g.setColour(juce::Colours::white.withAlpha(.03f)); g.fillRect(getLocalBounds()); }
            if (paintFn) paintFn(g, getLocalBounds());
        }
    };
    juce::Colour live;
    juce::String strong, detail;
    Button prev {Button::Kind::Plain}, next {Button::Kind::Plain};
    NameButton name;
};

// Label, value and a thin track (web .arp-knob). Double-click resets.
class FeelSlider final : public juce::Component {
public:
    std::function<juce::String(double)> format;
    std::function<void(double)> onChange;
    FeelSlider(const juce::String& title, juce::Colour liveColour, double lo, double hi, double resetTo)
        : text(title), live(liveColour), reset(resetTo) {
        slider.setName(title); slider.setTooltip(title);
        slider.setRange(lo, hi, 1); slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setDoubleClickReturnValue(true, resetTo);
        slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff344455));
        slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff344455));
        slider.setColour(juce::Slider::thumbColourId, live);
        slider.onValueChange = [this] { repaint(); if (onChange) onChange(slider.getValue()); };
        addAndMakeVisible(slider);
    }
    void setValue(double v) { if (std::abs(v - slider.getValue()) > 1e-9) { slider.setValue(v, juce::dontSendNotification); repaint(); } }
    void setTitle(const juce::String& t) { if (t != text) { text = t; repaint(); } }
    juce::Slider& control() { return slider; }
    void resized() override { slider.setBounds(getLocalBounds().withTrimmedTop(13).expanded(4, 0)); }
    void paint(juce::Graphics& g) override {
        auto top = getLocalBounds().removeFromTop(12);
        label(g, text, top, c::label, 9, true);
        g.setColour(c::value); g.setFont(font(9));
        g.drawText(format ? format(slider.getValue()) : juce::String((int)slider.getValue()), top, juce::Justification::centredRight);
    }
private:
    juce::String text; juce::Colour live; double reset;
    juce::Slider slider;
};
}
