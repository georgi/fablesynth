#pragma once
#include "DeviceUiModel.h"
#include "Theme.h"

namespace fui {
// Shared WT/BL surface; all writes go through the UI model, so hosted edits
// belong to the selected clip and never overwrite its sequencer bytes.
class ArpPanel final : public juce::Component, private juce::Timer {
public:
    template<class Model>
    ArpPanel(Model& m, bool bass) : model(m), bass_(bass), octave_(bass ? 2 : 3),
        playing([&m] { return m.sequencerPlaying(); }), current([&m] { return m.currentStep(); }),
        play([&m](bool b) { m.setSequencerPlaying(b); }) {
        auto combo = [&](juce::ComboBox& c, const juce::String& name, const juce::StringArray& items) {
            c.setName(name); c.setTooltip(name); c.addItemList(items, 1); addAndMakeVisible(c);
        };
        combo(order, "Arpeggiator order", {"UP", "DOWN", "UP / DOWN", "AS PLAYED", "RANDOM"});
        combo(rate, "Arpeggiator rate", {"1/4", "1/8 DOTTED", "1/8", "1/8 TRIPLET", "1/16 DOTTED", "1/16", "1/16 TRIPLET", "1/32"});
        combo(octaves, "Arpeggiator octaves", {"1 OCTAVE", "2 OCTAVES", "3 OCTAVES"});
        combo(input, "Arpeggiator input", {"STORED NOTES", "PLAYED KEYS"});
        order.onChange = [this] { change([&](auto& a) { a.order = order.getSelectedId() - 1; }); };
        rate.onChange = [this] { change([&](auto& a) { a.rate = rates[(size_t)std::max(0, rate.getSelectedId() - 1)]; }); };
        octaves.onChange = [this] { change([&](auto& a) { a.octaves = octaves.getSelectedId(); }); };
        input.onChange = [this] { change([&](auto& a) { a.keys = input.getSelectedId() == 2; }); };
        auto slider = [&](juce::Slider& s, const char* name, double lo, double hi, double step) {
            s.setName(name); s.setTooltip(name); s.setRange(lo, hi, step);
            s.setSliderStyle(juce::Slider::LinearHorizontal); s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 22); addAndMakeVisible(s);
        };
        slider(gate, "Gate %", 5, 95, 1); gate.setTextValueSuffix("%");
        slider(swing, "Swing %", 0, 100, 1); swing.setTextValueSuffix("%");
        slider(tempo, "Tempo BPM", 60, 200, 1);
        gate.setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 22);
        swing.setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 22);
        tempo.setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 22);
        gate.textFromValueFunction = [](double v) { return "GATE " + juce::String((int)v) + "%"; };
        swing.textFromValueFunction = [](double v) { return "SWING " + juce::String((int)v) + "%"; };
        tempo.textFromValueFunction = [](double v) { return juce::String((int)v) + " BPM"; };
        for (auto* s : {&gate, &swing, &tempo})
            s->valueFromTextFunction = [](const juce::String& text) { return text.retainCharacters("0123456789.-").getDoubleValue(); };
        gate.onValueChange = [this] { change([&](auto& a) { a.gate = gate.getValue() / 100; }); };
        swing.onValueChange = [this] {
            if (model.capabilities().hosted) model.setArpSwing(swing.getValue() / 100);
            else setParameter(bass_ ? "master.swing" : "seq.swing", swing.getValue() / 100);
        };
        tempo.onValueChange = [this] { setParameter("seq.bpm", tempo.getValue()); };
        auto button = [&](juce::TextButton& b, const char* text, std::function<void()> f) {
            b.setButtonText(text); b.onClick = std::move(f); addAndMakeVisible(b);
        };
        button(transport, "PLAY", [this] { play(!playing() && !model.arpQueued()); });
        button(latch, "LATCH", [this] { change([](auto& a) { a.latch = !a.latch; }); });
        button(store, "STORE", [this] { auto live = model.arpLiveSettings(); change([&](auto& a) { a.notes = live.notes; a.count = live.count; a.keys = false; }); });
        button(clear, "CLEAR", [this] { if (model.arpSettings().keys) model.clearArpKeys(); else change([](auto& a) { a.count = 0; }); });
        button(seed, "NEW SEED", [this] { change([](auto& a) { ++a.seed; }); });
        button(lower, "OCT -", [this] { octave_ = std::max(1, octave_ - 1); refresh(); });
        button(higher, "OCT +", [this] { octave_ = std::min(6, octave_ + 1); refresh(); });
        for (int n = 0; n < 12; ++n) {
            heldKeys[n] = -1;
            addAndMakeVisible(keys[n]); keys[n].onClick = [this, n] {
                if (model.arpSettings().keys) return;
                change([&](auto& a) {
                    const int note = (octave_ + 1) * 12 + n;
                    auto it = std::find(a.notes.begin(), a.notes.begin() + a.count, note);
                    if (it != a.notes.begin() + a.count) { std::move(it + 1, a.notes.begin() + a.count, it); --a.count; }
                    else if (a.count < 128) a.notes[a.count++] = note;
                });
            };
            keys[n].onStateChange = [this, n] {
                if (keys[n].isDown() && heldKeys[n] < 0 && model.arpSettings().keys) {
                    heldKeys[n] = (octave_ + 1) * 12 + n; model.arpKeyInput(heldKeys[n], true);
                } else if (!keys[n].isDown() && heldKeys[n] >= 0) {
                    model.arpKeyInput(heldKeys[n], false); heldKeys[n] = -1;
                }
            };
        }
        for (int row = 0; row < (bass_ ? 3 : 2); ++row) for (int s = 0; s < 16; ++s) {
            auto& b = lanes[row * 16 + s]; addAndMakeVisible(b); b.setButtonText(juce::String(s + 1));
            b.setName(juce::String(row == 0 ? "Hit step " : row == 1 ? "Accent step " : "Slide into step ") + juce::String(s + 1));
            b.onClick = [this, row, s] { change([&](auto& a) { auto& lane = row == 0 ? a.hits : row == 1 ? a.accents : a.slides; lane[s] = !lane[s]; }); };
        }
        refresh(); startTimerHz(20);
    }
    ~ArpPanel() override { for (int n : heldKeys) if (n >= 0) model.arpKeyInput(n, false); }
    void resized() override {
        auto area = getLocalBounds().reduced(9); auto row = area.removeFromTop(28);
        for (auto* c : std::initializer_list<juce::Component*>{&transport, &order, &rate, &octaves}) { c->setBounds(row.removeFromLeft(c == &transport ? 78 : 150).reduced(2)); }
        gate.setBounds(row.removeFromLeft(180)); swing.setBounds(row.removeFromLeft(180)); tempo.setBounds(row.removeFromLeft(180));
        auto source = area.removeFromTop(29);
        input.setBounds(source.removeFromLeft(160).reduced(2));
        for (auto* c : {&latch, &store, &clear, &seed}) c->setBounds(source.removeFromRight(90).reduced(2));
        poolBounds = source;
        auto keyboard = area.removeFromTop(27);
        lower.setBounds(keyboard.removeFromLeft(72)); higher.setBounds(keyboard.removeFromRight(72));
        const int kw = keyboard.getWidth() / 12;
        for (auto& key : keys) key.setBounds(keyboard.removeFromLeft(kw).reduced(2, 0));
        auto rhythm = area.removeFromBottom(bass_ ? 69 : 46);
        for (int rowIndex = 0; rowIndex < (bass_ ? 3 : 2); ++rowIndex) {
            auto lane = rhythm.removeFromTop(23); lane.removeFromLeft(40);
            const int w = lane.getWidth() / 16;
            for (int s = 0; s < 16; ++s) lanes[rowIndex * 16 + s].setBounds(lane.removeFromLeft(w).reduced(2, 1));
        }
        plotBounds = area.reduced(40, 4); repaint();
    }
    void paint(juce::Graphics& g) override {
        g.setColour(col::panelLo); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8);
        const auto a = model.arpSettings(), live = a.keys ? model.arpLiveSettings() : a;
        g.setColour(col::textHint); g.setFont(12);
        juce::String pool;
        for (int i = 0; i < live.count; ++i) pool += name(live.notes[i]) + "  ";
        g.drawFittedText(pool.isEmpty() ? "WAITING FOR NOTES" : pool, poolBounds, juce::Justification::centredLeft, 1);
        const auto p = fable::compileArp(live);
        const juce::Colour arpColour = bass_ ? juce::Colour(0xff4dff9e) : col::acA;
        int low = bass_ ? 36 : 48, high = low + 12;
        for (auto n : p.notes) if (n >= 0) { low = std::min(low, n); high = std::max(high, n); }
        const float width = plotBounds.getWidth() / 16.0f;
        for (int i = 0; i < 16; ++i) {
            const float x = plotBounds.getX() + i * width;
            if (playing() && current() == i) { g.setColour(arpColour.withAlpha(.12f)); g.fillRect(x, (float)plotBounds.getY(), width, (float)plotBounds.getHeight()); }
            if (p.notes[i] < 0) continue;
            const float y = plotBounds.getBottom() - 15.0f - (float)(p.notes[i] - low) / std::max(1, high - low) * std::max(0, plotBounds.getHeight() - 30);
            g.setColour((p.accents[i] ? col::acB : arpColour).withAlpha(p.hits[i] ? 1.0f : .2f));
            g.fillRoundedRectangle(x + 2, y, std::max(8.0f, width * (float)p.gate), 5, 2);
            g.drawText(name(p.notes[i]), (int)x + 2, (int)y - 14, (int)width, 14, juce::Justification::centredLeft);
        }
        for (int row = 0; row < (bass_ ? 3 : 2); ++row) {
            g.setColour(col::textHint); g.drawText(row == 0 ? "HIT" : row == 1 ? "ACC" : "SLD", 10, lanes[row * 16].getY(), 35, 22, juce::Justification::centredLeft);
        }
    }
