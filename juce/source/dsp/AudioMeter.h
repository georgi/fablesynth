#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace fable {

// All peaks reported by this helper are sample peaks. No oversampling or
// inter-sample (true-peak) reconstruction is performed.
struct AudioMeterLevelSnapshot {
    float rms = 0.0f;
    float samplePeak = 0.0f;
    float rmsDbfs = -120.0f;
    float samplePeakDbfs = -120.0f;
    float dc = 0.0f;
};

struct AudioMeterSnapshot {
    // coherent is false only when the bounded reader could not observe one
    // complete publication. Metrics must be ignored in that case.
    bool coherent = false;
    bool available = false;
    std::uint64_t generation = 0;
    std::uint64_t serial = 0;
    double sampleRate = 0.0;
    int channelCount = 0;
    std::uint64_t windowFrames = 0;
    std::uint64_t windowEndFrame = 0;
    std::uint64_t elapsedFrames = 0;
    AudioMeterLevelSnapshot left;
    AudioMeterLevelSnapshot right;
    AudioMeterLevelSnapshot combined;
    float stereoCorrelation = 0.0f;
    std::uint64_t fullScaleSampleCount = 0; // finite samples with abs(sample) >= 1
    std::uint64_t nonFiniteSampleCount = 0;
};

// Single audio-writer meter with bounded, coherent readers. prepare() and
// reset() must be called while the audio writer is stopped. process() performs
// no allocation, locking, logging, or system calls.
class AudioMeter final {
public:
    static constexpr float kDbFloor = -120.0f;

    AudioMeter() noexcept { publishUnavailable(); }

    void prepare(double sampleRate, int channels = 2,
                 double windowSeconds = 0.100) noexcept {
        sampleRate_ = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 0.0;
        configuredChannels_ = std::max(0, std::min(2, channels));
        if (!std::isfinite(windowSeconds) || windowSeconds <= 0.0) windowSeconds = 0.100;
        const double frames = sampleRate_ * windowSeconds;
        targetFrames_ = sampleRate_ > 0.0
            ? static_cast<std::uint64_t>(std::llround(std::max(1.0, std::min(
                  frames, static_cast<double>(std::numeric_limits<std::uint32_t>::max())))))
            : 0;
        ++generation_;
        clearAccumulation();
        elapsedFrames_ = 0;
        serial_ = 0;
        publishUnavailable();
    }

    void reset() noexcept {
        ++generation_;
        clearAccumulation();
        elapsedFrames_ = 0;
        serial_ = 0;
        publishUnavailable();
    }

    void process(const float* const* channelData, int numChannels,
                 int numFrames) noexcept {
        if (targetFrames_ == 0 || configuredChannels_ == 0 || numFrames <= 0) return;
        const int channels = std::max(0, std::min(configuredChannels_, numChannels));
        if (channels == 0) return;

        // A bus-layout change cannot share an RMS denominator with the old
        // partial window. Normal processor use prepares again before this path.
        if (accumulatedFrames_ != 0 && channels != accumulationChannels_)
            clearAccumulation();
        accumulationChannels_ = channels;

        for (int frame = 0; frame < numFrames; ++frame) {
            double values[2] { 0.0, 0.0 };
            for (int channel = 0; channel < channels; ++channel) {
                const float* data = channelData != nullptr ? channelData[channel] : nullptr;
                const float input = data != nullptr ? data[frame]
                                                    : std::numeric_limits<float>::quiet_NaN();
                if (!std::isfinite(input)) {
                    ++nonFiniteSamples_;
                    continue; // Invalid output is counted and treated as silence in statistics.
                }
                const double value = input;
                const double magnitude = std::abs(value);
                values[channel] = value;
                sums_[channel] += value;
                sumSquares_[channel] += value * value;
                peaks_[channel] = std::max(peaks_[channel], magnitude);
                if (magnitude >= 1.0) ++fullScaleSamples_;
            }
            if (channels == 2) crossSum_ += values[0] * values[1];
            ++accumulatedFrames_;
            ++elapsedFrames_;
            if (accumulatedFrames_ == targetFrames_) {
                publishWindow();
                clearAccumulation();
                accumulationChannels_ = channels;
            }
        }
    }

