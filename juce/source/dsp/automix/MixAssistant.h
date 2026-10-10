#pragma once
#include "AutoEq.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

// SQ-4 mix assistant. Port of src/seq/mixAssistant.ts: it listens to the
// pre-FX master sum and the per-track post-fader taps, then proposes master
// EQ, dynamics, loudness and fader moves against a tonal-balance target.
namespace fable::automix {

/** Approximate BS.1770 K-weighting magnitude (power). */
double kWeightPower(double f);

enum class Machine { DR1, BL1, WT1 };
struct TrackInfo { Machine machine = Machine::WT1; std::string name; };

/** A run of float samples; a null `data` means absent. */
struct Samples { const float* data = nullptr; int size = 0; };

/** One track's analyser frame: time-domain samples and, optionally, its spectrum in dB (fftSize / 2 bins). */
struct TrackFrame { Samples time; Samples spectrum; };

/** Accumulates analyser frames while the assistant listens. */
class MixListener {
public:
    MixListener(double sampleRate, int fftSize, int tracks);
    /**
     * `spectrum` is analyser dB data (fftSize / 2 bins) of the pre-FX sum,
     * `input` its time frame, `output` the post-FX time frame (optional).
     * `tracks` holds one frame per track; a null `time.data` skips a track.
     * Returns true when the frame was audible.
     */
    bool add(Samples spectrum, Samples input, Samples output = {}, const std::vector<TrackFrame>* tracks = nullptr);

    double sampleRate;
    int fftSize;
    Bands bandPower{};
    std::vector<double> inBlocks, outBlocks;
    std::vector<std::vector<double>> trackBlocks;
    std::vector<Bands> trackBandPower;
    /** Linear power summed over every frame, silent ones included. */
    std::vector<double> trackPower;
    int frames = 0;
    double peak = 0;
};

/** BS.1770 style gating over block powers (mono downmix, +3 dB for two channels). */
double gatedLoudness(const std::vector<double>& blocks, double kOffsetDb);

struct TrackStats {
    /** Gated loudness while the track plays. */
    double lufs = 0;
    /** Mean K-weighted power over the whole window, linear. */
    double power = 0;
    /** Fraction of the window in which the track is audible. */
    double activity = 0;
    /** Fraction of the track's energy below 160 Hz. */
    double lowShare = 0;
    /** 10th-90th percentile spread of its block levels, dB. */
    double rangeDb = 0;
};

struct MixAnalysis {
    /** Measured band level in dB relative to the total. */
    std::vector<double> spectrum;
    double lufsIn = 0, lufsOut = 0, peakDb = 0, crestDb = 0;
    /** Loud-section short-term level (90th percentile) of the pre-FX sum. */
    double loudDb = 0;
    double kOffsetDb = 0;
    /** Per track; empty when the track was heard in under a quarter of the window. */
    std::vector<std::optional<TrackStats>> tracks;
    double seconds = 0;
};

MixAnalysis analyse(const MixListener& listener);

enum class MixRole { Drums, Perc, Bass, Lead, Pad };
struct MixRoleInfo { MixRole id; const char* key; const char* label; double lu; };
/** Loudness of each role relative to the drums, in LU. */
extern const std::array<MixRoleInfo, 5> MIX_ROLES;
const MixRoleInfo& roleInfo(MixRole r);

/** Guesses a track's role from its name, machine, register and how it sustains. */
MixRole detectRole(Machine machine, const TrackStats& t, const std::string& name = {});

struct AssistMove { std::string id, label, from, to; };

struct TrackLevel {
    int index = 0;
    std::string name;
    MixRole role = MixRole::Drums, detected = MixRole::Drums;
    /** Level and target on a common scale where the drum role sits at 0 LU. */
    double current = 0, target = 0;
    /** Planned fader move in dB, 0 when the track stays. */
    double move = 0;
};

struct AssistPlan {
    /** Master FX param ids ("master.fx.eq.low", ...) to set. */
    std::map<std::string, float> masterFx;
    /** New track fader value per track, or empty when it stays. */
    std::vector<std::optional<float>> trackVol;
    std::optional<float> masterVol;
    EqFit eq;
    std::vector<TrackLevel> levels;
    std::vector<AssistMove> moves;
    std::vector<std::string> notes;
};

struct PlanInput {
    MixAnalysis analysis;
    /** An EQ_REFERENCES id. */
    std::string reference = "mix";
    /** -14, -11 or -9 LUFS. */
    int loudness = -14;
    /** 0..1: how much of each measured deviation to correct. */
    double amount = 1;
    double bpm = 120;
    double sampleRate = 48000;
    /** Current master FX params; must hold the master.fx.eq/ott/comp/limiter ids. */
    std::map<std::string, float> masterFx;
    double masterVol = 1;
    std::vector<double> trackVol;
    std::vector<TrackInfo> tracks;
    /** Per-track role overrides; empty uses the detected role. */
    std::vector<std::optional<MixRole>> roles;
};

AssistPlan buildPlan(const PlanInput& input);

}
