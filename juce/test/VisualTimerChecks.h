#pragma once
#include "../source/ui/VisualTimer.h"

inline bool checkVisualTimers() {
    struct Probe : juce::Component, fui::VisualTimer {
        int calls = 0, visibilityChanges = 0;
        std::function<void()> action;
        explicit Probe(int hz) { startVisualTimer(*this, hz); }
        void timerCallback() override { ++calls; if (action) action(); }
        void visualVisibilityChanged(bool) override { ++visibilityChanges; }
        void stop() { stopTimer(); }
    };
    juce::Component root, group, otherRoot;
    root.setVisible(true); otherRoot.setVisible(true);
    root.addAndMakeVisible(group);
    Probe fast(60), slow(20);
    group.addAndMakeVisible(fast); group.addAndMakeVisible(slow);
    bool ok = fast.calls == 0 && slow.calls == 0;
    ok &= root.getPeer() == nullptr && fui::VisualTimer::clockCountForTesting() == 1;
    for (int i = 0; i < 240; ++i) fui::VisualTimer::tickForTesting(i / 240.0);
    ok &= fast.calls == 60 && slow.calls == 20;
    fast.setVisible(false);
    ok &= fui::VisualTimer::clockCountForTesting() == 1
        && fui::VisualTimer::subscriptionCountForTesting() == 1;
    fast.setVisible(true);
    ok &= fui::VisualTimer::subscriptionCountForTesting() == 2;
    group.setVisible(false);
    ok &= fui::VisualTimer::clockCountForTesting() == 0;
    fui::VisualTimer::tickForTesting(2.0);
    ok &= fast.calls == 60 && slow.calls == 20;
    group.setVisible(true);
    fui::VisualTimer::tickForTesting(3.0);
    ok &= fast.calls == 61 && slow.calls == 21;
    otherRoot.addAndMakeVisible(fast);
    ok &= fui::VisualTimer::clockCountForTesting() == 2
        && fui::VisualTimer::subscriptionCountForTesting() == 2;
    fui::VisualTimer::tickForTesting(4.0);
    ok &= fast.calls == 62 && slow.calls == 22;
    fast.stop();
    ok &= fui::VisualTimer::subscriptionCountForTesting() == 1;
    slow.stop();
    ok &= fui::VisualTimer::clockCountForTesting() == 0;

    // Mutating another subscription while dispatching must not leave a dangling callback.
    Probe first(60);
    group.addAndMakeVisible(first);
    auto second = std::make_unique<Probe>(60);
    group.addAndMakeVisible(*second);
    first.action = [&] { second.reset(); first.stop(); };
    fui::VisualTimer::tickForTesting(5.0);
    ok &= !second && first.calls == 1 && fui::VisualTimer::clockCountForTesting() == 0;
    return ok;
}
