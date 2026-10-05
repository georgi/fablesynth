#pragma once
#include <array>
#include <cstddef>
#include <atomic>
#include <limits>

namespace fable {
// Audio-thread snapshots, read by controls without touching engine state or
// changing the stored patch. NaN means this parameter is not being automated.
template<std::size_t N> class AutomationTelemetry {
public:
    AutomationTelemetry() { clear(); }
    void clear() { active_=false; for (auto& v : values_) v.store(std::numeric_limits<float>::quiet_NaN(),std::memory_order_relaxed); }
    template<class Engine> void publish(const Engine& engine) {
        if (!engine.hasClipAutomation()) { if (active_) clear(); return; }
        active_=true;
        for (std::size_t i=0;i<N;++i) values_[i].store(engine.automationValue((int)i),std::memory_order_relaxed);
    }
    float value(int id) const { return id>=0 && id<(int)N ? values_[(size_t)id].load(std::memory_order_relaxed)
        : std::numeric_limits<float>::quiet_NaN(); }
private:
    std::array<std::atomic<float>,N> values_;
    bool active_=false;
};
}