    // Publication uses sequentially consistent atomics intentionally. A reader
    // accepting equal even sequence values therefore cannot have any payload
    // load ordered inside the writer's odd/even interval. Publication happens
    // only once per meter window, so this simple proof is preferable to relying
    // on architecture-specific relaxed seqlock fences. The attempt count is
    // clamped, so a racing reader never spins indefinitely.
    AudioMeterSnapshot snapshot(int maxAttempts = 3) const noexcept {
        maxAttempts = std::max(1, std::min(8, maxAttempts));
        for (int attempt = 0; attempt < maxAttempts; ++attempt) {
            const auto before = published_.sequence.load(std::memory_order_seq_cst);
            if ((before & 1u) != 0) continue;
            auto result = loadPublished();
            const auto after = published_.sequence.load(std::memory_order_seq_cst);
            if (before == after && (after & 1u) == 0) {
                result.coherent = true;
                return result;
            }
        }
        AudioMeterSnapshot unavailable;
        unavailable.generation = published_.generation.load(std::memory_order_seq_cst);
        unavailable.coherent = false;
        unavailable.available = false;
        return unavailable;
    }

private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "AudioMeter requires lock-free 64-bit atomics");
    static_assert(std::atomic<float>::is_always_lock_free,
                  "AudioMeter requires lock-free float atomics");
    static_assert(std::atomic<double>::is_always_lock_free,
                  "AudioMeter requires lock-free double atomics");
    static_assert(std::atomic<int>::is_always_lock_free,
                  "AudioMeter requires lock-free integer atomics");
    static_assert(std::atomic<bool>::is_always_lock_free,
                  "AudioMeter requires lock-free boolean atomics");

    struct AtomicLevel {
        std::atomic<float> rms { 0.0f };
        std::atomic<float> samplePeak { 0.0f };
        std::atomic<float> rmsDbfs { kDbFloor };
        std::atomic<float> samplePeakDbfs { kDbFloor };
        std::atomic<float> dc { 0.0f };
    };
    struct Published {
        std::atomic<std::uint64_t> sequence { 0 };
        std::atomic<bool> available { false };
        std::atomic<std::uint64_t> generation { 0 };
        std::atomic<std::uint64_t> serial { 0 };
        std::atomic<double> sampleRate { 0.0 };
        std::atomic<int> channelCount { 0 };
        std::atomic<std::uint64_t> windowFrames { 0 };
        std::atomic<std::uint64_t> windowEndFrame { 0 };
        std::atomic<std::uint64_t> elapsedFrames { 0 };
        AtomicLevel left, right, combined;
        std::atomic<float> stereoCorrelation { 0.0f };
        std::atomic<std::uint64_t> fullScaleSampleCount { 0 };
        std::atomic<std::uint64_t> nonFiniteSampleCount { 0 };
    };

    static float toDb(double linear) noexcept {
        if (!(linear > 0.0) || !std::isfinite(linear)) return kDbFloor;
        return std::max(kDbFloor, static_cast<float>(20.0 * std::log10(linear)));
    }

    static AudioMeterLevelSnapshot makeLevel(double sum, double sumSquares,
                                             double peak, double divisor) noexcept {
        AudioMeterLevelSnapshot level;
        const double meanSquare = divisor > 0.0 ? std::max(0.0, sumSquares / divisor) : 0.0;
        level.rms = static_cast<float>(std::sqrt(meanSquare));
        level.samplePeak = static_cast<float>(peak);
        level.rmsDbfs = toDb(level.rms);
        level.samplePeakDbfs = toDb(level.samplePeak);
        level.dc = divisor > 0.0 ? static_cast<float>(sum / divisor) : 0.0f;
        return level;
    }

    static void storeLevel(AtomicLevel& output,
                           const AudioMeterLevelSnapshot& value) noexcept {
        output.rms.store(value.rms, std::memory_order_seq_cst);
        output.samplePeak.store(value.samplePeak, std::memory_order_seq_cst);
        output.rmsDbfs.store(value.rmsDbfs, std::memory_order_seq_cst);
        output.samplePeakDbfs.store(value.samplePeakDbfs, std::memory_order_seq_cst);
        output.dc.store(value.dc, std::memory_order_seq_cst);
    }

    static AudioMeterLevelSnapshot loadLevel(const AtomicLevel& input) noexcept {
        AudioMeterLevelSnapshot value;
        value.rms = input.rms.load(std::memory_order_seq_cst);
        value.samplePeak = input.samplePeak.load(std::memory_order_seq_cst);
        value.rmsDbfs = input.rmsDbfs.load(std::memory_order_seq_cst);
        value.samplePeakDbfs = input.samplePeakDbfs.load(std::memory_order_seq_cst);
        value.dc = input.dc.load(std::memory_order_seq_cst);
        return value;
    }

    void beginPublish() noexcept {
        published_.sequence.fetch_add(1, std::memory_order_seq_cst);
    }
    void endPublish() noexcept {
        published_.sequence.fetch_add(1, std::memory_order_seq_cst);
    }

    void publishUnavailable() noexcept {
        beginPublish();
        published_.available.store(false, std::memory_order_seq_cst);
        published_.generation.store(generation_, std::memory_order_seq_cst);
        published_.serial.store(serial_, std::memory_order_seq_cst);
        published_.sampleRate.store(sampleRate_, std::memory_order_seq_cst);
        published_.channelCount.store(configuredChannels_, std::memory_order_seq_cst);
        published_.windowFrames.store(targetFrames_, std::memory_order_seq_cst);
        published_.windowEndFrame.store(0, std::memory_order_seq_cst);
        published_.elapsedFrames.store(elapsedFrames_, std::memory_order_seq_cst);
        storeLevel(published_.left, {});
        storeLevel(published_.right, {});
        storeLevel(published_.combined, {});
        published_.stereoCorrelation.store(0.0f, std::memory_order_seq_cst);
        published_.fullScaleSampleCount.store(0, std::memory_order_seq_cst);
        published_.nonFiniteSampleCount.store(0, std::memory_order_seq_cst);
        endPublish();
    }

    void publishWindow() noexcept {
        const double frames = static_cast<double>(targetFrames_);
        const auto left = makeLevel(sums_[0], sumSquares_[0], peaks_[0], frames);
        const auto right = accumulationChannels_ == 2
            ? makeLevel(sums_[1], sumSquares_[1], peaks_[1], frames)
            : AudioMeterLevelSnapshot {};
        const double combinedSum = sums_[0] + (accumulationChannels_ == 2 ? sums_[1] : 0.0);
        const double combinedSquares = sumSquares_[0]
                                     + (accumulationChannels_ == 2 ? sumSquares_[1] : 0.0);
        const double combinedPeak = std::max(peaks_[0], accumulationChannels_ == 2 ? peaks_[1] : 0.0);
        const auto combined = makeLevel(combinedSum, combinedSquares, combinedPeak,
                                        frames * accumulationChannels_);

        float correlation = 0.0f;
        if (accumulationChannels_ == 2) {
            const double covariance = crossSum_ - sums_[0] * sums_[1] / frames;
            const double varianceLeft = std::max(0.0, sumSquares_[0] - sums_[0] * sums_[0] / frames);
            const double varianceRight = std::max(0.0, sumSquares_[1] - sums_[1] * sums_[1] / frames);
            const double denominator = std::sqrt(varianceLeft * varianceRight);
            if (denominator > 0.0)
                correlation = static_cast<float>(std::max(-1.0, std::min(1.0, covariance / denominator)));
        }

        ++serial_;
        beginPublish();
        published_.available.store(true, std::memory_order_seq_cst);
        published_.generation.store(generation_, std::memory_order_seq_cst);
        published_.serial.store(serial_, std::memory_order_seq_cst);
        published_.sampleRate.store(sampleRate_, std::memory_order_seq_cst);
        published_.channelCount.store(accumulationChannels_, std::memory_order_seq_cst);
        published_.windowFrames.store(targetFrames_, std::memory_order_seq_cst);
        published_.windowEndFrame.store(elapsedFrames_, std::memory_order_seq_cst);
        published_.elapsedFrames.store(elapsedFrames_, std::memory_order_seq_cst);
        storeLevel(published_.left, left);
        storeLevel(published_.right, right);
        storeLevel(published_.combined, combined);
        published_.stereoCorrelation.store(correlation, std::memory_order_seq_cst);
        published_.fullScaleSampleCount.store(fullScaleSamples_, std::memory_order_seq_cst);
        published_.nonFiniteSampleCount.store(nonFiniteSamples_, std::memory_order_seq_cst);
        endPublish();
    }

    AudioMeterSnapshot loadPublished() const noexcept {
        AudioMeterSnapshot value;
        value.available = published_.available.load(std::memory_order_seq_cst);
        value.generation = published_.generation.load(std::memory_order_seq_cst);
        value.serial = published_.serial.load(std::memory_order_seq_cst);
        value.sampleRate = published_.sampleRate.load(std::memory_order_seq_cst);
        value.channelCount = published_.channelCount.load(std::memory_order_seq_cst);
        value.windowFrames = published_.windowFrames.load(std::memory_order_seq_cst);
        value.windowEndFrame = published_.windowEndFrame.load(std::memory_order_seq_cst);
        value.elapsedFrames = published_.elapsedFrames.load(std::memory_order_seq_cst);
        value.left = loadLevel(published_.left);
        value.right = loadLevel(published_.right);
        value.combined = loadLevel(published_.combined);
        value.stereoCorrelation = published_.stereoCorrelation.load(std::memory_order_seq_cst);
        value.fullScaleSampleCount = published_.fullScaleSampleCount.load(std::memory_order_seq_cst);
        value.nonFiniteSampleCount = published_.nonFiniteSampleCount.load(std::memory_order_seq_cst);
        return value;
    }

    void clearAccumulation() noexcept {
        accumulatedFrames_ = 0;
        accumulationChannels_ = configuredChannels_;
        sums_[0] = sums_[1] = 0.0;
        sumSquares_[0] = sumSquares_[1] = 0.0;
        peaks_[0] = peaks_[1] = 0.0;
        crossSum_ = 0.0;
        fullScaleSamples_ = 0;
        nonFiniteSamples_ = 0;
    }

    double sampleRate_ = 0.0;
    int configuredChannels_ = 0;
    int accumulationChannels_ = 0;
    std::uint64_t targetFrames_ = 0;
    std::uint64_t accumulatedFrames_ = 0;
    std::uint64_t elapsedFrames_ = 0;
    std::uint64_t serial_ = 0;
    std::uint64_t generation_ = 0;
    double sums_[2] { 0.0, 0.0 };
    double sumSquares_[2] { 0.0, 0.0 };
    double peaks_[2] { 0.0, 0.0 };
    double crossSum_ = 0.0;
    std::uint64_t fullScaleSamples_ = 0;
    std::uint64_t nonFiniteSamples_ = 0;
    Published published_;
};

} // namespace fable
