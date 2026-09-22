#include "DrumRhythm.h"

#include <cmath>
#include <limits>

namespace fable {

namespace {

constexpr double kGridSpacingBeats = 0.25;
constexpr double kSwingMax = 0.667;
constexpr double kBeatEpsilon = 1.0e-12;

void setError(DrumRhythmError* error, DrumRhythmError value) {
    if (error != nullptr)
        *error = value;
}

} // namespace

bool validateDrumRhythm(const DrumRhythm& rhythm, DrumRhythmError* error) {
    setError(error, DrumRhythmError::none);
    if (rhythm.v != DR_RHYTHM_VERSION) {
        setError(error, DrumRhythmError::unsupportedVersion);
        return false;
    }

    for (const auto& lane : rhythm.lanes) {
        if (!lane.enabled)
            continue;
        if (lane.sourceBar < 0) {
            setError(error, DrumRhythmError::sourceBarOutOfRange);
            return false;
        }
        if (lane.steps < 1 || lane.steps > DR_RHYTHM_MAX_STEPS) {
            setError(error, DrumRhythmError::stepsOutOfRange);
            return false;
        }
        if (lane.rotation < 0 || lane.rotation >= lane.steps) {
            setError(error, DrumRhythmError::rotationOutOfRange);
            return false;
        }
        if (lane.mode != DrumRhythmMode::grid && lane.mode != DrumRhythmMode::fit) {
            setError(error, DrumRhythmError::invalidMode);
            return false;
        }
        if (lane.mode == DrumRhythmMode::fit
            && lane.cycleBeats != 4 && lane.cycleBeats != 8) {
            setError(error, DrumRhythmError::invalidFitCycle);
            return false;
        }
    }
    return true;
}

DrumRhythmState::DrumRhythmState() {
    const auto word = pack(DrumLaneRhythm{});
    for (auto& lane : lanes_)
        lane.store(word, std::memory_order_relaxed);
}

std::uint64_t DrumRhythmState::pack(const DrumLaneRhythm& lane) {
    // The fixed word keeps every field atomic.  sourceBar is a non-negative
    // 32-bit value in the wire shape; the validator constrains the public int
    // input before publication.
    std::uint64_t word = lane.enabled ? 1u : 0u;
    word |= static_cast<std::uint64_t>(static_cast<std::uint32_t>(lane.sourceBar)) << 1;
    word |= static_cast<std::uint64_t>(static_cast<std::uint32_t>(lane.steps) & 0x1fu) << 33;
    word |= static_cast<std::uint64_t>(static_cast<std::uint32_t>(lane.rotation) & 0x1fu) << 38;
    word |= static_cast<std::uint64_t>(lane.mode == DrumRhythmMode::fit ? 1u : 0u) << 43;
    word |= static_cast<std::uint64_t>(static_cast<std::uint32_t>(lane.cycleBeats) & 0x0fu) << 44;
    return word;
}

DrumLaneRhythm DrumRhythmState::unpack(std::uint64_t word) {
    DrumLaneRhythm lane;
    lane.enabled = (word & 1u) != 0;
    lane.sourceBar = static_cast<int>((word >> 1) & 0xffffffffu);
    lane.steps = static_cast<int>((word >> 33) & 0x1fu);
    lane.rotation = static_cast<int>((word >> 38) & 0x1fu);
    lane.mode = ((word >> 43) & 1u) != 0 ? DrumRhythmMode::fit : DrumRhythmMode::grid;
    lane.cycleBeats = static_cast<int>((word >> 44) & 0x0fu);
    return lane;
}

bool DrumRhythmState::publish(const DrumRhythm& rhythm) {
    if (!validateDrumRhythm(rhythm))
        return false;

    // One publisher is intentional: it is the same message-thread ownership
    // used by the existing DR-1 sequence mailbox.  All payload fields remain
    // atomic so an audio reader is data-race-free while publication occurs.
    sequence_.fetch_add(1, std::memory_order_acq_rel); // odd: write in progress
    for (int i = 0; i < DR_RHYTHM_LANES; ++i)
        lanes_[static_cast<std::size_t>(i)].store(pack(rhythm.lanes[static_cast<std::size_t>(i)]),
                                                  std::memory_order_relaxed);
    sequence_.fetch_add(1, std::memory_order_release); // even: complete snapshot
    return true;
}

bool DrumRhythmState::read(DrumRhythm& rhythm, int maxAttempts) const {
    if (maxAttempts <= 0)
        return false;

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        const auto before = sequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;

        DrumRhythm candidate;
        candidate.v = DR_RHYTHM_VERSION;
        for (int i = 0; i < DR_RHYTHM_LANES; ++i)
            candidate.lanes[static_cast<std::size_t>(i)] = unpack(
                lanes_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed));

        const auto after = sequence_.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0) {
            rhythm = candidate;
            return true;
        }
    }
    return false;
}

DrumRhythmScheduler::DrumRhythmScheduler(double sampleRate, double bpm, double swing) {
    setTempo(sampleRate, bpm, swing);
}

bool DrumRhythmScheduler::setTempo(double sampleRate, double bpm, double swing) {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0
        || !std::isfinite(bpm) || bpm <= 0.0
        || !std::isfinite(swing) || swing < 0.0 || swing > 1.0)
        return false;
    sampleRate_ = sampleRate;
    bpm_ = bpm;
    swing_ = swing;
    return true;
}

bool DrumRhythmScheduler::setSwing(double swing) {
    if (!std::isfinite(swing) || swing < 0.0 || swing > 1.0)
        return false;
    swing_ = swing;
    return true;
}

