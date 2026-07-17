#include "../source/bass/dsp/BassEngine.h"
#include "../source/bass/dsp/BassPatches.h"
#include "../source/dsp/ClipHost.h"
#include "../source/dsp/Engine.h"
#include "../source/dsp/Presets.h"
#include "../source/dsp/Wavetables.h"
#include "../source/drum/dsp/DrumEngine.h"
#include "../source/drum/dsp/DrumKits.h"
#include "../source/drum/dsp/DrumTables.h"
#include "../source/drum/dsp/SampledTables.gen.h"
#include "../source/seq/dsp/Conductor.h"
#include "../source/seq/dsp/ClipLibrary.h"
#include "../source/seq/dsp/ClipLibrary.gen.h"
#include "../source/seq/dsp/SeqFactory.h"
#include "../source/seq/dsp/SeqModel.h"
#include "../source/seq/dsp/SeqProtocol.h"
#include "../source/seq/dsp/SnapshotHistory.h"
#if FABLE_SQ4_TEST_JUCE
#include "../source/seq/SessionCodec.h"
#include "../source/seq/ClipClipboardCodec.h"
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <iterator>
#include <set>
#include <string>
#include <tuple>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void testClipLibrarySchema() {
    using namespace fable;

    ClipLibraryEntry lead;
    lead.id = "neon-lead-01";
    lead.name = "NEON LEAD";
    lead.machine = Machine::WT1;
    lead.bars = 2;
    lead.bytes = sqEmptyClip(lead.machine, lead.bars);
    lead.family = "techno";
    lead.role = "lead";
    lead.energy = 3;
    lead.tags = {"bright", "melodic"};
    lead.root = 0;
    lead.scale = "minor";
    lead.transpose = true;

    CHECK(validateClipLibraryEntry(lead).empty());
    CHECK(sqIsKnownClipRole(Machine::DR1, "four-on-floor"));
    CHECK(!sqIsKnownClipRole(Machine::DR1, "lead"));
    CHECK(sqIsKnownClipFamily("lo-fi"));
    CHECK(!sqIsKnownClipFamily("unknown"));

    auto bad = lead;
    bad.bytes.pop_back();
    CHECK(!validateClipLibraryEntry(bad).empty());
    bad = lead;
    bad.bytes[(size_t)sqNoteIdx(0, 0) + 1] = 12;
    CHECK(!validateClipLibraryEntry(bad).empty());
    bad = lead;
    bad.energy = 6;
    CHECK(!validateClipLibraryEntry(bad).empty());
    bad = lead;
    bad.role = "four-on-floor";
    CHECK(!validateClipLibraryEntry(bad).empty());

    std::vector<ClipLibraryEntry> library {lead, lead};
    CHECK(!validateClipLibrary(library).empty());
    library[1].id = "neon-lead-02";
    CHECK(validateClipLibrary(library).empty());

    const auto& factory = factoryClipLibrary();
    CHECK(factory.size() == 72);
    CHECK(validateClipLibrary(factory).empty());
    int dr = 0, bl = 0, wt = 0;
    for (const auto& clip : factory) {
        if (clip.machine == Machine::DR1) ++dr;
        else if (clip.machine == Machine::BL1) ++bl;
        else if (clip.machine == Machine::WT1) ++wt;
    }
    CHECK(dr == 32 && bl == 20 && wt == 20);

    lead.bytes[(size_t)sqWtNoteIdx(0, 0)] = 1 | (1 << 2);
    lead.bytes[(size_t)sqWtNoteIdx(0, 0) + 1] = 11;
    lead.bytes[(size_t)sqWtNoteIdx(0, 0) + 2] = 2;
    lead.bytes[(size_t)sqWtNoteIdx(0, 2) + 1] = 9; // inactive metadata is preserved
    lead.bytes[(size_t)sqWtNoteIdx(0, 2) + 2] = 1;
    const auto original = lead.bytes;
    const auto transposed = transformClipLibraryEntry(lead, ClipTransformKind::transpose, 2);
    CHECK(lead.bytes == original);
    CHECK(transposed.bytes[(size_t)sqWtNoteIdx(0, 0) + 1] == 1);
    CHECK(transposed.bytes[(size_t)sqWtNoteIdx(0, 0) + 2] == 0);
    CHECK(transposed.bytes[(size_t)sqWtNoteIdx(0, 2) + 1] == 9);
    CHECK(transposed.bytes[(size_t)sqWtNoteIdx(0, 2) + 2] == 1);
    CHECK(transposed.root == 2);
    const auto rotated = transformClipLibraryEntry(lead, ClipTransformKind::rotate, 1);
    CHECK((rotated.bytes[(size_t)sqWtNoteIdx(0, 1)] & 1) != 0);
    const auto repeated = transformClipLibraryEntry(lead, ClipTransformKind::repeat, 4);
    CHECK(repeated.bars == 4 && repeated.bytes.size() == (size_t)(4 * sqBytesPerBar(Machine::WT1)));
}

