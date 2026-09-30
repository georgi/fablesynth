// Focused, JUCE-independent tests for the DR-1 POLY timing core.
#include "../source/drum/dsp/DrumRhythm.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace fable;

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}

DrumRhythm oneLane(DrumRhythmMode mode, int steps, int rotation = 0,
                   int cycleBeats = 4) {
    DrumRhythm rhythm;
    auto& lane = rhythm.lanes[0];
    lane.enabled = true;
    lane.sourceBar = 3;
    lane.steps = steps;
    lane.rotation = rotation;
    lane.mode = mode;
    lane.cycleBeats = cycleBeats;
    return rhythm;
}

std::vector<DrumRhythmEvent> collect(DrumRhythmScheduler& scheduler,
                                     double endBeat) {
    std::vector<DrumRhythmEvent> events;
    DrumRhythmEvent event;
    while (scheduler.nextEvent(endBeat, event))
        events.push_back(event);
    return events;
}

void checkAtomicState() {
    DrumRhythmState state;
    DrumRhythm rhythm = oneLane(DrumRhythmMode::fit, 5, 2, 8);
    rhythm.lanes[7].enabled = true;
    rhythm.lanes[7].sourceBar = 11;
    rhythm.lanes[7].steps = 1;
    check(state.publish(rhythm), "atomic state accepts a complete valid snapshot");

    DrumRhythm copy;
    check(state.read(copy) && copy.v == DR_RHYTHM_VERSION
              && copy.lanes[0].mode == DrumRhythmMode::fit
              && copy.lanes[0].steps == 5 && copy.lanes[0].rotation == 2
              && copy.lanes[7].sourceBar == 11,
          "atomic state returns coherent lane metadata");

    auto invalid = rhythm;
    invalid.lanes[0].rotation = 5;
    DrumRhythmError error = DrumRhythmError::none;
    check(!validateDrumRhythm(invalid, &error)
              && error == DrumRhythmError::rotationOutOfRange
              && state.sequence() % 2 == 0,
          "invalid publication is rejected without opening the snapshot");
}

void checkQuarterGrid() {
    // A 16-slot source phrase with hits at 0,4,8,12 is a quarter-note kick.
    // The scheduler still exposes each sixteenth so the later engine adapter
    // can read the selected source cell and preserve accents/rests.
    DrumRhythmScheduler scheduler;
    check(scheduler.setTempo(48000.0, 120.0)
              && scheduler.setRhythm(oneLane(DrumRhythmMode::grid, 16))
              && scheduler.reset(0.0),
          "120 BPM / 48 kHz quarter fixture setup");
    const auto all = collect(scheduler, 4.0 - 0.25);
    const std::uint16_t quarterHits = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 12);
    std::vector<std::int64_t> hits;
    for (const auto& event : all)
        if ((quarterHits & (1u << event.sourceStep)) != 0)
            hits.push_back(event.sample);
    check(hits == std::vector<std::int64_t>({0, 24000, 48000, 72000}),
          "GRID source phrase produces quarter-note samples");
    check(scheduler.lastLaneScanCount() == DR_RHYTHM_LANES,
          "next-event scan is bounded by the 16 fixed lanes");
}

void checkFitFixture(int steps, int cycleBeats,
                     const std::vector<std::int64_t>& expected,
                     const char* name) {
    DrumRhythmScheduler scheduler(48000.0, 120.0);
    check(scheduler.setRhythm(oneLane(DrumRhythmMode::fit, steps, 0, cycleBeats))
              && scheduler.reset(0.0), name);
    const auto events = collect(scheduler, static_cast<double>(cycleBeats) - 1.0e-9);
    std::vector<std::int64_t> samples;
    for (const auto& event : events)
        samples.push_back(event.sample);
    check(samples == expected, "FIT absolute sample conversion matches fixture");
    check(!events.empty() && events.back().beat < cycleBeats,
          "FIT excludes the next cycle boundary until its own event");
}

void checkRotationAndReset() {
    DrumRhythmScheduler scheduler(48000.0, 120.0);
    check(scheduler.setRhythm(oneLane(DrumRhythmMode::fit, 3, 1, 4))
              && scheduler.reset(0.0), "rotation fixture setup");
    const auto events = collect(scheduler, 1.0);
    check(events.size() == 1 && events[0].sourceStep == 2
              && scheduler.currentBeat() == 0.0,
          "positive rotation changes source cells, not event timing");

    const double seekBeat = 4.0 / 3.0;
    check(scheduler.reset(seekBeat), "reset accepts an absolute musical position");
    DrumRhythmEvent event;
    check(scheduler.nextEvent(seekBeat, event) && event.beat == seekBeat
              && event.sample == 32000,
          "reset seeks without a catch-up burst and preserves absolute samples");
}

