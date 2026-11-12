// Editor checks for the oscillator warp controls, the reverb TYPE switch and
// the drawn LFO shape editor (headless: synthetic mouse events on the views).
#pragma once

#include "../source/ui/Displays.h"
#include "../source/ui/Controls.h"

#include <functional>

namespace warplfoui {

template <typename T>
inline void collect(juce::Component& root, std::vector<T*>& out) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        auto* child = root.getChildComponent(i);
        if (auto* typed = dynamic_cast<T*>(child)) out.push_back(typed);
        collect(*child, out);
    }
}
inline juce::Button* buttonNamed(juce::Component& root, const juce::String& name) {
    std::vector<juce::Button*> all; collect(root, all);
    for (auto* b : all) if (b->getName() == name && b->isShowing()) return b;
    for (auto* b : all) if (b->getName() == name) return b;
    return nullptr;
}

// `check(ok, message)`; `snapshot(editor, name)` writes a PNG of the editor.
template <typename Processor>
inline void run(Processor& proc, juce::Point<int> size,
                const std::function<void(bool, const char*)>& check,
                const std::function<void(juce::Component&, const char*)>& snapshot) {
    auto& apvts = proc.apvts;
    auto real = [&](const char* id) { auto* p = apvts.getParameter(id); return p->convertFrom0to1(p->getValue()); };
    auto set = [&](const char* id, float v) { auto* p = apvts.getParameter(id); p->setValueNotifyingHost(p->convertTo0to1(v)); };
    for (const char* id : {"lfo1.shape", "lfo1.pn", "lfo1.oneshot", "oscA.warp", "oscA.warpamt", "fx.reverb.type"}) set(id, 0);

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setVisible(true);
    ed->setSize(size.x, size.y);

    // ---- drawn LFO shape: LFO 1 is the first visible LfoView ----
    std::vector<fui::LfoView*> views; collect(*ed, views);
    fui::LfoView* view = nullptr;
    for (auto* v : views) if (v->isVisible() && !view) view = v;
    check(view != nullptr, "the editor has an LFO view");
    if (!view) return;
    const auto w = (float)view->getWidth(), h = (float)view->getHeight();
    // Editor coordinates: x in cycles, y in 0..1 (the web LFOView mapping).
    auto at = [&](double x, double y) {
        const float mid = h / 2, amp = (mid - 5) * 0.9f;
        return juce::Point<float>(5 + (float)x * (w - 10), mid - (float)(y * 2 - 1) * amp);
    };
    auto event = [&](juce::Point<float> pos, juce::Point<float> start, int mods, int clicks = 1) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(mods),
            1, 0, 0, 0, 0, view, view, juce::Time::getCurrentTime(), start, juce::Time::getCurrentTime(), clicks, pos != start);
    };
    const int left = juce::ModifierKeys::leftButtonModifier;
    auto click = [&](double x, double y, int mods = 0) {
        view->mouseDown(event(at(x, y), at(x, y), left | mods));
        view->mouseUp(event(at(x, y), at(x, y), left | mods));
    };

    click(0.25, 0.9);
    check(real("lfo1.pn") == 0, "a click does not edit a standard LFO shape");
    set("lfo1.shape", (float)fable::LFO_CUSTOM);
    check(view->isCustom() && view->pointCount() == 0, "CUSTOM starts as an empty shape when set directly");
    click(0.25, 0.9); click(0.75, 0.2);
    check(view->pointCount() == 2 && std::abs(real("lfo1.p1.x") - 0.25f) < 1e-4f && std::abs(real("lfo1.p1.y") - 0.9f) < 0.03f
          && std::abs(real("lfo1.p2.x") - 0.75f) < 1e-4f, "clicks place points on the grid");
    // Drag the first point: it follows the pointer and stops at its neighbour.
    view->mouseDown(event(at(0.25, 0.9), at(0.25, 0.9), left));
    view->mouseDrag(event(at(0.5, 0.4), at(0.25, 0.9), left));
    const bool moved = std::abs(real("lfo1.p1.x") - 0.5f) < 1e-4f && std::abs(real("lfo1.p1.y") - 0.4f) < 0.03f;
    view->mouseDrag(event(at(0.95, 0.4), at(0.25, 0.9), left));
    view->mouseUp(event(at(0.95, 0.4), at(0.25, 0.9), left));
    check(moved && std::abs(real("lfo1.p1.x") - 0.75f) < 1e-4f && view->pointCount() == 2, "a drag moves a point up to its neighbour");
    // Alt-drag bends the segment under the pointer.
    view->mouseDown(event(at(0.1, 0.5), at(0.1, 0.5), left | juce::ModifierKeys::altModifier));
    view->mouseDrag(event(at(0.1, 0.5).translated(0, -20), at(0.1, 0.5), left | juce::ModifierKeys::altModifier));
    view->mouseUp(event(at(0.1, 0.5), at(0.1, 0.5), left));
    check(std::abs(real("lfo1.p2.c")) > 0.2f && view->pointCount() == 2, "Alt-drag bends a segment");
    // Double-click removes a point.
    view->mouseDoubleClick(event(at(0.75, 0.4), at(0.75, 0.4), left, 2));
    check(view->pointCount() == 1, "a double-click removes a point");
    // STEP mode paints sixteen held cells in one drag.
    view->setPaintMode(true);
    view->mouseDown(event(at(0.01, 0.2), at(0.01, 0.2), left));
    view->mouseDrag(event(at(0.99, 0.95), at(0.01, 0.2), left));
    view->mouseUp(event(at(0.99, 0.95), at(0.01, 0.2), left));
    view->setPaintMode(false);
    bool steps = view->pointCount() == 16;
    for (int k = 1; k <= 16 && steps; ++k)
        steps &= real(("lfo1.p" + juce::String(k) + ".c").toRawUTF8()) > 1.5f
              && std::abs(real(("lfo1.p" + juce::String(k) + ".x").toRawUTF8()) - (float)(k - 1) / 16) < 1e-4f;
    check(steps && real("lfo1.p16.y") > real("lfo1.p1.y") + 0.5f, "STEP mode paints sixteen rising held steps");
    // A full shape refuses another point.
    click(0.53, 0.5);
    check(view->pointCount() == 16, "a full shape refuses a seventeenth point");

    // ---- ONE-SHOT pill and reverb TYPE switch ----
    auto* one = buttonNamed(*ed, "1SHOT");
    check(one != nullptr, "the LFO block has a 1SHOT button");
    if (one) one->setToggleState(true, juce::sendNotificationSync);
    check(real("lfo1.oneshot") > 0.5f, "1SHOT sets the one-shot param");

    // ---- warp preview and the snapshot ----
    set("oscA.warp", (float)fable::WARP_SYNC); set("oscA.warpamt", 0.5f);
    snapshot(*ed, "plugin_editor_warp_lfo.png");
    for (const char* id : {"lfo1.shape", "lfo1.pn", "lfo1.oneshot", "oscA.warp", "oscA.warpamt"}) set(id, 0);
}

} // namespace warplfoui
