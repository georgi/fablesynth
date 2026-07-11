// Canonical BL-1 parameter definitions — C++ port of src/bass/params.ts.
// Single mono voice, so ids are flat (no pad namespace) — every knob, patch,
// APVTS entry and the engine key off this single table (same params-as-truth
// discipline as WT-1/DR-1).
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

// The sequencer's note 0 / keyboard's lowest key. C2 keeps a two-octave
// keyboard centered on classic acid bass territory (C2..B3). params.ts:18-19.
constexpr int BL_ROOT_MIDI = 36;
constexpr int BL_KEY_COUNT = 25;   // two octaves + top C

constexpr int BL_NPATTERNS  = 4;
constexpr int BL_STEPS      = 16;
constexpr int BL_STEP_STRIDE = 3;  // byte 0 flags, byte 1 note, byte 2 oct+1
constexpr int BL_PATTERN_BYTES = BL_NPATTERNS * BL_STEPS * BL_STEP_STRIDE;

// ---- option label tables (mirror src/bass/params.ts) ----
extern const std::vector<std::string> BASS_FILTER_TYPES; // LP 12, LP 24, BP 12, HP 12, NOTCH
// osc.table options: the 6 WT-1 procedural tables (TABLE_NAMES; no user slots
// in BL-1, matching the web app). SUB_SHAPES / LFO_DIVS / LFO_SHAPES come from
// the shared Params.h.

// Flat parameter index space — order MUST match BASS_PARAM_DEFS in
// src/bass/params.ts. (BL_ prefix: the WT-1 Pid enum already owns names like
// OSC_TABLE in this namespace.)
enum BassPid : int {
    BL_OSC_TABLE = 0, BL_OSC_POS, BL_OSC_TUNE, BL_OSC_FINE,
    BL_OSC_UNISON, BL_OSC_DETUNE, BL_OSC_SPREAD, BL_OSC_LEVEL,
    BL_SUB_SHAPE, BL_SUB_OCT, BL_SUB_LEVEL,
    BL_FLT_TYPE, BL_FLT_CUT, BL_FLT_RES, BL_FLT_DRIVE, BL_FLT_ENV, BL_FLT_TRACK,
    BL_FENV_ATT, BL_FENV_DEC,
    BL_AENV_ATT, BL_AENV_DEC, BL_AENV_SUS, BL_AENV_REL,
    BL_ACC_AMT, BL_SLIDE_TIME,
    BL_LFO_RATE, BL_LFO_SHAPE, BL_LFO_DEPTH,
    BL_FXDRIVE_ON, BL_FXDRIVE_AMT, BL_FXDRIVE_MIX,
    BL_FXCHORUS_ON, BL_FXCHORUS_RATE, BL_FXCHORUS_DEPTH, BL_FXCHORUS_MIX,
    BL_FXDELAY_ON, BL_FXDELAY_TIME, BL_FXDELAY_FB, BL_FXDELAY_MIX,
    BL_FXREVERB_ON, BL_FXREVERB_SIZE, BL_FXREVERB_MIX,
    BL_SEQ_BPM, BL_MASTER_SWING, BL_MASTER_VOLUME,
    BL_NUM_PARAMS                                   // == 45
};

using BassParamArray = std::array<float, BL_NUM_PARAMS>;

// The full ordered descriptor table (built once), ordered by flat id.
// pid strings run "osc.table" … "master.volume".
const std::vector<ParamInfo>& bassParamInfo();

// Defaults as a flat BassParamArray.
BassParamArray defaultBassParams();

// Map a string parameter id ("flt.cut") to its flat index, -1 if unknown.
int bassIdFromString(const std::string& pid);

} // namespace fable
