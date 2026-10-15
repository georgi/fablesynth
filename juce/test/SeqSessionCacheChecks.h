#pragma once
#include "../source/seq/SeqProcessor.h"
#include "../source/seq/SessionCodec.h"
#include "../source/seq/dsp/SeqFactory.h"
#include "../source/seq/dsp/ClipLibrary.gen.h"
#include <thread>

struct SeqSessionCacheTestAccess {
    static std::uint64_t evaluations(const SeqAudioProcessor& p) {
        const juce::ScopedLock lock(p.sessionPresetCacheLock_);
        return p.presetMatchEvaluations_;
    }
    static void poll(SeqAudioProcessor& p) { p.pollSessionParams(); }
};

inline bool runSeqSessionCacheChecks() {
    SeqAudioProcessor p;
    p.prepareToPlay(48000, 128);
    p.applySessionPreset(0);
    if (p.currentSessionPreset() != 0) return false;
    using Access = SeqSessionCacheTestAccess;
    auto oracle = [&] {
        const auto json = fable::sessionToJson(p.conductor().session());
        const auto& library = fable::factorySessionLibrary();
        for (int i = 0; i < (int)library.size(); ++i)
            if (json == fable::sessionToJson(library[(size_t)i].session)) return i;
        return -1;
    };
    auto cached = [&] {
        const auto before = Access::evaluations(p);
        const int expected = oracle();
        for (int i = 0; i < 20; ++i) if (p.currentSessionPreset() != expected) return false;
        return Access::evaluations(p) == before;
    };
    if (!cached()) return false;
    auto edit = [&](auto mutation) {
        const auto revision = p.sessionEditRevision(), before = Access::evaluations(p);
        mutation();
        if (p.sessionEditRevision() == revision || p.currentSessionPreset() != oracle()
            || Access::evaluations(p) != before + 1) return false;
        return cached();
    };
    if (!edit([&] { p.conductor().togglePassThrough(0, 0); })) return false;
    if (!edit([&] { p.conductor().cycleQuant(1); })) return false;
    if (!edit([&] { p.conductor().setTrackVol(1, .321f); })) return false;
    if (!edit([&] { p.conductor().setSwing(.123); })) return false;
    if (!edit([&] { p.conductor().setBpm(123); })) return false;
    if (!edit([&] { p.setTrackFactoryPatch(1, 1); })) return false;
    if (!edit([&] { p.setTrackInlineParams(1, p.trackParameterValues(1)); })) return false;
    auto clip = fable::ClipData { "CACHE", 1, fable::sqEmptyClip(fable::Machine::BL1, 1) };
    if (!edit([&] { p.conductor().pasteClip(0, 1, clip); })) return false;
    if (!edit([&] { clip.bytes[0] = 1; p.conductor().updateClipBytes(0, 1, clip.bytes, 1); })) return false;
    if (!edit([&] { p.conductor().updateClipArp(0, 1, fable::ArpSettings{}); })) return false;
    if (!edit([&] { p.conductor().updateClipAutomation(0, 1, {}); })) return false;
    if (!edit([&] { p.conductor().moveClip(0, 1, 1, 1, true); })) return false;
    if (!edit([&] { p.conductor().deleteClip(1, 1); })) return false;
    if (!edit([&] { p.conductor().createClip(1, 1); })) return false;
    auto drum = fable::ClipData { "DRUM", 1, fable::sqEmptyClip(fable::Machine::DR1, 1) };
    if (!edit([&] { p.conductor().pasteClip(0, 0, drum); })) return false;
    if (!edit([&] { drum.name = "EDITED"; p.conductor().updateDrumClip(0, drum); })) return false;
    const auto& clips = fable::factoryClipLibrary();
    for (const auto& entry : clips) if (entry.machine == fable::Machine::BL1) {
        if (!edit([&] { p.loadClipLibraryEntry(0, 1, entry); })) return false;
        break;
    }
    // Host automation reaches the document through the message-thread poll.
    if (!edit([&] {
        auto* parameter = p.apvts.getParameter("vol1");
        parameter->setValueNotifyingHost(parameter->convertTo0to1(.456f));
        Access::poll(p);
    })) return false;
    p.pushUndoSnapshot();
    if (!edit([&] { p.conductor().togglePassThrough(0, 2); })) return false;
    if (!edit([&] { p.undo(); })) return false;
    if (!edit([&] { p.redo(); })) return false;
    // Wholesale rename/reorder/state restore reset conductor-local revisions.
    auto session = p.conductor().session();
    session.name = "RENAMED";
    session.scenes[0].name = "RENAMED SCENE";
    std::swap(session.scenes[0], session.scenes[1]);
    if (!edit([&] { p.applySessionJson(fable::sessionToJson(session)); })) return false;
    juce::MemoryBlock state;
    p.getStateInformation(state);
    if (!edit([&] { p.applySessionPreset(1); })) return false;
    if (!edit([&] { p.setStateInformation(state.getData(), (int)state.getSize()); })) return false;
    if (!edit([&] { p.prepareToPlay(48000, 128); })) return false;
    // Rejected edits and unchanged surface polls must not invalidate.
    const auto unchanged = p.sessionEditRevision();
    if (p.conductor().deleteClip(-1, 0) || p.applySessionJson("invalid")) return false;
    Access::poll(p);
    if (p.sessionEditRevision() != unchanged || !cached()) return false;
    // Stationary document: host program queries and UI getters may overlap,
    // including the first cache miss after an edit. Only one reader serializes.
    p.conductor().cycleQuant(1);
    const int concurrentPreset = oracle();
    const auto concurrentBefore = Access::evaluations(p);
    std::atomic<bool> concurrentOk { true };
    std::array<std::thread, 4> readers;
    for (auto& reader : readers) reader = std::thread([&] {
        for (int i = 0; i < 100; ++i) {
            p.sessionEditRevision();
            if (p.currentSessionPreset() != concurrentPreset
                || p.getCurrentProgram() != juce::jmax(0, concurrentPreset))
                concurrentOk.store(false);
        }
    });
    for (auto& reader : readers) reader.join();
    if (!concurrentOk.load() || Access::evaluations(p) != concurrentBefore + 1 || !cached()) return false;
    const auto revision = p.sessionEditRevision(), before = Access::evaluations(p);
    const int playbackPreset = oracle();
    auto& c = p.conductor();
    c.toggleTrackMute(0); c.toggleSolo(1); c.toggleSceneMute(0);
    c.startTransport(); c.launchScene(0);
    juce::AudioBuffer<float> buffer(2, 128);
    for (int i = 0; i < 20; ++i) {
        juce::MidiBuffer midi;
        p.processBlock(buffer, midi); p.drainAcks();
        if (p.currentSessionPreset() != playbackPreset) return false;
    }
    c.stopAll(); c.stopTransport();
    return p.sessionEditRevision() == revision && Access::evaluations(p) == before && cached();
}
