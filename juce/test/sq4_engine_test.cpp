#include "../source/dsp/ClipHost.h"
#include "../source/dsp/Engine.h"
#include "../source/dsp/Presets.h"
#include "../source/dsp/Wavetables.h"
#include "../source/seq/dsp/SeqProtocol.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void testProtocol() {
    using namespace fable;
    CHECK(sqBytesPerBar(Machine::DR1) == 256);
    CHECK(sqBytesPerBar(Machine::BL1) == 48);
    CHECK(sqBytesPerBar(Machine::WT1) == 48);
    CHECK(sqDr1Idx(1, 2, 3) == (1 * 16 + 2) * 16 + 3);
    CHECK(sqNoteIdx(2, 5) == (2 * 16 + 5) * 3);

    auto dr = sqEmptyClip(Machine::DR1, 2);
    CHECK(dr.size() == 512);
    for (auto b : dr) CHECK(b == 0);
    auto bl = sqEmptyClip(Machine::BL1, 1);
    CHECK(bl.size() == 48);
    for (int i = 0; i < 48; i++) CHECK(bl[(size_t)i] == (i % 3 == 2 ? 1 : 0));

    const double sr = 48000, bpm = 122;
    const double spb = sr * 60 / bpm;
    CHECK(std::abs(sqSamplesPerBeat(bpm, sr) - spb) < 1e-9);
    CHECK(std::abs(sqSamplesPerStep(bpm, sr) - spb / 4) < 1e-9);
    CHECK(std::abs(sqBarFrames(bpm, sr) - spb * 4) < 1e-9);

    // OFF -> 0 ("this block"); 1/4 -> next beat; 1 BAR -> next bar; pure fn.
    CHECK(sqBoundaryFrame(Quant::Off, 99999, 256, bpm, sr) == 0.0);
    double b1 = sqBoundaryFrame(Quant::Quarter, 256 + spb * 1.5, 256, bpm, sr);
    CHECK(std::abs(b1 - (256 + spb * 2)) < 1e-6);
    double b2 = sqBoundaryFrame(Quant::Bar, 256 + spb * 1.5, 256, bpm, sr);
    CHECK(std::abs(b2 - (256 + spb * 4)) < 1e-6);
    // at-or-after: now exactly on a boundary returns now
    CHECK(std::abs(sqBoundaryFrame(Quant::Bar, 256 + spb * 4, 256, bpm, sr) - (256 + spb * 4)) < 1e-6);
    // now before anchor clamps to anchor
    CHECK(std::abs(sqBoundaryFrame(Quant::Bar, 0, 256, bpm, sr) - 256) < 1e-6);

    auto p = sqSongPosition(256 + spb * 5.2, 256, bpm, sr);
    CHECK(p.beat == 1 && p.bar == 2);
}

// Drive a ClipHost through consecutive 128-sample quanta from frame f0,
// recording fires; returns fired absolute steps.
static std::vector<int> run(fable::ClipHost& h, double f0, int blocks,
                            std::vector<fable::HostEvent>& evs) {
    std::vector<int> fired;
    for (int b = 0; b < blocks; b++)
        h.tick(f0 + b * 128.0, 128, [&](int abs) { fired.push_back(abs); });
    evs = h.events; // accumulated; caller clears
    return fired;
}

