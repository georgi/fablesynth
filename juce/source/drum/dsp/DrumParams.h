// Canonical DR-1 parameter definitions — C++ port of src/drum/params.ts.
// Per-pad params are namespaced "pad<i>.<field>"; every knob, kit, worklet
// message and the APVTS key off this single table (same params-as-truth
// discipline as WT-1's Params.h).
//
// The engine reads parameters from a flat float array indexed by the integer
// ids below (no string hashing on the audio thread). Reuses fable::ParamInfo,
// Curve, Kind, normToValue, valueToNorm from the WT-1 core. JUCE-free.
#pragma once

#include "../../dsp/Params.h"

#include <array>
#include <string>
#include <vector>

namespace fable {

constexpr int DR_NPADS = 16;
constexpr int DR_MIDI_BASE = 36;              // pads 0..15 = notes 36..51
constexpr int DR_NPATTERNS = 4;
constexpr int DR_STEPS = 16;

// ---- option label tables (mirror src/drum/params.ts) ----
extern const std::vector<std::string> DRUM_TABLE_NAMES;   // THUD CRACK TINE GRIT PRIME BLOOM PULSE VOX CHIME GLITCH
extern const std::vector<std::string> DRUM_FILTER_TYPES;  // LP 12, LP 24, BP 12, HP 12, NOTCH
extern const std::vector<std::string> DMOD_SOURCES;       // —, MOD ENV, VELO, RAND
extern const std::vector<std::string> DMOD_DESTS;         // —, A POS, B POS, LEVEL, CUTOFF, PITCH, A FINE, B FINE, NOISE LVL, RES
extern const std::vector<std::string> CHOKE_NAMES;        // —, CHK 1..CHK 4
extern const std::vector<std::string> OUT_NAMES;          // MAIN, AUX 1..AUX 4

// The full TABLE selector option list: the 10 drum table names followed by
// fixed "USER n" slots, mirroring WT-1's tableSlotNames() so imported tables
// stay addressable (and automatable) by a stable index.
const std::vector<std::string>& drumTableSlotNames();

// Per-pad field offsets — order MUST match PAD_DEFS in src/drum/params.ts.
enum DPadField : int {
    DP_OSCA_TABLE = 0, DP_OSCA_POS, DP_OSCA_TUNE, DP_OSCA_FINE, DP_OSCA_PHASE,
    DP_OSCA_UNISON, DP_OSCA_DETUNE, DP_OSCA_LEVEL,
    DP_OSCB_TABLE, DP_OSCB_POS, DP_OSCB_TUNE, DP_OSCB_FINE, DP_OSCB_PHASE,
    DP_OSCB_UNISON, DP_OSCB_DETUNE, DP_OSCB_LEVEL,
    DP_NOISE_COLOR, DP_NOISE_LEVEL,
    DP_PENV_AMT, DP_PENV_DEC,
    DP_AENV_ATT, DP_AENV_HOLD, DP_AENV_DEC, DP_AENV_CURVE,
    DP_FLT_ON, DP_FLT_TYPE, DP_FLT_CUT, DP_FLT_RES, DP_FLT_DRIVE,
    DP_MOD1_SRC, DP_MOD1_DST, DP_MOD1_AMT,
    DP_MOD2_SRC, DP_MOD2_DST, DP_MOD2_AMT,
    DP_MOD3_SRC, DP_MOD3_DST, DP_MOD3_AMT,
    DP_MOD4_SRC, DP_MOD4_DST, DP_MOD4_AMT,
    DP_MODENV_DEC,
    DP_LVL, DP_PAN, DP_V2L, DP_V2M, DP_CHOKE, DP_OUT,
    DPAD_NFIELDS                                   // == 48
};

enum DGlobalPid : int {
    DG_SEQ_BPM = DR_NPADS * DPAD_NFIELDS,          // 768
    DG_MASTER_SWING, DG_MASTER_VOLUME,
    DG_FXDRIVE_ON, DG_FXDRIVE_AMT, DG_FXDRIVE_MIX,
    DG_FXCOMP_ON, DG_FXCOMP_THR, DG_FXCOMP_GAIN,
    DG_FXCHORUS_ON, DG_FXCHORUS_RATE, DG_FXCHORUS_DEPTH, DG_FXCHORUS_MIX,
    DG_FXDELAY_ON, DG_FXDELAY_TIME, DG_FXDELAY_FB, DG_FXDELAY_MIX,
    DG_FXREVERB_ON, DG_FXREVERB_SIZE, DG_FXREVERB_MIX,
    DR_NUM_PARAMS                                  // == 788
};

inline constexpr int dpid(int padI, int field) { return padI * DPAD_NFIELDS + field; }
inline constexpr int doscBase(int osc) { return osc == 0 ? DP_OSCA_TABLE : DP_OSCB_TABLE; }

using DrumParamArray = std::array<float, DR_NUM_PARAMS>;

// The full ordered descriptor table (built once), ordered by flat id.
// pid strings run "pad0.oscA.table" … "fx.reverb.mix".
const std::vector<ParamInfo>& drumParamInfo();

// Defaults as a flat DrumParamArray.
DrumParamArray defaultDrumParams();

// Map a string parameter id ("pad3.flt.cut") to its flat index, -1 if unknown.
int drumIdFromString(const std::string& pid);

} // namespace fable
