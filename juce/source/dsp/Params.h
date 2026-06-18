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
inline const std::vector<std::string> SUB_SHAPES    = {"SINE", "SQR"};
inline const std::vector<std::string> NOISE_TYPES   = {"WHITE", "PINK"};
inline const std::vector<std::string> MOD_SOURCES   = {"-", "LFO 1", "LFO 2", "MOD ENV", "VELO", "NOTE"};
inline const std::vector<std::string> MOD_DESTS     = {"-", "A POS", "B POS", "F1 CUT", "PITCH", "AMP", "PAN", "A LVL", "B LVL", "F2 CUT", "F2 RES"};

// ---- field offsets within a repeated block ----
enum OscField  { OSC_ON, OSC_TABLE, OSC_POS, OSC_OCT, OSC_SEMI, OSC_FINE,
                 OSC_UNISON, OSC_DETUNE, OSC_SPREAD, OSC_LEVEL, OSC_PAN, OSC_NFIELDS };
enum FiltField { FLT_ON, FLT_TYPE, FLT_CUTOFF, FLT_RES, FLT_DRIVE, FLT_ENV, FLT_KEY, FLT_NFIELDS };
enum MatField  { MAT_SRC, MAT_DST, MAT_AMT, MAT_NFIELDS };

// ---- flat parameter index space ----
// Block bases keep oscA/oscB and filter/filter2 layouts identical, so the
// engine can address either with base + field (see Engine::setupOsc/setupFilter).
enum Pid : int {
    OSCA_BASE   = 0,                          // 11 fields
    OSCB_BASE   = OSCA_BASE + OSC_NFIELDS,    // 11
    SUB_ON      = OSCB_BASE + OSC_NFIELDS,    // 22
    SUB_SHAPE, SUB_OCT, SUB_LEVEL,
    NOISE_ON, NOISE_TYPE, NOISE_LEVEL,
    FILTER1_BASE,                             // 7 fields
    FILTER_ROUTE = FILTER1_BASE + FLT_NFIELDS,
    FILTER2_BASE,                             // 7 fields
    ENV1_BASE   = FILTER2_BASE + FLT_NFIELDS, // a,d,s,r
    ENV2_BASE   = ENV1_BASE + 4,
    LFO1_BASE   = ENV2_BASE + 4,              // shape,rate
    LFO2_BASE   = LFO1_BASE + 2,
    MAT1_BASE   = LFO2_BASE + 2,              // 4 slots x 3
    MAT2_BASE   = MAT1_BASE + MAT_NFIELDS,
    MAT3_BASE   = MAT2_BASE + MAT_NFIELDS,
    MAT4_BASE   = MAT3_BASE + MAT_NFIELDS,
    FXDRIVE_ON = MAT4_BASE + MAT_NFIELDS,     // on,amt,mix
    FXDRIVE_AMT, FXDRIVE_MIX,
    FXCHORUS_ON, FXCHORUS_RATE, FXCHORUS_DEPTH, FXCHORUS_MIX,
    FXDELAY_ON, FXDELAY_TIME, FXDELAY_FB, FXDELAY_MIX,
    FXREVERB_ON, FXREVERB_SIZE, FXREVERB_MIX,
    MASTER_VOLUME, MASTER_GLIDE,
    NUM_PARAMS
};

inline constexpr int oscBase(int i)  { return i == 0 ? OSCA_BASE : OSCB_BASE; }
inline constexpr int fltBase(int i)  { return i == 0 ? FILTER1_BASE : FILTER2_BASE; }
inline constexpr int matBase(int s)  { return MAT1_BASE + (s - 1) * MAT_NFIELDS; } // s = 1..4

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