bool DrumRhythmScheduler::setRhythm(const DrumRhythm& rhythm) {
    if (!validateDrumRhythm(rhythm))
        return false;
    rhythm_ = rhythm;
    initialized_ = false;
    return true;
}

bool DrumRhythmScheduler::setRhythm(const DrumRhythmState& state) {
    DrumRhythm rhythm;
    return state.read(rhythm) && setRhythm(rhythm);
}

bool DrumRhythmScheduler::reset(double absoluteBeat) {
    if (!std::isfinite(absoluteBeat) || absoluteBeat < 0.0)
        return false;

    currentBeat_ = absoluteBeat;
    for (int i = 0; i < DR_RHYTHM_LANES; ++i) {
        const auto& lane = rhythm_.lanes[static_cast<std::size_t>(i)];
        const auto slotBeats = spacing(lane);
        if (!lane.enabled || slotBeats <= 0.0) {
            cursors_[static_cast<std::size_t>(i)].ordinal = 0;
            continue;
        }

        // Subtracting a tiny epsilon makes a mathematically exact event at a
        // seek point inclusive, while still being insensitive to a caller's
        // last-bit floating-point representation.
        if (lane.mode == DrumRhythmMode::grid) {
            cursors_[static_cast<std::size_t>(i)].ordinal = firstGridOrdinal(absoluteBeat);
        } else {
            const auto ordinal = std::ceil(absoluteBeat / slotBeats - kBeatEpsilon);
            cursors_[static_cast<std::size_t>(i)].ordinal = ordinal <= 0.0
                ? 0u : static_cast<std::uint64_t>(ordinal);
        }
    }
    initialized_ = true;
    return true;
}

int DrumRhythmScheduler::positiveModulo(std::uint64_t value, int modulus, int rotation) {
    const auto step = static_cast<int>(value % static_cast<std::uint64_t>(modulus));
    const int result = step - rotation;
    const int wrapped = result % modulus;
    return wrapped < 0 ? wrapped + modulus : wrapped;
}

double DrumRhythmScheduler::spacing(const DrumLaneRhythm& lane) const {
    if (lane.mode == DrumRhythmMode::grid)
        return kGridSpacingBeats;
    return static_cast<double>(lane.cycleBeats) / static_cast<double>(lane.steps);
}

double DrumRhythmScheduler::eventBeat(const DrumLaneRhythm& lane,
                                      std::uint64_t ordinal) const {
    if (lane.mode == DrumRhythmMode::grid) {
        const double base = static_cast<double>(ordinal) * kGridSpacingBeats;
        return base + ((ordinal & 1u) != 0
            ? swing_ * kSwingMax * kGridSpacingBeats : 0.0);
    }

    const auto steps = static_cast<std::uint64_t>(lane.steps);
    const auto cycle = ordinal / steps;
    const auto step = ordinal % steps;
    return static_cast<double>(cycle) * static_cast<double>(lane.cycleBeats)
        + static_cast<double>(step) * spacing(lane);
}

std::uint64_t DrumRhythmScheduler::firstGridOrdinal(double absoluteBeat) const {
    const auto estimate = std::floor(absoluteBeat / kGridSpacingBeats);
    auto ordinal = estimate <= 0.0 ? 0u : static_cast<std::uint64_t>(estimate);
    const auto candidate = static_cast<double>(ordinal) * kGridSpacingBeats
        + ((ordinal & 1u) != 0 ? swing_ * kSwingMax * kGridSpacingBeats : 0.0);
    if (candidate < absoluteBeat - kBeatEpsilon)
        ++ordinal;
    return ordinal;
}

std::int64_t DrumRhythmScheduler::beatToSample(double beat) const {
    const long double samples = static_cast<long double>(beat)
        * (60.0L / static_cast<long double>(bpm_))
        * static_cast<long double>(sampleRate_);
    return static_cast<std::int64_t>(std::llround(samples));
}

bool DrumRhythmScheduler::nextEvent(double inclusiveEndBeat, DrumRhythmEvent& event) {
    if (!initialized_ && !reset(0.0))
        return false;
    if (!std::isfinite(inclusiveEndBeat))
        return false;

    int selected = -1;
    double selectedBeat = std::numeric_limits<double>::infinity();
    lastLaneScanCount_ = DR_RHYTHM_LANES;
    for (int i = 0; i < DR_RHYTHM_LANES; ++i) {
        const auto& lane = rhythm_.lanes[static_cast<std::size_t>(i)];
        if (!lane.enabled)
            continue;
        const auto candidate = eventBeat(lane, cursors_[static_cast<std::size_t>(i)].ordinal);
        if (candidate > inclusiveEndBeat + kBeatEpsilon)
            continue;
        if (selected < 0 || candidate < selectedBeat - kBeatEpsilon
            || (std::fabs(candidate - selectedBeat) <= kBeatEpsilon && i < selected)) {
            selected = i;
            selectedBeat = candidate;
        }
    }

    if (selected < 0)
        return false;

    const auto index = static_cast<std::size_t>(selected);
    const auto& lane = rhythm_.lanes[index];
    const auto ordinal = cursors_[index].ordinal++;
    event.lane = selected;
    event.sourceBar = lane.sourceBar;
    event.sourceStep = positiveModulo(ordinal, lane.steps, lane.rotation);
    event.eventOrdinal = ordinal;
    event.cycle = lane.mode == DrumRhythmMode::grid
        ? ordinal / static_cast<std::uint64_t>(lane.steps)
        : ordinal / static_cast<std::uint64_t>(lane.steps);
    event.beat = selectedBeat;
    event.sample = beatToSample(selectedBeat);
    event.mode = lane.mode;
    currentBeat_ = selectedBeat;
    return true;
}

} // namespace fable
