#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace fui {
// Message-thread visual polling shared by all controls in a native editor.
// No native peer is created: unattached components can be driven explicitly in tests.
class VisualTimer : private juce::ComponentListener {
    struct Client {
        VisualTimer* owner = nullptr;
        double next = -1.0;
        int hz = 60;
    };
    struct Clock : std::enable_shared_from_this<Clock> {
        juce::Component::SafePointer<juce::Component> root;
        std::unique_ptr<juce::VBlankAttachment> attachment;
        std::vector<std::weak_ptr<Client>> clients;
        double next = -1.0;
        explicit Clock(juce::Component& c) : root(&c) {}
        void attach() {
            if (!attachment && root && root->getPeer())
                attachment = std::make_unique<juce::VBlankAttachment>(root.getComponent(),
                    [this](double seconds) { auto keepAlive = shared_from_this(); tick(seconds); });
        }
        static void advance(double& deadline, double now, double interval) {
            if (deadline < 0.0) deadline = now;
            deadline += (std::floor(std::max(0.0, now - deadline) / interval) + 1.0) * interval;
        }
        void tick(double now) {
            if (!std::isfinite(now) || (next >= 0.0 && now + 1.0e-9 < next)) return;
            advance(next, now, 1.0 / 60.0);
            // A callback may stop/delete/reparent itself or another client.
            const auto snapshot = clients;
            for (const auto& weak : snapshot) {
                const auto c = weak.lock();
                if (!c || !c->owner || c->owner->clock.get() != this) continue;
                auto* owner = c->owner;
                if (!owner->component || !visible(*owner->component)) continue;
                if (c->next >= 0.0 && now + 1.0e-9 < c->next) continue;
                advance(c->next, now, 1.0 / c->hz);
                owner->timerCallback();
            }
            clients.erase(std::remove_if(clients.begin(), clients.end(),
                [](const auto& c) { auto p = c.lock(); return !p || !p->owner; }), clients.end());
        }
    };
    static auto& clocks() {
        static std::vector<std::weak_ptr<Clock>> value;
        return value;
    }
    juce::Component::SafePointer<juce::Component> component;
    std::vector<juce::Component::SafePointer<juce::Component>> ancestors;
    std::shared_ptr<Client> client;
    std::shared_ptr<Clock> clock;
    int rate = 0;
    bool showing = false;
    static bool visible(const juce::Component& component) {
        for (auto* c = &component; c; c = c->getParentComponent())
            if (!c->isVisible()) return false;
        // Headless trees are eligible only for explicit test ticks.
        return component.getPeer() == nullptr || component.isShowing();
    }
    void unlisten() {
        for (auto& c : ancestors) if (c) c->removeComponentListener(this);
        ancestors.clear();
    }
    void detachClock() {
        if (clock) {
            auto& subscribers = clock->clients;
            subscribers.erase(std::remove_if(subscribers.begin(), subscribers.end(),
                [this](const auto& w) { auto c = w.lock(); return !c || c == client; }), subscribers.end());
            clock.reset();
        }
    }
    void refresh(bool notify = true) {
        unlisten();
        if (!component || rate == 0) return;
        for (auto* c = component.getComponent(); c; c = c->getParentComponent()) {
            ancestors.emplace_back(c);
            c->addComponentListener(this);
        }
        const bool nowShowing = visible(*component);
        const bool changed = showing != nowShowing;
        showing = nowShowing;
        if (!nowShowing) {
            detachClock(); client->next = -1.0;
            if (notify && changed) visualVisibilityChanged(false);
            return;
        }
        auto* root = component->getTopLevelComponent();
        if (clock && clock->root == root) {
            clock->attach();
            if (notify && changed) visualVisibilityChanged(true);
            return;
        }
        detachClock(); client->next = -1.0;
        auto& all = clocks();
        all.erase(std::remove_if(all.begin(), all.end(), [](const auto& w) { return w.expired(); }), all.end());
        for (auto& w : all) if (auto c = w.lock(); c && c->root == root) { clock = c; break; }
        if (!clock) { clock = std::make_shared<Clock>(*root); all.push_back(clock); }
        // Remove stale subscriptions when moving away and then back to a clock.
        clock->clients.erase(std::remove_if(clock->clients.begin(), clock->clients.end(),
            [this](const auto& w) { auto c = w.lock(); return !c || c == client; }), clock->clients.end());
        clock->clients.push_back(client);
        clock->attach();
        if (notify && changed) visualVisibilityChanged(true);
    }
    void componentVisibilityChanged(juce::Component&) override { refresh(); }
    void componentParentHierarchyChanged(juce::Component&) override { refresh(); }
    void componentBeingDeleted(juce::Component& c) override {
        // Do not re-register listeners while a component is being torn down.
        if (component.getComponent() == &c) {
            component = nullptr;
            stopTimer();
            return;
        }
        c.removeComponentListener(this);
        ancestors.erase(std::remove_if(ancestors.begin(), ancestors.end(),
            [&c](const auto& ancestor) { return ancestor.getComponent() == &c; }), ancestors.end());
        detachClock();
        if (client) client->next = -1.0;
        // Surviving children receive a hierarchy notification when detached.
        // Keep the requested rate so reparenting can register them again.
    }
protected:
    VisualTimer() = default;
    ~VisualTimer() override { stopTimer(); }
    VisualTimer(const VisualTimer&) = delete;
    VisualTimer& operator=(const VisualTimer&) = delete;
    void startVisualTimer(juce::Component& c, int hz) {
        stopTimer();
        component = &c;
        rate = juce::jlimit(1, 60, hz);
        client = std::make_shared<Client>(); client->owner = this; client->hz = rate;
        showing = visible(c);
        refresh(false); // Registration never invokes the virtual callback.
    }
    void startTimerHz(int hz) { if (component) startVisualTimer(*component, hz); }
    void stopTimer() {
        unlisten();
        if (client) client->owner = nullptr;
        detachClock(); client.reset(); rate = 0;
    }
    bool isTimerRunning() const { return rate != 0; }
    virtual void timerCallback() = 0;
    virtual void visualVisibilityChanged(bool) {}
public:
    static void tickForTesting(double seconds) {
        const auto snapshot = clocks();
        for (const auto& w : snapshot) if (auto c = w.lock()) c->tick(seconds);
    }
    static int subscriptionCountForTesting() {
        int count = 0;
        for (const auto& w : clocks()) if (auto c = w.lock()) count += (int) c->clients.size();
        return count;
    }
    static int clockCountForTesting() {
        return (int) std::count_if(clocks().begin(), clocks().end(), [](const auto& w) { return !w.expired(); });
    }
};
} // namespace fui