static void testProtocol() {
    using namespace fable;
    CHECK(sqBytesPerBar(Machine::DR1) == 256);
    CHECK(sqBytesPerBar(Machine::BL1) == 48);
    CHECK(sqBytesPerBar(Machine::WT1) == 384);
    CHECK(sqDr1Idx(1, 2, 3) == (1 * 16 + 2) * 16 + 3);
    CHECK(sqNoteIdx(2, 5) == (2 * 16 + 5) * 3);

    auto dr = sqEmptyClip(Machine::DR1, 2);
    CHECK(dr.size() == 512);
    for (auto b : dr) CHECK(b == 0);
    auto bl = sqEmptyClip(Machine::BL1, 1);
    CHECK(bl.size() == 48);
    for (int i = 0; i < 48; i++) CHECK(bl[(size_t)i] == (i % 3 == 0 ? 4 : i % 3 == 2 ? 1 : 0));

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

static void testModelAndFactory() {
    using namespace fable;
    SessionData s = factorySession();
    CHECK(validateSession(s).empty());
    CHECK(s.name == "NEON TALE" && s.bpm == 122 && s.quant == Quant::Bar);
    CHECK(s.tracks.size() == 4 && s.scenes.size() == 6);
    CHECK(s.tracks[0].machine == Machine::DR1 && s.tracks[1].machine == Machine::BL1);
    CHECK(s.tracks[2].machine == Machine::WT1 && s.tracks[3].machine == Machine::WT1);
    CHECK(s.tracks[0].color == 0xff4de8ffu);
    // INTRO: clips on tracks 0 and 3 only
    CHECK(s.scenes[0].hasClip[0] && !s.scenes[0].hasClip[1] && !s.scenes[0].hasClip[2] && s.scenes[0].hasClip[3]);
    // DROP A drum clip: four-on-the-floor kick, accent step 0, 2 bars
    const ClipData& kit = s.scenes[2].clips[0];
    CHECK(kit.bars == 2 && kit.bytes.size() == 512);
    CHECK(kit.bytes[(size_t)sqDr1Idx(0, 0, 0)] == 2);
    for (int st : {4, 8, 12}) CHECK(kit.bytes[(size_t)sqDr1Idx(0, 0, st)] == 1);
    // every clip's byte length matches bars * bytesPerBar
    for (auto& sc : s.scenes)
        for (size_t t = 0; t < sc.clips.size(); t++)
            if (sc.hasClip[t])
                CHECK((int)sc.clips[t].bytes.size() == sc.clips[t].bars * sqBytesPerBar(s.tracks[t].machine));
    // the factory ships no pass-through tracks: every empty cell is a stop
    // button on scene launch (protocol.ts:66-72)
    for (auto& sc : s.scenes) CHECK(sc.pass.empty());
    // validation rejects bad docs
    SessionData bad = factorySession(); bad.bpm = 300;
    CHECK(!validateSession(bad).empty());
    SessionData bad2 = factorySession(); bad2.scenes[1].clips[1].bytes.pop_back();
    CHECK(!validateSession(bad2).empty());

    // mute/solo gates (model.ts:20-51)
    std::unordered_map<int,int> owner{{0, 2}};
    std::vector<bool> tm(4,false), sm(6,false), solo(4,false);
    CHECK(isTrackAudible(0, owner, tm, sm, solo));
    CHECK(!isTrackAudible(1, owner, tm, sm, solo));   // unowned
    CHECK(isTrackOpen(1, owner, tm, sm, solo));       // open even between clips
    solo[2] = true;
    CHECK(!isTrackAudible(0, owner, tm, sm, solo));   // another track soloed
    solo[2] = false; sm[2] = true;
    CHECK(!isTrackAudible(0, owner, tm, sm, solo));   // owning scene muted
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
    // 9. A host quantum spanning more than one step's duration (offline
    //    render with a large block, e.g. 8192 samples at 200 bpm, where
    //    stepDur = 3600) must fire every step actually due, not just one —
    //    the single-fire `if` this replaced would silently drop the rest and
    //    leak the backlog forever. bpm 200 / sr 48000 -> stepDur = 3600.
    //    Quantum 1 (frame 0..8192): the phase-locked entry fires step 0 at
    //    frame 0 (due 3600 lies outside this quantum -> exactly one fire).
    //    Quantum 2 (frame 8192..16384): step 1 (due 3600) and step 2 (due
    //    7200) both fall inside it -> exactly two fires, in order.
    {
        const double bpm2 = 200, dur2 = sqSamplesPerStep(bpm2, sr); // 3600
        CHECK(dur2 == 3600.0);
        ClipHost h; h.setTempo(bpm2, 0, sr, 0);
        auto clip = sqEmptyClip(Machine::DR1, 1);
        h.scheduleClip(clip.data(), clip.size(), 1, 0);

        std::vector<int> q1;
        h.tick(0.0, 8192, [&](int abs) { q1.push_back(abs); });
        CHECK(q1.size() == 1 && q1[0] == 0);

        std::vector<int> q2;
        h.tick(8192.0, 8192, [&](int abs) { q2.push_back(abs); });
        CHECK(q2.size() == 2);
        if (q2.size() == 2) { CHECK(q2[0] == 1); CHECK(q2[1] == 2); }
    }
    // 10. After prepare(), a full launch / live-resize / re-launch / stop cycle
    //     never reallocates the clip, pend, or events buffers — the swap and
    //     the assigns all stay within the reserved capacity (Finding 1: no
    //     audio-thread allocation in steady state). Capacities are captured
    //     once and must be byte-for-byte identical at the end.
    {
        ClipHost h; h.setTempo(bpm, 0, sr, 0);
        h.prepare(SQ_MAX_BARS * 256, 64);
        const size_t cc = h.clipCapacity(), pc = h.pendCapacity(), ec = h.eventsCapacity();
        CHECK(cc >= (size_t)(SQ_MAX_BARS * 256) && pc >= (size_t)(SQ_MAX_BARS * 256) && ec >= 64);

        auto a = sqEmptyClip(Machine::DR1, 1);
        h.scheduleClip(a.data(), a.size(), 1, 0);          // launch (quant OFF)
        std::vector<HostEvent> evs;
        run(h, 0, 4, evs);                                  // swap + a few fires
        auto bigger = sqEmptyClip(Machine::DR1, 2);
        h.updateClip(bigger.data(), bigger.size(), 2);      // live resize (assign into clip_)
        h.events.clear(); evs.clear();
        run(h, 4 * stepDur, 4, evs);
        auto b = sqEmptyClip(Machine::DR1, 1);
        h.scheduleClip(b.data(), b.size(), 1, 8 * stepDur); // re-launch -> second swap
        h.events.clear(); evs.clear();
        run(h, 8 * stepDur - 128, 4, evs);
        h.scheduleStop(12 * stepDur);
        h.events.clear(); evs.clear();
        run(h, 12 * stepDur - 128, 4, evs);

        CHECK(h.clipCapacity() == cc);
        CHECK(h.pendCapacity() == pc);
        CHECK(h.eventsCapacity() == ec);
    }
}

// Finding 3: one prepared render block can span more grid steps than any
// single takeHostEvents call returns. The event buffer must be sized to that
// worst case (no audio-thread realloc), and the drain must be lossless — every
// step's Pos event observed across repeated 64-at-a-time takes, none dropped.
static void testHostEventLossless() {
    using namespace fable;
    const double sr = 48000;
    Engine e;
    e.prepare(sr);
    std::vector<TablePtr> tables;
    for (auto& g : generateTables()) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    e.setTables(tables);
    e.setParams(applyPreset(factoryPresets()[3]));

    // Prepared block far larger than one step at 200 bpm (stepDur = 3600):
    // 256000 samples ~= 71 steps, well past a single 64-event take.
    const int maxBlock = 256000;
    e.setHostClipMode(true, maxBlock);
    e.hostTempo(200, 0, 0);

    // Capacity must cover the worst case: ceil(maxBlock/minStepDur) + 8, min 64.
    const double minStepDur = sr * 60.0 / 200.0 / 4.0; // 3600
    const size_t want = std::max((size_t)64, (size_t)std::ceil((double)maxBlock / minStepDur) + 8);
    const size_t cap = e.hostEventsCapacity();
    CHECK(cap >= want);

    auto clip = sqEmptyClip(Machine::WT1, 1);              // 1 bar, 16 steps
    e.hostClip(clip.data(), (int)clip.size(), 1, 0.0, /*tag*/0); // quant OFF

    std::vector<float> L((size_t)maxBlock), R((size_t)maxBlock);
    e.hostSetFrame(0);
    e.render(L.data(), R.data(), maxBlock); // accumulates > 64 events, no drain between chunks

    // Lossless drain: 64 at a time until empty, collecting every Pos step.
    std::vector<int> pos; int starts = 0; HostEvent evs[64]; int m;
    while ((m = e.takeHostEvents(evs, 64)) > 0)
        for (int k = 0; k < m; k++) {
            if (evs[k].t == HostEvent::T::Start) starts++;
            else if (evs[k].t == HostEvent::T::Pos) pos.push_back(evs[k].step);
        }

    CHECK(starts == 1);
    CHECK(pos.size() > 64); // proves the block held more than one take's worth
    // Contiguous grid: each Pos step advances by exactly 1 (mod 16). A dropped
    // event (the old clear()-everything drain) would leave a gap here.
    bool contiguous = !pos.empty();
    for (size_t i = 1; i < pos.size(); i++)
        if (pos[i] != (pos[i - 1] + 1) % SQ_STEPS_PER_BAR) contiguous = false;
    CHECK(contiguous);
    // No audio-thread realloc: capacity is byte-for-byte unchanged.
    CHECK(e.hostEventsCapacity() == cap);
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
    for (int s : {0, 4, 8, 12}) { clip[(size_t)sqNoteIdx(0, s)] = 1 | (1 << 2); clip[(size_t)sqNoteIdx(0, s) + 1] = 0; }
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

// docs/sq4-clips.md §6 rule 4: "the old clip's last gate-off and the new
// clip's first trigger execute in the same block, old before new." Clip A
// holds a note with a long step duration (seqToGateOff_ = duration * dur,
// so it never gates off on its own within the test window); clip B is
// scheduled mid-flight and is all rests, so its own entry-step fire never
// gates anything off either. Without the swap hook, A's note would keep
// sounding past its duration once B takes over.
static void testWt1ClipSwapGatesOldNote() {
    using namespace fable;
    Engine e;
    e.prepare(48000);
    std::vector<TablePtr> tables;
    for (auto& g : generateTables()) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    e.setTables(tables);
    e.setParams(applyPreset(factoryPresets()[3]));
    e.setHostClipMode(true);
    e.hostTempo(120, 0, 0);

    // Clip A: step 0 on with a 63-step duration (keeps the gate held well
    // past the test window — seqToGateOff_ = 63 * dur). All later steps are
    // irrelevant: the test stops inside step 0's ~6000-sample first step at
    // bpm 120/sr 48000.
    auto clipA = sqEmptyClip(Machine::WT1, 1);
    clipA[(size_t)sqNoteIdx(0, 0)] = (uint8_t)(1 | (63 << 2));   // on, 63-step duration
    clipA[(size_t)sqNoteIdx(0, 0) + 1] = 0;
    e.hostClip(clipA.data(), (int)clipA.size(), 1, 0.0); // quant OFF

    double frame = 0;
    float L[128], R[128];
    for (int b = 0; b < 5; b++) { e.hostSetFrame(frame); e.render(L, R, 128); frame += 128; }
    CHECK(e.seqCurrentNote() >= 0);                      // A's note held, well before its own gate-off

    // Clip B: all rests. Its own entry-step fire is a no-op (readSeqStep().on
    // == false), so only the swap hook can end A's note here.
    auto clipB = sqEmptyClip(Machine::WT1, 1);
    e.hostClip(clipB.data(), (int)clipB.size(), 1, frame); // swap this block
    for (int b = 0; b < 3; b++) { e.hostSetFrame(frame); e.render(L, R, 128); frame += 128; }

    CHECK(e.seqCurrentNote() < 0);                       // A's note ended at the swap, not left hanging
}

// WT-1 clips are polyphonic: every active lane gets its own duration timer.
// A short chord tone must release without cutting a longer tone from the same
// trigger, and neither may be cut by the following rest step.
static void testWt1HostedIndependentChordDurations() {
    using namespace fable;
    Engine e;
    e.prepare(48000);
    std::vector<TablePtr> tables;
    for (auto& g : generateTables()) tables.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    e.setTables(tables);
    e.setParams(applyPreset(factoryPresets()[3]));
    e.setHostClipMode(true);
    e.hostTempo(120, 0, 0);

    auto clip = sqEmptyClip(Machine::WT1, 1);
    const auto put = [&](int lane, int note, int duration) {
        const int o = sqWtNoteIdx(0, 0, lane);
        clip[(size_t)o] = (uint8_t)(1 | (duration << 2));
        clip[(size_t)o + 1] = (uint8_t)note;
        clip[(size_t)o + 2] = 1;
    };
    put(0, 0, 3); // C: three 16ths
    put(1, 4, 1); // E: one 16th
    e.hostClip(clip.data(), (int)clip.size(), 1, 0.0);

    double frame = 0;
    float L[128], R[128];
    auto run = [&](int samples) {
        while (samples > 0) {
            const int n = std::min(samples, 128);
            e.hostSetFrame(frame);
            e.render(L, R, n);
            frame += n;
            samples -= n;
        }
    };

    run(128);
    CHECK(e.seqPendingOffCount() == 2);
    run(6000);
    CHECK(e.seqPendingOffCount() == 1);
    run(12000);
    CHECK(e.seqPendingOffCount() == 0);
}

static double rmsBass(fable::BassEngine& e, double& frame, int blocks) {
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

static std::vector<fable::TablePtr> makeBassTables() {
    using namespace fable;
    std::vector<TablePtr> out;
    for (auto& g : generateTables()) out.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    return out;
}

static void testBl1Hosted() {
    using namespace fable;
    BassEngine e;
    e.prepare(48000);
    e.setTables(makeBassTables());
    e.setParams(applyBassPatch(bassFactoryPatches()[0]));  // ACID LINE
    e.setHostClipMode(true);
    e.hostTempo(122, 0, 256);

    // steps 0..3: 0 on, 1 slide->2, 2 on, 3 off.
    auto clip = sqEmptyClip(Machine::BL1, 1);
    auto set = [&](int s, int note, bool slide) {
        clip[(size_t)sqNoteIdx(0, s)] = 1 | (1 << 2);
        clip[(size_t)sqNoteIdx(0, s) + 1] = (uint8_t)(note | (slide ? 0x80 : 0));
    };
    set(0, 0, false); set(1, 0, true); set(2, 3, false);
    e.hostClip(clip.data(), (int)clip.size(), 1, 0.0);

    double frame = 256;
    CHECK(rmsBass(e, frame, 400) > 1e-5);

    HostEvent evs[64];
    int n = e.takeHostEvents(evs, 64);
    bool sawStart = false, sawPos = false;
    for (int i = 0; i < n; i++) {
        if (evs[i].t == HostEvent::T::Start) sawStart = true;
        if (evs[i].t == HostEvent::T::Pos) sawPos = true;
    }
    CHECK(sawStart && sawPos);

    // stop releases the mono voice
    e.hostClipStop(frame);
    (void)rmsBass(e, frame, 100);
    CHECK(rmsBass(e, frame, 300) < 1e-4);
}

// docs/sq4-clips.md §6 rule 4: "the old clip's last gate-off and the new
// clip's first trigger execute in the same block, old before new." Clip A
// holds its gate via a continuous slide chain (step 1 on+slide keeps
// samplesToGateOff_ pinned to -1, so it never gates off on its own); clip B
// is scheduled mid-flight and is all rests, so its own entry-step fire never
// gates anything off either. Without the swap hook, A's note would keep
// sounding forever once B takes over.
static void testBl1ClipSwapGatesOldNote() {
    using namespace fable;
    BassEngine e;
    e.prepare(48000);
    e.setTables(makeBassTables());
    e.setParams(applyBassPatch(bassFactoryPatches()[0]));  // ACID LINE
    e.setHostClipMode(true);
    e.hostTempo(120, 0, 0);

    // Clip A: step 0 on, step 1 on+slide (keeps the gate held indefinitely —
    // clipFireAt's lookahead sees an on+slide next step and sets
    // samplesToGateOff_ = -1). All later steps are irrelevant: the test
    // stops well inside step 0's ~6000-sample duration at bpm 120/sr 48000.
    auto clipA = sqEmptyClip(Machine::BL1, 1);
    clipA[(size_t)sqNoteIdx(0, 0)] = 1 | (1 << 2);      // on, one step
    clipA[(size_t)sqNoteIdx(0, 0) + 1] = 0;
    clipA[(size_t)sqNoteIdx(0, 1)] = 1 | (1 << 2);      // on
    clipA[(size_t)sqNoteIdx(0, 1) + 1] = 0x80;          // slide
    e.hostClip(clipA.data(), (int)clipA.size(), 1, 0.0); // quant OFF

    double frame = 0;
    float L[128], R[128];
    for (int b = 0; b < 5; b++) { e.hostSetFrame(frame); e.render(L, R, 128); frame += 128; }
    CHECK(e.vizGate);                                    // A's note held, well before its own gate-off

    // Clip B: all rests. Its own entry-step fire is a no-op (on == false),
    // so only the swap hook can end A's note here.
    auto clipB = sqEmptyClip(Machine::BL1, 1);
    e.hostClip(clipB.data(), (int)clipB.size(), 1, frame); // swap this block
    for (int b = 0; b < 3; b++) { e.hostSetFrame(frame); e.render(L, R, 128); frame += 128; }

    CHECK(!e.vizGate);                                    // A's note ended at the swap, not left hanging
}

static double rmsDrum(fable::DrumEngine& e, double& frame, int blocks) {
    using namespace fable;
    double sumSq = 0; long cnt = 0;
    float bufs[DR_NBUSES][2][128];
    float* outs[DR_NBUSES][2];
    for (int b = 0; b < DR_NBUSES; b++)
        for (int c = 0; c < 2; c++) outs[b][c] = bufs[b][c];
    for (int b = 0; b < blocks; b++) {
        e.hostSetFrame(frame);
        e.render(outs, 128);
        frame += 128;
        for (int i = 0; i < 128; i++)
            sumSq += (double)bufs[0][0][i]*bufs[0][0][i] + (double)bufs[0][1][i]*bufs[0][1][i];
        cnt += 256;
    }
    return std::sqrt(sumSq / (double)cnt);
}

static std::vector<fable::TablePtr> makeDrumTables() {
    using namespace fable;
    std::vector<TablePtr> out;
    for (auto& t : generateDrumTables())
        out.push_back(std::make_shared<const GeneratedTable>(std::move(t)));
    for (auto& t : generateTables())
        out.push_back(std::make_shared<const GeneratedTable>(std::move(t)));
    for (auto& t : generateSampledDrumTables())
        out.push_back(std::make_shared<const GeneratedTable>(std::move(t)));
    return out;
}

static void testDr1Hosted() {
    using namespace fable;
    DrumEngine e;
    e.prepare(48000);
    e.setTables(makeDrumTables());
    e.setParams(applyKit(factoryKits()[0]));   // TR-VOID
    e.setHostClipMode(true);
    e.hostTempo(122, 0, 256);

    // four-on-the-floor kick (pad 0), accent on step 0
    auto clip = sqEmptyClip(Machine::DR1, 1);
    for (int s : {0, 4, 8, 12}) clip[(size_t)sqDr1Idx(0, 0, s)] = (uint8_t)(s == 0 ? 2 : 1);
    e.hostClip(clip.data(), (int)clip.size(), 1, 0.0);

    double frame = 256;
    CHECK(rmsDrum(e, frame, 400) > 1e-5);   // renders the MAIN bus (bus 0)

    HostEvent evs[64];
    int n = e.takeHostEvents(evs, 64);
    bool sawStart = false, sawPos = false;
    for (int i = 0; i < n; i++) {
        if (evs[i].t == HostEvent::T::Start) sawStart = true;
        if (evs[i].t == HostEvent::T::Pos) sawPos = true;
    }
    CHECK(sawStart && sawPos);

    // stop: pads ring out (one-shot AHD envelopes, not gated) — audio
    // continues briefly, then the envelopes finish decaying on their own.
    // Render contiguously (a voice's envelope only advances by samples
    // actually rendered) rather than skipping frame forward.
    e.hostClipStop(frame);
    double tail = rmsDrum(e, frame, 10);    // right after stop: the last hit still rings
    CHECK(tail > 1e-5);
    (void)rmsDrum(e, frame, 200);           // consume the ~0.2s AHD decay tail
    CHECK(rmsDrum(e, frame, 200) < 1e-4);   // fully silent once the tail has decayed
}

// Conductor tests: a FakeIO with recordable calls and a controllable frame
// clock stands in for SeqProcessor's FIFO. Acks are fired manually to assert
// the owner-flips-on-ack contract (store.test.ts).
struct FakeIO : fable::ConductorIO {
    double frame = 0;
    struct Sched { int t; int bars; double at; size_t bytes; };
    std::vector<Sched> clips; std::vector<std::pair<int,double>> stops;
    std::vector<std::tuple<int, std::vector<uint8_t>, int>> updates;
    std::map<int,float> gains; double bpm = 0, swing = -1, anchor = -1;
    double now() override { return frame; }
    std::vector<int> tags; // launch identity per scheduled clip (Finding 1)
    void ioScheduleClip(int t, const std::vector<uint8_t>& b, int bars, double at, int tag) override { clips.push_back({t,bars,at,b.size()}); tags.push_back(tag); }
    void ioScheduleStop(int t, double at) override { stops.push_back({t,at}); }
    void ioUpdateClip(int t, const std::vector<uint8_t>& b, int bars) override { updates.push_back({t,b,bars}); }
    void ioSetTrackGain(int t, float g) override { gains[t] = g; }
    void ioSendTempo(double b, double s, double a) override { bpm = b; swing = s; anchor = a; }
};

static void testConductor() {
    using namespace fable;

    // 1. powerOn: anchor 256 frames ahead of a zero clock, tempo sent,
    //    every track's initial gain applied.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        CHECK(!c.playing());
        CHECK(c.anchor() == 256.0);
        CHECK(io.bpm == 122.0);
        CHECK(io.anchor == 256.0);
        for (int t = 0; t < 4; t++)
            CHECK(std::abs(io.gains[t] - Conductor::gainCurve(c.trackVol(t))) < 1e-9);
    }

    // 2/3/4/5/6: quantized launch, owner-flips-on-ack, re-launch retargets,
    // quant OFF, empty-slot launch is a no-op.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        io.frame = 1000;
        c.launch(0, 2); // DROP A drums
        CHECK(c.playing());
        CHECK(io.clips.size() == 1);
        CHECK(io.clips[0].t == 0 && io.clips[0].bars == 2 && io.clips[0].bytes == 512);
        CHECK(c.anchor() == 1256.0);
        CHECK(std::abs(io.clips[0].at - c.anchor()) < 1e-6);
        CHECK(c.queueOf(0) == 2);
        CHECK(c.ownerOf(0) == -2);
        CHECK(io.tags.back() == 2); // the scheduled clip carries its scene tag

        c.onClipStart(0, 2);
        CHECK(c.ownerOf(0) == 2);
        CHECK(c.queueOf(0) == -2);

        // Re-launch before the boundary re-targets: only the last clip (scene 3)
        // ever swaps in at the device, so only its Start ack fires -> owner 3.
        c.launch(0, 2);
        c.launch(0, 3);
        CHECK(io.clips.size() == 3);
        c.onClipStart(0, 3);
        CHECK(c.ownerOf(0) == 3);
    }

    // Finding 1: identity-carrying acks. A clip that started keeps ownership
    // even if a newer clip is queued while its Start ack is still in flight —
    // the ack names the scene that actually started, not the latest scheduled.
    // (Diverges from the web's identity-free acks; see Conductor.h.)
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(0, 2);              // A (scene 2) queued; reaches its boundary
                                    // and starts at the device — its Start ack
                                    // is now in flight, not yet delivered.
        c.launch(0, 3);             // B (scene 3) queued in that window: the
                                    // device now has A live and B pending.
        CHECK(c.queueOf(0) == 3);
        c.onClipStart(0, 2);        // A's delayed ack finally lands, naming A.
        CHECK(c.ownerOf(0) == 2);   // A owns, by identity — NOT the queued B.
        CHECK(c.queueOf(0) == 3);   // B stays pending; its own ack will promote it.
        c.onClipStart(0, 3);        // B's Start ack arrives.
        CHECK(c.ownerOf(0) == 3);
        CHECK(c.queueOf(0) == -2);
    }
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.cycleQuant(1);
        c.cycleQuant(1); // 1 BAR -> 1/4 -> OFF
        CHECK(c.quant() == Quant::Off);
        c.launch(1, 2); // DROP A bass
        CHECK(io.clips[0].at == 0.0);
    }
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(1, 0); // INTRO has no BASS clip
        CHECK(io.clips.empty());
    }

    // 7. stopTrack on an owned track schedules a stop, clears on ack; on an
    //    idle track it's a no-op.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(0, 2);
        c.onClipStart(0, 2);
        c.stopTrack(0);
        CHECK(io.stops.size() == 1);
        CHECK(c.queueOf(0) == SQ_STOP);
        c.onClipStop(0);
        CHECK(c.ownerOf(0) == -2);
        CHECK(c.queueOf(0) == -2);

        c.stopTrack(2); // never touched
        CHECK(io.stops.size() == 1);
    }

    // 8. stopTrack on a pending-only (queued, never started) launch.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(0, 2);
        c.stopTrack(0);
        CHECK(io.stops.size() == 1);
        CHECK(c.queueOf(0) == SQ_STOP);
        c.onClipStop(0);
        CHECK(c.queueOf(0) == -2);
        CHECK(c.ownerOf(0) == -2);
    }

    // 9. launchScene: DROP A schedules all four; INTRO schedules 0/3 and
    //    stops the empty cells 1/2 unless pass-through.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launchScene(2); // DROP A
        CHECK(io.clips.size() == 4);
        for (auto& s : io.clips) CHECK(std::abs(s.at - io.clips[0].at) < 1e-9);
    }
    // Empty cells only trigger a stop when the track is owned/queued
    // (stopTrack is a no-op on an idle track — store.test.ts "launchScene
    // leaves idle empty tracks alone"); so exercise it on tracks brought up
    // by DROP A first, matching the web spec's setup.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launchScene(2); // DROP A: bring all four tracks up first
        for (int t = 0; t < 4; t++) c.onClipStart(t, 2);
        io.clips.clear(); io.stops.clear();
        c.launchScene(0); // INTRO: clips on 0,3; stops on 1,2 (now owned)
        CHECK(io.clips.size() == 2);
        CHECK(io.stops.size() == 2);
    }
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launchScene(2);
        for (int t = 0; t < 4; t++) c.onClipStart(t, 2);
        c.togglePassThrough(0, 1); // INTRO track 1: remove the stop button
        io.clips.clear(); io.stops.clear();
        c.launchScene(0);
        for (auto& s : io.clips) CHECK(s.t != 1);
        for (auto& s : io.stops) CHECK(s.first != 1); // pass-through: no stop
        CHECK(c.ownerOf(1) == 2); // DROP A bass keeps riding
    }

    // 10. stopScene stops only tracks owned by/queued for that scene;
    //     stopAll stops every owned/queued track.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launchScene(2); // DROP A, all four
        for (int t = 0; t < 4; t++) c.onClipStart(t, 2);
        c.launch(0, 3); // re-queue drums to DROP B
        c.onClipStart(0, 3);
        io.stops.clear();
        c.stopScene(2);
        bool stopped0 = false;
        for (auto& s : io.stops) if (s.first == 0) stopped0 = true;
        CHECK(!stopped0); // now owned by scene 3
        CHECK(io.stops.size() == 3); // tracks 1,2,3 still owned by scene 2
    }
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launchScene(2); // queued only, no acks
        io.stops.clear();
        c.stopAll();
        CHECK(io.stops.size() == 4);
    }

    // Combined transport stop is immediate (not launch-quantized), marks the
    // conductor stopped, and a later scene launch starts from a fresh anchor.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launchScene(2);
        for (int t = 0; t < 4; ++t) c.onClipStart(t, 2);
        io.stops.clear();
        c.stopTransport();
        CHECK(!c.playing());
        CHECK(io.stops.size() == 4);
        for (const auto& stop : io.stops) CHECK(stop.second == 0.0);
        for (int t = 0; t < 4; ++t) CHECK(c.queueOf(t) == SQ_STOP);
        CHECK(c.songPos().beat == 0 && c.songPos().bar == 1);

        io.frame = 5000;
        c.launchScene(3);
        CHECK(c.playing());
        CHECK(c.anchor() == 5256.0);
        CHECK(io.clips.back().at == c.anchor());
    }

    // 11. mute / solo / scene-mute gains.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        const float before = io.gains[0];
        c.toggleTrackMute(0);
        CHECK(io.gains[0] == 0.0f);
        c.toggleTrackMute(0);
        CHECK(std::abs(io.gains[0] - before) < 1e-9);
    }
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.toggleSolo(1);
        CHECK(io.gains[1] > 0.0f);
        CHECK(io.gains[0] == 0.0f);
        CHECK(io.gains[2] == 0.0f);
        CHECK(io.gains[3] == 0.0f);
    }
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(0, 2);
        c.onClipStart(0, 2);
        c.toggleSceneMute(2);
        CHECK(io.gains[0] == 0.0f);
        CHECK(io.gains[1] > 0.0f); // unowned track keeps its independent open gate
    }

    // 12. setSwing re-sends tempo with the unchanged anchor.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        const double anchor = c.anchor();
        c.setSwing(0.4);
        CHECK(io.swing == 0.4);
        CHECK(io.anchor == anchor);
    }

    // 13. setBpm is guarded while any track is owned/queued; applies and
    //     re-anchors once the launcher is idle.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(0, 2);
        c.setBpm(140);
        CHECK(c.session().bpm == 122.0);
        CHECK(io.bpm == 122.0);
        c.onClipStart(0, 2);
        c.setBpm(140); // still owned
        CHECK(c.session().bpm == 122.0);
        c.stopTrack(0);
        c.onClipStop(0);
        io.frame = c.anchor() + 500;
        c.setBpm(140);
        CHECK(c.session().bpm == 140.0);
        CHECK(io.bpm == 140.0);
        CHECK(io.anchor == io.frame + 256.0);
    }

    // 14. updateClipBytes routes to the queued target when one is real,
    //     else the owner; unrelated edits never reach the engine; the doc
    //     is always updated.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(0, 0); // INTRO drums
        c.onClipStart(0, 0); // owner[0] = 0
        std::vector<uint8_t> bytes(256, 7);
        c.updateClipBytes(0, 0, bytes, 1);
        CHECK(io.updates.size() == 1);
        CHECK(std::get<0>(io.updates[0]) == 0);
        CHECK(c.session().scenes[0].clips[0].bytes[0] == 7);

        c.launch(0, 1); // BUILD drums queued — the worklet's write target
        std::vector<uint8_t> liveBytes(256, 9);
        c.updateClipBytes(0, 0, liveBytes, 1); // edit the outgoing live scene
        CHECK(io.updates.size() == 1); // unchanged: would clobber the pending clip
        CHECK(c.session().scenes[0].clips[0].bytes[0] == 9); // doc still updates

        std::vector<uint8_t> queuedBytes(512, 3);
        c.updateClipBytes(1, 0, queuedBytes, 2); // edit the queued scene
        CHECK(io.updates.size() == 2);
        CHECK(std::get<0>(io.updates[1]) == 0);
        CHECK(std::get<2>(io.updates[1]) == 2);
    }
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        std::vector<uint8_t> bytes(256, 5);
        c.updateClipBytes(5, 0, bytes, 1); // OUTRO drums, track idle
        CHECK(io.updates.empty());
        CHECK(c.session().scenes[5].clips[0].bytes[0] == 5);
    }

    // 15. createClip writes a silent 1-bar clip into an empty cell only.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        CHECK(!c.session().scenes[0].hasClip[1]); // INTRO has no BASS clip
        c.createClip(0, 1);
        CHECK(c.session().scenes[0].hasClip[1]);
        const auto& clip = c.session().scenes[0].clips[1];
        CHECK(clip.bars == 1);
        CHECK(clip.bytes.size() == 48); // BL1: 16 * 3
        for (size_t i = 2; i < clip.bytes.size(); i += 3) CHECK(clip.bytes[i] == 1); // neutral oct
        c.createClip(0, 1); // no-op: cell no longer empty
        CHECK(c.session().scenes[0].clips[1].bars == 1);
    }

    // 16. Library loads replace/create only the target cell, preserve the
    //     track patch, reject incompatible machines, hot-update a live or
    //     pending target, and optionally transpose note payloads.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        const auto beforePatch = c.session().tracks[1].patch;
        const auto beforeDrums = c.session().scenes[0].clips[0];
        const auto bassIt = std::find_if(factoryClipLibrary().begin(), factoryClipLibrary().end(),
                                         [](const auto& clip) { return clip.machine == Machine::BL1; });
        CHECK(bassIt != factoryClipLibrary().end());
        const auto& bassEntry = *bassIt;
        CHECK(!c.session().scenes[0].hasClip[1]);
        CHECK(c.loadLibraryClip(0, 1, bassEntry));
        CHECK(c.session().scenes[0].hasClip[1]);
        CHECK(c.session().scenes[0].clips[1].name == bassEntry.name);
        CHECK(c.session().scenes[0].clips[1].bytes == bassEntry.bytes);
        CHECK(c.session().tracks[1].patch.factory == beforePatch.factory);
        CHECK(c.session().tracks[1].patch.index == beforePatch.index);
        CHECK(c.session().tracks[1].patch.params == beforePatch.params);
        CHECK(c.session().scenes[0].clips[0].bytes == beforeDrums.bytes);
        CHECK(io.updates.empty()); // newly-created idle cell: no engine traffic

        const auto saved = c.session().scenes[0].clips[1];
        CHECK(!c.loadLibraryClip(0, 1, factoryClipLibrary()[0])); // DR1 -> BL1
        CHECK(c.session().scenes[0].clips[1].bytes == saved.bytes);

        c.launch(1, 0); // pending target
        CHECK(c.loadLibraryClip(0, 1, *std::next(bassIt)));
        CHECK(io.updates.size() == 1);
        CHECK(std::get<0>(io.updates.back()) == 1);
        c.onClipStart(1, 0); // live target
        CHECK(c.loadLibraryClip(0, 1, bassEntry));
        CHECK(io.updates.size() == 2);
    }
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        const auto noteIt = std::find_if(factoryClipLibrary().begin(), factoryClipLibrary().end(),
                                         [](const auto& clip) { return clip.machine == Machine::WT1; });
        CHECK(noteIt != factoryClipLibrary().end());
        ClipLibraryEntry note = *noteIt;
        note.bytes = sqEmptyClip(Machine::WT1, 1);
        note.bytes[(size_t)sqNoteIdx(0, 0)] = 1 | (1 << 2);
        note.bytes[(size_t)sqNoteIdx(0, 0) + 1] = 3;
        note.bytes[(size_t)sqNoteIdx(0, 0) + 2] = 1;
        CHECK(c.loadLibraryClip(0, 2, note, 5)); // empty INTRO lead cell
        const auto& loaded = c.session().scenes[0].clips[2];
        CHECK(loaded.bytes[(size_t)sqNoteIdx(0, 0) + 1] == 8);
        CHECK(loaded.bytes[(size_t)sqNoteIdx(0, 0) + 2] == 1);

        note.bytes[(size_t)sqNoteIdx(0, 0) + 1] = 11;
        note.bytes[(size_t)sqNoteIdx(0, 0) + 2] = 2; // +23; +1 folds to +12
        CHECK(c.loadLibraryClip(0, 2, note, 1));
        const auto folded = c.session().scenes[0].clips[2].bytes;
        CHECK(folded[(size_t)sqNoteIdx(0, 0) + 1] == 0);
        CHECK(folded[(size_t)sqNoteIdx(0, 0) + 2] == 2);
        note.transpose = false;
        CHECK(!c.loadLibraryClip(0, 2, note, -1));
        CHECK(c.session().scenes[0].clips[2].bytes == folded);
    }

    // 17. cycleQuant wraps 1 BAR -> 1/4 -> OFF -> 1 BAR in both directions.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        CHECK(c.quant() == Quant::Bar);
        c.cycleQuant(1);
        CHECK(c.quant() == Quant::Quarter);
        c.cycleQuant(1);
        CHECK(c.quant() == Quant::Off);
        c.cycleQuant(1);
        CHECK(c.quant() == Quant::Bar);
        c.cycleQuant(-1);
        CHECK(c.quant() == Quant::Off);
    }
}

