// The SQ-4 conductor (docs/sq4-clips.md §9) — C++ port of src/seq/store.ts
// (owner/queue launcher state, quantize boundary scheduling against the
// shared context-frame timebase, and the track gain (fader x mute x solo)
// math), minus React/zustand/persistence. JUCE-free.
//
// Devices execute stamped commands and report back; owner flips on their
// clipstart/clipstop acks so the UI shows what is audible. The web rig
// carries a 250 ms watchdog to cope with dropped WebAudio port messages;
// this port has none — acks travel an in-process FIFO (Task 8's
// SeqProcessor) sized so they cannot be dropped.
#pragma once

#include "SeqModel.h"
#include "SeqProtocol.h"
#include <map>
#include <unordered_map>
#include <vector>

namespace fable {

// The conductor's outbound side — implemented by SeqProcessor (Task 8) as a
// FIFO into the audio thread, and by tests as a recording fake.
struct ConductorIO {
    virtual double now() = 0;                                                     // current shared frame
    virtual void ioScheduleClip(int t, const std::vector<uint8_t>& bytes, int bars, double atFrame) = 0;
    virtual void ioScheduleStop(int t, double atFrame) = 0;
    virtual void ioUpdateClip(int t, const std::vector<uint8_t>& bytes, int bars) = 0;
    virtual void ioSetTrackGain(int t, float gain) = 0;                           // post-curve, 0 when closed
    virtual void ioSendTempo(double bpm, double swing, double anchorFrame) = 0;
    virtual ~ConductorIO() = default;
};

class Conductor {
public:
    Conductor(SessionData session, ConductorIO& io, double sampleRate);

    void powerOn();                       // anchor = now()+256, tempo out, gains out

    // UI actions (message thread only):
    void launch(int t, int s);
    void stopTrack(int t);
    void launchScene(int s);
    void stopScene(int s);
    void stopAll();
    void togglePassThrough(int s, int t);
    void updateClipBytes(int s, int t, std::vector<uint8_t> bytes, int bars);
    void createClip(int s, int t);
    void setTrackPatch(int t, PatchRef patch);
    void toggleSceneMute(int s);
    void toggleTrackMute(int t);
    void toggleSolo(int t);
    void cycleQuant(int d);
    void setTrackVol(int t, float v);
    void setSwing(double v);
    void setBpm(double bpm);              // guarded: only while no track owned/queued; re-anchors

    // audio-thread acks, delivered on the message thread by the editor timer:
    void onClipStart(int t);
    void onClipStop(int t);

    // views for the UI:
    const SessionData& session() const { return session_; }
    int ownerOf(int t) const;    // -2 = none
    int queueOf(int t) const;    // -2 = none, SQ_STOP = stop queued
    bool sceneMuted(int s) const;
    bool trackMuted(int t) const;
    bool soloed(int t) const;
    // Port of model.ts isTrackAudible (docs/sq4-clips.md): a track is audible
    // only when owned AND not muted/soloed-out AND its owning scene isn't
    // muted -- the single gate the UI (scene dots, cell MUTED state, footer
    // NOW label) should read instead of checking ownerOf/sceneMuted alone.
    bool trackAudible(int t) const { return isTrackAudible(t, owner_, trackMute_, sceneMute_, solo_); }
    Quant quant() const { return quant_; }
    double swing() const { return swing_; }
    float trackVol(int t) const;
    double anchor() const { return anchor_; }
    SqSongPos songPos() const;   // derived from io.now()

    static float gainCurve(float v) { return v * v * 1.4f; }

private:
    double boundary() const;
    void applyGains();

    SessionData session_;
    std::unordered_map<int, int> owner_, queue_;
    std::map<int, int> lastScheduled_;
    std::vector<bool> sceneMute_, trackMute_, solo_;
    Quant quant_;
    std::vector<float> trackVol_;
    double swing_ = 0, anchor_ = 0;
    ConductorIO& io_;
    double sr_;
};

} // namespace fable
