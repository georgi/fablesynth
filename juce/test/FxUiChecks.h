#pragma once
#include "../source/ui/FxChain.h"

// Exercise the shipping editor and real processor together. Captures live FX,
// checks tab navigation, parameter gestures, and stopped-audio meter clearing.
template <class T> T *findFxComponent(juce::Component &root, const juce::String &name = {}) {
    if (auto *found = dynamic_cast<T *>(&root); found && (name.isEmpty() || root.getName() == name))
        return found;
    for (auto *child : root.getChildren())
        if (auto *found = findFxComponent<T>(*child, name))
            return found;
    return nullptr;
}

template <class Processor, class ReadMeter>
bool runFxUiChecks(const juce::String &machine, int width, int height, bool drum, ReadMeter readMeter) {
    auto proc = std::make_unique<Processor>();
    proc->prepareToPlay(48000, 1600);
    auto set = [&](juce::String id, float value) {
        if (auto *p = proc->apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    juce::String prefix = drum ? "pad0." : "";
    for (auto fx : {"ott", "comp", "delay", "reverb"})
        set(prefix + "fx." + fx + ".on", 1);
    set(prefix + "fx.delay.time", .18f);
    set(prefix + "fx.delay.mix", .55f);
    set(prefix + "fx.reverb.mix", .4f);
    set(prefix + "fx.eq.on", 1);
    set(prefix + "fx.eq.low", 4);
    set(prefix + "fx.eq.mid", -5);
    set(prefix + "fx.eq.mid2", 3);
    set(prefix + "fx.eq.high", -2);
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc->createEditor());
    editor->setSize(width, height);
    editor->setVisible(true);
    auto *tab = findFxComponent<juce::TextButton>(*editor, "FX CHAIN");
    auto *sound = findFxComponent<juce::TextButton>(*editor, "SOUND");
    auto *chain = findFxComponent<fui::FxChain>(*editor);
    if (!tab || !sound || !chain || chain->isVisible())
        return false;
    sound->setToggleState(false, juce::dontSendNotification);
    tab->setToggleState(true, juce::dontSendNotification);
    tab->onClick();
    bool ok = chain->isVisible();
    for (auto *child : chain->getChildren())
        ok &= child->getWidth() >= 180 && child->getHeight() >= 90 &&
              chain->getLocalBounds().contains(child->getBounds());
    if (auto *eq = findFxComponent<fui::FxModuleView>(*chain, "EQ visual FX")) {
        auto *gain = proc->apvts.getParameter(prefix + "fx.eq.mid");
        if (!gain) return false;
        float before = gain->getValue();
        ok &= eq->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
        ok &= gain->getValue() > before;
        eq->keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
        ok &= std::abs(gain->getValue() - gain->getDefaultValue()) < 1e-6f;
        set(prefix + "fx.eq.mid", -5);
    } else { return false; }
    if (drum) {
        chain->setPad(7, "EQ ISOLATION");
        auto* eq = findFxComponent<fui::FxModuleView>(*chain, "EQ visual FX");
        auto* other = proc->apvts.getParameter("pad7.fx.eq.mid");
        if (!eq || !other) return false;
        const auto before = other->getValue();
        eq->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
        ok &= other->getValue() > before;
        ok &= std::abs(proc->apvts.getRawParameterValue("pad0.fx.eq.mid")->load() + 5.f) < 1e-5f;
        // Internal pad stride grows; established host automation positions must not.
        for (int pad = 0; pad < 16; ++pad)
            ok &= proc->apvts.getParameter("pad" + juce::String(pad) + ".oscA.table")->getParameterIndex() == pad * 73;
        ok &= proc->apvts.getParameter("seq.bpm")->getParameterIndex() == 16 * 73;
        chain->setPad(0, "KICK");
    }
    {
        set(prefix + "fx.eq.m2q", 3.2f);
        set(prefix + "fx.eq.m2type", 0);
        juce::MemoryBlock state;
        proc->getStateInformation(state);
        auto restored = std::make_unique<Processor>();
        restored->setStateInformation(state.getData(), (int)state.getSize());
        for (auto key : {"on", "mid", "m2q", "m2type"}) {
            const auto id = prefix + "fx.eq." + key;
            ok &= std::abs(restored->apvts.getRawParameterValue(id)->load() -
                           proc->apvts.getRawParameterValue(id)->load()) < 1e-5f;
        }
    }
    juce::AudioBuffer<float> buffer(juce::jmax(2, proc->getTotalNumOutputChannels()), 1600);
    bool received = false, echo = false, tail = false;
    for (int frame = 0; frame < 96; ++frame) {
        buffer.clear();
        juce::MidiBuffer midi;
        if (frame % 12 == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, drum ? 36 : 48, 0.8f), 0);
        if (frame % 12 == 5)
            midi.addEvent(juce::MidiMessage::noteOff(1, drum ? 36 : 48), 0);
        proc->processBlock(buffer, midi);
        auto meter = readMeter(*proc);
        received |= meter[fable::FxTelemetry::compIn] > -80;
        echo |= meter[fable::FxTelemetry::echoL] > -80;
        tail |= meter[fable::FxTelemetry::verbL] > -80;
        for (auto v : meter.values)
            ok &= std::isfinite(v);
        juce::Thread::sleep(34);
        juce::Timer::callPendingTimersSynchronously();
    }
    ok &= received && echo && tail;
    if (!received || !echo || !tail)
        printf("  missing meters: dynamics=%d echo=%d reverb=%d\n", received, echo, tail);
    auto dir = juce::File::getCurrentWorkingDirectory().getChildFile("build/fx-visuals");
    dir.createDirectory();
    auto save = [&](const juce::String &name) {
        auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        auto out = dir.getChildFile(machine + name + ".png").createOutputStream();
        if (!out) {
            ok = false;
            return;
        }
        out->setPosition(0);
        out->truncate();
        juce::PNGImageFormat png;
        ok &= png.writeImageToStream(image, *out);
    };
    save("-fx-live");
    juce::Thread::sleep(400);
    juce::Timer::callPendingTimersSynchronously();
    save("-fx-stopped");
    tab->setToggleState(false, juce::dontSendNotification);
    sound->setToggleState(true, juce::dontSendNotification);
    sound->onClick();
    ok &= !chain->isVisible();
    save("-sound");
    printf("  [%s] %s FX page: tabs, EQ edits, live dynamics/echo/reverb, finite telemetry, screenshots\n",
           ok ? "PASS" : "FAIL", machine.toRawUTF8());
    return ok;
}
