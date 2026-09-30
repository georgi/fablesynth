#include "DrumRhythm.h"

#include <cmath>
#include <algorithm>
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
        if (lane.delayMs < -50 || lane.delayMs > 50
            || std::any_of(lane.stepDelayMs.begin(), lane.stepDelayMs.end(), [](int v) { return v < -50 || v > 50; })) {
            setError(error, DrumRhythmError::invalidDelay); return false;
        }
        if (!lane.enabled) continue;
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
    word |= (std::uint64_t)(lane.micro ? 1 : 0) << 48;
    word |= (std::uint64_t)(lane.delayMs + 50) << 49;
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
    lane.micro = ((word >> 48) & 1u) != 0;
    lane.delayMs = (int)((word >> 49) & 127u) - 50;
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
    for (int i = 0; i < 16; ++i) for (int s = 0; s < 256; ++s)
        stepDelays_[(size_t)i][(size_t)s].store(rhythm.lanes[(size_t)i].stepDelayMs[(size_t)s], std::memory_order_relaxed);
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

        for (int i = 0; i < 16; ++i) for (int s = 0; s < 256; ++s)
            candidate.lanes[(size_t)i].stepDelayMs[(size_t)s] = stepDelays_[(size_t)i][(size_t)s].load(std::memory_order_relaxed);
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

