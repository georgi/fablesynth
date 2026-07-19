// Canonical parameter definitions — C++ port of src/params.ts.
// Every plugin parameter, preset and the DSP engine key off this single list.
//
// The engine reads parameters from a flat float array indexed by the integer
// ids below (no string hashing in the audio thread). The descriptor table
// (PARAM_INFO) carries the string id / range / curve used to build the JUCE
// APVTS and to seed defaults — exactly mirroring the web app's params.ts.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace fable {

// ---- enum / option label tables (mirror src/constants.ts) ----
inline const std::vector<std::string> TABLE_NAMES   = {"PRIME", "BLOOM", "PULSE", "VOX", "CHIME", "GLITCH"};

// Max imported/drawn tables an instance can hold. The oscillator TABLE param
// reserves this many slots after the procedural names so user tables stay
// addressable (and automatable) by a stable index. Mirrors the web's
// "procedural + user pool" indexing.
constexpr int MAX_USER_TABLES = 16;

// The full TABLE selector option list: the procedural names followed by fixed
// "USER n" slots. The Stepper shows the live user-table name for filled slots;
// the engine reads the raw index and falls silent for empty slots.
inline const std::vector<std::string>& tableSlotNames() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> out = TABLE_NAMES;
        for (int i = 0; i < MAX_USER_TABLES; ++i) out.push_back("USER " + std::to_string(i + 1));
        return out;
    }();
    return v;
}

