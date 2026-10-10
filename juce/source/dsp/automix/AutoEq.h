#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>

// Auto-EQ: measure a third-octave spectrum, compare it with a reference tonal
// curve and fit the four-band parametric EQ to close the gap. Port of
// src/engine/autoEq.ts + eqResponse.ts; the same constants, rounding and
// clamps, checked against web vectors by test/automix_test.cpp.
namespace fable::automix {

constexpr size_t NUM_BANDS = 28;
using Bands = std::array<double, NUM_BANDS>;

/** Third-octave centres used for measurement, display and the fit. */
extern const Bands BAND_HZ;

constexpr double SILENCE_DB = -60;
double powerDb(double p);
/** JavaScript Math.round: halves round toward +infinity. */
double jsRound(double v);
/** JavaScript truthiness of a number: nonzero and not NaN (also keeps -Wfloat-equal quiet). */
inline bool truthy(double v) { return v < 0 || v > 0; }
/** JavaScript Number.prototype.toFixed (exact decimal, ties away from zero). */
std::string toFixed(double v, int digits);

struct EqShape {
    /** Tilt in dB per octave above `bassHz`, pivoting at 1 kHz. */
    double slope = 0;
    /** The curve holds its level below this frequency, plus `bass` dB. */
    double bassHz = 20, bass = 0;
    /** Roll-off below `lowCutHz` in dB per octave (0 Hz = none). */
    double lowCutHz = 0, lowCut = 0;
    /** Extra roll-off above `rollHz` in dB per octave (0 Hz = none). */
    double rollHz = 0, roll = 0;
};

struct EqReference { const char* id; const char* label; const char* hint; EqShape shape; };

extern const std::array<EqReference, 9> EQ_REFERENCES;
/** Falls back to the first reference ('mix') for an unknown id. */
const EqReference& referenceById(const std::string& id);

/** Reference level in dB relative to 1 kHz. */
double referenceDb(const EqReference& ref, double f);

/** RBJ shelf/bell as the worklet runs it. type: 0 low shelf, 1 bell, 2 high shelf. */
struct EqBand { double freq = 1000, gain = 0, q = 0.7071067811865476; int type = 1; bool on = true; };
struct EqCoefficients { double b0, b1, b2, a1, a2; };
EqCoefficients eqCoefficients(const EqBand& band, double sampleRate);
double eqResponseDb(const EqCoefficients& c, double freq, double sampleRate);
double eqCurveDb(const std::vector<EqBand>& bands, double f, double sampleRate);

/**
 * Web Audio AnalyserNode float frequency data with smoothingTimeConstant 0:
 * Blackman window, |X[k]| / n, in dB. `time` holds n samples (a power of
 * two), `outDb` receives n / 2 bins. Native taps use it in place of the
 * browser analyser so the band levels match the web measurement.
 */
void analyserSpectrumDb(const float* time, int n, float* outDb);

/** Adds one analyser dB frame (`bins` = fftSize / 2 values) into band powers. */
void addBandPower(Bands& bands, const float* spectrumDb, int bins, double sampleRate, int fftSize);
double rmsDb(const float* x, int n);

/** Accumulates audible analyser frames into a long-term band spectrum. */
class SpectrumCapture {
public:
    SpectrumCapture(double sr, int size) : sampleRate(sr), fftSize(size) {}
    /** `spectrumDb` has fftSize / 2 bins, `time` fftSize samples. */
    bool add(const float* spectrumDb, const float* time, int timeSize);
    double seconds() const { return frames * fftSize / sampleRate; }
    /** Band levels in dB relative to the total. */
    std::vector<double> levels() const;

    Bands bandPower{};
    int frames = 0;
    double sampleRate;
    int fftSize;
};

struct Alignment { std::vector<bool> present; double offset = 0; };
/**
 * Aligns measured band levels with a reference: `offset` is the median
 * deviation over the bands with energy between 100 Hz and 8 kHz.
 */
Alignment alignToReference(const std::vector<double>& levels, const EqReference& ref);

struct EqFit {
    std::vector<EqBand> bands;
    /** Measured levels aligned to the reference (dB relative to 1 kHz). */
    std::vector<double> measured;
    /** Measured minus reference; 0 where the source has no energy. */
    std::vector<double> deviation;
    /** Deviation left after the fitted EQ. */
    std::vector<double> residual;
    /** Bands with enough energy to judge. */
    std::vector<bool> present;
    /** Total fitted move in dB, summed over the four bands. */
    double change = 0;
};

/** Fits low shelf, two bells and high shelf to the measured deviation (28 levels). */
EqFit fitEq(const std::vector<double>& levels, const EqReference& ref, double amount, double sampleRate);

/** Removes an EQ's colouring from levels measured after it. */
std::vector<double> removeEq(const std::vector<double>& levels, const std::vector<EqBand>& bands, double sampleRate);

}