// Editing verbs (concept decisions 1-3): delete/paste/move on the Conductor,
// following the loadLibraryClip mutate-then-emit pattern.
static void testConductorEditVerbs() {
    using namespace fable;

    // deleteClip stops-and-clears an owned cell: immediate stop emitted, cell
    // nulled, owner cleared on the Stop ack.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(0, 2); // DROP A drums
        c.onClipStart(0, 2);
        io.stops.clear();
        CHECK(c.deleteClip(2, 0));
        CHECK(!c.session().scenes[2].hasClip[0]);
        CHECK(c.session().scenes[2].clips[0].bytes.empty());
        CHECK(io.stops.size() == 1);
        CHECK(io.stops[0].first == 0 && io.stops[0].second == 0.0); // immediate
        CHECK(c.queueOf(0) == SQ_STOP);
        c.onClipStop(0);
        CHECK(c.ownerOf(0) == -2 && c.queueOf(0) == -2);
        // deleting an already-empty cell is a no-op
        CHECK(!c.deleteClip(2, 0));
        CHECK(io.stops.size() == 1);
        // out-of-range cells are rejected
        CHECK(!c.deleteClip(-1, 0));
        CHECK(!c.deleteClip(0, 99));
    }
    // deleteClip disarms a queued (pending, never started) cell too; deleting
    // a cell the track neither owns nor queues emits no stop.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(0, 2); // queued only, no ack yet
        io.stops.clear();
        CHECK(c.deleteClip(2, 0));
        CHECK(io.stops.size() == 1);
        CHECK(c.queueOf(0) == SQ_STOP);
        CHECK(c.deleteClip(3, 0)); // DROP B drums: idle cell
        CHECK(io.stops.size() == 1); // no extra stop
    }
    // pasteClip: machine-compat gate (byte length vs the target track's
    // machine), overwrite allowed, live target hot-updated in place.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        const auto before = c.session().scenes[0].clips[0];
        ClipData bass { "BASSLINE", 1, sqEmptyClip(Machine::BL1, 1) };
        CHECK(!c.pasteClip(0, 0, bass)); // BL1 payload onto the DR1 track
        CHECK(c.session().scenes[0].clips[0].bytes == before.bytes); // untouched
        ClipData drums { "PASTED", 2, sqEmptyClip(Machine::DR1, 2) };
        drums.bytes[(size_t)sqDr1Idx(0, 0, 0)] = 2;
        CHECK(c.pasteClip(0, 0, drums)); // overwrite the INTRO drum clip
        CHECK(c.session().scenes[0].clips[0].name == "PASTED");
        CHECK(c.session().scenes[0].clips[0].bars == 2);
        CHECK(io.updates.empty()); // idle target: no engine traffic
        CHECK(c.pasteClip(4, 0, drums)); // create in the empty BREAK cell
        CHECK(c.session().scenes[4].hasClip[0]);
        c.launch(0, 0);
        c.onClipStart(0, 0); // owner[0] = scene 0
        CHECK(c.pasteClip(0, 0, drums)); // live target hot-updates
        CHECK(io.updates.size() == 1);
        CHECK(std::get<0>(io.updates[0]) == 0 && std::get<2>(io.updates[0]) == 2);
        ClipData bad { "BAD", 0, {} };
        CHECK(!c.pasteClip(0, 0, bad));       // bars out of range
        CHECK(!c.pasteClip(-1, 0, drums));    // bounds
        CHECK(!c.pasteClip(0, 9, drums));
    }
    // moveClip: move clears the source, copy keeps it; cross-machine, onto
    // itself, and empty-source moves are rejected.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        const auto src = c.session().scenes[2].clips[0]; // DROP A drums
        CHECK(!c.session().scenes[4].hasClip[0]);        // BREAK is drumless
        CHECK(c.moveClip(2, 0, 4, 0, /*copy*/ false));
        CHECK(!c.session().scenes[2].hasClip[0]);        // moved away
        CHECK(c.session().scenes[4].hasClip[0]);
        CHECK(c.session().scenes[4].clips[0].bytes == src.bytes);
        CHECK(c.moveClip(4, 0, 2, 0, /*copy*/ true));    // copy back
        CHECK(c.session().scenes[4].hasClip[0]);         // source survives
        CHECK(c.session().scenes[2].hasClip[0]);
        CHECK(c.session().scenes[2].clips[0].bytes == src.bytes);
        const auto bass = c.session().scenes[2].clips[1]; // DROP A bass
        CHECK(!c.moveClip(2, 0, 2, 1, false));           // DR1 -> BL1 track
        CHECK(c.session().scenes[2].hasClip[0]);         // rejected: unchanged
        CHECK(c.session().scenes[2].clips[1].bytes == bass.bytes);
        CHECK(!c.moveClip(2, 0, 2, 0, false));           // onto itself
        CHECK(!c.moveClip(0, 1, 1, 1, false));           // INTRO bass is empty
    }
    // moving the owned clip away stops the track (the delete half) and the landing
    // cell needs its own launch to sound.
    {
        FakeIO io;
        Conductor c(factorySession(), io, 48000);
        c.powerOn();
        c.launch(0, 2);
        c.onClipStart(0, 2);
        io.stops.clear();
        CHECK(c.moveClip(2, 0, 4, 0, false));
        CHECK(io.stops.size() == 1); // the owned source cell was cleared
        CHECK(c.queueOf(0) == SQ_STOP);
    }
}

