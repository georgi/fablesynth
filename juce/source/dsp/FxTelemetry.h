#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace fable {

// Audio-owned accumulators, atomic UI snapshots. No locks or allocations in
// the render path. Values match the web meter messages (dBFS, dB, seconds).
struct FxTelemetry {
    enum Field {
        ottIn,
        ottOut,
        compIn,
        compOut,
        echoL,
        echoR,
        verbL,
        verbR,
        ottLow,
        ottMid,
        ottHigh,
        gainLow,
        gainMid,
        gainHigh,
        ottAuto,
        compAuto,
        reduction,
        time,
        driftL,
        driftR,
        correlation,
        count
    };
    std::array<float, count> values{};
    uint64_t serial = 0;
    double seconds = 0, sampleRate = 48000;
    uint64_t reverbSerial = 0;
    double reverbSeconds = 0;
    FxTelemetry() {
        for (int i = ottIn; i <= ottHigh; ++i)
            values[(size_t)i] = -90;
    }
    float operator[](Field f) const { return values[(size_t)f]; }
};

class FxMeter {
  public:
    FxMeter() { clear(); }
    void prepare(double sr) {
        sr_ = sr;
        rate_.store(sr, std::memory_order_relaxed);
        window_ = std::max(1, (int)(sr / 30));
        reset();
    }
    void reset() {
        sums_.fill(0);
        cross_ = 0;
        samples_ = 0;
        clear();
    }
    static float db(double amplitude) {
        return (float)std::max(-90.0, 20 * std::log10(std::max(1e-9, amplitude)));
    }
    void stereo(FxTelemetry::Field first, double l, double r) {
        sums_[(size_t)first] += l * l;
        sums_[(size_t)first + 1] += r * r;
        if (first == FxTelemetry::verbL)
            cross_ += l * r;
    }
    void level(FxTelemetry::Field field, double l, double r) {
        sums_[(size_t)field] += (l * l + r * r) * 0.5;
    }
    template <class Ott, class Comp>
    void finish(const Ott &ott, const Comp &comp, double time, double driftL = 0, double driftR = 0) {
        if (!advance())
            return;
        for (size_t i = 0; i < 3; ++i) {
            put((int)FxTelemetry::ottLow + (int)i, db(ott.env[i]));
            put((int)FxTelemetry::gainLow + (int)i, db(ott.gain[i]));
        }
        put(FxTelemetry::ottAuto, db(ott.autoGain.gain));
        put(FxTelemetry::compAuto, db(comp.autoGain.gain));
        put(FxTelemetry::reduction, std::max(0.f, -db(comp.gain)));
        put(FxTelemetry::time, (float)time);
        put(FxTelemetry::driftL, (float)driftL);
        put(FxTelemetry::driftR, (float)driftR);
        publish();
    }
    void finishReverb() {
        if (advance())
            publish();
    }
    FxTelemetry read() const {
        FxTelemetry result;
        result.serial = serial_.load(std::memory_order_acquire);
        for (size_t i = 0; i < result.values.size(); ++i)
            result.values[i] = published_[i].load(std::memory_order_relaxed);
        result.seconds = seconds_.load(std::memory_order_relaxed);
        result.sampleRate = rate_.load(std::memory_order_relaxed);
        result.reverbSerial = result.serial;
        result.reverbSeconds = result.seconds;
        return result;
    }

  private:
    void put(int field, float v) {
        published_[(size_t)field].store(std::isfinite(v) ? v : 0, std::memory_order_relaxed);
    }
    void clear() {
        FxTelemetry idle;
        for (size_t i = 0; i < idle.values.size(); ++i)
            published_[i].store(idle.values[i], std::memory_order_relaxed);
        serial_.fetch_add(1, std::memory_order_release);
    }
    bool advance() {
        if (++samples_ < window_)
            return false;
        for (int i = 0; i < 8; ++i)
            put(i, db(std::sqrt(sums_[(size_t)i] / samples_)));
        const double denominator = std::sqrt(sums_[6] * sums_[7]);
        put(FxTelemetry::correlation,
            denominator > 1e-18 ? (float)std::clamp(cross_ / denominator, -1.0, 1.0) : 0);
        elapsed_ += samples_ / sr_;
        sums_.fill(0);
        cross_ = 0;
        samples_ = 0;
        return true;
    }
    void publish() {
        seconds_.store(elapsed_, std::memory_order_relaxed);
        rate_.store(sr_, std::memory_order_relaxed);
        serial_.fetch_add(1, std::memory_order_release);
    }
    std::array<double, 8> sums_{};
    std::array<std::atomic<float>, FxTelemetry::count> published_{};
    std::atomic<uint64_t> serial_{0};
    std::atomic<double> seconds_{0}, rate_{48000};
    double cross_ = 0, elapsed_ = 0, sr_ = 48000;
    int samples_ = 0, window_ = 1600;
};
} // namespace fable
