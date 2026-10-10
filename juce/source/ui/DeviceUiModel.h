#pragma once

#include "ParameterSource.h"
#include "../dsp/FxTelemetry.h"
#include "../dsp/Arp.h"

// Host transport snapshot used by tempo-synchronised device displays.
struct HostTransport {
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
};

namespace fui {

// Melodic sequence POLY (src/shared/sequenceRhythm.ts): one clock plays the
// first `steps` cells of bar one, on the 1/16 grid with swing or FIT into
// `cycleBeats` straight slots. UI-side mirror of fable::SequenceRhythm.
struct SeqPolySettings {
    bool enabled = false;
    bool fit = false;
    int steps = 16;          // 1..16
    int rotation = 0;        // 0..steps-1
    int cycleBeats = 4;      // 4 or 8
    bool operator==(const SeqPolySettings& o) const {
        return enabled == o.enabled && fit == o.fit && steps == o.steps
            && rotation == o.rotation && cycleBeats == o.cycleBeats;
    }
    bool operator!=(const SeqPolySettings& o) const { return !(*this == o); }
};

struct DeviceUiCapabilities {
    bool hosted = false;
    bool ownsTransport = true;
    bool supportsPatternChain = true;
    bool supportsUserTables = true;
    bool supportsAudition = true;
};

// Common contract shared by the three native device bodies. Implementations
// are message-thread objects and must outlive the components that reference
// them. Audio-thread state is exposed only through atomics owned by processors.
class DeviceUiModel {
public:
    virtual ~DeviceUiModel() = default;
    virtual ParameterSource parameters() = 0;
    virtual fable::FxTelemetry fxTelemetry(int = 0, int = 0) const { return {}; }
    virtual DeviceUiCapabilities capabilities() const = 0;

    // True once any param/pattern edit has landed since the current program
    // (preset/kit/patch) was loaded — drives the small dirty dot the headers
    // draw next to the program name (web parity: the stores' dirty flags).
    // Hosted SQ-4 models have no program concept and keep the default.
    virtual bool programDirty() const { return false; }
    virtual fable::ArpSettings arpSettings() const { return {}; }
    virtual void setArpSettings(const fable::ArpSettings&) {}
    virtual fable::ArpSettings arpLiveSettings() const { return arpSettings(); }
    // SEQUENCER tab LED. Hosted clips have no separate off state: the
    // sequencer is on whenever the clip's arp is off.
    virtual bool seqEnabled() const { return !arpSettings().enabled; }
    virtual void setSeqEnabled(bool) {}
    virtual void clearArpKeys() {}
    virtual void arpKeyInput(int, bool) {}
    virtual double arpSwing() const { return 0; }
    virtual void setArpSwing(double) {}
    virtual double arpTempo() const { return 120; }
    virtual bool arpQueued() const { return false; }
};

} // namespace fui
