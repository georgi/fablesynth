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
    // DR-1 exposes the same unscoped FX parameter ids as the other devices:
    // its rack is a group channel strip rather than a selected-pad editor.
    juce::String prefix;
    for (auto fx : {"ott", "comp", "chorus", "delay", "reverb"})
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
    auto* comp = findFxComponent<fui::FxModuleView>(*chain, "COMP visual FX");
    auto* drive = findFxComponent<fui::FxModuleView>(*chain, "DRIVE visual FX");
    auto* chorus = findFxComponent<fui::FxModuleView>(*chain, "CHORUS visual FX");
    if (!comp || !drive || !chorus) return false;
    ok &= drive->getY() == chorus->getY() && drive->getRight() < chorus->getX();
    ok &= chorus->getWidth() >= 220;
    auto checkKnobs = [&](juce::Component& module, const juce::StringArray& keys) {
        int index = 0;
        for (auto* child : module.getChildren()) {
            if (auto* knob = dynamic_cast<fui::Knob*>(child)) {
                if (index >= keys.size()) return false;
                auto* param = proc->apvts.getParameter(prefix + keys[index++]);
                if (!param || knob->getWidth() < 40 || knob->getHeight() < 44) return false;
                param->setValueNotifyingHost(.4f);
                juce::MouseWheelDetails wheel; wheel.deltaY = .1f;
                const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),
                    {}, {}, 1, 0, 0, 0, 0, knob, knob, juce::Time::getCurrentTime(),
                    {}, juce::Time::getCurrentTime(), 1, false);
                knob->mouseWheelMove(event, wheel);
                if (param->getValue() <= .4f) return false;
            }
        }
        return index == keys.size();
    };
    ok &= checkKnobs(*comp, {"fx.comp.thr", "fx.comp.att", "fx.comp.rel", "fx.comp.ratio"});
    ok &= checkKnobs(*drive, {"fx.drive.amt", "fx.drive.tone", "fx.drive.mix"});
    ok &= checkKnobs(*chorus, {"fx.chorus.rate", "fx.chorus.depth", "fx.chorus.mix"});
    // Hash only the plot: a changing knob label must not masquerade as a
    // working visualizer. Preview redraws without audio or a running timer.
    auto chorusPlotHash = [](fui::FxModuleView& module) {
        const auto image = module.createComponentSnapshot({12, 40, module.getWidth() - 24,
                                                           module.getHeight() / 2 - 40});
        uint64_t hash = 1469598103934665603ull;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                hash = (hash ^ image.getPixelAt(x, y).getARGB()) * 1099511628211ull;
        return hash;
    };
    set(prefix + "fx.chorus.rate", .6f);
    set(prefix + "fx.chorus.depth", .5f);
    const auto preview = chorusPlotHash(*chorus);
    set(prefix + "fx.chorus.rate", 4);
    ok &= chorusPlotHash(*chorus) != preview;
    set(prefix + "fx.chorus.rate", .6f);
    set(prefix + "fx.chorus.depth", 1);
    ok &= chorusPlotHash(*chorus) != preview;
    auto* hard = findFxComponent<juce::TextButton>(*drive, "HARD");
    if (!hard) return false;
    hard->onClick();
    ok &= proc->apvts.getRawParameterValue(prefix + "fx.drive.type")->load() == 2;
    set(prefix + "fx.drive.on", 1);
    if (drum) {
        // Established pad automation positions remain stable; the new group
        // strip is appended after them rather than rebinding the editor.
        // Internal pad stride grows; established host automation positions must not.
        for (int pad = 0; pad < 16; ++pad)
            ok &= proc->apvts.getParameter("pad" + juce::String(pad) + ".oscA.table")->getParameterIndex() == pad * 73;
        ok &= proc->apvts.getParameter("seq.bpm")->getParameterIndex() == 16 * 73;
        // The appended controls preserve all previous EQ automation positions too.
        for (int pad = 0; pad < 16; ++pad)
            ok &= proc->apvts.getParameter("pad" + juce::String(pad) + ".fx.eq.on")->getParameterIndex() == 16 * 73 + 3 + pad * 21;
        ok &= proc->apvts.getParameter("fx.eq.mid") != nullptr;
    }
    {
        set(prefix + "fx.eq.m2q", 3.2f);
        set(prefix + "fx.eq.m2type", 0);
        juce::MemoryBlock state;
        proc->getStateInformation(state);
        auto restored = std::make_unique<Processor>();
        restored->setStateInformation(state.getData(), (int)state.getSize());
        for (auto key : {"fx.eq.on", "fx.eq.mid", "fx.eq.m2q", "fx.eq.m2type", "fx.comp.att", "fx.comp.rel", "fx.comp.ratio", "fx.drive.tone", "fx.drive.type"}) {
            const auto id = prefix + key;
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
    printf("  [%s] %s FX page: tabs, EQ edits, Chorus preview/layout, live dynamics/echo/reverb, finite telemetry, screenshots\n",
           ok ? "PASS" : "FAIL", machine.toRawUTF8());
    return ok;
}