// Bounded snapshot history (concept decision 2): one snapshot per gesture,
// linear undo/redo, oldest dropped past the limit.
static void testSnapshotHistory() {
    using namespace fable;
    SnapshotHistory<std::string> h(3);
    CHECK(!h.canUndo() && !h.canRedo());
    CHECK(!h.undo("cur").has_value());
    CHECK(!h.redo("cur").has_value());

    h.push("a"); // state was "a", an edit made it "b"
    h.push("b"); // ... then "c"
    auto u = h.undo("c");
    CHECK(u.has_value() && *u == "b");
    CHECK(h.canRedo());
    auto r = h.redo("b");
    CHECK(r.has_value() && *r == "c");
    // a fresh edit invalidates the redo branch
    (void)h.undo("c");
    h.push("b");
    CHECK(!h.canRedo());

    // bounded: pushing past the limit drops the oldest snapshot
    h.clear();
    for (int i = 0; i < 60; i++) h.push(std::to_string(i));
    CHECK(h.undoDepth() == 3);
    auto last = h.undo("cur");
    CHECK(last.has_value() && *last == "59");

    // the substrate default is 50 (editing-concept)
    SnapshotHistory<std::string> h50;
    for (int i = 0; i < 60; i++) h50.push(std::to_string(i));
    CHECK(h50.undoDepth() == 50);
    h50.clear();
    CHECK(!h50.canUndo() && !h50.canRedo());
}