private:
    template<class Fn> void change(Fn&& f) { auto a = model.arpSettings(); f(a); model.setArpSettings(a); refresh(); }
    static juce::String name(int n) { static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; return n < 0 ? "—" : juce::String(names[n % 12]) + juce::String(n / 12 - 1); }
    void setParameter(const char* id, double v) { if (auto* p = model.parameters().parameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((float)v)); p->endChangeGesture(); } }
    double parameter(const char* id, double fallback) const { auto* p = model.parameters().parameter(id); return p ? p->convertFrom0to1(p->getValue()) : fallback; }
    void timerCallback() override { if (isShowing()) refresh(); }
    void refresh() {
        const auto a = model.arpSettings(); const bool hosted = model.capabilities().hosted;
        order.setSelectedId(a.order + 1, juce::dontSendNotification);
        const auto it = std::find(rates.begin(), rates.end(), a.rate); rate.setSelectedId((int)(it - rates.begin()) + 1, juce::dontSendNotification);
        octaves.setSelectedId(a.octaves, juce::dontSendNotification); input.setSelectedId(a.keys ? 2 : 1, juce::dontSendNotification);
        input.setEnabled(!hosted); latch.setVisible(!hosted && a.keys); store.setVisible(!hosted && a.keys);
        latch.setToggleState(a.latch, juce::dontSendNotification); seed.setVisible(a.order == 4);
        gate.setValue(a.gate * 100, juce::dontSendNotification);
        swing.setValue((hosted ? model.arpSwing() : parameter(bass_ ? "master.swing" : "seq.swing", 0)) * 100, juce::dontSendNotification);
        tempo.setEnabled(!hosted); tempo.setValue(hosted ? model.arpTempo() : parameter("seq.bpm", 120), juce::dontSendNotification);
        transport.setButtonText(model.arpQueued() ? "QUEUED" : playing() ? "STOP" : hosted ? "LAUNCH" : "PLAY");
        for (int i = 0; i < 12; ++i) {
            const int n = (octave_ + 1) * 12 + i;
            keys[i].setButtonText(name(n));
            const auto keyPool = a.keys ? model.arpLiveSettings() : a;
            keys[i].setToggleState(std::find(keyPool.notes.begin(), keyPool.notes.begin() + keyPool.count, n) != keyPool.notes.begin() + keyPool.count, juce::dontSendNotification);
        }
        for (int row = 0; row < (bass_ ? 3 : 2); ++row) for (int i = 0; i < 16; ++i)
            lanes[row * 16 + i].setToggleState((row == 0 ? a.hits : row == 1 ? a.accents : a.slides)[i], juce::dontSendNotification);
        repaint();
    }
    DeviceUiModel& model; bool bass_; int octave_;
    std::function<bool()> playing; std::function<int()> current; std::function<void(bool)> play;
    const std::array<double, 8> rates {{1, .75, .5, 1.0/3, .375, .25, 1.0/6, .125}};
    juce::ComboBox order, rate, octaves, input;
    juce::Slider gate, swing, tempo;
    juce::TextButton transport, latch, store, clear, seed, lower, higher;
    std::array<juce::TextButton, 12> keys;
    std::array<int, 12> heldKeys {};
    std::array<juce::TextButton, 48> lanes;
    juce::Rectangle<int> poolBounds, plotBounds;
};

class ArpModeBar final : public juce::Component, private juce::Timer {
public:
    ArpModeBar(DeviceUiModel& m, juce::Component& seq, ArpPanel& arp) : model(m), seq_(seq), arp_(arp) {
        addAndMakeVisible(sequence); addAndMakeVisible(arpeggiator);
        sequence.setButtonText("SEQ"); arpeggiator.setButtonText("ARP");
        sequence.onClick = [this] { select(false); }; arpeggiator.onClick = [this] { select(true); };
        timerCallback(); startTimerHz(15);
    }
    void resized() override { sequence.setBounds(0, 0, 65, getHeight()); arpeggiator.setBounds(70, 0, 65, getHeight()); timerCallback(); }
private:
    void select(bool on) { auto a = model.arpSettings(); a.enabled = on; model.setArpSettings(a); timerCallback(); }
    void timerCallback() override { const bool on = model.arpSettings().enabled; sequence.setToggleState(!on, juce::dontSendNotification); arpeggiator.setToggleState(on, juce::dontSendNotification); seq_.setVisible(!on); arp_.setVisible(on); }
    DeviceUiModel& model; juce::Component& seq_; ArpPanel& arp_; juce::TextButton sequence, arpeggiator;
};
}