void checkGridSwingUsesAbsoluteOrdinal() {
    DrumRhythmScheduler scheduler(48000.0, 120.0, 0.5);
    check(scheduler.setRhythm(oneLane(DrumRhythmMode::grid, 15))
              && scheduler.reset(0.0), "GRID swing fixture setup");
    const auto events = collect(scheduler, 0.5);
    check(events.size() == 3 && events[0].sample == 0
              && events[1].sample == 8001 && events[2].sample == 12000
              && events[0].sourceStep == 0 && events[1].sourceStep == 1
              && events[2].sourceStep == 2,
          "GRID swing follows the unwrapped global ordinal across lane loops");
}

void checkInvalidInputs() {
    DrumRhythmScheduler scheduler;
    auto invalid = oneLane(DrumRhythmMode::fit, 3, 0, 6);
    DrumRhythmError error = DrumRhythmError::none;
    check(!validateDrumRhythm(invalid, &error)
              && error == DrumRhythmError::invalidFitCycle,
          "FIT accepts only the v1 four- or eight-beat cycles");
    check(!scheduler.setTempo(0.0, 120.0) && !scheduler.reset(-1.0),
          "invalid sample rate and negative seek are rejected");
}

} // namespace

int main() {
    std::printf("\n== DR-1 POLY rhythm timing core ==\n");
    {
        DrumRhythmScheduler scheduler(48000, 120, 0);
        auto rhythm = oneLane(DrumRhythmMode::fit, 3);
        rhythm.lanes[1] = rhythm.lanes[0]; rhythm.lanes[1].steps = 5;
        scheduler.setRhythm(rhythm); scheduler.reset();
        auto initial = collect(scheduler, 1.0);
        rhythm.lanes[0].rotation = 1; rhythm.lanes[0].sourceBar = 2;
        check(scheduler.updateRhythm(rhythm, 1.0), "live metadata edit accepted");
        auto next = collect(scheduler, 1.5);
        check(next.size() == 1 && next[0].lane == 0 && next[0].eventOrdinal == 1 && next[0].sourceStep == 0
            && next[0].sourceBar == 2, "rotation and source edits retain event cursors");
        rhythm.lanes[0].steps = 7;
        scheduler.updateRhythm(rhythm, 1.5);
        auto unchanged = collect(scheduler, 1.65);
        check(unchanged.size() == 1 && unchanged[0].lane == 1 && unchanged[0].eventOrdinal == 2,
            "step-count edit leaves other lane phase untouched");
    }
    {
        DrumRhythm rhythm; auto& lane = rhythm.lanes[0];
        lane.micro = true; lane.delayMs = 10; lane.stepDelayMs[1] = 15; lane.stepDelayMs[16] = -50;
        DrumRhythmScheduler scheduler(48000, 120, 0); const int chain[] = {0, 1};
        scheduler.setChain(chain, 2); scheduler.setRhythm(rhythm); scheduler.reset();
        const auto events = collect(scheduler, 4.0);
        check(events.size() == 17 && events[1].sample == 7200 && events[16].sample == 94080
            && events[16].sourceBar == 1, "micro timing adds offsets and anticipates the next bar");
        lane.delayMs = 0; lane.stepDelayMs.fill(0); lane.stepDelayMs[1] = 50; lane.stepDelayMs[2] = -50;
        scheduler.setTempo(48000, 200, 1); scheduler.setRhythm(rhythm); scheduler.reset();
        const auto reordered = collect(scheduler, .6);
        check(reordered.size() == 3 && reordered[1].eventOrdinal == 2 && reordered[1].sample == 4800
            && reordered[2].eventOrdinal == 1 && reordered[2].sample == 8401,
            "micro timing orders adjacent swung hits by actual time");
        DrumRhythmState state; DrumRhythm restored;
        check(state.publish(rhythm) && state.read(restored) && restored.lanes[0].micro
            && restored.lanes[0].stepDelayMs[2] == -50, "atomic snapshot retains signed micro timing");
        lane.delayMs = -20; lane.stepDelayMs.fill(0);
        scheduler.setTempo(48000, 120, 0); scheduler.setRhythm(rhythm); scheduler.reset();
        const auto early = collect(scheduler, .3);
        check(early.size() == 2 && early[0].sample == 0 && early[1].sample == 5040,
            "first early event clamps to start, subsequent events stay early");
    }
    checkAtomicState();
    checkQuarterGrid();
    checkFitFixture(3, 4, {0, 32000, 64000}, "FIT 3 fixture setup");
    checkFitFixture(5, 4, {0, 19200, 38400, 57600, 76800}, "FIT 5 fixture setup");
    checkRotationAndReset();
    checkGridSwingUsesAbsoluteOrdinal();
    checkInvalidInputs();
    return failures == 0 ? 0 : 1;
}
