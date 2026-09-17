#pragma once
#include "../source/ui/ArpCodec.h"
#include "../source/ui/ArpPanel.h"
#include "FxUiChecks.h"

template<class Processor>
bool runArpProcessorChecks(const char* machine, bool bass) {
    bool ok = true;
    auto check = [&](bool pass, const char* what) {
        if (!pass) std::printf("  [FAIL] %s arp: %s\n", machine, what);
        ok &= pass;
    };
    auto proc = std::make_unique<Processor>();
    proc->prepareToPlay(48000, 128);
    auto set = [&](const char* id, float value) {
        if (auto* p = proc->apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    set("seq.bpm", 120);
    for (const auto* id : {"fx.drive.on", "fx.chorus.on", "fx.delay.on", "fx.reverb.on", "fx.ott.on", "fx.comp.on"}) set(id, 0);
    set(bass ? "aenv.rel" : "env1.rel", .005f);
    juce::AudioBuffer<float> buffer(2, 128);
    auto block = [&](juce::MidiBuffer midi = {}) {
        buffer.clear(); proc->processBlock(buffer, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                check(std::isfinite(buffer.getSample(c, i)), "finite audio");
    };
    auto send = [&](juce::MidiMessage message) { juce::MidiBuffer midi; midi.addEvent(message, 0); block(midi); };
    auto tailPeak = [&] {
        float peak = 0;
        for (int i = 0; i < 500; ++i) {
            block();
            if (i >= 450) peak = std::max(peak, buffer.getMagnitude(0, 128));
        }
        return peak;
    };
    auto a = bass ? fable::bassArpDefaults() : fable::ArpSettings{};
    a.enabled = true; a.keys = true; a.latch = false;
    proc->setArpSettings(a); proc->setSeqPlaying(true);
    send(juce::MidiMessage::noteOn(1, 48, .8f));
    float peak = 0;
    for (int i = 0; i < 20; ++i) { block(); peak = std::max(peak, buffer.getMagnitude(0, 128)); }
    check(peak > .0001f, "MIDI keys drive the arp");
    send(juce::MidiMessage::noteOff(1, 48));
    check(tailPeak() < .0001f, "unlatched key release empties pool");
    a.latch = true; proc->setArpSettings(a);
    send(juce::MidiMessage::noteOn(1, 48, .8f)); send(juce::MidiMessage::noteOff(1, 48));
    check(tailPeak() > .0001f, "latch keeps playing after key release");
    send(juce::MidiMessage::allNotesOff(1));
    check(tailPeak() < .0001f, "all notes off clears latch without retriggering");
    proc->setSeqPlaying(false); a.keys = false; a.latch = false; proc->setArpSettings(a);
    send(juce::MidiMessage::noteOn(1, 60, .8f));
    peak = 0;
    for (int i = 0; i < 20; ++i) { block(); peak = std::max(peak, buffer.getMagnitude(0, 128)); }
    check(peak > .0001f, "stored arp mode retains stopped keyboard audition");
    a.keys = true; proc->setArpSettings(a); block();
    send(juce::MidiMessage::noteOff(1, 60));
    check(tailPeak() < .0001f, "switching audition into played-key mode leaves no hanging note");
    a.keys = false; a.latch = false; a.order = 4; a.seed = 19;
    a.rate = 1.0 / 6; a.octaves = 2; a.gate = .37;
    a.hits[3] = false; a.accents[2] = true; a.slides[7] = true;
    proc->setArpSettings(a);
    juce::MemoryBlock state; proc->getStateInformation(state);
    auto restored = std::make_unique<Processor>();
    restored->setStateInformation(state.getData(), (int)state.getSize());
    check(juce::JSON::toString(fable::arpToVar(restored->getArpSettings())) == juce::JSON::toString(fable::arpToVar(a)),
          "plugin state round-trips all arp settings");
    a.enabled = false; proc->setArpSettings(a); proc->getStateInformation(state);
    restored->setStateInformation(state.getData(), (int)state.getSize());
    check(!restored->getArpSettings().enabled && restored->getArpSettings().seed == 19,
          "disabled arp retains its settings");
    if (auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int)state.getSize())) {
        if (auto* seq = xml->getChildByName(bass ? "BASS" : "NOTESEQ")) {
            seq->removeAttribute("arp");
            juce::MemoryBlock legacy; juce::AudioProcessor::copyXmlToBinary(*xml, legacy);
            a.enabled = true; restored->setArpSettings(a);
            restored->setStateInformation(legacy.getData(), (int)legacy.getSize());
            check(!restored->getArpSettings().enabled, "legacy plugin state resets arp to disabled");
        } else check(false, "sequencer state node exists");
    } else check(false, "plugin state is readable XML");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(proc->createEditor());
        editor->setVisible(true);
        auto* mode = findFxComponent<fui::ArpModeBar>(*editor);
        auto* arp = findFxComponent<fui::ArpPanel>(*editor);
        check(mode && arp, "native SEQ/ARP controls exist");
        if (mode && arp) {
            for (auto* child : mode->getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*>(child); button && button->getButtonText() == "ARP") button->onClick();
            check(arp->isVisible() && proc->getArpSettings().enabled, "ARP tab selects the live arp editor");
            for (auto* child : arp->getChildren()) if (child->isVisible())
                check(!child->getBounds().isEmpty() && arp->getLocalBounds().contains(child->getBounds()), "arp controls fit inside editor");
            if (auto* order = findFxComponent<juce::ComboBox>(*arp, "Arpeggiator order")) {
                order->setSelectedId(2, juce::sendNotificationSync);
                check(proc->getArpSettings().order == 1, "arp UI writes settings");
            } else check(false, "arp order selector exists");
            auto* input = findFxComponent<juce::ComboBox>(*arp, "Arpeggiator input");
            if (input) {
                input->setSelectedId(2, juce::sendNotificationSync); block();
                juce::TextButton* key = nullptr;
                for (auto* child : arp->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(child); b && b->getButtonText() == (bass ? "C2" : "C3")) key = b;
                check(key != nullptr, "arp on-screen root key exists");
                if (key) {
                    key->setState(juce::Button::buttonDown); block();
                    const auto live = proc->getLiveArp();
                    check(live.count == 1 && live.notes[0] == (bass ? 36 : 48), "on-screen key enters live arp pool");
                    key->setState(juce::Button::buttonNormal); block();
                    check(proc->getLiveArp().count == 0, "on-screen key release clears unlatched pool");
                }
                input->setSelectedId(1, juce::sendNotificationSync);
            } else check(false, "arp input selector exists");
            const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile("build/fx-visuals");
            dir.createDirectory();
            if (auto out = dir.getChildFile(juce::String(machine) + "-arp.png").createOutputStream()) {
                out->setPosition(0); out->truncate();
                juce::PNGImageFormat png;
                check(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), *out), "native arp snapshot writes");
            } else check(false, "native arp snapshot output opens");
        }
    }
    std::printf("  [%s] %s arp processor: MIDI, latch, panic, state round-trip, native UI\n", ok ? "PASS" : "FAIL", machine);
    return ok;
}
