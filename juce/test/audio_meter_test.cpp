#include "../source/dsp/AudioMeter.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <atomic>
#include <thread>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool near(double actual, double expected, double tolerance = 1.0e-5) {
    return std::abs(actual - expected) <= tolerance;
}
}

int main() {
    fable::AudioMeter meter;
    auto initial = meter.snapshot();
    check(initial.coherent && !initial.available, "fresh meter is unavailable");

    meter.prepare(1000.0, 2, 0.100); // exactly 100 frames per window
    const auto prepared = meter.snapshot();
    check(prepared.coherent && !prepared.available && prepared.windowFrames == 100,
          "prepare publishes unavailable window metadata");
    check(prepared.generation > initial.generation, "prepare advances generation");

    float left[100], right[100];
    for (int i = 0; i < 100; ++i) {
        // Alternating polarity keeps each channel's DC at zero and makes the
        // stereo pair perfectly anti-correlated.
        left[i] = (i & 1) == 0 ? 0.5f : -0.5f;
        right[i] = -left[i];
    }
    const float* stereo[] { left, right };
    meter.process(stereo, 2, 37);
    check(!meter.snapshot().available, "partial window remains unavailable");
    const float* stereoTail[] { left + 37, right + 37 };
    meter.process(stereoTail, 2, 63);
    const auto tone = meter.snapshot();
    check(tone.coherent && tone.available && tone.serial == 1
              && tone.windowEndFrame == 100 && tone.elapsedFrames == 100,
          "uneven blocks publish one exact completed window");
    check(near(tone.left.rms, 0.5) && near(tone.right.rms, 0.5)
              && near(tone.combined.rms, 0.5),
          "stereo and combined linear RMS are accurate");
    check(near(tone.left.samplePeak, 0.5) && near(tone.combined.samplePeak, 0.5),
          "sample peaks are accurate");
    check(near(tone.left.rmsDbfs, -6.0205999, 1.0e-4)
              && near(tone.left.samplePeakDbfs, -6.0205999, 1.0e-4),
          "dBFS conversion is accurate");
    check(near(tone.left.dc, 0.0) && near(tone.right.dc, 0.0), "DC is accurate");
    check(near(tone.stereoCorrelation, -1.0), "anti-phase stereo correlation is -1");
    check(tone.fullScaleSampleCount == 0 && tone.nonFiniteSampleCount == 0,
          "ordinary samples do not raise fault counts");

    for (int i = 0; i < 100; ++i) left[i] = right[i] = 0.0f;
    left[0] = std::numeric_limits<float>::quiet_NaN();
    right[0] = 1.2f;
    meter.process(stereo, 2, 100);
    const auto faults = meter.snapshot();
    check(faults.serial == 2 && faults.fullScaleSampleCount == 1
              && faults.nonFiniteSampleCount == 1,
          "full-scale and non-finite sample counts are explicit");
    check(near(faults.right.samplePeak, 1.2, 1.0e-6)
              && faults.right.samplePeakDbfs > 0.0f,
          "over-range sample peak remains truthful above 0 dBFS");
    check(near(faults.left.rms, 0.0) && faults.left.rmsDbfs == fable::AudioMeter::kDbFloor,
          "invalid samples are counted and treated as silence");

    float longLeft[250] {}, longRight[250] {};
    const float* longBlock[] { longLeft, longRight };
    meter.process(longBlock, 2, 250);
    const auto longResult = meter.snapshot();
    check(longResult.serial == 4 && longResult.windowEndFrame == 400,
          "one large block publishes every full window and retains its partial tail");

    const auto generationBeforeReset = longResult.generation;
    meter.reset();
    const auto reset = meter.snapshot();
    check(reset.coherent && !reset.available && reset.serial == 0
              && reset.elapsedFrames == 0 && reset.generation > generationBeforeReset,
          "reset invalidates data and advances generation");

    meter.prepare(1000.0, 1, 0.010); // 10-frame mono silence
    float mono[10] {};
    const float* monoChannels[] { mono };
    meter.process(monoChannels, 1, 10);
    const auto silence = meter.snapshot();
    check(silence.available && silence.channelCount == 1 && silence.windowFrames == 10,
          "mono window is published");
    check(silence.left.rms == 0.0f && silence.left.samplePeak == 0.0f
              && silence.left.rmsDbfs == fable::AudioMeter::kDbFloor
              && silence.left.samplePeakDbfs == fable::AudioMeter::kDbFloor,
          "digital silence uses the dBFS floor");
    check(silence.right.rms == 0.0f && silence.combined.rms == 0.0f
              && silence.stereoCorrelation == 0.0f,
          "mono right channel and stereo correlation are explicitly unavailable as zero");

    for (int i = 0; i < 10; ++i) mono[i] = (i & 1) == 0 ? 0.25f : -0.25f;
    meter.process(monoChannels, 1, 10);
    const auto monoTone = meter.snapshot();
    check(near(monoTone.left.rms, 0.25) && near(monoTone.combined.rms, 0.25)
              && near(monoTone.left.samplePeak, 0.25)
              && near(monoTone.combined.samplePeak, 0.25),
          "nonzero mono RMS and peak use a one-channel denominator");
    check(monoTone.right.rms == 0.0f && monoTone.channelCount == 1,
          "nonzero mono does not invent a right channel");

    // Exercise the bounded sequence reader while publications change. Each
    // serial has an exact binary-fraction amplitude, so a torn snapshot cannot
    // accidentally pass by rounding to a neighbouring window's values.
    fable::AudioMeter concurrent;
    constexpr int concurrentWindows = 2000;
    constexpr int concurrentWindowFrames = 16;
    concurrent.prepare(16000.0, 2, 0.001);
    std::atomic<bool> started { false }, finished { false };
    std::atomic<int> coherentReads { 0 }, coherenceFailures { 0 };
    auto amplitudeForSerial = [](std::uint64_t serial) {
        return static_cast<float>(1 + serial % 8) / 16.0f;
    };
    std::thread writer([&] {
        float l[concurrentWindowFrames], r[concurrentWindowFrames];
        const float* channels[] { l, r };
        started.store(true, std::memory_order_release);
        for (int window = 0; window < concurrentWindows; ++window) {
            const auto serial = static_cast<std::uint64_t>(window + 1);
            const float amplitude = amplitudeForSerial(serial);
            for (int frame = 0; frame < concurrentWindowFrames; ++frame)
                l[frame] = r[frame] = (frame & 1) == 0 ? amplitude : -amplitude;
            concurrent.process(channels, 2, concurrentWindowFrames);
            if ((window & 31) == 0) std::this_thread::yield();
        }
        finished.store(true, std::memory_order_release);
    });
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    do {
        const auto observed = concurrent.snapshot();
        if (observed.coherent && observed.available) {
            ++coherentReads;
            const float expected = amplitudeForSerial(observed.serial);
            if (observed.serial < 1 || observed.serial > concurrentWindows
                || observed.windowEndFrame != observed.serial * concurrentWindowFrames
                || !near(observed.left.rms, expected) || !near(observed.right.rms, expected)
                || !near(observed.combined.rms, expected)
                || !near(observed.left.samplePeak, expected)
                || !near(observed.right.samplePeak, expected)
                || !near(observed.combined.samplePeak, expected)
                || !near(observed.stereoCorrelation, 1.0))
                ++coherenceFailures;
        }
    } while (!finished.load(std::memory_order_acquire));
    writer.join();
    const auto concurrentFinal = concurrent.snapshot();
    check(coherentReads.load() > 0, "concurrent reader observed a completed publication");
    check(coherenceFailures.load() == 0, "concurrent reader never observed a torn publication");
    check(concurrentFinal.coherent && concurrentFinal.available
              && concurrentFinal.serial == concurrentWindows,
          "concurrent writer publishes its final serial coherently");

    meter.prepare(0.0, 2);
    meter.process(stereo, 2, 100);
    check(!meter.snapshot().available, "invalid sample rate cannot publish a window");

    std::printf(failures == 0 ? "AUDIO METER CHECKS PASSED\n" : "AUDIO METER CHECKS FAILED\n");
    return failures == 0 ? 0 : 1;
}
