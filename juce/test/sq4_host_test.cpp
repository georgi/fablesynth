// SQ-4 plugin-boundary verification: instantiates the real SeqAudioProcessor
// and drives it the way a DAW + the editor timer would — the conductor
// (message thread) issues launches/stops/mutes, the audio thread renders and
// publishes acks, and drainAcks() bridges the two. Covers: silent-but-ticking
// idle, a scene launch that starts all four hosted engines on the shared bar
// grid, per-track mute, combined play/stop transport, stop decay, a full
// session+params state round-trip, and the web-compatible session JSON codec
// (SessionCodec.h), including cross-compat against a real web export
// (test/fixtures/web-session.json — regenerate with
// `npx tsx scripts/dump-session.ts > juce/test/fixtures/web-session.json`).
// Modeled on bass_host_test.cpp.
#include "../source/seq/SeqProcessor.h"
#include "../source/seq/SeqEditor.h"
#include "../source/seq/SessionCodec.h"
#include "../source/seq/ClipLibraryStorage.h"
#include "../source/seq/dsp/SeqFactory.h"
#include "../source/seq/dsp/SeqProtocol.h"
#include "../source/seq/dsp/ClipLibrary.gen.h"
#include "../source/dsp/Presets.h"
#include "../source/ui/DeviceParameterBank.h"
#include "../source/ui/DeviceUiModel.h"
#include "../source/ui/WtUiModel.h"
#include "../source/drum/ui/DrumUiModel.h"
#include "../source/bass/ui/BassUiModel.h"
#include "../source/bass/dsp/BassParams.h"
#include "../source/bass/dsp/BassPatches.h"
#include "../source/drum/dsp/DrumKits.h"
#include "../source/drum/dsp/DrumPatches.h"
#include "../source/dsp/NoteSeq.h"
#include "../source/seq/ui/HostedDrumModel.h"
#include "../source/seq/ui/HostedBassModel.h"
#include "../source/seq/ui/HostedWtModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

static int failures = 0;
static void check(bool c, const char* msg, double val = 0) {
    std::printf("  [%s] %s (%.6g)\n", c ? "PASS" : "FAIL", msg, val);
    if (!c) failures++;
}