#if FABLE_SQ4_TEST_JUCE
// Clip-clipboard codec (concept decision 3): tagged JSON, per-column machine
// tags, null slots, whole-document rejection of foreign/tampered payloads.
static void testClipClipboardCodec() {
    using namespace fable;
    ClipClipboardData d;
    d.machines = { Machine::DR1, Machine::BL1 };
    ClipData drum { "KICKS", 2, sqEmptyClip(Machine::DR1, 2) };
    drum.bytes[(size_t)sqDr1Idx(0, 0, 0)] = 2;
    drum.bytes[(size_t)sqDr1Idx(0, 1, 4)] = 1;
    d.cells = { { drum, ClipData{} } };
    d.hasCell = { { true, false } };

    const juce::String json = clipClipboardToJson(d);
    CHECK(json.contains("\"fable\""));
    CHECK(json.contains("sq4-clips"));

    ClipClipboardData back;
    CHECK(clipClipboardFromJson(json, back));
    CHECK(back.machines == d.machines);
    CHECK(back.cells.size() == 1 && back.cells[0].size() == 2);
    CHECK(back.hasCell[0][0] && !back.hasCell[0][1]);
    CHECK(back.cells[0][0].name == "KICKS");
    CHECK(back.cells[0][0].bars == 2);
    CHECK(back.cells[0][0].bytes == drum.bytes); // byte-exact round-trip

    // foreign or untagged clipboard text is ignored wholesale
    ClipClipboardData ignore;
    CHECK(!clipClipboardFromJson("plain text", ignore));
    CHECK(!clipClipboardFromJson("{\"fable\":\"other\",\"v\":1}", ignore));
    CHECK(!clipClipboardFromJson("{\"v\":1,\"machines\":[],\"cells\":[]}", ignore));
    // a tampered column machine no longer matches the cell's byte count
    CHECK(!clipClipboardFromJson(json.replace("\"DR1\"", "\"BL1\""), ignore));
}

