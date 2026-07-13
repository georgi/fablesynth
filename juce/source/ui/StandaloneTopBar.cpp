#include "StandaloneTopBar.h"

namespace fui {

TopBar::TopBar(juce::AudioProcessorValueTreeState& s, FableAudioProcessor& p)
    : proc(p),
      scope([&p](float* out, int n) { p.readScope(out, n); }, col::acA),
      spectrum([&p](float* out, int n) { p.readScope(out, n); },
               [&p] { return p.getCurrentSr(); }, col::acB),
      master(s, "master.volume", Knob::Md, Accent::N) {
    addAndMakeVisible(scope); addAndMakeVisible(spectrum); addAndMakeVisible(master);
    for (int i = 0; i < proc.getNumPrograms(); ++i) presets.addItem(proc.getProgramName(i), i + 1);
    presets.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.onChange = [this] { proc.setCurrentProgram(presets.getSelectedId() - 1); };
    addAndMakeVisible(presets);
    prev.onClick = [this] {
        int n = proc.getNumPrograms(), i = (proc.getCurrentProgram() - 1 + n) % n;
        proc.setCurrentProgram(i); presets.setSelectedId(i + 1, juce::dontSendNotification);
    };
    next.onClick = [this] {
        int n = proc.getNumPrograms(), i = (proc.getCurrentProgram() + 1) % n;
        proc.setCurrentProgram(i); presets.setSelectedId(i + 1, juce::dontSendNotification);
    };
    addAndMakeVisible(prev); addAndMakeVisible(next); addAndMakeVisible(save);
    startTimerHz(15);
}

void TopBar::timerCallback() {
    if (proc.getVoiceCount() != lastVoices || proc.getMidiActive() != lastMidi) {
        lastVoices = proc.getVoiceCount(); lastMidi = proc.getMidiActive();
        repaint(statusArea);
    }
}

void TopBar::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    g.setFont(dispFont(17.0f));
    int bx = brandArea.getX(), by = brandArea.getY();
    g.setColour(col::text);
    drawSpaced(g, "FABLE", { bx, by, 70, brandArea.getHeight() }, 1.5f);
    int fableW = (int)g.getCurrentFont().getStringWidthFloat("FABLE") + 5 * 5;
    g.setColour(col::acA);
    drawSpaced(g, "SYNTH", { bx + fableW, by, 80, brandArea.getHeight() }, 1.5f);
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    drawSpaced(g, "WT-1", { bx + fableW + 88, by + 4, 50, brandArea.getHeight() }, 2.0f);
    for (auto& bx2 : { std::make_pair(scopeBox, juce::String("SCOPE")),
                       std::make_pair(specBox, juce::String("SPECTRUM")) }) {
        drawDisplayBox(g, bx2.first.toFloat(), 8.0f);
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        drawSpaced(g, bx2.second, bx2.first.reduced(6, 3).removeFromTop(10), 2.0f, juce::Justification::right);
    }
    auto st = statusArea;
    auto midiRow = st.removeFromTop(st.getHeight() / 2);
    auto led = midiRow.removeFromLeft(14).withSizeKeepingCentre(7, 7).toFloat();
    g.setColour(proc.getMidiActive() ? col::acA : juce::Colour(0xff232936));
    g.fillEllipse(led);
    if (proc.getMidiActive()) { g.setColour(col::acA.withAlpha(0.5f)); g.fillEllipse(led.expanded(2)); }
    g.setColour(col::textDim); g.setFont(monoFont(9.0f));
    g.drawText("MIDI", midiRow, juce::Justification::centredLeft);
    g.setColour(col::acB);
    g.drawText(juce::String(proc.getVoiceCount()), st.removeFromLeft(14), juce::Justification::centred);
    g.setColour(col::textDim);
    g.drawText("VOICES", st, juce::Justification::centredLeft);
}

void TopBar::resized() {
    auto r = getLocalBounds().reduced(16, 10);
    brandArea = r.removeFromLeft(230).withSizeKeepingCentre(230, 22);
    auto pb = r.removeFromLeft(330).withSizeKeepingCentre(330, 28);
    prev.setBounds(pb.removeFromLeft(28)); pb.removeFromLeft(6);
    save.setBounds(pb.removeFromRight(46)); pb.removeFromRight(6);
    next.setBounds(pb.removeFromRight(28)); pb.removeFromRight(6);
    presets.setBounds(pb);
    master.setBounds(r.removeFromRight(70)); r.removeFromRight(10);
    statusArea = r.removeFromRight(96); r.removeFromRight(10);
    specBox = r.removeFromRight(168).withSizeKeepingCentre(168, 46); r.removeFromRight(10);
    scopeBox = r.removeFromRight(168).withSizeKeepingCentre(168, 46);
    scope.setBounds(scopeBox.reduced(1));
    spectrum.setBounds(specBox.reduced(1));
}

} // namespace fui