// Renders `blocks` blocks of 128 through the processor and returns the overall
// output RMS. Asserts every rendered sample is finite (check 3).
static double renderRms(SeqAudioProcessor& p, juce::AudioBuffer<float>& buf, int blocks) {
    double sumSq = 0;
    long cnt = 0;
    const int n = buf.getNumSamples();
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        p.processBlock(buf, midi);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
            const float* d = buf.getReadPointer(ch);
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(d[i])) { failures++; }
                sumSq += (double)d[i] * d[i];
            }
        }
        cnt += (long)buf.getNumChannels() * n;
    }
    return std::sqrt(sumSq / (double)std::max(1L, cnt));
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui; // message manager for the processor

    std::printf("\n== SQ-4 plugin-boundary test (SeqAudioProcessor) ==\n");

    // Native USER/IMPORTED persistence and portable .sqclip validation.
    {
        const auto root = juce::File("/private/tmp")
            .getChildFile("sq4-clip-library-test-" + juce::Uuid().toString());
        fable::ClipLibraryStorage storage(root);
        auto user = fable::factoryClipLibrary().front();
        user.id = "user-native-test"; user.name = "NATIVE USER TEST";
        juce::String error;
        check(storage.addUser(user, &error), "native USER clip persists", error.length());
        fable::ClipLibraryStorage restored(root);
        check(restored.users().size() == 1 && restored.users()[0].bytes == user.bytes,
              "native USER clip reloads from application storage");
        auto imported = fable::factoryClipLibrary()[1];
        imported.id = "imported-native-test"; imported.name = "NATIVE IMPORT TEST";
        check(restored.importSqclip(fable::ClipLibraryStorage::encodeSqclip({ imported }), &error),
              "validated .sqclip import persists as IMPORTED");
        fable::ClipLibraryStorage importedAgain(root);
        check(importedAgain.imported().size() == 1,
              "native IMPORTED clip reloads from application storage");
        auto malformed = fable::ClipLibraryStorage::encodeSqclip({ imported })
            .replace("\"bars\": 2", "\"bars\": \"2\"");
        check(!importedAgain.importSqclip(malformed, &error),
              "native .sqclip rejects coerced field types");
        root.deleteRecursively();
    }

    // ---- shared hosted parameter backing ---------------------------------
    // A bank uses canonical descriptors, exposes the same parameter gestures
    // as APVTS-backed controls, and defers real work through its dirty flag.
    {
        const auto& catalog = fable::bassParamInfo();
        fui::DeviceParameterBank bank(catalog.data(), catalog.size());
        auto source = bank.source();
        auto* cutoff = source.parameter("flt.cut");
        check(cutoff != nullptr, "hosted parameter source resolves canonical ids");
        check(source.info("flt.cut") != nullptr, "hosted parameter source resolves metadata");
        check(!bank.consumeDirty(), "fresh hosted parameter bank is clean");
        cutoff->setValueNotifyingHost(0.25f);
        check(bank.consumeDirty(), "parameter gesture marks hosted bank dirty");
        check(!bank.consumeDirty(), "dirty flag is consumable");

        std::unordered_map<std::string, float> values {{"flt.cut", 4321.0f}};
        bank.load(values);
        auto snap = bank.snapshot();
        check(std::abs(snap["flt.cut"] - 4321.0f) < 1.0f,
              "hosted parameter bank loads and snapshots real values", snap["flt.cut"]);
        check(!bank.consumeDirty(), "programmatic hosted-bank load finishes clean");
    }

    SeqAudioProcessor p;
    p.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buf(2, 128);

    // ---- native hosted device models ------------------------------------
    {
        SeqAudioProcessor hosted;
        hosted.prepareToPlay(48000.0, 128);
        juce::AudioBuffer<float> hostedBuf(2, 128);
        fui::HostedDrumModel drum(hosted);
        fui::HostedBassModel bass(hosted);
        fui::HostedWtModel wt2(hosted, 2), wt3(hosted, 3);
        drum.setTargetScene(2);
        bass.setTargetScene(2);
        wt2.setTarget(2);
        wt3.setTarget(2);

        check(drum.capabilities().hosted && !drum.capabilities().ownsTransport,
              "DR-1 model advertises hosted transport semantics");
        check(bass.capabilities().hosted && bass.capabilities().supportsPatternChain,
              "BL-1 model exposes hosted sequence length controls");
        check(wt2.capabilities().hosted && !wt2.capabilities().supportsUserTables,
              "WT-1 model disables hosted user-table mutation");

        const int oldBassBars = bass.clipBars();
        const int resizedBassBars = oldBassBars == fable::SQ_HOSTED_MAX_BARS ? oldBassBars - 1 : oldBassBars + 1;
        bass.setChain(std::vector<int>((size_t)resizedBassBars, 0));
        check(bass.clipBars() == resizedBassBars,
              "SQ-4 hosted length control resizes the focused clip", bass.clipBars());
        bass.setChain(std::vector<int>((size_t)oldBassBars, 0));

        const auto before0 = hosted.debugTrackParams(0);
        const auto before2 = hosted.debugTrackParams(2);
        auto* cutoff = bass.parameters().parameter("flt.cut");
        cutoff->setValueNotifyingHost(cutoff->convertTo0to1(6789.0f));
        bass.flushPendingPatch();
        renderRms(hosted, hostedBuf, 1); // drain the Patch command on the audio thread
        const auto after1 = hosted.debugTrackParams(1);
        check(std::abs(after1[(size_t)fable::BL_FLT_CUT] - 6789.0f) < 2.0f,
              "BL-1 hosted knob edit reaches its engine through the patch FIFO",
              after1[(size_t)fable::BL_FLT_CUT]);
        check(hosted.debugTrackParams(0) == before0 && hosted.debugTrackParams(2) == before2,
              "hosted parameter edit is isolated to the focused track");

        const uint8_t oldDrum = drum.step(0, 0, 0);
        drum.setStep(0, 0, 0, oldDrum == 2 ? 1 : 2);
        check(drum.step(0, 0, 0) != oldDrum,
              "DR-1 native sequencer writes the selected SQ clip bytes");

        auto bassStep = bass.sequenceStep(0, 0);
        bassStep.on = !bassStep.on; bassStep.note = 7; bassStep.acc = true;
        bass.setSequenceStep(0, 0, bassStep);
        const auto bassRead = bass.sequenceStep(0, 0);
        check(bassRead.on == bassStep.on && bassRead.note == 7 && bassRead.acc,
              "BL-1 native sequencer round-trips a hosted clip step");

        auto wtStep = wt2.sequenceStep(0, 1);
        wtStep.on = true; wtStep.note = 9; wtStep.oct = 1; wtStep.duration = 4;
        wt2.setSequenceStep(0, 1, wtStep);
        const auto wtRead = wt2.sequenceStep(0, 1);
        check(wtRead.on && wtRead.note == 9 && wtRead.oct == 1 && wtRead.duration == 4,
              "WT-1 native sequencer round-trips a hosted clip step");
        check(wt3.sequenceStep(0, 1).note != 9 || wt3.sequenceStep(0, 1).duration != 4,
              "WT-1 clip edit remains isolated from the second WT track");

        wt2.auditionNoteOn(60, 0.8f);
        renderRms(hosted, hostedBuf, 2);
        wt2.auditionNoteOff(60);
        renderRms(hosted, hostedBuf, 1);
        check(hosted.wtVoiceCount(2) >= 0, "WT-1 audition commands cross the audio FIFO safely");
    }

    // ---- clip-library processor boundary --------------------------------
    // Loading is a session mutation, so the existing session/state codecs
    // must persist it without adding library-specific project state.
    {
        SeqAudioProcessor libraryHost;
        libraryHost.prepareToPlay(48000.0, 128);
        const auto patchBefore = libraryHost.conductor().session().tracks[1].patch;
        const auto neighbourBefore = libraryHost.conductor().session().scenes[0].clips[0].bytes;
        check(!libraryHost.conductor().session().scenes[0].hasClip[1],
              "library target starts as an empty focused cell");
        check(libraryHost.loadFactoryClip(0, 1, 8),
              "factory clip API creates a compatible empty cell");
        const auto& loaded = libraryHost.conductor().session().scenes[0].clips[1];
        check(loaded.name == "ACID CRAWL" && loaded.bytes.size() == 48,
              "library load applies entry name, bars, and bytes");
        const auto& patchAfter = libraryHost.conductor().session().tracks[1].patch;
        check(patchAfter.factory == patchBefore.factory && patchAfter.index == patchBefore.index
                  && patchAfter.params == patchBefore.params,
              "library load preserves the focused track patch");
        check(libraryHost.conductor().session().scenes[0].clips[0].bytes == neighbourBefore,
              "library load leaves neighbouring cells unchanged");
        check(!libraryHost.loadFactoryClip(0, 1, 0),
              "processor API rejects an incompatible factory clip machine");

        fable::SessionData jsonRoundTrip;
        check(fable::sessionFromJson(libraryHost.currentSessionJson(), jsonRoundTrip)
                  && jsonRoundTrip.scenes[0].hasClip[1]
                  && jsonRoundTrip.scenes[0].clips[1].name == "ACID CRAWL",
              "loaded library clip persists through session JSON");
        juce::MemoryBlock libraryState;
        libraryHost.getStateInformation(libraryState);
        SeqAudioProcessor restored;
        restored.prepareToPlay(48000.0, 128);
        restored.setStateInformation(libraryState.getData(), (int)libraryState.getSize());
        check(restored.conductor().session().scenes[0].hasClip[1]
                  && restored.conductor().session().scenes[0].clips[1].bytes == loaded.bytes,
              "loaded library clip persists through plugin state");
    }

    // ---- 1. Silent and transport-stopped before any launch, while the audio
    //        engine's shared frame clock keeps running. ----
    check(renderRms(p, buf, 50) < 1e-6, "idle output is silent");
    check(!p.conductor().playing(), "transport starts stopped after power-on");
    check(p.currentFrame.load() >= 50 * 128, "clock advanced while idle",
          p.currentFrame.load());

    // ---- 2. Launch DROP A (scene 2, all four tracks), quant 1 BAR: clips
    //        arm and fire at the next bar boundary, then all tracks are
    //        audible with a live step position. ----
    p.conductor().launchScene(2);
    check(p.conductor().playing(), "scene launch starts stopped transport");
    renderRms(p, buf, 800); // > one bar at 122 bpm / 48k (94,488 frames)
    p.drainAcks();          // the editor timer's job — deliver clipstart acks
    check(p.conductor().ownerOf(0) == 2, "track 0 owned by scene 2",
          p.conductor().ownerOf(0));
    check(p.conductor().ownerOf(3) == 2, "track 3 owned by scene 2",
          p.conductor().ownerOf(3));
    double rms = renderRms(p, buf, 400);
    check(rms > 1e-4, "scene 2 produces audio", rms);
    for (int t = 0; t < 4; ++t)
        check(p.trackRms[t].load() > 1e-6, "track audible", p.trackRms[t].load());
    for (int t = 0; t < 4; ++t)
        check(p.trackStep[t].load() >= 0, "track step position published",
              p.trackStep[t].load());

    // ---- 3. Every sample finite (asserted inside renderRms). ----

    // ---- 4. Mute track 0: its RMS collapses, others keep playing. ----
    p.conductor().toggleTrackMute(0);
    renderRms(p, buf, 400);
    check(p.trackRms[0].load() < 1e-5, "muted track 0 collapses",
          p.trackRms[0].load());
    check(p.trackRms[1].load() > 1e-6, "track 1 keeps playing while 0 muted",
          p.trackRms[1].load());
    p.conductor().toggleTrackMute(0);
    renderRms(p, buf, 200); // let it come back before pausing

    // ---- 5/6. Transport stop is immediate, keeps the engine processing, and
    //          owners clear as the stop acknowledgements arrive. ----
    double f0 = p.currentFrame.load();
    p.conductor().stopTransport();
    check(!p.conductor().playing(), "transport stop enters stopped state");
    renderRms(p, buf, 1200);
    check(p.currentFrame.load() > f0, "audio clock continues while transport is stopped",
          p.currentFrame.load());
    p.drainAcks();
    check(p.conductor().ownerOf(0) == -2, "transport stop clears owner",
          p.conductor().ownerOf(0));
    check(renderRms(p, buf, 400) < 1e-4, "audio decays to silence after transport stop");

    // ---- 7. State round-trip into a fresh processor: an edited clip and the
    //        params survive; garbage state does not crash. This block is the
    //        load-bearing check for the whole rebuild-a-processor-from-state
    //        path (getStateInformation -> setStateInformation -> a fresh
    //        Conductor/engines) — every other setStateInformation scenario
    //        in this file (garbage, layout-guard rejection) only exercises
    //        the reject branch, not a real successful rebuild. ----
    std::printf("\n== state round-trip ==\n");
    auto bytes = fable::sqEmptyClip(fable::Machine::BL1, 1);
    bytes[0] = 1; // step 0 on
    p.conductor().updateClipBytes(1, 1, bytes, 1); // scene 1, track 1 (BASS)
    if (auto* mp = p.apvts.getParameter("master"))
        mp->setValueNotifyingHost(0.5f);

    juce::MemoryBlock state;
    p.getStateInformation(state);
    check(state.getSize() > 0, "state serialises", (double)state.getSize());

    SeqAudioProcessor q;
    q.prepareToPlay(48000.0, 128);
    q.setStateInformation(state.getData(), (int)state.getSize());
    check(q.conductor().session().scenes[1].clips[1].bytes[0] == 1,
          "edited clip byte round-trips",
          q.conductor().session().scenes[1].clips[1].bytes[0]);
    float m2 = q.apvts.getRawParameterValue("master")->load();
    check(std::abs(m2 - 0.5f) < 1e-3f, "master param round-trips", m2);

    q.setStateInformation("garbage", 7); // must not crash
    check(q.getName() == "FableSynth SQ-4", "processor alive after garbage load");

    // ---- 8. Editor: correct logical size, paints without crashing, snapshot PNG. ----
    auto* ed = p.createEditor();
    check(ed != nullptr, "createEditor returns non-null");
    ed->setSize(1460, 920);
    juce::Image img(juce::Image::ARGB, 1460, 920, true);
    { juce::Graphics g(img); ed->paintEntireComponent(g, true); }
    // background pixel is the theme bg, not uninitialized black-with-alpha-0
    check(img.getPixelAt(4, 900).getAlpha() == 255, "editor background pixel opaque",
          img.getPixelAt(4, 900).getAlpha());

    // ---- 9. Header interactions drive the conductor. ----
    std::printf("\n== header ==\n");
    auto* seqEditor = dynamic_cast<SeqEditor*>(ed);
    check(seqEditor != nullptr, "editor is a SeqEditor");
    auto& hdr = seqEditor->header();
    hdr.quantStep(1);
    check(p.conductor().quant() == fable::Quant::Quarter, "quantStep(+1) advances quant",
          (double)(int)p.conductor().quant());
    hdr.quantStep(-1);
    check(p.conductor().quant() == fable::Quant::Bar, "quantStep(-1) returns to 1 BAR",
          (double)(int)p.conductor().quant());
    hdr.playClick();
    check(p.conductor().playing(), "combined transport button starts playback");
    hdr.playClick();
    check(!p.conductor().playing(), "combined transport button stops playback");

    const auto& sessionLibrary = fable::factorySessionLibrary();
    check(sessionLibrary.size() == 24 && p.getNumPrograms() == 24,
          "SQ-4 ships 24 complete session programs");
    std::map<std::string, int> familyCounts;
    std::set<std::string> rigNames;
    bool rigMetadataValid = true, rigProgramsValid = true, completeSessionContent = true;
    for (const auto& preset : sessionLibrary) {
        familyCounts[preset.family]++;
        rigNames.insert(preset.name);
        if (preset.name.empty() || preset.variation.empty() || preset.tags.empty()
            || preset.energy < 1 || preset.energy > 5
            || !fable::validateSession(preset.session).empty()) rigMetadataValid = false;
        const std::array<int, 4> counts {
            (int)fable::factoryKits().size(), (int)fable::bassFactoryPatches().size(),
            (int)fable::factoryPresets().size(), (int)fable::factoryPresets().size()
        };
        for (int t = 0; t < 4; ++t) {
            const auto& patch = preset.session.tracks[(size_t)t].patch;
            if (!patch.factory || patch.index < 0 || patch.index >= counts[(size_t)t])
                rigProgramsValid = false;
        }
        for (int t = 0; t < 4; ++t) {
            bool hasPlayableClip = false;
            for (const auto& scene : preset.session.scenes)
                if (scene.hasClip[(size_t)t] && !scene.clips[(size_t)t].bytes.empty())
                    hasPlayableClip = true;
            if (!hasPlayableClip) completeSessionContent = false;
        }
    }
    bool familiesValid = familyCounts.size() == 6;
    for (const auto& [family, count] : familyCounts)
        if (family.empty() || count != 4) familiesValid = false;
    check(rigMetadataValid, "every SQ-4 library entry is a valid complete session");
    check(familiesValid, "session library has six families with four variations each");
    check(rigProgramsValid, "every session references valid device programs");
    check(completeSessionContent, "every session contains playable clips and device patches");
    // Session names are unique. Four-device rig *combinations* are deliberately
    // not required to be unique: the web source of truth (sessionPresets.ts)
    // ships TAPE DISCO and VHS GARDEN on the same {3,7,15,17} rig, distinguished
    // by tempo/swing/harmony/clips rather than device selection.
    check(rigNames.size() == sessionLibrary.size(),
          "session names are unique");

    const auto clipsBeforeRigPatch = p.conductor().session().scenes[2].clips;
    hdr.selectLibrarySession(1);
    check(hdr.libraryForTest().getSelectedId() == 2,
          "SQ-4 library selects the complete NEON CHASE session");
    const std::array<int, 4> neonChase { 13, 2, 50, 42 };
    bool rigMatches = true;
    for (int t = 0; t < 4; ++t) {
        const auto& patch = p.conductor().session().tracks[(size_t)t].patch;
        if (!patch.factory || patch.index != neonChase[(size_t)t]) rigMatches = false;
    }
    check(rigMatches, "session library updates all four device patches");
    bool sessionChangedClips = false;
    for (int t = 0; t < 4; ++t)
        if (p.conductor().session().scenes[2].clips[(size_t)t].bytes
            != clipsBeforeRigPatch[(size_t)t].bytes) sessionChangedClips = true;
    check(sessionChangedClips, "session recall replaces scene clips as well as patches");
    check(p.conductor().session().name == "NEON CHASE"
              && std::abs(p.conductor().session().bpm - sessionLibrary[1].session.bpm) < 1.0e-9,
          "session recall applies global session metadata and tempo");
    auto customizedBytes = p.conductor().session().scenes[0].clips[0].bytes;
    customizedBytes[0] ^= 1u;
    p.conductor().updateClipBytes(0, 0, customizedBytes,
                                  p.conductor().session().scenes[0].clips[0].bars);
    check(p.currentSessionPreset() == -1,
          "editing a recalled clip marks the complete session as CUSTOM");
    hdr.selectLibrarySession(0);
    check(p.currentSessionPreset() == 0, "NEON TALE restores the full factory session");

    p.conductor().launchScene(1);
    renderRms(p, buf, 800);
    p.drainAcks();
    check(p.conductor().ownerOf(0) != -2, "scene launched before stopAllClick",
          p.conductor().ownerOf(0));
    hdr.playClick();
    renderRms(p, buf, 1200);
    p.drainAcks();
    check(!p.conductor().playing(), "header button enters stopped state");
    check(p.conductor().ownerOf(0) == -2, "header stop clears owned tracks",
          p.conductor().ownerOf(0));

    // ---- 10. Track heads: mute/solo reach the conductor; patch stepper swaps sounds. ----
    std::printf("\n== track heads ==\n");
    auto& heads = seqEditor->heads();
    heads.muteClick(0);
    check(p.conductor().trackMuted(0), "muteClick(0) mutes track 0");
    heads.muteClick(0);
    check(!p.conductor().trackMuted(0), "muteClick(0) again unmutes");
    heads.soloClick(1);
    check(p.conductor().soloed(1), "soloClick(1) solos track 1");
    heads.soloClick(1);
    check(!p.conductor().soloed(1), "soloClick(1) again unsolos");
    heads.patchStep(2, 1); // LEAD: preset 3 -> 4; session doc updated
    check(p.conductor().session().tracks[2].patch.index == 4,
          "patchStep(2, +1) advances LEAD's patch index",
          p.conductor().session().tracks[2].patch.index);

    // The session doc updating isn't enough on its own: applyTrackPatch must
    // read the *conductor's* session (the runtime truth) to compute the Cmd
    // it pushes, not a stale processor-owned copy — otherwise the chip shows
    // preset 4 while the engine keeps sounding preset 3. Drain the FIFO
    // (any processBlock does it) and compare the live engine params against
    // preset 4's, computed the same way SeqProcessor does internally.
    renderRms(p, buf, 1);
    auto expected = fable::applyPreset(fable::factoryPresets()[4]);
    auto live = p.debugTrackParams(2);
    bool matches = live.size() == expected.size();
    if (matches)
        for (size_t i = 0; i < live.size(); ++i)
            if (std::abs(live[i] - expected[i]) > 1e-6f) { matches = false; break; }
    check(matches, "patchStep(2, +1) reaches the engine (not just the session doc)");

    // ---- 11. Grid semantics through real click handlers. ----
    std::printf("\n== scene grid + footer ==\n");
    auto& grid = seqEditor->grid();
    auto& footer = seqEditor->footer();

    grid.cellClick(2, 0);                          // launch DROP A drums
    check(p.conductor().queueOf(0) == 2, "cellClick(2,0) queues scene 2 on track 0",
          p.conductor().queueOf(0));
    renderRms(p, buf, 800); p.drainAcks();
    check(p.conductor().ownerOf(0) == 2, "track 0 becomes owned by scene 2 after render",
          p.conductor().ownerOf(0));

    grid.cellClick(2, 0);                          // click a LIVE cell -> stop
    check(p.conductor().queueOf(0) == fable::SQ_STOP, "cellClick(2,0) on a live cell queues a stop",
          p.conductor().queueOf(0));
    check(grid.cellStopping(2, 0), "live cell exposes distinct STOPPING state");
    grid.cellClick(2, 0);                          // duplicate click is guarded
    check(p.conductor().queueOf(0) == fable::SQ_STOP, "second click keeps the existing queued stop",
          p.conductor().queueOf(0));
    renderRms(p, buf, 800); p.drainAcks();

    grid.cellRightClick(0, 1);                     // INTRO empty BASS cell -> pass-through
    bool pass = false;
    for (int x : p.conductor().session().scenes[0].pass) if (x == 1) pass = true;
    check(pass, "cellRightClick on an empty cell toggles pass-through");

    grid.sceneLaunch(3);                           // DROP B
    check(p.conductor().queueOf(0) == 3 && p.conductor().queueOf(3) == 3,
          "sceneLaunch(3) queues every track of scene 3");
    renderRms(p, buf, 800); p.drainAcks();

    // A live cell's audible gate must track mute/solo, not just scene mute
    // (SceneRow.tsx's isTrackAudible) -- exercise it through the real
    // conductor mute toggle rather than probing pixels.
    check(grid.cellAudible(3, 0), "track 0's live cell reads audible before muting");
    p.conductor().toggleTrackMute(0);
    check(!grid.cellAudible(3, 0), "toggleTrackMute(0) flips the live cell's audible gate off");
    p.conductor().toggleTrackMute(0);
    check(grid.cellAudible(3, 0), "unmuting restores the live cell's audible gate");

    // Re-render the snapshot now that scene 3 is live and the INTRO pass-
    // through toggle is set -- a livelier picture than the idle factory grid.
    { juce::Graphics g(img); ed->paintEntireComponent(g, true); }
    if (argc > 1) {
        juce::File out(argv[1]);
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    footer.trackStopClick(3);
    check(p.conductor().queueOf(3) == fable::SQ_STOP, "footer trackStopClick(3) queues a stop");

    footer.stopAllClick();
    renderRms(p, buf, 1200); p.drainAcks();
    check(p.conductor().ownerOf(0) == -2, "footer stopAllClick clears owners",
          p.conductor().ownerOf(0));

    // ---- 12. Focus mode: enter, edit a live clip, doc + engine hot-swap. ----
    std::printf("\n== focus mode ==\n");
    auto* ed2 = seqEditor;
    auto& focusView = ed2->deviceFocus();

    ed2->enterFocus(0, 2);            // DRUMS, DROP A
    focusView.clipSourceForTest().setSelectedId(2, juce::sendNotificationSync);
    check(ed2->focus() == std::make_pair(0, 2), "enterFocus(0,2) targets scene 2 / track 0");
    check(focusView.activeBody() == fui::DeviceFocusView::ActiveBody::drum,
          "DRUMS focus shows the native DR-1 body");
    check(focusView.patchSelectorForTest().isVisible(),
          "native focus keeps a visible device patch selector");
    check(focusView.clipSelectorForTest().isVisible()
              && focusView.clipSelectorForTest().getNumItems() == 32,
          "clip browser shows only the eight DR-1 factory clips");
    check(focusView.clipTargetForTest().contains("DROP A")
              && focusView.clipTargetForTest().contains("DRUMS"),
          "clip browser identifies its target scene and track");
    check(focusView.clipMetadataForTest().isNotEmpty()
              && focusView.clipActionsForTest().isVisible(),
          "DR-1 clip browser keeps metadata and utilities in one action menu");

    // Applying a per-pad patch makes the enclosing session custom. The SQ-4
    // library synchronizer must not mistake that local edit for an external
    // session recall and reload the old bank over it; likewise the pad-patch
    // strip must retain the selected patch instead of clearing it next tick.
    hdr.selectLibrarySession(0);
    const auto padPatchRevision = focusView.drumModelForTest().patchContextRevision();
    focusView.drumModelForTest().applyFactoryPadPatch(0);
    hdr.syncLibraryForTest();
    const auto expectedPadValues = fable::applyPatchToPad(0, fable::factoryPatches()[0]);
    const auto& expectedPadValue = expectedPadValues.front();
    const auto& expectedPadInfo = fable::drumParamInfo()[(size_t)expectedPadValue.first];
    const auto& inlinePadPatch = p.conductor().session().tracks[0].patch;
    const auto storedPadValue = inlinePadPatch.params.find(expectedPadInfo.pid);
    check(!inlinePadPatch.factory && storedPadValue != inlinePadPatch.params.end()
              && std::abs(storedPadValue->second - expectedPadValue.second) < 1.0e-6f,
          "SQ-4 pad patch remains committed after the session library sync");
    check(focusView.drumModelForTest().patchContextRevision() == padPatchRevision,
          "applying a pad patch does not immediately invalidate its own readout");

    const int drumProgram = focusView.drumModelForTest().currentProgram();
    const int nextDrumProgram = drumProgram < 0 ? 1
        : (drumProgram + 1) % focusView.drumModelForTest().numPrograms();
    focusView.patchSelectorForTest().setSelectedId(nextDrumProgram + 1,
                                                   juce::sendNotificationSync);
    check(p.conductor().session().tracks[0].patch.factory
              && p.conductor().session().tracks[0].patch.index == nextDrumProgram,
          "DR-1 patch selector updates the SQ-4 track patch");
    p.setTrackInlineParams(0, p.trackParameterValues(0));
    focusView.reloadPatchesFromSession();
    check(focusView.drumModelForTest().currentProgram() == -1
              && focusView.patchSelectorForTest().getText() == "CUSTOM",
          "DR-1 inline patch is shown as CUSTOM");
    focusView.patchSelectorForTest().setSelectedId(nextDrumProgram + 1,
                                                   juce::sendNotificationSync);
    grid.cellClick(2, 0);            // launch DROP A drums so the edited clip is live
    renderRms(p, buf, 800); p.drainAcks();
    check(p.conductor().ownerOf(0) == 2, "DROP A drums are live before editing",
          p.conductor().ownerOf(0));

    auto before = p.conductor().session().scenes[2].clips[0].bytes;
    const auto drumValue = focusView.drumModelForTest().step(0, 5, 3);
    focusView.drumModelForTest().setStep(0, 5, 3, drumValue == 2 ? 1 : 2);
    auto after = p.conductor().session().scenes[2].clips[0].bytes;
    check(before != after, "toggleDrumCell mutates the live clip's bytes");
    check(after[(size_t)fable::sqDr1Idx(0, 5, 3)] != before[(size_t)fable::sqDr1Idx(0, 5, 3)],
          "edit lands exactly at sqDr1Idx(0,5,3)");
    // audio keeps running (phase preserved by ClipHost::updateClip — Task 2 case 6)
    check(renderRms(p, buf, 200) > 1e-5, "audio keeps running through the live edit");

    const auto drumPatchBeforeClipLoad = p.conductor().session().tracks[0].patch;
    const auto bassNeighbourBeforeClipLoad = p.conductor().session().scenes[2].clips[1].bytes;
    focusView.clipSelectorForTest().setSelectedId(2, juce::sendNotificationSync); // 808 FLOOR
    check(p.conductor().session().scenes[2].clips[0].name == "808 FLOOR",
          "selecting a library clip immediately replaces the focused live cell");
    check(p.conductor().session().tracks[0].patch.factory == drumPatchBeforeClipLoad.factory
              && p.conductor().session().tracks[0].patch.index == drumPatchBeforeClipLoad.index
              && p.conductor().session().tracks[0].patch.params == drumPatchBeforeClipLoad.params,
          "native clip selection preserves the focused device patch");
    check(p.conductor().session().scenes[2].clips[1].bytes == bassNeighbourBeforeClipLoad,
          "native clip selection leaves neighbouring cells unchanged");
    check(renderRms(p, buf, 200) > 1e-5,
          "native factory selection keeps live playback running through hot-update");

    // focus rules: head switch keeps scene, explicit scene wins, exit remembers.
    ed2->enterFocus(1);
    focusView.clipSourceForTest().setSelectedId(2, juce::sendNotificationSync);
    check(ed2->focus() == std::make_pair(1, 2), "switching heads keeps the scene");

    check(focusView.activeBody() == fui::DeviceFocusView::ActiveBody::bass,
          "head switch replaces DR-1 with the native BL-1 body");
    const int bassProgram = focusView.bassModelForTest().currentProgram();
    const int nextBassProgram = (bassProgram + 1) % focusView.bassModelForTest().numPrograms();
    focusView.patchSelectorForTest().setSelectedId(nextBassProgram + 1,
                                                   juce::sendNotificationSync);
    check(p.conductor().session().tracks[1].patch.factory
              && p.conductor().session().tracks[1].patch.index == nextBassProgram,
          "BL-1 patch selector updates the SQ-4 track patch");
    p.setTrackInlineParams(1, p.trackParameterValues(1));
    focusView.reloadPatchesFromSession();
    check(focusView.bassModelForTest().currentProgram() == -1
              && focusView.patchSelectorForTest().getText() == "CUSTOM",
          "BL-1 inline patch is shown as CUSTOM");
    focusView.patchSelectorForTest().setSelectedId(nextBassProgram + 1,
                                                   juce::sendNotificationSync);

    // BL-1 native sequencer: exact byte encoding at sqNoteIdx. Focus is
    // BASS / DROP A (ACID 303, one bar); edit a rest step with the same model
    // used by PitchSeqView.
    auto noteByte = [&](int off) {
        return p.conductor().session().scenes[2].clips[1].bytes[(size_t)(fable::sqNoteIdx(0, 5) + off)];
    };
    check((noteByte(0) & 1) == 0, "note step 5 starts as a rest", noteByte(0));
    fable::BassSeqStep nativeBass;
    nativeBass.on = true; nativeBass.note = 7; nativeBass.oct = -1;
    nativeBass.acc = true; nativeBass.slide = true;
    focusView.bassModelForTest().setSequenceStep(0, 5, nativeBass);
    // BL-1 packs slide in the NOTE byte (bit 7), duration in flag bits 2-7
    // (BassPatches.h) — the same packed layout the web uses.
    check(noteByte(0) == (1 | 2 | (1 << 2)), "native BL-1 writes on/accent/duration-1 flags", noteByte(0));
    check(noteByte(1) == (7 | 0x80), "native BL-1 writes the pitch lane + slide bit", noteByte(1));
    check(noteByte(2) == 0, "native BL-1 writes octave as oct+1", noteByte(2));

    check(focusView.clipSelectorForTest().getNumItems() == 20,
          "BL-1 focus filters its factory clips without auxiliary load controls");
    const auto bassPatchBeforeClipLoad = p.conductor().session().tracks[1].patch;
    focusView.clipSelectorForTest().setSelectedId(1, juce::sendNotificationSync); // ACID CRAWL
    check(p.conductor().session().scenes[2].clips[1].name == "ACID CRAWL",
          "native note-clip selection applies the factory entry");
    check(p.conductor().session().tracks[1].patch.factory == bassPatchBeforeClipLoad.factory
              && p.conductor().session().tracks[1].patch.index == bassPatchBeforeClipLoad.index
              && p.conductor().session().tracks[1].patch.params == bassPatchBeforeClipLoad.params,
          "native clip selection preserves the BL-1 patch");

    { juce::Graphics g(img); ed->paintEntireComponent(g, true); } // paints the note grid
    if (argc > 3) { // BASS / ACID 303 focus: the 12-lane pitch + OCT/ACC/TIE editor
        juce::File out(argv[3]);
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    // Locked (>4-bar) clip: view-only. Edits and the bars stepper are ignored,
    // the clip keeps its length, and the lock-banner paint path (no chips) runs.
    ed2->enterFocus(3, 4);                 // PADS / BREAK = FOG SWELL, 8 bars
    check(ed2->focus() == std::make_pair(3, 4), "enterFocus(3,4) targets the 8-bar FOG SWELL");
    check(focusView.activeBody() == fui::DeviceFocusView::ActiveBody::wt3,
          "PADS focus shows the second native WT-1 body");
    auto locked0 = p.conductor().session().scenes[4].clips[3].bytes;
    auto lockedStep = focusView.wt3ModelForTest().sequenceStep(0, 0);
    lockedStep.on = !lockedStep.on;
    focusView.wt3ModelForTest().setSequenceStep(0, 0, lockedStep);
    check(p.conductor().session().scenes[4].clips[3].bytes == locked0,
          "native device edits are ignored on an over-four-bar view-only clip");
    check(p.conductor().session().scenes[4].clips[3].bars == 8, "locked clip keeps its 8 bars");
    { juce::Graphics g(img); ed->paintEntireComponent(g, true); } // lock-banner paint, no chips
    if (argc > 4) {
        juce::File out(argv[4]);
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    ed2->enterFocus(1, 2);                 // back to BASS / DROP A for the following checks
    ed2->focusScene(4);
    check(ed2->focus() == std::make_pair(1, 4), "focusScene(4) moves the scene, keeps the track");
    ed2->exitFocus();
    check(ed2->focus() == std::make_pair(-1, -1), "exitFocus returns to session mode");
    ed2->enterFocus(1);
    check(ed2->focus() == std::make_pair(1, 4), "re-entering remembers the last focus scene");

    // create a clip on an empty cell (LEAD / INTRO).
    ed2->enterFocus(2, 0);
    check(ed2->focus() == std::make_pair(2, 0), "enterFocus(2,0) targets the empty LEAD/INTRO cell");
    check(focusView.activeBody() == fui::DeviceFocusView::ActiveBody::wt2,
          "LEAD focus shows the first native WT-1 body");
    const int wtProgram = focusView.wt2ModelForTest().currentProgram();
    const int nextWtProgram = (wtProgram + 1) % focusView.wt2ModelForTest().numPrograms();
    focusView.patchSelectorForTest().setSelectedId(nextWtProgram + 1,
                                                   juce::sendNotificationSync);
    check(p.conductor().session().tracks[2].patch.factory
              && p.conductor().session().tracks[2].patch.index == nextWtProgram,
          "WT-1 patch selector updates the focused SQ-4 track patch");
    check(!p.conductor().session().scenes[0].hasClip[2], "LEAD/INTRO starts empty");
    focusView.wt2ModelForTest().createTargetClip();
    check(p.conductor().session().scenes[0].hasClip[2], "native focus create action writes a clip into the doc");

    // Focus-mode snapshot: the DRUMS / DROP A clip editor over the live scene.
    ed2->enterFocus(0, 2);
    check(focusView.activeBodyComponent() == &focusView.drumBodyForTest(),
          "focus container exposes exactly the active native body");
    ed2->resized();
    { juce::Graphics g(img); ed->paintEntireComponent(g, true); }
    if (argc > 2) {
        juce::File out(argv[2]);
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    ed2->exitFocus();

    delete ed;

    // ---- session JSON codec (web SessionDoc v:1 schema, SessionCodec.h) ----

    {
        fable::SessionData s = fable::factorySession();
        juce::String json = fable::sessionToJson(s);
        fable::SessionData r;
        check(fable::sessionFromJson(json, r), "sessionFromJson parses sessionToJson's own output");
        check(r.name == s.name && r.scenes.size() == 6, "round-tripped name + scene count");
        check(r.scenes[2].clips[0].bytes == s.scenes[2].clips[0].bytes,
              "round-tripped DRUMS/DROP A pattern bytes");
        check(r.tracks[0].color == 0xff4de8ffu, "round-tripped DRUMS track color");

        // schema spot-checks against the literal web format.
        auto v = juce::JSON::parse(json);
        check((int)v.getProperty("v", 0) == 1, "schema: v == 1");
        check(v.getProperty("quant", "").toString() == "1 BAR", "schema: quant is the string \"1 BAR\"");
        check(v.getProperty("scenes", juce::var())[0].getProperty("clips", juce::var())[1].isVoid(),
              "schema: INTRO/BASS is a null clip slot");

        // inline patch (web schema: { kind:"inline", data:{ params:{...} } } --
        // src/seq/devices.ts inlineParams, protocol.ts PatchDoc). Give track 2
        // (LEAD/WT1) an inline patch and assert the JSON nests params under
        // data, round-trips, and that a legacy JUCE-flat doc still loads.
        {
            fable::SessionData s2 = fable::factorySession();
            s2.tracks[2].patch.factory = false;
            s2.tracks[2].patch.params = { {"cutoff", 0.42f}, {"reso", 0.7f} };
            juce::String j2 = fable::sessionToJson(s2);

            // (a) the JSON literally carries data -> params as an object.
            auto pv = juce::JSON::parse(j2);
            auto patch2 = pv.getProperty("tracks", juce::var())[2].getProperty("patch", juce::var());
            check(patch2.getProperty("kind", "").toString() == "inline", "inline patch: kind is \"inline\"");
            auto data2 = patch2.getProperty("data", juce::var());
            check(data2.getDynamicObject() != nullptr, "inline patch: data is an object");
            auto params2 = data2.getProperty("params", juce::var());
            check(params2.getDynamicObject() != nullptr, "inline patch: data.params is a nested object (web schema)");
            check(std::abs((double)params2.getProperty("cutoff", -1.0) - 0.42) < 1e-4,
                  "inline patch: data.params.cutoff is the written value");

            // (b) round-trip restores the params map.
            fable::SessionData r2;
            check(fable::sessionFromJson(j2, r2), "inline patch round-trips");
            check(!r2.tracks[2].patch.factory, "inline patch stays inline on load");
            check(std::abs(r2.tracks[2].patch.params["cutoff"] - 0.42f) < 1e-4f &&
                  std::abs(r2.tracks[2].patch.params["reso"] - 0.7f) < 1e-4f,
                  "inline patch params map round-trips");

            // (c) a legacy JUCE-flat doc (params written directly into data,
            // pre-Finding-2) still parses into the same params map. Build the
            // flat form by flattening data.params -> data in the parsed tree
            // (robust to float formatting, unlike string surgery).
            {
                auto vflat = juce::JSON::parse(j2);
                auto* patchObj = vflat.getProperty("tracks", juce::var())[2]
                                     .getProperty("patch", juce::var()).getDynamicObject();
                check(patchObj != nullptr, "legacy-flat setup: found the inline patch object");
                auto paramsFlat = patchObj->getProperty("data").getProperty("params", juce::var());
                patchObj->setProperty("data", paramsFlat); // data == the flat params object
                juce::String jf = juce::JSON::toString(vflat);
                check(!jf.contains("\"params\""), "legacy-flat doc has no nested params key");

                fable::SessionData rf;
                check(fable::sessionFromJson(jf, rf), "legacy flat inline patch still parses");
                check(std::abs(rf.tracks[2].patch.params["cutoff"] - 0.42f) < 1e-4f &&
                      std::abs(rf.tracks[2].patch.params["reso"] - 0.7f) < 1e-4f,
                      "legacy flat inline patch yields the same params map");
            }
        }

        // rejects bad docs.
        fable::SessionData junk;
        check(!fable::sessionFromJson("{\"v\":2}", junk), "sessionFromJson rejects v != 1");
        check(!fable::sessionFromJson("not json", junk), "sessionFromJson rejects non-JSON");

        // cross-compat: a real web export (dumped via scripts/dump-session.ts,
        // byte-identical to what the web's saveSession persists) loads and
        // matches fable::factorySession() byte-for-byte — proving the two
        // factories AND codecs agree end-to-end.
        juce::File fixture = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile("test/fixtures/web-session.json");
        if (!fixture.existsAsFile())
            fixture = juce::File(__FILE__).getSiblingFile("fixtures").getChildFile("web-session.json");
        check(fixture.existsAsFile(), "web-session.json fixture is present");
        fable::SessionData webSession;
        check(fable::sessionFromJson(fixture.loadFileAsString(), webSession),
              "sessionFromJson parses the web-exported fixture");
        check(webSession.tracks.size() == s.tracks.size() && webSession.scenes.size() == s.scenes.size(),
              "web fixture track/scene counts match fable::factorySession()");
        bool scenesMatch = webSession.scenes.size() == s.scenes.size();
        for (size_t sc = 0; scenesMatch && sc < webSession.scenes.size(); ++sc)
            for (size_t t = 0; t < webSession.scenes[sc].clips.size(); ++t)
                if (webSession.scenes[sc].hasClip[t] != s.scenes[sc].hasClip[t] ||
                    (s.scenes[sc].hasClip[t] &&
                     webSession.scenes[sc].clips[t].bytes != s.scenes[sc].clips[t].bytes))
                    scenesMatch = false;
        check(scenesMatch, "web fixture clip bytes match fable::factorySession() scene-by-scene, track-by-track");
    }

    {
        juce::File fixture = juce::File::getCurrentWorkingDirectory()
                                 .getChildFile("test/fixtures/web-session-presets.json");
        if (!fixture.existsAsFile())
            fixture = juce::File(__FILE__).getSiblingFile("fixtures").getChildFile("web-session-presets.json");
        check(fixture.existsAsFile(), "web-session-presets.json fixture is present");
        const auto parsed = juce::JSON::parse(fixture.loadFileAsString());
        const auto* webPresets = parsed.getArray();
        const auto& native = fable::factorySessionLibrary();
        check(webPresets != nullptr && webPresets->size() == (int)native.size(),
              "fixture carries all 24 presets");
        bool metaMatches = true, clipsMatch = true;
        for (int p = 0; webPresets != nullptr && p < webPresets->size(); ++p) {
            const auto& web = (*webPresets)[p];
            const auto& mine = native[(size_t)p];
            const auto webSession = web.getProperty("session", {});
            if (web.getProperty("name", "").toString() != juce::String(mine.name)
                || web.getProperty("family", "").toString() != juce::String(mine.family)
                || (int)web.getProperty("energy", -1) != mine.energy
                || std::abs((double)webSession.getProperty("bpm", 0.0) - mine.session.bpm) > 1e-9
                || std::abs((double)webSession.getProperty("swing", -1.0) - mine.session.swing) > 1e-9)
                metaMatches = false;
            const auto* tracks = webSession.getProperty("tracks", {}).getArray();
            if (tracks == nullptr || tracks->size() != (int)mine.session.tracks.size())
                metaMatches = false;
            for (int t = 0; tracks != nullptr && t < tracks->size()
                            && t < (int)mine.session.tracks.size(); ++t) {
                const auto& webTrack = (*tracks)[t];
                const auto& nativeTrack = mine.session.tracks[(size_t)t];
                if ((int)webTrack.getProperty("patch", {}).getProperty("index", -1) != nativeTrack.patch.index
                    || std::abs((double)webTrack.getProperty("gain", -1.0) - (double)nativeTrack.gain) > 1e-6)
                    metaMatches = false;
            }
            const auto* scenes = webSession.getProperty("scenes", {}).getArray();
            if (scenes == nullptr || scenes->size() != (int)mine.session.scenes.size()) { clipsMatch = false; continue; }
            for (int s = 0; s < scenes->size(); ++s) {
                const auto* clips = (*scenes)[s].getProperty("clips", {}).getArray();
                for (int t = 0; clips != nullptr && t < clips->size(); ++t) {
                    const auto& webClip = (*clips)[t];
                    const bool webHas = !webClip.isVoid() && webClip.isObject();
                    if (webHas != mine.session.scenes[(size_t)s].hasClip[(size_t)t]) { clipsMatch = false; continue; }
                    if (!webHas) continue;
                    juce::MemoryOutputStream decoded;
                    juce::Base64::convertFromBase64(decoded, webClip.getProperty("pattern", "").toString());
                    const auto& nativeBytes = mine.session.scenes[(size_t)s].clips[(size_t)t].bytes;
                    if (decoded.getDataSize() != nativeBytes.size()
                        || std::memcmp(decoded.getData(), nativeBytes.data(), nativeBytes.size()) != 0)
                        clipsMatch = false;
                    if (webClip.getProperty("name", "").toString()
                            != juce::String(mine.session.scenes[(size_t)s].clips[(size_t)t].name))
                        clipsMatch = false;
                }
            }
        }
        check(metaMatches, "session-preset metadata matches the web generator");
        check(clipsMatch, "all 24 preset sessions match the web generator byte-for-byte");
    }

    // ---- LOAD/SAVE UI test handles (SeqHeader::loadClick/saveClick apply the
    // FileChooser result via applySessionJson/currentSessionJson underneath;
    // tested directly here without opening a real OS dialog) ----

    {
        SeqAudioProcessor p2;
        p2.prepareToPlay(48000.0, 128);
        juce::String json = p2.currentSessionJson();
        check(json.isNotEmpty(), "currentSessionJson returns the live session as JSON");

        fable::SessionData parsed;
        check(fable::sessionFromJson(json, parsed), "currentSessionJson's output round-trips through sessionFromJson");

        juce::String mutated = json.replace("\"NEON TALE\"", "\"RENAMED\"");
        check(p2.applySessionJson(mutated), "applySessionJson accepts a valid mutated doc");
        check(p2.conductor().session().name == "RENAMED", "applySessionJson replaces the live session");

        check(!p2.applySessionJson("not json"), "applySessionJson rejects garbage and returns false");
        check(p2.conductor().session().name == "RENAMED", "a rejected applySessionJson leaves the session untouched");
    }

    // ---- editing undo substrate (pushUndoSnapshot/undo/redo/clearHistory
    // over currentSessionJson/applySessionJson; editing-concept decision 2) ----
    {
        std::printf("\n== editing undo substrate ==\n");
        SeqAudioProcessor p2;
        p2.prepareToPlay(48000.0, 128);
        check(!p2.canUndo() && !p2.canRedo(), "fresh processor has no edit history");
        check(!p2.undo() && !p2.redo(), "undo/redo on an empty history are no-ops");

        // one snapshot per gesture, BEFORE the verb, then delete a clip
        p2.pushUndoSnapshot();
        check(p2.conductor().deleteClip(2, 0), "deleteClip clears the DROP A drum cell");
        check(!p2.conductor().session().scenes[2].hasClip[0], "cell is empty after delete");
        check(p2.canUndo(), "the gesture left one undo entry");

        check(p2.undo(), "undo applies the pre-delete snapshot");
        check(p2.conductor().session().scenes[2].hasClip[0], "undo restores the deleted clip");
        check(p2.conductor().session().scenes[2].clips[0].bytes
                  == fable::factorySession().scenes[2].clips[0].bytes,
              "restored clip is byte-identical to the factory clip");
        check(p2.canRedo(), "undo leaves a redo entry");

        check(p2.redo(), "redo re-applies the delete");
        check(!p2.conductor().session().scenes[2].hasClip[0], "redo re-deletes the clip");
        check(p2.undo(), "undo after redo restores again");

        // an external session load clears the whole history (after the final
        // undo the surviving entry sits on the redo branch)
        check(p2.canRedo(), "history is non-empty before the external load");
        check(p2.applySessionJson(p2.currentSessionJson()), "external load applies");
        check(!p2.canUndo() && !p2.canRedo(), "applySessionJson clears the edit history");
        check(!p2.undo(), "undo after a session load is a no-op");
        renderRms(p2, buf, 4); // must not crash after the rebuilds
    }

    // ---- grid editing UI: selection rectangle, keyboard verbs, clipboard,
    // drag-and-drop drop verb (editing-concept decisions 4-5, 7) ----
    {
        std::printf("\n== grid editing: selection + verbs + dnd ==\n");
        SeqAudioProcessor p6;
        p6.prepareToPlay(48000.0, 128);
        std::unique_ptr<juce::AudioProcessorEditor> ed6(p6.createEditor());
        auto* se = dynamic_cast<SeqEditor*>(ed6.get());
        check(se != nullptr, "grid-editing editor is a SeqEditor");
        auto& g6 = se->grid();
        auto sess = [&]() -> const fable::SessionData& { return p6.conductor().session(); };

        // -- selection rectangle (anchor/head)
        check(!g6.hasSelection(), "grid starts with no selection");
        g6.selectCell(2, 0);
        check(g6.cellSelected(2, 0) && !g6.cellSelected(2, 1), "selectCell anchors a single cell");
        g6.extendSelection(3, 1);
        check(g6.cellSelected(2, 0) && g6.cellSelected(2, 1) && g6.cellSelected(3, 1)
                  && !g6.cellSelected(4, 0),
              "extendSelection spans the anchor/head rectangle");

        // -- keyboard nav routed through SeqEditor::keyPressed (session mode)
        check(se->keyPressed(juce::KeyPress(juce::KeyPress::downKey)),
              "down-arrow is claimed in session mode");
        check(g6.cellSelected(4, 1) && !g6.cellSelected(2, 0),
              "plain arrow collapses the rect and moves from the head");
        check(se->keyPressed(juce::KeyPress(juce::KeyPress::upKey,
                                            juce::ModifierKeys::shiftModifier, 0)),
              "shift-arrow is claimed");
        check(g6.cellSelected(3, 1) && g6.cellSelected(4, 1), "shift-arrow extends the rect");
        check(se->keyPressed(juce::KeyPress('a', juce::ModifierKeys::commandModifier, 'a')),
              "cmd-A is claimed");
        check(g6.cellSelected(0, 0) && g6.cellSelected(5, 3), "cmd-A selects the whole grid");
        check(se->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),
              "escape is claimed while a selection exists");
        check(!g6.hasSelection(), "escape clears the selection");

        // -- copy / paste within the drum column (scene 2 filled, scene 4 empty)
        const auto kitA = sess().scenes[2].clips[0].bytes;
        g6.selectCell(2, 0);
        check(g6.selCopy(), "selCopy serialises the selected drum cell");
        check(!sess().scenes[4].hasClip[0], "BREAK/DRUMS starts empty");
        g6.selectCell(4, 0);
        check(g6.selPaste(), "selPaste fills the empty same-machine cell");
        check(sess().scenes[4].hasClip[0] && sess().scenes[4].clips[0].bytes == kitA,
              "pasted clip is byte-identical to the copied one");
        check(p6.canUndo(), "paste pushed exactly one undo snapshot");
        check(se->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')),
              "cmd-Z is claimed");
        check(!sess().scenes[4].hasClip[0], "cmd-Z undoes the paste");
        check(se->keyPressed(juce::KeyPress('z',
                  juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 'z')),
              "shift-cmd-Z is claimed");
        check(sess().scenes[4].hasClip[0], "shift-cmd-Z redoes the paste");
        p6.undo();

        // -- machine-mismatched paste is rejected wholesale
        g6.selectCell(0, 1); // INTRO/BASS: empty BL1 cell, clipboard holds a DR1 payload
        check(!g6.selPaste(), "DR1 payload onto the BL1 column is rejected");
        check(!sess().scenes[0].hasClip[1], "mismatched paste leaves the cell empty");
        check(!p6.canUndo(), "a rejected paste pushes no undo snapshot");

        // -- cut = copy + clear, one snapshot each for cut and the later paste
        g6.selectCell(2, 0);
        check(g6.selCut(), "selCut copies then clears the source");
        check(!sess().scenes[2].hasClip[0], "cut clears DROP A / DRUMS");
        g6.selectCell(4, 0);
        check(g6.selPaste(), "cut payload pastes elsewhere in the column");
        check(sess().scenes[4].clips[0].bytes == kitA, "cut+paste moves the exact bytes");
        p6.undo(); p6.undo();
        check(sess().scenes[2].hasClip[0] && !sess().scenes[4].hasClip[0],
              "two undos restore the pre-cut grid");

        // -- multi-cell delete is ONE gesture / ONE snapshot
        g6.selectCell(2, 0);
        g6.extendSelection(2, 3);
        check(g6.selDelete(), "selDelete clears the filled cells in the rect");
        bool anyLeft = false;
        for (int t = 0; t < 4; ++t) if (sess().scenes[2].hasClip[(size_t)t]) anyLeft = true;
        check(!anyLeft, "all four DROP A cells cleared");
        check(p6.undo() && sess().scenes[2].hasClip[0] && sess().scenes[2].hasClip[3],
              "a single undo restores the whole multi-cell delete");

        // -- duplicate = paste one scene down (DROP A row -> DROP B row)
        const auto dropB0 = sess().scenes[3].clips[0].bytes;
        g6.selectCell(2, 0);
        g6.extendSelection(2, 3);
        check(g6.selDuplicate(), "selDuplicate pastes one scene down");
        check(sess().scenes[3].clips[0].bytes == sess().scenes[2].clips[0].bytes,
              "DROP B now carries DROP A's drums");
        check(p6.undo(), "duplicate is one undo step");
        check(sess().scenes[3].clips[0].bytes == dropB0, "undo restores DROP B's own drums");

        // -- multi-cell rect copy/paste anchored top-left, per-column machines
        g6.selectCell(2, 0);
        g6.extendSelection(2, 1); // DRUMS + BASS of DROP A
        check(g6.selCopy(), "rect copy captures a two-machine rectangle");
        g6.selectCell(4, 0);      // BREAK: DRUMS empty, BASS filled (overwrite allowed)
        check(g6.selPaste(), "rect paste lands anchored at the selection top-left");
        check(sess().scenes[4].clips[0].bytes == sess().scenes[2].clips[0].bytes
                  && sess().scenes[4].clips[1].bytes == sess().scenes[2].clips[1].bytes,
              "both columns pasted into their matching machines");
        p6.undo();
        g6.selectCell(4, 1);      // shifted one column: DR1->BL1, BL1->WT1 — all skip
        check(!g6.selPaste(), "a wholly machine-mismatched rect paste is rejected");

        // -- drag-and-drop drop verb (block move/copy)
        g6.clearSelection();
        check(g6.dropCells(2, 0, 4, 0, false), "dropCells moves the grabbed cell");
        check(!sess().scenes[2].hasClip[0], "move clears the source");
        check(sess().scenes[4].clips[0].bytes == kitA, "move lands the exact bytes");
        check(g6.cellSelected(4, 0), "selection follows the dropped block");
        check(p6.undo(), "a drop is one undo step");
        g6.clearSelection();
        check(g6.dropCells(2, 0, 4, 0, true), "dropCells copies with copy=true (Alt)");
        check(sess().scenes[2].hasClip[0] && sess().scenes[4].clips[0].bytes == kitA,
              "copy keeps the source and fills the target");
        p6.undo();
        check(!g6.dropCells(2, 0, 2, 1, false), "a cross-machine drop target is rejected");
        check(!g6.dropCells(4, 0, 2, 0, false), "dragging an empty cell is rejected");
        check(!g6.dropCells(2, 0, 2, 0, false), "a self-drop is a no-op");

        // -- focus-mode keys keep their existing meanings
        se->enterFocus(0, 2);
        check(se->keyPressed(juce::KeyPress(juce::KeyPress::downKey)),
              "down-arrow still claimed in focus mode");
        check(se->focus() == std::make_pair(0, 3), "focus-mode down-arrow moves the scene, not the selection");
        check(se->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "escape claimed in focus mode");
        check(se->focus() == std::make_pair(-1, -1), "escape still exits focus mode");
    }

    // ---- layout guard: this processor hardcodes a 4-track {DR1,BL1,WT1,WT1}
    // rig; a schema-valid doc that doesn't match it (fewer tracks, or a
    // swapped machine) must be rejected by applySessionJson (and by
    // extension setStateInformation's SESSION path, which funnels through
    // it) rather than reaching the index-based engine routing with a
    // mismatched track. ----
    {
        std::printf("\n== session layout guard ==\n");

        // A 3-track doc: schema-valid (validateSession only checks internal
        // consistency, not track count), but not the fixed rig.
        auto threeTrack = [] {
            fable::SessionData s = fable::factorySession();
            s.tracks.resize(3);
            for (auto& sc : s.scenes) {
                sc.clips.resize(3);
                sc.hasClip.resize(3);
                sc.pass.erase(std::remove_if(sc.pass.begin(), sc.pass.end(),
                                              [](int t) { return t >= 3; }),
                              sc.pass.end());
            }
            return s;
        }();
        check(fable::validateSession(threeTrack).empty(),
              "3-track doc is schema-valid (sanity check on the test fixture itself)");

        // A 4-track doc with track 0 swapped DR1 -> BL1 (byte-matched to the
        // new machine so it stays schema-valid).
        auto swappedMachine = [] {
            fable::SessionData s = fable::factorySession();
            s.tracks[0].machine = fable::Machine::BL1;
            for (auto& sc : s.scenes)
                if (sc.hasClip[0])
                    sc.clips[0].bytes = fable::sqEmptyClip(fable::Machine::BL1, sc.clips[0].bars);
            return s;
        }();
        check(fable::validateSession(swappedMachine).empty(),
              "track-0-swapped doc is schema-valid (sanity check on the test fixture itself)");

        SeqAudioProcessor p3;
        p3.prepareToPlay(48000.0, 128);
        juce::String nameBefore = p3.conductor().session().name;

        check(!p3.applySessionJson(fable::sessionToJson(threeTrack)),
              "applySessionJson rejects a schema-valid 3-track doc");
        check(p3.conductor().session().name == nameBefore,
              "rejected 3-track doc leaves the current session in place");
        renderRms(p3, buf, 4); // must not crash

        check(!p3.applySessionJson(fable::sessionToJson(swappedMachine)),
              "applySessionJson rejects a schema-valid doc with track 0 = BL1");
        check(p3.conductor().session().name == nameBefore,
              "rejected swapped-machine doc leaves the current session in place");
        renderRms(p3, buf, 4); // must not crash

        // Same rejection, through setStateInformation's SESSION-doc path (the
        // DAW state-load entry point, distinct from the LOAD button's direct
        // applySessionJson call) -- build a real state blob, then swap only
        // its embedded session JSON for the layout-violating doc.
        juce::MemoryBlock validState;
        p3.getStateInformation(validState);
        auto xml = juce::AudioProcessor::getXmlFromBinary(validState.getData(), (int)validState.getSize());
        check(xml != nullptr, "processor state serialises to XML for the setStateInformation test");
        auto root = juce::ValueTree::fromXml(*xml);
        auto sess = root.getChildWithName("SESSION");
        check(sess.isValid(), "state XML carries a SESSION child");
        sess.setProperty("doc", fable::sessionToJson(threeTrack), nullptr);
        auto badXml = root.createXml();
        juce::MemoryBlock badState;
        juce::AudioProcessor::copyXmlToBinary(*badXml, badState);

        p3.setStateInformation(badState.getData(), (int)badState.getSize());
        check(p3.conductor().session().name == nameBefore,
              "setStateInformation rejects a layout-violating SESSION doc, current session retained");
        renderRms(p3, buf, 4); // must not crash
        check(p3.getName() == "FableSynth SQ-4", "processor alive after the layout-guard rejection");
    }

    // ---- Finding 3: a session swap invalidates in-flight acks. Launch scene 2
    // (live), then applySessionJson a fresh session while clipstart acks are
    // still queued (not yet drained). After the swap + drain, no track may be
    // owned -- a stale pre-swap Start ack must not reach the new conductor and
    // install a false owner (the acks name scene 2, which onClipStart would make
    // the owner). Draining the FIFO at the swap alone can't close this: the acks
    // were already queued by earlier blocks; the generation tag is what discards
    // them. The generation now advances via an ordered K::Reset command (not a
    // raced cmdGen_.load on the audio thread — Finding 2), so an ack stamped by
    // the old session before the Reset drains keeps the old generation and is
    // dropped. Output must be silent until a new launch. ----
    {
        std::printf("\n== stale-ack invalidation across a session swap ==\n");
        SeqAudioProcessor p4;
        p4.prepareToPlay(48000.0, 128);
        juce::AudioBuffer<float> b4(2, 128);

        p4.conductor().launchScene(2);   // DROP A: all four tracks
        renderRms(p4, b4, 800);          // arm + fire across the bar boundary...
        // ...but DON'T drainAcks: the four clipstart acks sit queued in the FIFO
        // while we swap the session out from under them (a LOAD landing mid-flight).
        juce::String fresh = p4.currentSessionJson();
        check(p4.applySessionJson(fresh), "applySessionJson accepts the fresh session mid-flight");
        p4.drainAcks();                  // the queued pre-swap acks reach the drain here
        // Check immediately, before any render processes the swap's stop
        // commands: at this instant no Stop ack has been produced yet, so a
        // stale Start ack that slipped through would show as a false owner
        // (scene 2) right now. (Without the generation guard this fails: the
        // four gen-0 Start acks flip owner_[t] to the scene they name on the new
        // conductor. The swap's own stops would later scrub it, which is why
        // the guard -- not drain-at-swap -- is what closes the window.)
        bool anyOwned = false;
        for (int t = 0; t < 4; ++t)
            if (p4.conductor().ownerOf(t) != -2) anyOwned = true;
        check(!anyOwned, "no false owner from a stale ack immediately after the swap + drain");

        // Then let the swap's stops land and the engine tails decay: still
        // unowned, and silent until a new launch.
        renderRms(p4, b4, 1200);
        p4.drainAcks();
        bool anyOwned2 = false;
        for (int t = 0; t < 4; ++t)
            if (p4.conductor().ownerOf(t) != -2) anyOwned2 = true;
        check(!anyOwned2, "still unowned after the swap's stops settle");
        check(renderRms(p4, b4, 400) < 1e-4, "silent until a new launch after the swap");
    }

    // ---- Finding 4: an over-large host buffer is processed in prepared-size
    // sub-blocks (no setSize on the audio thread). Prepare at 128, then drive
    // the processor with 1024-sample buffers and a live scene: audio stays
    // finite (asserted in renderRms) and audible, the step readout advances
    // across the run (per-chunk frame accounting + ClipHost catch-up, also
    // exercising Finding 1's swap), and currentFrame advances by the full
    // block. One 1024-sample block is < one step at 122 bpm, so step advance
    // is checked across several blocks. ----
    {
        std::printf("\n== over-large host buffer (chunked render) ==\n");
        SeqAudioProcessor p5;
        p5.prepareToPlay(48000.0, 128);              // prepared block = 128
        p5.conductor().launchScene(2);               // DROP A
        juce::AudioBuffer<float> big(2, 1024);

        renderRms(p5, big, 200);                     // past the bar boundary, 1024-sample blocks
        p5.drainAcks();
        check(p5.conductor().ownerOf(0) == 2, "scene live under 1024-sample blocks (prepared at 128)",
              p5.conductor().ownerOf(0));

        const int stepBefore = p5.trackStep[0].load();
        const double bigRms = renderRms(p5, big, 20); // finite asserted inside; spans ~3.5 steps
        check(bigRms > 1e-5, "1024-sample blocks produce audio (chunked render)", bigRms);
        check(p5.trackStep[0].load() != stepBefore, "step readout advances across 1024-sample blocks",
              p5.trackStep[0].load());

        const double frameBeforeLargeBlock = p5.currentFrame.load();
        renderRms(p5, big, 1);
        check(std::abs(p5.currentFrame.load() - (frameBeforeLargeBlock + 1024)) < 1e-6,
              "currentFrame advances by the full over-large block", p5.currentFrame.load());
    }

    std::printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