// Undo substrate wiring (the same shape SeqProcessor::undo uses): snapshot
// the session JSON before the verb, restore by rebuilding a conductor from
// the decoded snapshot. Coarse restore (fresh conductor, tracks stopped) is
// the accepted v1 contract.
static void testUndoRestoresDeletedClip() {
    using namespace fable;
    FakeIO io;
    auto c = std::make_unique<Conductor>(factorySession(), io, 48000);
    c->powerOn();
    SnapshotHistory<juce::String> history(50);

    history.push(sessionToJson(c->session())); // one snapshot, before the verb
    CHECK(c->deleteClip(2, 0));
    CHECK(!c->session().scenes[2].hasClip[0]);

    auto snap = history.undo(sessionToJson(c->session()));
    CHECK(snap.has_value());
    SessionData restored;
    CHECK(sessionFromJson(*snap, restored));
    c = std::make_unique<Conductor>(restored, io, 48000);
    c->powerOn();
    CHECK(c->session().scenes[2].hasClip[0]); // the deleted clip is back
    CHECK(c->session().scenes[2].clips[0].bytes
          == factorySession().scenes[2].clips[0].bytes);

    auto again = history.redo(sessionToJson(c->session()));
    CHECK(again.has_value());
    SessionData redone;
    CHECK(sessionFromJson(*again, redone));
    CHECK(!redone.scenes[2].hasClip[0]); // redo re-deletes
    CHECK(!history.canRedo());
}
#endif // FABLE_SQ4_TEST_JUCE

