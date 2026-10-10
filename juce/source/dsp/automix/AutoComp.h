#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

// Auto-comp: learn the signal entering a compressor, then choose threshold,
// ratio, attack and release for a style. Port of src/engine/autoComp.ts.
// With detector traces the fit runs the shared Compressor's peak detector and
// soft-knee curve exactly; without them it falls back to block RMS plus a
// fixed peak offset.
namespace fable::automix {

struct CompStyle {
    const char* id; const char* label; const char* hint;
    double ratio;
    /** Attack in seconds. */
    double attack;
    /** Release as a fraction of one beat. */
    double releaseBeats;
    /** Average gain reduction wanted on the loud section, in dB. */
    double targetGr;
    /** The loud section starts at this percentile of the level distribution. */
    double loudPct;
};

extern const std::array<CompStyle, 5> COMP_STYLES;
/** Falls back to the first style ('glue') for an unknown id. */
const CompStyle& compStyleById(const std::string& id);

/** Fallback only: the peak detector reads this much above ~33 ms RMS blocks. */
constexpr double DETECTOR_OFFSET_DB = 3;
constexpr double BLOCK_RATE = 30;

/** Static gain change in dB (<= 0) of the shared compressor: 18 dB soft knee. */
double compGainDb(double levelDb, double threshold, double ratio);

/** Beat estimate from the level envelope's onset autocorrelation; empty when no 70-180 BPM tempo stands out. */
std::optional<double> estimateBpm(const std::vector<double>& blocks, double rate = BLOCK_RATE);

/** One block's detector input (max |L|,|R| per sample). A null `data` is a missing trace. */
struct Trace { const float* data = nullptr; int size = 0; };

/** Per-block detector level in dB per sample; `present` false for a missing trace. */
struct DetectorBlock { bool present = false; std::vector<float> db; };

/**
 * Runs the Compressor's peak detector over the traces (one-pole on the peak,
 * attack when rising, release when falling). The envelope carries across blocks.
 */
std::vector<DetectorBlock> detectorDb(const std::vector<Trace>& traces, double attack, double release, double sampleRate);

struct CompFit {
    double threshold = 0, ratio = 0, attack = 0, release = 0;
    /** Predicted average gain reduction on the loud section, dB (positive). */
    double loudGr = 0;
    /** Predicted average gain reduction over all audible blocks, dB (positive). */
    double meanGr = 0;
    /** 10th-90th percentile level spread of the input, dB. */
    double rangeDb = 0;
    std::optional<double> bpm;
    /** Mean detector level of each audible block in dB, for the histogram. */
    std::vector<double> levels;
    /** True when the fit ran the exact detector on a trace. */
    bool exact = false;
};

struct FitOptions {
    /** Host tempo; when empty the tempo is estimated from the blocks. */
    std::optional<double> bpm;
    /** Detector input per block, aligned with `blocks`; may be shorter or empty. */
    std::vector<Trace> traces;
    double sampleRate = 0;
};

/** `blocks` are block RMS levels in dB at BLOCK_RATE. `depth` scales the style's target reduction. */
std::optional<CompFit> fitComp(const std::vector<double>& blocks, const CompStyle& style, double depth,
                               const FitOptions& options = {});

}
