// JUCE-independent timing core for the DR-1 POLY lane scheduler.
//
// This file deliberately contains no pattern-bank, transport, editor, or
// serialization code.  It describes the timing/source-step contract that a
// later DrumEngine integration can adapt to each of its three clocks.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace fable {

constexpr int DR_RHYTHM_LANES = 16;
constexpr int DR_RHYTHM_MAX_STEPS = 16;
constexpr std::uint32_t DR_RHYTHM_VERSION = 1;

enum class DrumRhythmMode : std::uint8_t {
    grid = 0,
    fit = 1,
};

// A disabled lane is the native equivalent of a null lane entry.  The other
// fields remain present so the complete 16-lane snapshot has a fixed layout.
struct DrumLaneRhythm {
    bool enabled = false;
    int sourceBar = 0;
    int steps = DR_RHYTHM_MAX_STEPS;
    int rotation = 0;
    DrumRhythmMode mode = DrumRhythmMode::grid;
    int cycleBeats = 4; // FIT only: the v1 contract permits 4 or 8.
};

struct DrumRhythm {
    std::uint32_t v = DR_RHYTHM_VERSION;
    std::array<DrumLaneRhythm, DR_RHYTHM_LANES> lanes{};
};

enum class DrumRhythmError : std::uint8_t {
    none = 0,
    unsupportedVersion,
    sourceBarOutOfRange,
    stepsOutOfRange,
    rotationOutOfRange,
    invalidMode,
    invalidFitCycle,
};

// Validation is allocation-free and does not modify the supplied snapshot.
// A disabled lane is treated as a null entry and its lane-specific metadata is
// ignored.  sourceBar is deliberately only checked for non-negativity: the
// owning sequence/clip knows its bar count and can apply that narrower check.
bool validateDrumRhythm(const DrumRhythm& rhythm,
                        DrumRhythmError* error = nullptr);

struct DrumRhythmEvent {
    int lane = -1;
    int sourceBar = 0;
    int sourceStep = -1;
    std::uint64_t eventOrdinal = 0;
    std::uint64_t cycle = 0;
    double beat = 0.0;
    std::int64_t sample = 0;
    DrumRhythmMode mode = DrumRhythmMode::grid;
};

// A fixed-size atomic snapshot.  The normal usage is one message-thread
// publisher and one audio-thread reader.  Each lane is encoded in an atomic
// word and a seqlock makes a read coherent without a mutex, allocation, or a
// second heap-owned snapshot.  read() is bounded; false means a writer was
// continuously publishing during all attempts and the caller should retain
// its previous snapshot.
class DrumRhythmState {
public:
    DrumRhythmState();

    DrumRhythmState(const DrumRhythmState&) = delete;
    DrumRhythmState& operator=(const DrumRhythmState&) = delete;

    bool publish(const DrumRhythm& rhythm);
    bool read(DrumRhythm& rhythm, int maxAttempts = 4) const;
    std::uint32_t sequence() const {
        return sequence_.load(std::memory_order_acquire);
    }

private:
    static std::uint64_t pack(const DrumLaneRhythm& lane);
    static DrumLaneRhythm unpack(std::uint64_t word);

    std::array<std::atomic<std::uint64_t>, DR_RHYTHM_LANES> lanes_{};
    std::atomic<std::uint32_t> sequence_{0};
};

// Schedules only POLY lane events.  Each call to nextEvent() examines exactly
// DR_RHYTHM_LANES cursors at most; no repeating event list is built.  Cursors
// are represented by integer ordinals and event positions are recomputed from
// those ordinals, so sample conversion never accumulates rounded durations.
class DrumRhythmScheduler {
public:
    explicit DrumRhythmScheduler(double sampleRate = 48000.0,
                                 double bpm = 120.0,
                                 double swing = 0.0);

    bool setTempo(double sampleRate, double bpm, double swing = 0.0);
    bool setSwing(double swing);
    bool setRhythm(const DrumRhythm& rhythm);
    bool setRhythm(const DrumRhythmState& state);

    // The next event at or after absoluteBeat is returned first.  reset() is
    // also the explicit seek/loop boundary operation; it prevents catch-up
    // bursts after a transport jump.
    bool reset(double absoluteBeat = 0.0);

    // Includes an event exactly on inclusiveEndBeat.  Equal-time events are
    // returned in ascending lane order.  The caller can repeatedly call this
    // method until it returns false for a rendering segment.
    bool nextEvent(double inclusiveEndBeat, DrumRhythmEvent& event);

    double sampleRate() const { return sampleRate_; }
    double bpm() const { return bpm_; }
    double swing() const { return swing_; }
    double currentBeat() const { return currentBeat_; }
    int lastLaneScanCount() const { return lastLaneScanCount_; }

private:
    struct Cursor {
        std::uint64_t ordinal = 0;
    };

    static int positiveModulo(std::uint64_t value, int modulus, int rotation);
    double spacing(const DrumLaneRhythm& lane) const;
    double eventBeat(const DrumLaneRhythm& lane,
                     std::uint64_t ordinal) const;
    std::uint64_t firstGridOrdinal(double absoluteBeat) const;
    std::int64_t beatToSample(double beat) const;

    DrumRhythm rhythm_{};
    std::array<Cursor, DR_RHYTHM_LANES> cursors_{};
    double sampleRate_ = 48000.0;
    double bpm_ = 120.0;
    double swing_ = 0.0;
    double currentBeat_ = 0.0;
    bool initialized_ = false;
    int lastLaneScanCount_ = 0;
};

} // namespace fable