inline const std::vector<std::string> FILTER_TYPES  = {"LP 12", "LP 24", "BP 12", "HP 12", "NOTCH", "COMB", "VOWEL"};
inline const std::vector<std::string> FILTER_ROUTES = {"SERIAL", "PARALLEL", "SPLIT"};
inline const std::vector<std::string> LFO_SHAPES    = {"SINE", "TRI", "SAW", "SQR", "S&H"};
inline const std::vector<std::string> LFO_DIVS      = {"1/1", "1/2", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16", "1/16T", "1/32"};

// Cycles per beat (beat = quarter note) for each LFO_DIVS entry. Clamped.
inline double lfoDivFactor(int i) {
    static const double f[] = {0.25, 0.5, 1.0, 2.0 / 3.0, 1.5, 2.0, 4.0 / 3.0, 3.0, 4.0, 6.0, 8.0};
    constexpr int n = (int)(sizeof(f) / sizeof(f[0]));
    return f[i < 0 ? 0 : (i >= n ? n - 1 : i)];
}
inline const std::vector<std::string> SUB_SHAPES    = {"SINE", "SQR"};
inline const std::vector<std::string> NOISE_TYPES   = {"WHITE", "PINK"};
inline const std::vector<std::string> MOD_SOURCES   = {"—", "LFO 1", "LFO 2", "MOD ENV", "VELO", "NOTE"};
// Modulation destinations (append-only; indices 0..10 keep their exact meaning so
// factory presets sound unchanged). dstTarget() below is the canonical map from a
// dst index to either a flat Pid (per-param dest) or a negative global sentinel.
inline const std::vector<std::string> MOD_DESTS     = {
    "—", "A POS", "B POS", "F1 CUT", "PITCH", "AMP", "PAN", "A LVL", "B LVL", "F2 CUT", "F2 RES",
    "A DETUNE", "A SPREAD", "A PAN", "B DETUNE", "B SPREAD", "B PAN",
    "F1 RES", "F1 DRIVE", "F1 ENV", "F1 KEY", "F2 DRIVE", "F2 ENV", "F2 KEY",
    "SUB LVL", "NOISE LVL", "A BLEND", "B BLEND"};
// MOD_DESTS.size() as a compile-time constant (array sizing for the live-mod
// viz snapshot). engine_test asserts the two stay in sync.
constexpr int NUM_MOD_DESTS = 28;

// ---- field offsets within a repeated block ----
enum OscField  { OSC_ON, OSC_TABLE, OSC_POS, OSC_OCT, OSC_SEMI, OSC_FINE,
                 OSC_UNISON, OSC_DETUNE, OSC_SPREAD, OSC_LEVEL, OSC_PAN, OSC_BLEND, OSC_NFIELDS };
enum FiltField { FLT_ON, FLT_TYPE, FLT_CUTOFF, FLT_RES, FLT_DRIVE, FLT_ENV, FLT_KEY, FLT_NFIELDS };
enum MatField  { MAT_SRC, MAT_DST, MAT_AMT, MAT_NFIELDS };

// Fixed mod-matrix pool size — shared by the engine, params and UI. Kept juce-free
// (no juce types in the DSP header) and mirrored by MOD_MATRIX_SIZE in the web.
constexpr int MOD_MATRIX_SIZE = 16;
enum LfoField  { LFO_SHAPE, LFO_RATE, LFO_SYNC, LFO_SYNCRATE, LFO_RISE, LFO_PHASE, LFO_RETRIG, LFO_NFIELDS };

// ---- flat parameter index space ----
// Block bases keep oscA/oscB and filter/filter2 layouts identical, so the
// engine can address either with base + field (see Engine::setupOsc/setupFilter).
enum Pid : int {
    OSCA_BASE   = 0,                          // 11 fields
    OSCB_BASE   = static_cast<int>(OSCA_BASE) + static_cast<int>(OSC_NFIELDS), // 11
    SUB_ON      = static_cast<int>(OSCB_BASE) + static_cast<int>(OSC_NFIELDS), // 22
    SUB_SHAPE, SUB_OCT, SUB_LEVEL,
    NOISE_ON, NOISE_TYPE, NOISE_LEVEL,
    FILTER1_BASE,                             // 7 fields
    FILTER_ROUTE = static_cast<int>(FILTER1_BASE) + static_cast<int>(FLT_NFIELDS),
    FILTER2_BASE,                             // 7 fields
    ENV1_BASE   = static_cast<int>(FILTER2_BASE) + static_cast<int>(FLT_NFIELDS), // a,d,s,r
    ENV2_BASE   = ENV1_BASE + 4,
    LFO1_BASE   = ENV2_BASE + 4,              // shape,rate,sync,syncrate,rise,phase,retrig
    LFO2_BASE   = static_cast<int>(LFO1_BASE) + static_cast<int>(LFO_NFIELDS),
    MAT1_BASE   = static_cast<int>(LFO2_BASE) + static_cast<int>(LFO_NFIELDS), // 16 slots x 3
    MAT2_BASE   = static_cast<int>(MAT1_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT3_BASE   = static_cast<int>(MAT2_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT4_BASE   = static_cast<int>(MAT3_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT5_BASE   = static_cast<int>(MAT4_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT6_BASE   = static_cast<int>(MAT5_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT7_BASE   = static_cast<int>(MAT6_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT8_BASE   = static_cast<int>(MAT7_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT9_BASE   = static_cast<int>(MAT8_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT10_BASE  = static_cast<int>(MAT9_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT11_BASE  = static_cast<int>(MAT10_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT12_BASE  = static_cast<int>(MAT11_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT13_BASE  = static_cast<int>(MAT12_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT14_BASE  = static_cast<int>(MAT13_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT15_BASE  = static_cast<int>(MAT14_BASE) + static_cast<int>(MAT_NFIELDS),
    MAT16_BASE  = static_cast<int>(MAT15_BASE) + static_cast<int>(MAT_NFIELDS),
    FXDRIVE_ON = static_cast<int>(MAT16_BASE) + static_cast<int>(MAT_NFIELDS), // on,amt,mix
    FXDRIVE_AMT, FXDRIVE_MIX,
    FXCHORUS_ON, FXCHORUS_RATE, FXCHORUS_DEPTH, FXCHORUS_MIX,
    FXDELAY_ON, FXDELAY_TIME, FXDELAY_FB, FXDELAY_MIX,
    FXREVERB_ON, FXREVERB_SIZE, FXREVERB_MIX,
    FXCOMP_ON, FXCOMP_THR, FXCOMP_GAIN, // leveling "glue" comp, last FX
    FXEQ_ON, FXEQ_LOW, FXEQ_MID, FXEQ_MFREQ, FXEQ_HIGH, // 3-band tone EQ, first FX
    MASTER_VOLUME, MASTER_GLIDE,
    // Note sequencer clock (params.ts appends these after master too). seq.bpm
    // also drives the engine's virtual transport while the internal sequencer
    // plays, so synced LFOs phase-lock to the sequencer tempo (web parity).
    SEQ_BPM, SEQ_SWING, SEQ_GATE, SEQ_ROOT,
    NUM_PARAMS
};

inline constexpr int oscBase(int i)  { return i == 0 ? OSCA_BASE : OSCB_BASE; }
inline constexpr int fltBase(int i)  { return i == 0 ? FILTER1_BASE : FILTER2_BASE; }
inline constexpr int matBase(int s)  { return MAT1_BASE + (s - 1) * MAT_NFIELDS; } // s = 1..16

// Typed block-offset helpers keep parameter IDs explicit and prevent accidental
// arithmetic between unrelated field enums. They return the same flat integer
// indices used by ParamArray and the DSP engines.
inline constexpr int paramIndex(Pid base, OscField field) {
    return static_cast<int>(base) + static_cast<int>(field);
}
inline constexpr int paramIndex(Pid base, FiltField field) {
    return static_cast<int>(base) + static_cast<int>(field);
}
inline constexpr int paramIndex(Pid base, LfoField field) {
    return static_cast<int>(base) + static_cast<int>(field);
}
inline constexpr int paramIndex(Pid base, MatField field) {
    return static_cast<int>(base) + static_cast<int>(field);
}

// ---- modulation destination -> target (canonical, juce-free) ----
// Per-param dests return a flat Pid; "none" and the three global dests return a
// negative sentinel handled directly by the engine (PITCH/AMP/PAN math unchanged).
enum DstSentinel : int { DST_NONE = -1, DST_PITCH = -2, DST_AMP = -3, DST_PAN = -4 };

inline constexpr int dstTarget(int dst) {
    switch (dst) {
        case 0:  return DST_NONE;
        case 1:  return paramIndex(OSCA_BASE, OSC_POS);
        case 2:  return paramIndex(OSCB_BASE, OSC_POS);
        case 3:  return paramIndex(FILTER1_BASE, FLT_CUTOFF);
        case 4:  return DST_PITCH;
        case 5:  return DST_AMP;
        case 6:  return DST_PAN;
        case 7:  return paramIndex(OSCA_BASE, OSC_LEVEL);
        case 8:  return paramIndex(OSCB_BASE, OSC_LEVEL);
        case 9:  return paramIndex(FILTER2_BASE, FLT_CUTOFF);
        case 10: return paramIndex(FILTER2_BASE, FLT_RES);
        case 11: return paramIndex(OSCA_BASE, OSC_DETUNE);
        case 12: return paramIndex(OSCA_BASE, OSC_SPREAD);
        case 13: return paramIndex(OSCA_BASE, OSC_PAN);
        case 14: return paramIndex(OSCB_BASE, OSC_DETUNE);
        case 15: return paramIndex(OSCB_BASE, OSC_SPREAD);
        case 16: return paramIndex(OSCB_BASE, OSC_PAN);
        case 17: return paramIndex(FILTER1_BASE, FLT_RES);
        case 18: return paramIndex(FILTER1_BASE, FLT_DRIVE);
        case 19: return paramIndex(FILTER1_BASE, FLT_ENV);
        case 20: return paramIndex(FILTER1_BASE, FLT_KEY);
        case 21: return paramIndex(FILTER2_BASE, FLT_DRIVE);
        case 22: return paramIndex(FILTER2_BASE, FLT_ENV);
        case 23: return paramIndex(FILTER2_BASE, FLT_KEY);
        case 24: return SUB_LEVEL;
        case 25: return NOISE_LEVEL;
        case 26: return paramIndex(OSCA_BASE, OSC_BLEND);
        case 27: return paramIndex(OSCB_BASE, OSC_BLEND);
        default: return DST_NONE;
    }
}

using ParamArray = std::array<float, NUM_PARAMS>;

// ---- descriptor for APVTS construction + defaults ----
enum class Curve { Lin, Log, Int };
enum class Kind  { Float, Bool, Enum };

struct ParamInfo {
    int         id;
    std::string pid;     // string id, e.g. "oscA.pos" (APVTS parameter id)
    std::string label;   // display name
    float       min;
    float       max;
    float       def;
    Curve       curve;
    Kind        kind;
    const std::vector<std::string>* options; // for Enum
};

// The full ordered descriptor table (built once). Order matches the Pid enum.
const std::array<ParamInfo, NUM_PARAMS>& paramInfo();

// Defaults as a flat ParamArray.
ParamArray defaultParams();

// Map a string parameter id ("oscA.pos") to its flat index, or -1 if unknown.
int idFromString(const std::string& pid);

// normalized [0,1] <-> value, matching params.ts normToValue / valueToNorm.
float normToValue(const ParamInfo& d, float n);
float valueToNorm(const ParamInfo& d, float v);

} // namespace fable
