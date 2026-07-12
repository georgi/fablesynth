// SQ-4 plugin-boundary verification: instantiates the real SeqAudioProcessor
// and drives it the way a DAW + the editor timer would — the conductor
// (message thread) issues launches/stops/mutes, the audio thread renders and
// publishes acks, and drainAcks() bridges the two. Covers: silent-but-ticking
// idle, a scene launch that starts all four hosted engines on the shared bar
// grid, per-track mute, pause (frame freeze), stopAll decay, and a full
// session+params state round-trip. Modeled on bass_host_test.cpp.
#include "../source/seq/SeqProcessor.h"
#include "../source/seq/SeqEditor.h"
#include "../source/seq/dsp/SeqProtocol.h"
#include "../source/dsp/Presets.h"

#include <array>
#include <cmath>
#include <cstdio>
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

    SeqAudioProcessor p;
    p.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buf(2, 128);

    // ---- 1. Silent before any launch, but the shared clock runs. ----
    check(renderRms(p, buf, 50) < 1e-6, "idle output is silent");
    check(p.currentFrame.load() >= 50 * 128, "clock advanced while idle",
          p.currentFrame.load());

    // ---- 2. Launch DROP A (scene 2, all four tracks), quant 1 BAR: clips
    //        arm and fire at the next bar boundary, then all tracks are
    //        audible with a live step position. ----
    p.conductor().launchScene(2);
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

    // ---- 5. Pause freezes the frame counter and outputs silence. ----
    p.setPaused(true);
    double f0 = p.currentFrame.load();
    check(renderRms(p, buf, 50) < 1e-6, "paused output is silent");
    check(p.currentFrame.load() == f0, "paused clock is frozen",
          p.currentFrame.load());
    p.setPaused(false);

    // ---- 6. stopAll + acks: owners clear and audio decays to silence. ----
    p.conductor().stopAll();
    renderRms(p, buf, 1200);
    p.drainAcks();
    check(p.conductor().ownerOf(0) == -2, "stopAll clears owner",
          p.conductor().ownerOf(0));
    check(renderRms(p, buf, 400) < 1e-4, "audio decays to silence after stop");

    // ---- 7. State round-trip into a fresh processor: an edited clip and the
    //        params survive; garbage state does not crash. ----
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
    check(p.paused(), "playClick pauses");
    hdr.playClick();
    check(!p.paused(), "playClick again unpauses");

    p.conductor().launchScene(1);
    renderRms(p, buf, 800);
    p.drainAcks();
    check(p.conductor().ownerOf(0) != -2, "scene launched before stopAllClick",
          p.conductor().ownerOf(0));
    hdr.stopAllClick();
    renderRms(p, buf, 1200);
    p.drainAcks();
    check(p.conductor().ownerOf(0) == -2, "stopAllClick schedules stops for owned tracks",
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
    auto& clipEd = ed2->clipEdit();

    ed2->enterFocus(0, 2);            // DRUMS, DROP A
    check(ed2->focus() == std::make_pair(0, 2), "enterFocus(0,2) targets scene 2 / track 0");
    grid.cellClick(2, 0);            // launch DROP A drums so the edited clip is live
    renderRms(p, buf, 800); p.drainAcks();
    check(p.conductor().ownerOf(0) == 2, "DROP A drums are live before editing",
          p.conductor().ownerOf(0));

    auto before = p.conductor().session().scenes[2].clips[0].bytes;
    clipEd.toggleDrumCell(/*pad*/ 5, /*step*/ 3);
    auto after = p.conductor().session().scenes[2].clips[0].bytes;
    check(before != after, "toggleDrumCell mutates the live clip's bytes");
    check(after[(size_t)fable::sqDr1Idx(0, 5, 3)] != before[(size_t)fable::sqDr1Idx(0, 5, 3)],
          "edit lands exactly at sqDr1Idx(0,5,3)");
    // audio keeps running (phase preserved by ClipHost::updateClip — Task 2 case 6)
    check(renderRms(p, buf, 200) > 1e-5, "audio keeps running through the live edit");

    // focus rules: head switch keeps scene, explicit scene wins, exit remembers.
    ed2->enterFocus(1);
    check(ed2->focus() == std::make_pair(1, 2), "switching heads keeps the scene");

    // BL-1 note editor: exact byte encoding at sqNoteIdx + web step hygiene
    // (src/bass/store.ts:112-146). Focus is BASS / DROP A (ACID 303, 1 bar);
    // edit a rest step (step 5) so every transition starts from a clean slate.
    auto noteByte = [&](int off) {
        return p.conductor().session().scenes[2].clips[1].bytes[(size_t)(fable::sqNoteIdx(0, 5) + off)];
    };
    check((noteByte(0) & 1) == 0, "note step 5 starts as a rest", noteByte(0));

    // acc/tie must NO-OP on an off step (store.ts: if (!cur.on) return).
    clipEd.toggleAcc(5);
    check(noteByte(0) == 0, "toggleAcc no-ops on an off step", noteByte(0));
    clipEd.toggleTie(5);
    check(noteByte(0) == 0, "toggleTie no-ops on an off step", noteByte(0));

    clipEd.toggleNoteCell(/*lane*/ 7, /*step*/ 5);
    check((noteByte(0) & 1) && noteByte(1) == 7, "toggleNoteCell sets on + note 7", noteByte(1));

    clipEd.toggleAcc(5);
    check(noteByte(0) == (1 | 2), "toggleAcc sets the accent bit on a live step", noteByte(0));
    clipEd.toggleTie(5);
    check(noteByte(0) == (1 | 2 | 4), "toggleTie sets the tie bit on a live step", noteByte(0));

    clipEd.setOct(5, -1);
    check(noteByte(2) == 0, "setOct(-1) writes oct+1 = 0", noteByte(2));
    clipEd.setOct(5, 1);
    check(noteByte(2) == 2, "setOct(+1) writes oct+1 = 2", noteByte(2));

    // turning the note off clears acc + tie too (web setStep {on,acc,slide}=false).
    clipEd.toggleNoteCell(7, 5);
    check((noteByte(0) & 0x07) == 0, "turning a note off clears on + acc + tie", noteByte(0));

    // re-arm on a different lane: no stale acc/tie survive the on->off->on cycle.
    clipEd.toggleNoteCell(9, 5);
    check(noteByte(0) == 1, "re-armed step has on set and no stale acc/tie", noteByte(0));
    check(noteByte(1) == 9, "re-armed step carries the new note", noteByte(1));

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
    auto locked0 = p.conductor().session().scenes[4].clips[3].bytes;
    clipEd.toggleNoteCell(5, 0);
    clipEd.barsStep(-1);
    check(p.conductor().session().scenes[4].clips[3].bytes == locked0,
          "edits + bars stepper are ignored on a view-only clip");
    check(p.conductor().session().scenes[4].clips[3].bars == 8, "locked clip keeps its 8 bars");
    { juce::Graphics g(img); ed->paintEntireComponent(g, true); } // lock-banner paint, no chips

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
    check(!p.conductor().session().scenes[0].hasClip[2], "LEAD/INTRO starts empty");
    clipEd.createClipClick();
    check(p.conductor().session().scenes[0].hasClip[2], "createClipClick writes a clip into the doc");

    // Focus-mode snapshot: the DRUMS / DROP A clip editor over the live scene.
    ed2->enterFocus(0, 2);
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

    std::printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