static void testClipHost() {
    using namespace fable;
    const double sr = 48000, bpm = 125;           // stepDur = 5760 exactly
    const double stepDur = sqSamplesPerStep(bpm, sr);

    // 1. Pending swaps in at atFrame, Start event posted, first fire is the
    //    phase-locked entry step (anchor-derived), not step 0.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, /*anchor*/ 256);
        auto clip = sqEmptyClip(Machine::DR1, 1);
        const double at = 256 + 16 * stepDur;      // exactly one bar after anchor
        h.scheduleClip(clip.data(), clip.size(), 1, at);
        std::vector<HostEvent> evs;
        auto fired = run(h, at - 128, 4, evs);     // spans the swap frame
        CHECK(!evs.empty() && evs[0].t == HostEvent::T::Start);
        CHECK(!fired.empty() && fired[0] == 0);    // 16 steps in = step 0 of a 1-bar clip
        // steps advance by stepDur
        if (fired.size() > 1) CHECK(fired[1] == 1);
    }
    // 2. Entry mid-clip: launch a 2-bar clip 5 steps after the anchor -> enters at step 5.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto clip = sqEmptyClip(Machine::BL1, 2);
        const double at = 5 * stepDur;
        h.scheduleClip(clip.data(), clip.size(), 2, at);
        std::vector<HostEvent> evs;
        auto fired = run(h, at - 64, 2, evs);
        CHECK(!fired.empty() && fired[0] == 5);
    }
    // 3. Re-schedule before the boundary replaces pending (one pending slot).
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::DR1, 1), b = sqEmptyClip(Machine::DR1, 2);
        h.scheduleClip(a.data(), a.size(), 1, 16 * stepDur);
        h.scheduleClip(b.data(), b.size(), 2, 16 * stepDur);
        std::vector<HostEvent> evs;
        run(h, 16 * stepDur - 128, 2, evs);
        CHECK(h.playingBars() == 2);
    }
    // 4. scheduleStop cancels a pending clip; Stop event still acks.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::DR1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 16 * stepDur);
        h.scheduleStop(16 * stepDur);
        std::vector<HostEvent> evs;
        auto fired = run(h, 16 * stepDur - 128, 3, evs);
        CHECK(fired.empty());
        bool sawStop = false;
        for (auto& e : evs) if (e.t == HostEvent::T::Stop) sawStop = true;
        CHECK(sawStop && !h.isPlaying());
    }
    // 5. Stop on a playing clip stops it at atFrame; no fires after.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::DR1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 0);   // quant OFF: at<=frame fires now
        std::vector<HostEvent> evs;
        run(h, 0, 4, evs);
        CHECK(h.isPlaying());
        h.scheduleStop(8 * stepDur);
        evs.clear(); h.events.clear();
        auto fired = run(h, 4 * stepDur, 8 * (int)(stepDur / 128) + 4, evs);
        for (int s : fired) CHECK(s < 8);
        CHECK(!h.isPlaying());
    }
    // 6. updateClip on the live slot preserves phase (playhead doesn't move):
    //    grow 1 bar -> 2 bars mid-flight, next fire continues the global grid.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::WT1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 0);
        std::vector<HostEvent> evs;
        run(h, 0, 3 * (int)(stepDur / 128), evs);   // ~3 steps in
        auto bigger = sqEmptyClip(Machine::WT1, 2);
        h.updateClip(bigger.data(), bigger.size(), 2);
        evs.clear(); h.events.clear();
        auto fired = run(h, 3 * stepDur + 128, (int)(stepDur / 128), evs);
        CHECK(!fired.empty() && fired[0] >= 3 && fired[0] <= 4);
        CHECK(h.playingBars() == 2);
    }
    // 7. updateClip targets the PENDING slot when one exists.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        auto a = sqEmptyClip(Machine::WT1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 32 * stepDur);
        auto b = sqEmptyClip(Machine::WT1, 4);
        h.updateClip(b.data(), b.size(), 4);
        std::vector<HostEvent> evs;
        run(h, 32 * stepDur - 128, 2, evs);
        CHECK(h.playingBars() == 4);
    }
    // 8. Swing delays odd 16ths by swing*0.667 of a step.
    {
        ClipHost h; h.setTempo(bpm, 0.5, sr, 0);
        auto a = sqEmptyClip(Machine::DR1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 0);
        std::vector<double> fireFrames;
        double f = 0;
        for (int b = 0; b < 200; b++) {
            h.tick(f, 128, [&](int) { fireFrames.push_back(f); });
            f += 128;
        }
        // step1 fires ~ stepDur*(1 + 0.5*0.667) after step0 (within a quantum)
        CHECK(fireFrames.size() >= 3);
        double d01 = fireFrames[1] - fireFrames[0];
        double d12 = fireFrames[2] - fireFrames[1];
        CHECK(std::abs(d01 - stepDur * (1 + 0.5 * 0.667)) <= 128.0);
        CHECK(std::abs(d12 - stepDur * (1 - 0.5 * 0.667)) <= 128.0);
    }
}

static double rmsOf(fable::Engine& e, double& frame, int blocks) {
    double sumSq = 0; long cnt = 0;
    float L[128], R[128];
    for (int b = 0; b < blocks; b++) {
        e.hostSetFrame(frame);
        e.render(L, R, 128);
        frame += 128;
        for (int i = 0; i < 128; i++) { sumSq += (double)L[i]*L[i] + (double)R[i]*R[i]; cnt += 2; }
    }
    return std::sqrt(sumSq / (double)cnt);
}

static void testWt1Hosted() {
    using namespace fable;
    Engine e;
    e.prepare(48000);
    std::vector<TablePtr> tables;
    for (auto& g : generateTables()) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    e.setTables(tables);
    // load factory preset 3 (CRYSTAL PLUCK) exactly as
    // FableAudioProcessor::setCurrentProgram does (PluginProcessor.cpp:320-335),
    // minus the APVTS round-trip which is JUCE-only.
    e.setParams(applyPreset(factoryPresets()[3]));
    e.setHostClipMode(true);
    e.hostTempo(122, 0, 256);

    // one-bar clip: quarter-note C lane hits on steps 0,4,8,12
    auto clip = sqEmptyClip(Machine::WT1, 1);
    for (int s : {0, 4, 8, 12}) { clip[(size_t)sqNoteIdx(0, s)] = 1; clip[(size_t)sqNoteIdx(0, s) + 1] = 0; }
    e.hostClip(clip.data(), (int)clip.size(), 1, 0.0);   // quant OFF

    double frame = 256;
    CHECK(rmsOf(e, frame, 400) > 1e-5);                  // audible

    HostEvent evs[64];
    int n = e.takeHostEvents(evs, 64);
    bool sawStart = false, sawPos = false;
    for (int i = 0; i < n; i++) {
        if (evs[i].t == HostEvent::T::Start) sawStart = true;
        if (evs[i].t == HostEvent::T::Pos) sawPos = true;
    }
    CHECK(sawStart && sawPos);

    // Stop gates the seq note off; the decay to silence takes the full ADSR
    // release, not an instant cut (WT-1 is a "pluck" patch: env1.r = 0.6s, so
    // the release tail rings past the stop). Skip past the release (300
    // blocks = 800ms > 9 release time-constants) before asserting silence —
    // matches the docs §6 rule 3 contract (seqGateOff(), not panic()).
    e.hostClipStop(frame);
    (void)rmsOf(e, frame, 300);
    CHECK(rmsOf(e, frame, 200) < 1e-4);                  // silent tail after release
}

int main() {
    testProtocol();
    testClipHost();
    testWt1Hosted();
    if (failures) { std::printf("%d FAILURES\n", failures); return 1; }
    std::printf("ALL PASS\n");
    return 0;
}