static void testSessionLibraryMusicality() {
    using namespace fable;
    const auto& library = factorySessionLibrary();
    CHECK(library.size() == 24);

    // Register split: every generated pad sits strictly below every lead note.
    for (size_t p = 1; p < library.size(); ++p) {
        for (const auto& scene : library[p].session.scenes) {
            if (!scene.hasClip[2] || !scene.hasClip[3]) continue;
            const auto pitches = [](const ClipData& clip) {
                std::vector<int> out;
                for (size_t i = 0; i + 2 < clip.bytes.size(); i += SQ_NOTE_STRIDE)
                    if (clip.bytes[i] & 1)
                        out.push_back(((int)clip.bytes[i + 2] - 1) * 12 + (clip.bytes[i + 1] & 0x7f));
                return out;
            };
            const auto lead = pitches(scene.clips[2]);
            const auto pad = pitches(scene.clips[3]);
            CHECK(!lead.empty() && !pad.empty());
            CHECK(*std::min_element(lead.begin(), lead.end()) >= 12);
            CHECK(*std::max_element(lead.begin(), lead.end()) <= 23);
            CHECK(*std::max_element(pad.begin(), pad.end()) <= 11);
            CHECK(*std::min_element(pad.begin(), pad.end()) >= 0);
        }
    }

    // Unique drums: all 24 DROP-A patterns differ; scenes differ within a song.
    std::set<std::vector<uint8_t>> dropDrums;
    for (const auto& preset : library)
        dropDrums.insert(preset.session.scenes[2].clips[0].bytes);
    CHECK(dropDrums.size() == library.size());
    for (size_t p = 1; p < library.size(); ++p) {
        const auto& scenes = library[p].session.scenes;
        CHECK(!scenes[4].hasClip[0]); // BREAK stays drumless
        std::set<std::vector<uint8_t>> perScene;
        for (size_t s : { (size_t)0, (size_t)1, (size_t)2, (size_t)3, (size_t)5 })
            perScene.insert(scenes[s].clips[0].bytes);
        CHECK(perScene.size() == 5);
    }
}

int main() {
    testClipLibrarySchema();
    testProtocol();
    testModelAndFactory();
    testClipHost();
    testHostEventLossless();
    testWt1Hosted();
    testWt1ClipSwapGatesOldNote();
    testWt1HostedIndependentChordDurations();
    testBl1Hosted();
    testBl1ClipSwapGatesOldNote();
    testDr1Hosted();
    testConductor();
    testConductorEditVerbs();
    testSnapshotHistory();
#if FABLE_SQ4_TEST_JUCE
    testClipClipboardCodec();
    testUndoRestoresDeletedClip();
#endif
    testSessionLibraryMusicality();
    if (failures) { std::printf("%d FAILURES\n", failures); return 1; }
    std::printf("ALL PASS\n");
    return 0;
}