bool DrumRhythmScheduler::retime(double absoluteBeat, std::int64_t absoluteSample) {
    if (!std::isfinite(absoluteBeat) || absoluteBeat < 0.0 || absoluteSample < 0)
        return false;
    beatAnchor_ = absoluteBeat;
    sampleAnchor_ = absoluteSample;
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

void DrumRhythmScheduler::setChain(const int* bars, int count) {
    if (!bars || count < 1 || count > 16) return;
    chainLength_ = count;
    for (int i = 0; i < count; ++i) chain_[(size_t)i] = std::clamp(bars[i], 0, 15);
}

void DrumRhythmScheduler::returnEvent(const DrumRhythmEvent& event) {
    if (event.lane < 0 || event.lane >= 16) return;
    auto& cursor = cursors_[(size_t)event.lane];
    if (event.eventOrdinal < cursor.ordinal) {
        const auto distance = cursor.ordinal - event.eventOrdinal;
        cursor.consumed = distance >= 32 ? 0xfffffffeu
            : (cursor.consumed << distance) | (((1u << distance) - 1u) & ~1u);
        cursor.ordinal = event.eventOrdinal;
    } else {
        const auto distance = event.eventOrdinal - cursor.ordinal;
        if (distance < 32) cursor.consumed &= ~(1u << distance);
    }
}

bool DrumRhythmScheduler::updateRhythm(const DrumRhythm& next, double beat, std::uint64_t minimumGridOrdinal) {
    if (!validateDrumRhythm(next)) return false;
    if (!initialized_) reset(beat);
    for (int i = 0; i < DR_RHYTHM_LANES; ++i) {
        const auto& a = rhythm_.lanes[(size_t)i];
        const auto& b = next.lanes[(size_t)i];
        if (b.scheduled() && (!a.scheduled() || a.enabled != b.enabled || a.mode != b.mode
            || (b.mode == DrumRhythmMode::fit && (a.steps != b.steps || a.cycleBeats != b.cycleBeats)))) {
            cursors_[(size_t)i].consumed = 0;
            cursors_[(size_t)i].ordinal = b.mode == DrumRhythmMode::grid ? std::max(minimumGridOrdinal, firstGridOrdinal(beat))
                : (std::uint64_t)std::max(0.0, std::ceil(beat / spacing(b) - kBeatEpsilon));
        }
    }
    rhythm_ = next;
    return true;
}

bool DrumRhythmScheduler::reset(double absoluteBeat, bool includeEarlyEntry) {
    if (!std::isfinite(absoluteBeat) || absoluteBeat < 0.0)
        return false;

    currentBeat_ = absoluteBeat;
    for (int i = 0; i < DR_RHYTHM_LANES; ++i) {
        const auto& lane = rhythm_.lanes[static_cast<std::size_t>(i)];
        const auto slotBeats = spacing(lane);
        cursors_[(size_t)i].consumed = 0;
        if (!lane.scheduled() || slotBeats <= 0.0) {
            cursors_[static_cast<std::size_t>(i)].ordinal = 0;
            continue;
        }

        if (lane.micro && includeEarlyEntry) {
            auto& cursor = cursors_[(size_t)i];
            cursor.ordinal = (uint64_t)std::max(0.0, std::floor(absoluteBeat / slotBeats) - 1);
            const bool fit = lane.enabled && lane.mode == DrumRhythmMode::fit;
            while ((double)cursor.ordinal * slotBeats + (!fit && (cursor.ordinal & 1u)
                ? swing_ * kSwingMax * kGridSpacingBeats : 0.0) < absoluteBeat - kBeatEpsilon) ++cursor.ordinal;
            continue;
        }
        if (lane.micro) {
            auto& cursor = cursors_[(size_t)i];
            cursor.ordinal = (std::uint64_t)std::max(0.0, std::floor((absoluteBeat - .1 * bpm_ / 60.0) / slotBeats) - 1);
            for (int bit = 0; bit < 32; ++bit)
                if (eventBeat(lane, cursor.ordinal + bit) < absoluteBeat - kBeatEpsilon) cursor.consumed |= 1u << bit;
            while (cursor.consumed & 1u) { cursor.consumed >>= 1; ++cursor.ordinal; }
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

int DrumRhythmScheduler::sourceBar(const DrumLaneRhythm& lane, std::uint64_t ordinal) const {
    return lane.enabled ? lane.sourceBar : chain_[(size_t)((ordinal / 16) % (std::uint64_t)chainLength_)];
}
int DrumRhythmScheduler::sourceStep(const DrumLaneRhythm& lane, std::uint64_t ordinal) const {
    return lane.enabled ? positiveModulo(ordinal, lane.steps, lane.rotation) : (int)(ordinal % 16);
}
double DrumRhythmScheduler::spacing(const DrumLaneRhythm& lane) const {
    return lane.enabled && lane.mode == DrumRhythmMode::fit
        ? (double)lane.cycleBeats / lane.steps : kGridSpacingBeats;
}

double DrumRhythmScheduler::eventBeat(const DrumLaneRhythm& lane, std::uint64_t ordinal) const {
    const bool fit = lane.enabled && lane.mode == DrumRhythmMode::fit;
    const double base = (double)ordinal * spacing(lane)
        + (!fit && (ordinal & 1u) ? swing_ * kSwingMax * kGridSpacingBeats : 0.0);
    const int index = sourceBar(lane, ordinal) * 16 + sourceStep(lane, ordinal);
    const int offset = lane.delayMs + (index >= 0 && index < 256 ? lane.stepDelayMs[(size_t)index] : 0);
    return std::max(0.0, base + (lane.micro ? offset * bpm_ / 60000.0 : 0.0));
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
    const long double samples = static_cast<long double>(sampleAnchor_)
        + static_cast<long double>(beat - beatAnchor_)
        * (60.0L / static_cast<long double>(bpm_))
        * static_cast<long double>(sampleRate_);
    return static_cast<std::int64_t>(std::llround(samples));
}

bool DrumRhythmScheduler::nextEvent(double inclusiveEndBeat, DrumRhythmEvent& event) {
    if (!initialized_ && !reset(0.0))
        return false;
    if (!std::isfinite(inclusiveEndBeat))
        return false;

    int selected = -1, selectedBit = 0;
    double selectedBeat = std::numeric_limits<double>::infinity();
    lastLaneScanCount_ = DR_RHYTHM_LANES;
    for (int i = 0; i < DR_RHYTHM_LANES; ++i) {
        const auto& lane = rhythm_.lanes[(size_t)i];
        if (!lane.scheduled()) continue;
        const auto& cursor = cursors_[(size_t)i];
        for (int bit = 0; bit < (lane.micro ? 32 : 1); ++bit) {
            const auto ordinal = cursor.ordinal + bit;
            // Unswung base minus the largest possible advance bounds all later hits.
            if ((double)ordinal * spacing(lane) - (lane.micro ? .1 * bpm_ / 60.0 : 0.0)
                > std::min(inclusiveEndBeat, selectedBeat) + kBeatEpsilon) break;
            if ((cursor.consumed >> bit) & 1u) continue;
            const auto candidate = eventBeat(lane, ordinal);
            if (candidate > inclusiveEndBeat + kBeatEpsilon) continue;
            if (selected < 0 || candidate < selectedBeat - kBeatEpsilon) {
                selected = i; selectedBit = bit; selectedBeat = candidate;
            }
        }
    }

    if (selected < 0)
        return false;

    const auto index = static_cast<std::size_t>(selected);
    const auto& lane = rhythm_.lanes[index];
    auto& cursor = cursors_[index];
    const auto ordinal = cursor.ordinal + selectedBit;
    cursor.consumed |= 1u << selectedBit;
    while (cursor.consumed & 1u) { cursor.consumed >>= 1; ++cursor.ordinal; }
    event.lane = selected;
    event.eventOrdinal = ordinal;
    event.sourceBar = sourceBar(lane, ordinal);
    event.sourceStep = sourceStep(lane, ordinal);
    event.cycle = lane.mode == DrumRhythmMode::grid
        ? ordinal / static_cast<std::uint64_t>(lane.steps)
        : ordinal / static_cast<std::uint64_t>(lane.steps);
    event.beat = selectedBeat;
    event.sample = beatToSample(selectedBeat);
    event.mode = lane.mode;
    currentBeat_ = selectedBeat;
    return true;
}

bool DrumRhythmScheduler::reschedule(DrumRhythmEvent& event) const {
    if (event.lane < 0 || event.lane >= DR_RHYTHM_LANES)
        return false;
    const auto& lane = rhythm_.lanes[static_cast<std::size_t>(event.lane)];
    if (!lane.scheduled())
        return false;

    event.sourceBar = sourceBar(lane, event.eventOrdinal);
    event.sourceStep = sourceStep(lane, event.eventOrdinal);
    event.cycle = event.eventOrdinal / static_cast<std::uint64_t>(lane.steps);
    event.beat = eventBeat(lane, event.eventOrdinal);
    event.sample = beatToSample(event.beat);
    event.mode = lane.mode;
    return true;
}

} // namespace fable
